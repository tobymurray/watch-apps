/**
 ******************************************************************************
 * @file    Qr_test.cpp
 * @brief   The QR encoder, and the oracle that is the only reason to trust it.
 ******************************************************************************
 *
 * Code128_test.cpp opens by saying a barcode that scans as somebody else's
 * athlete number is the one genuinely harmful thing this app could do, and
 * that a transposed row in the Code 128 table is an invisible way to cause it.
 * QR is the same failure with the diagnostics removed.
 *
 * Reed-Solomon computes error correction *over whatever it is given*. A wrong
 * codeword gets consistent parity, the symbol is well-formed, a scanner reads
 * it happily, and it decodes to a valid, wrong value. Nothing about the symbol
 * betrays it, and -- unlike Code 128's 107 rows -- no amount of reading the
 * code finds it either. GF(256) arithmetic, a BCH remainder and eight mask
 * patterns are not things a person checks by eye.
 *
 * So this file leans on evidence rather than on structure:
 *
 *   1. ZintQrOracle diffs the whole grid against zint, module for module,
 *      **for all eight masks**, over a corpus of eighteen payloads. That is
 *      144 complete symbols from an independent implementation with its own
 *      field arithmetic, its own placement and its own tables.
 *   2. The structural claims below are still worth having, but they are the
 *      cheap half: they catch a broken build, not a subtly wrong one.
 *
 * There is a third leg the test build cannot carry, and it matters: this
 * encoder's own output, rendered and decoded back by zbar, for the payloads
 * zint encodes with a different mode segmentation -- the parkrun shapes among
 * them. That runs offline beside the oracle generator. Tests/README.md has it.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "Qr.hpp"
#include "Symbology.hpp"
#include "oracle/zint_qr_vectors.hpp"

namespace {

constexpr int kSize = Qr::kSize;

/// Chebyshev distance: which concentric ring of a finder or alignment pattern
/// a module falls on. The patterns are defined by ring, so the tests are too.
int ring(int dx, int dy)
{
    const int ax = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    return ax > ay ? ax : ay;
}

std::string gridOf(const Barcode::Matrix &m)
{
    std::string out;
    for (int y = 0; y < m.size; y++) {
        for (int x = 0; x < m.size; x++) {
            out += m.dark(static_cast<uint8_t>(x), static_cast<uint8_t>(y)) ? '1' : '0';
        }
    }
    return out;
}

/// The oracle's hex form expanded to the same bit string gridOf() produces.
std::string expand(const char *hex)
{
    std::string out;
    for (const char *p = hex; *p; p++) {
        const int v = (*p >= 'a') ? (*p - 'a' + 10) : (*p - '0');
        for (int b = 3; b >= 0; b--) {
            out += ((v >> b) & 1) ? '1' : '0';
            if (out.size() == static_cast<size_t>(kSize) * kSize) {
                return out;
            }
        }
    }
    return out;
}

std::string repeat(char c, size_t n) { return std::string(n, c); }

} // namespace

// ---------------------------------------------------------------------------
// The oracle
//
// The load-bearing part of this file. Everything else here could pass with an
// encoder that produces a beautifully structured, wrong symbol.
// ---------------------------------------------------------------------------

TEST(ZintQrOracle, EveryVectorMatchesZintModuleForModule)
{
    ASSERT_GT(ZintQrVectors::kVectorCount, 0);
    ASSERT_EQ(ZintQrVectors::kSize, kSize);

    for (int i = 0; i < ZintQrVectors::kVectorCount; i++) {
        const auto &v = ZintQrVectors::kVectors[i];
        SCOPED_TRACE(std::string("id \"") + v.id + "\" mask " + std::to_string(v.mask));

        Barcode::Matrix m{};
        ASSERT_TRUE(Qr::encodeWithMask(v.id, m, static_cast<uint8_t>(v.mask)));
        EXPECT_EQ(gridOf(m), expand(v.grid));
    }
}

TEST(ZintQrOracle, TheCorpusExercisesEveryMask)
{
    std::set<int> masks;
    for (int i = 0; i < ZintQrVectors::kVectorCount; i++) {
        masks.insert(ZintQrVectors::kVectors[i].mask);
    }
    EXPECT_EQ(masks.size(), 8u) << "the oracle must cover all eight mask patterns";
}

TEST(ZintQrOracle, TheCorpusReachesTheVersionsCapacity)
{
    size_t longest = 0;
    for (int i = 0; i < ZintQrVectors::kVectorCount; i++) {
        const size_t n = std::string(ZintQrVectors::kVectors[i].id).size();
        longest = n > longest ? n : longest;
    }
    EXPECT_EQ(longest, Qr::kMaxDataLength)
        << "the oracle must include a payload at the version's exact capacity";
}

// ---------------------------------------------------------------------------
// What the app depends on being true
// ---------------------------------------------------------------------------

TEST(QrCapacity, HoldsEveryIdThisAppAccepts)
{
    // The property that makes QR add no new way for a code to be refused, and
    // the reason Problem::BadValue's prompt is still true as written.
    EXPECT_GE(Qr::kMaxDataLength, Barcode::kMaxIdLength);
    EXPECT_EQ(Qr::kMaxDataLength, 26u);

    Barcode::Matrix m{};
    EXPECT_TRUE(Qr::encode(repeat('W', Barcode::kMaxIdLength).c_str(), m));
}

TEST(QrCapacity, RefusesOneByteOverRatherThanTruncating)
{
    Barcode::Matrix m{};
    EXPECT_TRUE(Qr::encode(repeat('a', Qr::kMaxDataLength).c_str(), m));

    Barcode::Matrix over{};
    EXPECT_FALSE(Qr::encode(repeat('a', Qr::kMaxDataLength + 1).c_str(), over));
    EXPECT_EQ(over.size, 0) << "a refusal must leave nothing partially encoded";
}

TEST(QrEncode, RefusesEmpty)
{
    Barcode::Matrix m{};
    EXPECT_FALSE(Qr::encode("", m));
    EXPECT_EQ(m.size, 0);
}

TEST(QrEncode, AcceptsTheWholePrintableAsciiRange)
{
    for (char c = 32; c > 0 && c < 127; c++) {
        const char text[2] = {c, '\0'};
        Barcode::Matrix m{};
        EXPECT_TRUE(Qr::encode(text, m)) << "refused character " << static_cast<int>(c);
    }
}

TEST(QrEncode, RefusesOutsidePrintableAscii)
{
    // The same set Code 128 refuses, and for the same reason: it is what the
    // screen can show underneath, not what the symbology could carry. Byte
    // mode would happily encode a control character that no wearer could read.
    for (const char *bad : {"\x01", "\x1f", "\x7f", "a\tb", "\xc3\xa9"}) {
        Barcode::Matrix m{};
        EXPECT_FALSE(Qr::encode(bad, m));
    }
}

// ---------------------------------------------------------------------------
// Structure
//
// The cheap half: these catch a broken encoder, not a subtly wrong one. They
// earn their place by being the claims a reader can check against ISO/IEC
// 18004 without running anything.
// ---------------------------------------------------------------------------

TEST(QrStructure, IsTheVersionTheAppFixed)
{
    Barcode::Matrix m{};
    ASSERT_TRUE(Qr::encode("A1234567", m));
    EXPECT_EQ(Qr::kVersion, 2);
    EXPECT_EQ(m.size, 25) << "version 2 is 17 + 4 * 2 modules a side";
    EXPECT_LE(m.size, Barcode::Matrix::kMaxSize);
}

TEST(QrStructure, ThreeFinderPatternsWithTheirSeparators)
{
    Barcode::Matrix m{};
    ASSERT_TRUE(Qr::encode("gym-card", m));

    for (const auto &centre : {std::make_pair(3, 3),
                               std::make_pair(kSize - 4, 3),
                               std::make_pair(3, kSize - 4)}) {
        for (int dy = -4; dy <= 4; dy++) {
            for (int dx = -4; dx <= 4; dx++) {
                const int x = centre.first + dx, y = centre.second + dy;
                if (x < 0 || y < 0 || x >= kSize || y >= kSize) {
                    continue;
                }
                const int r = ring(dx, dy);
                // Rings 2 and 4 are light -- 4 being the separator.
                EXPECT_EQ(m.dark(static_cast<uint8_t>(x), static_cast<uint8_t>(y)),
                          r != 2 && r != 4)
                    << "finder at (" << centre.first << "," << centre.second << ") offset ("
                    << dx << "," << dy << ")";
            }
        }
    }
}

TEST(QrStructure, TheFourthCornerHasNoFinder)
{
    // The absence is what tells a decoder which way up the symbol is.
    Barcode::Matrix m{};
    ASSERT_TRUE(Qr::encode("gym-card", m));

    int dark = 0;
    for (int y = kSize - 7; y < kSize; y++) {
        for (int x = kSize - 7; x < kSize; x++) {
            if (m.dark(static_cast<uint8_t>(x), static_cast<uint8_t>(y))) {
                dark++;
            }
        }
    }
    EXPECT_NE(dark, 33) << "the bottom-right corner must not be a finder pattern";
}

TEST(QrStructure, OneAlignmentPatternAtEighteenEighteen)
{
    // Version 2's row/column rule gives {6, 18}. Three of the four positions
    // collide with finder patterns and are not drawn; only (18,18) survives.
    Barcode::Matrix m{};
    ASSERT_TRUE(Qr::encode("hello world", m));

    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            EXPECT_EQ(m.dark(static_cast<uint8_t>(18 + dx), static_cast<uint8_t>(18 + dy)),
                      ring(dx, dy) != 1)
                << "alignment offset (" << dx << "," << dy << ")";
        }
    }
}

TEST(QrStructure, TimingPatternsAlternateBetweenTheFinders)
{
    Barcode::Matrix m{};
    ASSERT_TRUE(Qr::encode("hello world", m));

    for (int i = 8; i <= kSize - 9; i++) {
        EXPECT_EQ(m.dark(6, static_cast<uint8_t>(i)), i % 2 == 0) << "column 6 row " << i;
        EXPECT_EQ(m.dark(static_cast<uint8_t>(i), 6), i % 2 == 0) << "row 6 column " << i;
    }
}

TEST(QrStructure, TheDarkModuleIsAlwaysDark)
{
    // (8, 4v + 9). Fixed by the standard and carried by every symbol, so it is
    // the same module for every payload and every mask.
    for (const char *text : {"a", "hello world", "zzzzzzzzzzzzzzzz"}) {
        Barcode::Matrix m{};
        ASSERT_TRUE(Qr::encode(text, m));
        EXPECT_TRUE(m.dark(8, 4 * Qr::kVersion + 9)) << text;
    }
}

TEST(QrStructure, BothCopiesOfTheFormatInformationAgree)
{
    // A decoder reads whichever copy is legible. If they disagreed, a damaged
    // symbol could be read with the wrong mask and decode to nonsense -- or,
    // worse, to something.
    Barcode::Matrix m{};
    ASSERT_TRUE(Qr::encode("Sam's card", m));

    auto bit = [&](int x, int y) { return m.dark(static_cast<uint8_t>(x), static_cast<uint8_t>(y)); };

    for (int i = 0; i <= 5; i++) {
        EXPECT_EQ(bit(8, i), bit(kSize - 1 - i, 8)) << "format bit " << i;
    }
    EXPECT_EQ(bit(8, 7), bit(kSize - 7, 8));
    EXPECT_EQ(bit(8, 8), bit(kSize - 8, 8));
    for (int i = 9; i < 15; i++) {
        EXPECT_EQ(bit(14 - i, 8), bit(8, kSize - 15 + i)) << "format bit " << i;
    }
}

TEST(QrStructure, TheFormatInformationRecordsLevelMAndTheChosenMask)
{
    // Read back out of the symbol rather than recomputed, so this is evidence
    // about the format strip and not just about the selector.
    for (const char *text : {"a", "gym-card", "hello world", "zzzzzzzzzzzzzzzz"}) {
        const int mask = Qr::chosenMask(text);
        EXPECT_GE(mask, 0) << text;
        EXPECT_LE(mask, 7) << text;

        Barcode::Matrix forced{}, chosen{};
        ASSERT_TRUE(Qr::encodeWithMask(text, forced, static_cast<uint8_t>(mask)));
        ASSERT_TRUE(Qr::encode(text, chosen));
        EXPECT_EQ(gridOf(forced), gridOf(chosen))
            << "encode() must produce exactly what its recorded mask says it did: " << text;
    }
}

TEST(QrMask, EveryMaskProducesADifferentSymbol)
{
    std::set<std::string> grids;
    for (uint8_t mask = 0; mask < 8; mask++) {
        Barcode::Matrix m{};
        ASSERT_TRUE(Qr::encodeWithMask("hello world", m, mask));
        grids.insert(gridOf(m));
    }
    EXPECT_EQ(grids.size(), 8u);
}

TEST(QrMask, RefusesAMaskOutsideTheEight)
{
    Barcode::Matrix m{};
    EXPECT_FALSE(Qr::encodeWithMask("hello world", m, 8));
    EXPECT_FALSE(Qr::encodeWithMask("hello world", m, 255));
}

TEST(QrMask, TheChosenMaskScoresNoWorseThanAnyOther)
{
    // The selector's whole job, stated as a property rather than by asserting
    // which mask a particular payload gets. Nothing about correctness rests on
    // this -- every mask decodes, which is why the oracle forces the mask -- but
    // a selector that silently always picked 0 would look identical from
    // outside without it.
    for (const char *text : {"a", "gym-card", "hello world", "aB3!aB3!aB3!aB3!"}) {
        SCOPED_TRACE(text);
        const int chosen = Qr::chosenMask(text);
        ASSERT_GE(chosen, 0);

        // The function map is a property of the version, not of the payload,
        // so it is built once. Grid's constructor clears whatever it is handed,
        // which is why the finished symbol is copied in afterwards rather than
        // scored in place.
        Barcode::Matrix scratch{};
        Qr::detail::Grid probe(scratch);
        Qr::detail::drawFunctionPatterns(probe);

        uint32_t chosenScore = 0;
        std::vector<uint32_t> scores;
        for (uint8_t mask = 0; mask < 8; mask++) {
            Barcode::Matrix m{};
            ASSERT_TRUE(Qr::encodeWithMask(text, m, mask));
            probe.modules = m;
            const uint32_t score = Qr::detail::penalty(probe);
            scores.push_back(score);
            if (mask == static_cast<uint8_t>(chosen)) {
                chosenScore = score;
            }
        }
        for (size_t i = 0; i < scores.size(); i++) {
            EXPECT_LE(chosenScore, scores[i]) << "mask " << i;
        }
    }
}

// ---------------------------------------------------------------------------
// The seam
//
// Symbology.hpp is the only file that names more than one symbology, and these
// are the claims the service and the two widgets rely on it for.
// ---------------------------------------------------------------------------

TEST(SymbologySeam, Code128IsLinearAndQrIsNot)
{
    EXPECT_FALSE(Barcode::isMatrix(Barcode::Format::Code128));
    EXPECT_TRUE(Barcode::isMatrix(Barcode::Format::Qr));
}

TEST(SymbologySeam, AskingForTheWrongShapeIsRefusedRatherThanGuessed)
{
    Barcode::Encoded linear{};
    EXPECT_FALSE(Barcode::encode(Barcode::Format::Qr, "A1234567", linear));
    EXPECT_EQ(linear.totalModules, 0);

    Barcode::Matrix matrix{};
    EXPECT_FALSE(Barcode::encode(Barcode::Format::Code128, "A1234567", matrix));
    EXPECT_EQ(matrix.size, 0);
}

TEST(SymbologySeam, IsDrawableAnswersForEitherKind)
{
    EXPECT_TRUE(Barcode::isDrawable(Barcode::Format::Code128, "A1234567"));
    EXPECT_TRUE(Barcode::isDrawable(Barcode::Format::Qr, "A1234567"));
    EXPECT_FALSE(Barcode::isDrawable(Barcode::Format::Code128, ""));
    EXPECT_FALSE(Barcode::isDrawable(Barcode::Format::Qr, ""));
}

// ---------------------------------------------------------------------------
// parseFormat: the compatibility promise, as a test
//
// An input.json written before the fmtN field existed must keep working and
// keep meaning exactly what it meant. That is decided here and nowhere else.
// ---------------------------------------------------------------------------

TEST(ParseFormat, AnAbsentOrEmptyValueMeansCode128)
{
    // The declared default is the literal "Code128", so this is the *other*
    // route to the same place: a file hand-edited to clear the value. Both must
    // land on Code 128, or an input.json written before this field existed
    // stops meaning what it meant.
    Barcode::Format format = Barcode::Format::Qr;
    EXPECT_TRUE(Barcode::parseFormat("", format));
    EXPECT_EQ(format, Barcode::Format::Code128);

    format = Barcode::Format::Qr;
    EXPECT_TRUE(Barcode::parseFormat(nullptr, format));
    EXPECT_EQ(format, Barcode::Format::Code128);
}

TEST(ParseFormat, Code128IsZeroSoAValueInitialisedCodeDraws)
{
    // Every Code in this app is value-initialised somewhere, so zero has to be
    // a format that draws rather than a hole.
    EXPECT_EQ(static_cast<uint8_t>(Barcode::Format::Code128), 0);

    Barcode::Code code{};
    EXPECT_EQ(code.format, Barcode::Format::Code128);
}

TEST(ParseFormat, TheTwoWordsAreMatchedWithoutRegardToCase)
{
    // The wearer is typing into a plain text box -- the SDK has no enum field
    // type -- so refusing "qrcode" because it wanted "QRCode" would be a
    // spelling test rather than a safety check.
    struct { const char *text; Barcode::Format want; } kCases[] = {
        { "Code128", Barcode::Format::Code128 },
        { "code128", Barcode::Format::Code128 },
        { "CODE128", Barcode::Format::Code128 },
        { "CoDe128", Barcode::Format::Code128 },
        { "QRCode",  Barcode::Format::Qr },
        { "qrcode",  Barcode::Format::Qr },
        { "QRCODE",  Barcode::Format::Qr },
        { "QrCoDe",  Barcode::Format::Qr },
    };

    for (const auto &c : kCases) {
        Barcode::Format format = Barcode::Format::Code128;
        EXPECT_TRUE(Barcode::parseFormat(c.text, format)) << c.text;
        EXPECT_EQ(format, c.want) << c.text;
    }
}

TEST(ParseFormat, TheManifestsDefaultIsAWordThisParses)
{
    // AppConfigFields.cpp declares "Code128" as the default, and the phone
    // pre-fills the form with it. If this ever stopped parsing, every code
    // would be refused the moment a wearer opened the form and saved it.
    Barcode::Format format = Barcode::Format::Qr;
    EXPECT_TRUE(Barcode::parseFormat("Code128", format));
    EXPECT_EQ(format, Barcode::Format::Code128);
}

TEST(ParseFormat, RefusesAnythingElseRatherThanFallingBack)
{
    // Falling back to Code 128 here would draw something other than what the
    // file asked for, silently. The wearer's own id either way, so not the
    // harm Barcode.hpp is about -- but not a thing this app does.
    //
    // "qr" is on this list deliberately. It is the obvious thing to type and it
    // was the spelling in an earlier draft, but two words for one format is a
    // wart; refusing it costs nothing, because the screen says what to use and
    // the phone's pattern means only a hand-edited file gets here.
    for (const char *bad : {"qr", "QR", "qr code", "QRCode ", " QRCode", "q",
                            "code", "code 128", "code39", "ean13", "1", "0"}) {
        Barcode::Format format = Barcode::Format::Code128;
        EXPECT_FALSE(Barcode::parseFormat(bad, format)) << "should not parse: " << bad;
    }
}
