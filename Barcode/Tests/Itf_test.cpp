/**
 ******************************************************************************
 * @file    Itf_test.cpp
 * @brief   The ITF encoder, its ten-row table, and a diff against zint.
 ******************************************************************************
 *
 * ITF is the one symbology this app draws that can decode *wrongly* rather
 * than merely fail. It is continuous, it has no check character, and a scan
 * that clips the top or bottom of the symbol can come back as a valid shorter
 * number. Bearer bars are the mitigation and Layout_test.cpp holds the
 * geometry to them; this file is about the widths.
 *
 * Three kinds of claim, weakest to strongest:
 *
 *   - **Structure.** Ten rows, five elements each, exactly two wide. That is
 *     the "2 of 5" and it is what a transcription slip breaks.
 *   - **Round trip.** The widths read back as the digits, through a decoder
 *     built the other way round. Shares the table, so it cannot prove the
 *     table right -- it catches the interleaving and the bookkeeping.
 *   - **zint.** Width for width against an independent implementation, which
 *     is the only claim here that does not share anything with the encoder.
 *
 * What is deliberately not claimed: that any of it scans. Nothing in this file
 * has seen a panel, and ITF on a reflective LCD is unproven the same way
 * everything else here is.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "Encoded.hpp"
#include "Itf.hpp"
#include "Symbology.hpp"
#include "oracle/zint_itf_vectors.hpp"

namespace {

std::vector<uint8_t> widthsOf(const Barcode::Encoded &e)
{
    return std::vector<uint8_t>(e.widths, e.widths + e.count);
}

/// The widths a vector records, as a list. Single digits throughout, because
/// ITF has only two element sizes and both are one character.
std::vector<uint8_t> widthsOf(const char *text)
{
    std::vector<uint8_t> out;
    for (const char *p = text; *p; p++) {
        out.push_back(static_cast<uint8_t>(*p - '0'));
    }
    return out;
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

/// The symbology's own rule, and the one a mistyped row breaks: five elements
/// a digit, exactly two of them wide. "2 of 5" is the name of the code.
TEST(ItfTable, EveryDigitHasExactlyTwoWideOfFive)
{
    for (size_t d = 0; d < 10; d++) {
        int wide = 0;
        for (size_t e = 0; e < Itf::kElementsPerDigit; e++) {
            const uint8_t flag = Itf::kWideFlags[d][e];
            EXPECT_LE(flag, 1u) << "digit " << d << " element " << e << " is not a flag";
            wide += flag;
        }
        EXPECT_EQ(wide, static_cast<int>(Itf::kWidePerDigit)) << "digit " << d;
    }
}

TEST(ItfTable, AllTenRowsAreDistinct)
{
    std::set<std::vector<uint8_t>> seen;
    for (size_t d = 0; d < 10; d++) {
        std::vector<uint8_t> row(Itf::kWideFlags[d],
                                 Itf::kWideFlags[d] + Itf::kElementsPerDigit);
        EXPECT_TRUE(seen.insert(row).second)
            << "digit " << d << " duplicates an earlier row -- ambiguous decode";
    }
}

/// The ratio is two constants and nothing else, so that changing it is one
/// edit. 3:1 is deliberate -- see the header of Itf.hpp -- and it is also what
/// makes the zint diff below a width-for-width comparison.
TEST(ItfTable, TheRatioIsThreeToOne)
{
    EXPECT_EQ(Itf::kNarrow, 1);
    EXPECT_EQ(Itf::kWide, 3);
}

// ---------------------------------------------------------------------------
// What it accepts
// ---------------------------------------------------------------------------

TEST(ItfAccepts, TakesAnEvenNumberOfDigits)
{
    for (const char *id : { "12", "1234", "123456", "1234567890123456" }) {
        EXPECT_TRUE(Itf::accepts(id)) << id;
    }
}

/// **Refuses odd rather than padding**, which is the one place this encoder
/// deliberately differs from every other ITF implementation including zint.
/// Padding a leading zero makes the symbol scan as a number the wearer did not
/// type, and that is the harm the app exists to prevent.
TEST(ItfAccepts, RefusesAnOddNumberOfDigitsRatherThanPadding)
{
    for (const char *id : { "1", "123", "12345", "123456789012345" }) {
        EXPECT_FALSE(Itf::accepts(id)) << id;

        Barcode::Encoded e{};
        EXPECT_FALSE(Itf::encode(id, e)) << id;
        EXPECT_EQ(e.count, 0u) << id << ": a refusal left widths behind";
        EXPECT_EQ(e.totalModules, 0u) << id;
    }
}

TEST(ItfAccepts, RefusesAnythingThatIsNotADigit)
{
    for (const char *id : { "", "A1", "12A4", "12 34", "12-34", "１２" }) {
        EXPECT_FALSE(Itf::accepts(id)) << id;
    }
}

TEST(ItfAccepts, RefusesOverlongRatherThanTruncating)
{
    EXPECT_TRUE(Itf::accepts("1234567890123456"));       // 16
    EXPECT_FALSE(Itf::accepts("123456789012345678"));    // 18
    EXPECT_FALSE(Itf::accepts("12345678901234567890"));  // 20
}

// ---------------------------------------------------------------------------
// Why it refuses -- the split Barcode::Refusal turns into a specific screen
// ---------------------------------------------------------------------------

TEST(ItfDiagnose, OkForEverythingAcceptsAccepts)
{
    for (const char *id : { "12", "1234", "123456", "1234567890123456" }) {
        EXPECT_EQ(Itf::diagnose(id), Itf::Diagnosis::Ok) << id;
    }
}

/// A stray non-digit is BadCharacters even when the digit count would
/// otherwise have been fine -- "12A4" is four characters, the right count,
/// and still not what the wearer meant to type.
TEST(ItfDiagnose, BadCharactersForAnyNonDigit)
{
    for (const char *id : { "A1", "12A4", "12 34", "12-34", "１２" }) {
        EXPECT_EQ(Itf::diagnose(id), Itf::Diagnosis::BadCharacters) << id;
    }
}

/// All-digit, but the wrong shape of digits: empty, odd, or over the limit.
/// None of these has a character to point at, so BadCount rather than
/// BadCharacters -- see diagnose()'s null case for the same reasoning.
TEST(ItfDiagnose, BadCountForEmptyOddOrOverlong)
{
    for (const char *id : { "", "1", "123", "12345",
                            "123456789012345", "123456789012345678" }) {
        EXPECT_EQ(Itf::diagnose(id), Itf::Diagnosis::BadCount) << id;
    }
}

TEST(ItfDiagnose, BadCountForNullptr)
{
    EXPECT_EQ(Itf::diagnose(nullptr), Itf::Diagnosis::BadCount);
}

/// The first bad character wins over a count problem further along, since
/// diagnose() (like accepts()) reads left to right and stops at the first
/// thing wrong: "1234A67" is both a stray letter and, if that letter were a
/// digit, still an odd count -- but the letter is what a wearer needs to
/// see named first.
TEST(ItfDiagnose, ACharacterProblemWinsOverACountProblemInTheSameId)
{
    EXPECT_EQ(Itf::diagnose("1234A67"), Itf::Diagnosis::BadCharacters);
}

// ---------------------------------------------------------------------------
// Structure of the output
// ---------------------------------------------------------------------------

TEST(ItfEncode, ElementAndUnitCountsFollowTheDigitCount)
{
    for (size_t digits = 2; digits <= Itf::kMaxDataLength; digits += 2) {
        std::string id;
        for (size_t i = 0; i < digits; i++) id += static_cast<char>('0' + (i % 10));

        Barcode::Encoded e{};
        ASSERT_TRUE(Itf::encode(id.c_str(), e)) << id;

        EXPECT_EQ(e.count, Itf::elementsFor(digits)) << id;
        EXPECT_EQ(e.totalModules, Itf::unitsFor(digits)) << id;

        // 5 elements a digit plus 7 for start and stop; 9 units a digit plus 9.
        EXPECT_EQ(e.count, 5 * digits + 7) << id;
        EXPECT_EQ(e.totalModules, 9 * digits + 9) << id;

        uint32_t sum = 0;
        for (uint8_t i = 0; i < e.count; i++) sum += e.widths[i];
        EXPECT_EQ(sum, e.totalModules) << id;
    }
}

TEST(ItfEncode, EveryElementIsNarrowOrWideAndNothingElse)
{
    Barcode::Encoded e{};
    ASSERT_TRUE(Itf::encode("1234567890123456", e));
    for (uint8_t i = 0; i < e.count; i++) {
        EXPECT_TRUE(e.widths[i] == Itf::kNarrow || e.widths[i] == Itf::kWide)
            << "element " << static_cast<int>(i) << " is " << static_cast<int>(e.widths[i]);
    }
}

/// The renderer alternates from "bar", so an encoder that led with a space
/// would invert the whole symbol. ITF's start is four narrow elements, so the
/// first is a narrow bar.
TEST(ItfEncode, FirstElementIsANarrowBar)
{
    for (const char *id : { "12", "0000", "9999999999999999" }) {
        Barcode::Encoded e{};
        ASSERT_TRUE(Itf::encode(id, e)) << id;
        EXPECT_EQ(e.widths[0], Itf::kNarrow) << id;
    }
}

/// The longest id fits the buffer the two encoders share, with room left.
TEST(ItfEncode, TheLongestIdFitsTheSharedBuffer)
{
    Barcode::Encoded e{};
    ASSERT_TRUE(Itf::encode("1234567890123456", e));
    EXPECT_EQ(e.count, 87u);
    EXPECT_LE(e.count, Barcode::Encoded::kMaxWidths);
}

/// Golden vector, worked out by hand from the table. Four digits, so start,
/// two interleaved pairs and stop.
///
///   start  n n n n
///   1 / 2  bars  wnnnw, spaces nwnnw, interleaved
///   3 / 4  bars  wwnnn, spaces nnwnw, interleaved
///   stop   w n n
TEST(ItfEncode, GoldenVectorForFourDigits)
{
    const std::vector<uint8_t> kExpected = {
        1, 1, 1, 1,             // start: narrow bar/space/bar/space
        3, 1, 1, 3, 1, 1, 1, 1, 3, 3,  // '1' bars w n n n w / '2' spaces n w n n w
        3, 1, 3, 1, 1, 3, 1, 1, 1, 3,  // '3' bars w w n n n / '4' spaces n n w n w
        3, 1, 1,                // stop: wide bar, narrow space, narrow bar
    };

    Barcode::Encoded e{};
    ASSERT_TRUE(Itf::encode("1234", e));
    EXPECT_EQ(widthsOf(e), kExpected);
    EXPECT_EQ(e.totalModules, 45);  // 9 * 4 + 9
}

// ---------------------------------------------------------------------------
// Round trip
//
// A decoder built the other way round: elements back to narrow/wide, then
// de-interleaved into digits. Shares the table, so it is evidence about the
// interleaving and not about the table.
// ---------------------------------------------------------------------------

std::string decodeItf(const Barcode::Encoded &e, std::string &err)
{
    err.clear();
    if (e.count < 5 * 2 + 7 || (e.count - 7) % 5 != 0) { err = "element count is not 5n+7"; return ""; }

    // Start and stop, before anything is read from between them.
    for (int i = 0; i < 4; i++) {
        if (e.widths[i] != Itf::kNarrow) { err = "start is not four narrow elements"; return ""; }
    }
    const uint8_t *stop = e.widths + e.count - 3;
    if (stop[0] != Itf::kWide || stop[1] != Itf::kNarrow || stop[2] != Itf::kNarrow) {
        err = "stop is not wide/narrow/narrow";
        return "";
    }

    // Reverse the table: a five-flag pattern back to the digit it came from.
    auto digitFor = [&](const uint8_t *flags) -> int {
        for (int d = 0; d < 10; d++) {
            bool same = true;
            for (size_t k = 0; k < Itf::kElementsPerDigit; k++) {
                if (Itf::kWideFlags[d][k] != flags[k]) { same = false; break; }
            }
            if (same) return d;
        }
        return -1;
    };

    std::string text;
    const size_t pairs = (e.count - 7) / 10;
    for (size_t p = 0; p < pairs; p++) {
        const uint8_t *run = e.widths + 4 + p * 10;
        uint8_t bars[Itf::kElementsPerDigit], spaces[Itf::kElementsPerDigit];
        for (size_t k = 0; k < Itf::kElementsPerDigit; k++) {
            const uint8_t b = run[2 * k], s = run[2 * k + 1];
            if ((b != Itf::kNarrow && b != Itf::kWide) || (s != Itf::kNarrow && s != Itf::kWide)) {
                err = "an element is neither narrow nor wide";
                return "";
            }
            bars[k]   = b == Itf::kWide ? 1 : 0;
            spaces[k] = s == Itf::kWide ? 1 : 0;
        }
        const int first = digitFor(bars), second = digitFor(spaces);
        if (first < 0 || second < 0) { err = "a five-element group is not a digit"; return ""; }
        text += static_cast<char>('0' + first);
        text += static_cast<char>('0' + second);
    }
    return text;
}

TEST(ItfRoundTrip, EveryIdReadsBackAsItself)
{
    for (const char *id : { "12", "00", "99", "1234", "0123456789", "1032547698",
                            "00012345678905", "1234567890123456", "9999999999999999",
                            "1010101010", "5555555555", "10", "01" }) {
        Barcode::Encoded e{};
        ASSERT_TRUE(Itf::encode(id, e)) << id;

        std::string err;
        const std::string back = decodeItf(e, err);
        EXPECT_TRUE(err.empty()) << id << ": " << err;
        EXPECT_EQ(back, id) << "round trip changed the id";
    }
}

/// Every digit read back from a bar position and from a space position. The
/// two ids are complements: the first puts the even digits on the bars, the
/// second puts them on the spaces, so between them each of the ten is decoded
/// from both roles.
TEST(ItfRoundTrip, EveryDigitDecodesFromBothBarAndSpacePositions)
{
    for (const char *id : { "0123456789", "1032547698" }) {
        Barcode::Encoded e{};
        ASSERT_TRUE(Itf::encode(id, e)) << id;
        std::string err;
        EXPECT_EQ(decodeItf(e, err), id) << err;
    }
}

// ---------------------------------------------------------------------------
// zint
//
// The only claim in this file that shares nothing with the encoder.
// ---------------------------------------------------------------------------

TEST(ZintItfOracle, EveryVectorMatchesWidthForWidth)
{
    ASSERT_GT(ZintItfVectors::kVectorCount, 20) << "the corpus shrank";

    for (int i = 0; i < ZintItfVectors::kVectorCount; i++) {
        const auto &v = ZintItfVectors::kVectors[i];

        Barcode::Encoded e{};
        ASSERT_TRUE(Itf::encode(v.id, e)) << v.id << " (" << ZintItfVectors::kZintVersion
                                          << " encoded it and this did not)";

        EXPECT_EQ(e.totalModules, v.totalUnits) << v.id;
        EXPECT_EQ(widthsOf(e), widthsOf(v.widths))
            << v.id << ": differs from " << ZintItfVectors::kZintVersion;
    }
}

/// The corpus is even-length only, and that is a stated rule rather than an
/// oversight: zint pads an odd id and this encoder refuses one, so there is no
/// width to compare. Asserted so the rule cannot quietly lapse.
TEST(ZintItfOracle, TheCorpusIsEvenLengthOnlyByRule)
{
    for (int i = 0; i < ZintItfVectors::kVectorCount; i++) {
        const std::string id = ZintItfVectors::kVectors[i].id;
        EXPECT_EQ(id.size() % 2, 0u) << id << " is odd; see oracle/generate_itf.cpp";
    }
}

// ---------------------------------------------------------------------------
// The seam
// ---------------------------------------------------------------------------

/// The config word, matched without regard to case the way the other two are.
/// The manifest's pattern spells the cases out per letter, so the phone accepts
/// exactly this set.
TEST(ItfFormat, TheConfigWordIsItfInAnyCase)
{
    for (const char *word : { "ITF", "itf", "Itf", "iTF" }) {
        Barcode::Format f = Barcode::Format::Code128;
        EXPECT_TRUE(Barcode::parseFormat(word, f)) << word;
        EXPECT_EQ(f, Barcode::Format::Itf) << word;
    }

    // Neighbouring spellings that are not the word. Refusing costs nothing --
    // the screen names the vocabulary and the phone's pattern makes these
    // reachable only from a hand-edited file.
    for (const char *word : { "i2of5", "interleaved", "itf14", "2of5", "ITF-14" }) {
        Barcode::Format f = Barcode::Format::Code128;
        EXPECT_FALSE(Barcode::parseFormat(word, f)) << word;
    }
}

/// ITF is linear, so it goes to the bar renderer and not the matrix one, and
/// it is laid out whole-pixel rather than stretched. Both answers come from
/// Symbology.hpp so that neither widget has to know a format's name.
TEST(ItfFormat, ItIsLinearAndDrawnWholePixel)
{
    EXPECT_FALSE(Barcode::isMatrix(Barcode::Format::Itf));
    EXPECT_EQ(Barcode::renderStyle(Barcode::Format::Itf), Barcode::Render::WholePixel);

    // Code 128 keeps the scaled layout it has always had.
    EXPECT_EQ(Barcode::renderStyle(Barcode::Format::Code128), Barcode::Render::Scaled);
}

/// isDrawable() routes ITF to accepts(), and the two agree by construction.
TEST(ItfFormat, IsDrawableAgreesWithAccepts)
{
    for (const char *id : { "12", "1234567890123456", "1", "123", "A1", "", "12345678901234567" }) {
        EXPECT_EQ(Barcode::isDrawable(Barcode::Format::Itf, id), Itf::accepts(id)) << id;
    }
}

/// Asking the linear encoder for a matrix format, or the other way round, is
/// refused rather than asserted -- and ITF must not have opened a hole in that.
TEST(ItfFormat, TheWrongShapeIsRefused)
{
    Barcode::Matrix m{};
    EXPECT_FALSE(Barcode::encode(Barcode::Format::Itf, "1234", m));

    Barcode::Encoded e{};
    EXPECT_TRUE(Barcode::encode(Barcode::Format::Itf, "1234", e));
}

} // namespace
