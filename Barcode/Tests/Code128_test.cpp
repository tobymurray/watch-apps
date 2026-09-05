/**
 ******************************************************************************
 * @file    Code128_test.cpp
 * @brief   The encoder, and the 107-row spec table underneath it.
 ******************************************************************************
 *
 * The README says a barcode that scans as somebody else's athlete number is
 * the one genuinely harmful thing this app could do. Until this file existed,
 * the whole of that guarantee was a comment saying the table had been
 * "cross-checked against the published reference table on 2026-07-25" -- once,
 * by eye, by one person.
 *
 * A transposed row in kPatterns is the failure that comment is standing in
 * front of, and it is invisible: the app starts, the screen fills with bars,
 * the bars scan, and they decode to the wrong number. So the table tests below
 * are not box-ticking. They are the part of this suite that is load-bearing.
 *
 * There is no external oracle here -- no reference implementation to diff
 * against -- so the table is held to Code 128's own structure instead, which a
 * hand-transcription error breaks even when the row still looks plausible:
 *
 *   - 107 rows, six elements each, every element 1..4
 *   - every row sums to 11 modules
 *   - the three bars of every row sum to an EVEN number, and the three spaces
 *     to an odd one. This is the symbology's self-checking property, and it is
 *     the strongest structural claim available: it fails for most single-digit
 *     slips that leave the row summing to 11.
 *   - all 107 rows distinct -- a duplicate is an ambiguous decode
 *   - the four published constants (Start A/B/C, Stop) match their documented
 *     patterns exactly
 *
 * That is not proof the table is right. It is a great deal more than a comment.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Code128.hpp"
#include "oracle/zint_vectors.hpp"

namespace {

/// Elements per symbol in the table (the Stop symbol's 13th module is carried
/// separately by the encoder, not in the table).
constexpr size_t kElementsPerSymbol = 6;
constexpr size_t kSymbolModules     = 11;

std::vector<uint8_t> widthsOf(const Barcode::Encoded &e)
{
    return std::vector<uint8_t>(e.widths, e.widths + e.count);
}

/// Symbol values back out of a width run, by reverse map rather than by
/// indexing, so it cannot simply agree with the encoder's own bookkeeping.
/// Returns empty if any six-element row is not in the table.
///
/// Defined here rather than beside the round-trip tests because the subset C
/// tests need it too: what a switch decision produced is a question about the
/// symbol stream, and reading the widths is the only way to ask it.
std::vector<uint8_t> decodeSymbols(const Barcode::Encoded &e)
{
    std::map<std::vector<uint8_t>, uint8_t> reverse;
    for (size_t v = 0; v < 107; v++) {
        reverse[std::vector<uint8_t>(Code128::kPatterns[v],
                                     Code128::kPatterns[v] + kElementsPerSymbol)] =
            static_cast<uint8_t>(v);
    }

    std::vector<uint8_t> values;
    // The trailing extra bar is not part of a symbol.
    const size_t symbolBytes = e.count - 1;
    for (size_t i = 0; i + kElementsPerSymbol <= symbolBytes; i += kElementsPerSymbol) {
        std::vector<uint8_t> row(e.widths + i, e.widths + i + kElementsPerSymbol);
        auto it = reverse.find(row);
        if (it == reverse.end()) {
            return {};
        }
        values.push_back(it->second);
    }
    return values;
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

TEST(Code128Table, EverySymbolHasSixElementsInRange)
{
    for (size_t v = 0; v < 107; v++) {
        for (size_t i = 0; i < kElementsPerSymbol; i++) {
            const uint8_t e = Code128::kPatterns[v][i];
            EXPECT_GE(e, 1) << "symbol " << v << " element " << i;
            EXPECT_LE(e, 4) << "symbol " << v << " element " << i;
        }
    }
}

TEST(Code128Table, EverySymbolIsElevenModulesWide)
{
    for (size_t v = 0; v < 107; v++) {
        int sum = 0;
        for (size_t i = 0; i < kElementsPerSymbol; i++) {
            sum += Code128::kPatterns[v][i];
        }
        EXPECT_EQ(sum, static_cast<int>(kSymbolModules)) << "symbol " << v;
    }
}

/// Code 128's self-checking property, and the one test here most likely to
/// catch a transcription slip: a wrong digit that keeps the row at 11 modules
/// almost always breaks the bar/space parity.
TEST(Code128Table, BarsSumEvenAndSpacesSumOdd)
{
    for (size_t v = 0; v < 107; v++) {
        const uint8_t *p = Code128::kPatterns[v];
        const int bars   = p[0] + p[2] + p[4];
        const int spaces = p[1] + p[3] + p[5];
        EXPECT_EQ(bars % 2, 0) << "symbol " << v << " bars sum " << bars;
        EXPECT_EQ(spaces % 2, 1) << "symbol " << v << " spaces sum " << spaces;
    }
}

TEST(Code128Table, AllSymbolsAreDistinct)
{
    std::set<std::vector<uint8_t>> seen;
    for (size_t v = 0; v < 107; v++) {
        std::vector<uint8_t> row(Code128::kPatterns[v],
                                 Code128::kPatterns[v] + kElementsPerSymbol);
        EXPECT_TRUE(seen.insert(row).second)
            << "symbol " << v << " duplicates an earlier row -- ambiguous decode";
    }
}

/// The patterns quoted in every published description of Code 128. If the
/// table were shifted or misaligned anywhere, these are what would move.
///
/// Code C and Code B are here because the encoder now depends on them: they
/// are the switch symbols, and they sit next to Shift and Code A in a run of
/// six near-identical rows at the tail of the table. Getting 99 and 100 the
/// wrong way round would still produce a barcode, and it would decode as a
/// different string -- exactly the failure this file exists to catch.
TEST(Code128Table, PublishedStartStopAndSwitchPatternsMatch)
{
    const std::map<size_t, std::vector<uint8_t>> kPublished = {
        { 99,  { 1, 1, 3, 1, 4, 1 } }, // Code C -- into the digit-pair subset
        { 100, { 1, 1, 4, 1, 3, 1 } }, // Code B -- and back out of it
        { 103, { 2, 1, 1, 4, 1, 2 } }, // Start Code A
        { 104, { 2, 1, 1, 2, 1, 4 } }, // Start Code B
        { 105, { 2, 1, 1, 2, 3, 2 } }, // Start Code C
        { 106, { 2, 3, 3, 1, 1, 1 } }, // Stop (plus the trailing bar of 2)
    };
    for (const auto &[value, pattern] : kPublished) {
        std::vector<uint8_t> row(Code128::kPatterns[value],
                                 Code128::kPatterns[value] + kElementsPerSymbol);
        EXPECT_EQ(row, pattern) << "symbol " << value;
    }
    EXPECT_EQ(Code128::kCodeC, 99);
    EXPECT_EQ(Code128::kCodeB, 100);
    EXPECT_EQ(Code128::kStartB, 104);
    EXPECT_EQ(Code128::kStartC, 105);
    EXPECT_EQ(Code128::kStop, 106);
}

// ---------------------------------------------------------------------------
// Golden vector
//
// Every width for a short id, worked out by hand from the symbol values and
// the checksum rule and written out in full. If the encoder starts emitting
// something else -- a symbol in the wrong order, the check digit in the wrong
// place, the Stop's extra bar missing -- this is what says so.
// ---------------------------------------------------------------------------

TEST(Code128Encode, GoldenVectorForA1)
{
    // "A1" -> Start B(104), 'A'(33), '1'(17), check, Stop(106) + trailing bar.
    // check = (104 + 33*1 + 17*2) mod 103 = 171 mod 103 = 68.
    const std::vector<uint8_t> kExpected = {
        2, 1, 1, 2, 1, 4, // Start B  = 104
        1, 1, 1, 3, 2, 3, // 'A'      = 33
        1, 2, 3, 2, 2, 1, // '1'      = 17
        1, 4, 1, 2, 2, 1, // check    = 68
        2, 3, 3, 1, 1, 1, // Stop     = 106
        2,                // Stop's 13th module
    };

    Barcode::Encoded e{};
    ASSERT_TRUE(Code128::encode("A1", e));
    EXPECT_EQ(widthsOf(e), kExpected);
    EXPECT_EQ(e.totalModules, 57); // 5 symbols * 11 + 2
}

/// A parkrun-shaped id, checked on the arithmetic rather than the full widths.
///
/// This is the test that changed when subset C arrived, and the arithmetic is
/// worth following because it is where the switching either pays or does not.
/// `A1234567` is a letter and seven digits. The letter has to be subset B. The
/// digit run is seven long -- odd -- so spending its first digit in B as well
/// leaves six, which pair up into three symbols:
///
///   Start B(104), 'A'(33), '1'(17), Code C(99), 23, 45, 67, check, Stop
///
/// Nine symbols against the eleven pure subset B needed. The switch symbol
/// carries a weight like any other, which is the easiest thing here to get
/// wrong:
///
///   check = (104 + 33*1 + 17*2 + 99*3 + 23*4 + 45*5 + 67*6) mod 103
///         = 1187 mod 103 = 54.
TEST(Code128Encode, ChecksumForAParkrunShapedId)
{
    Barcode::Encoded e{};
    ASSERT_TRUE(Code128::encode("A1234567", e));

    // Start plus six data symbols come first, so the check is the eighth.
    const size_t checkStart = 7 * kElementsPerSymbol;
    std::vector<uint8_t> check(e.widths + checkStart,
                              e.widths + checkStart + kElementsPerSymbol);
    std::vector<uint8_t> expected(Code128::kPatterns[54],
                                 Code128::kPatterns[54] + kElementsPerSymbol);
    EXPECT_EQ(check, expected) << "check symbol should be value 54";
    EXPECT_EQ(e.totalModules, 101); // 9 symbols * 11 + 2
}

TEST(Code128Encode, ChangingOneCharacterChangesTheCheckSymbol)
{
    Barcode::Encoded a{}, b{};
    ASSERT_TRUE(Code128::encode("A1234567", a));
    ASSERT_TRUE(Code128::encode("A1234568", b));
    EXPECT_NE(widthsOf(a), widthsOf(b));
}

/// The weighting is positional, so a transposition must not encode the same.
/// A checksum that dropped the position multiplier would pass everything above
/// and fail this.
TEST(Code128Encode, TranspositionDoesNotProduceTheSameSymbols)
{
    Barcode::Encoded a{}, b{};
    ASSERT_TRUE(Code128::encode("AB", a));
    ASSERT_TRUE(Code128::encode("BA", b));
    EXPECT_NE(widthsOf(a), widthsOf(b));
}

// ---------------------------------------------------------------------------
// Structure of the output
// ---------------------------------------------------------------------------

TEST(Code128Encode, ModuleCountFollowsTheSymbolCount)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const std::string id(len, 'X');
        Barcode::Encoded e{};
        ASSERT_TRUE(Code128::encode(id.c_str(), e)) << "length " << len;

        // Start + data + check + Stop, then the Stop's extra bar.
        const size_t symbols = len + 3;
        EXPECT_EQ(e.count, symbols * kElementsPerSymbol + 1) << "length " << len;
        EXPECT_EQ(e.totalModules, symbols * kSymbolModules + 2) << "length " << len;
    }
}

TEST(Code128Encode, WidthsSumToTotalModules)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const std::string id(len, 'M');
        Barcode::Encoded e{};
        ASSERT_TRUE(Code128::encode(id.c_str(), e));

        int sum = 0;
        for (uint8_t i = 0; i < e.count; i++) {
            sum += e.widths[i];
        }
        EXPECT_EQ(sum, e.totalModules) << "length " << len;
    }
}

/// The longest id fills the buffer exactly. Worth pinning: kMaxWidths is
/// derived from a formula, and this is the case where getting it wrong writes
/// past the end.
TEST(Code128Encode, TheLongestIdFillsTheBufferExactlyAndNoFurther)
{
    const std::string id(Code128::kMaxDataLength, 'W');
    Barcode::Encoded e{};
    ASSERT_TRUE(Code128::encode(id.c_str(), e));
    EXPECT_EQ(e.count, Barcode::Encoded::kMaxWidths);
    EXPECT_EQ(e.count, 151u);
}

TEST(Code128Encode, FirstElementIsAlwaysABar)
{
    // The renderer relies on this: it alternates starting from "bar", so an
    // encoder that ever led with a space would invert the whole symbol.
    //
    // Every id now begins with Start B or Start C depending on its digits, and
    // both of those open with a bar two modules wide -- so the claim survives
    // subset switching, but it can no longer be made by naming Start B.
    for (const char *id : { "A", "A1", "0", "~", "ABCDEFGHIJKLMNOP",
                            "12345678", "12", "0123456789ABCDEF" }) {
        Barcode::Encoded e{};
        ASSERT_TRUE(Code128::encode(id, e)) << id;

        const bool startsB = e.widths[0] == Code128::kPatterns[Code128::kStartB][0];
        const bool startsC = e.widths[0] == Code128::kPatterns[Code128::kStartC][0];
        EXPECT_TRUE(startsB || startsC) << id;
        EXPECT_EQ(e.widths[0], 2) << id;
    }
}

// ---------------------------------------------------------------------------
// Subset C
//
// The encoder packs a pair of digits into one symbol wherever that shortens
// the barcode. It matters because a symbol is 11 modules whatever it holds and
// the bars get a fixed 200 px, so the symbol count is the X-dimension -- see
// Layout_test.cpp, where the same change moves a numeric id across the retail
// floor it used to miss.
//
// These tests are about the *decision*, not the arithmetic: the round-trip
// tests already prove any given encoding decodes back to its id. What could go
// wrong here is switching when it does not pay, or failing to when it does,
// neither of which produces a wrong barcode -- only a needlessly narrow one.
// ---------------------------------------------------------------------------

TEST(Code128SubsetC, GoldenVectorForAnAllDigitId)
{
    // "12345678" -> Start C(105), 12, 34, 56, 78, check, Stop(106) + bar.
    // check = (105 + 12*1 + 34*2 + 56*3 + 78*4) mod 103 = 665 mod 103 = 47.
    const std::vector<uint8_t> kExpected = {
        2, 1, 1, 2, 3, 2, // Start C = 105
        1, 1, 2, 2, 3, 2, // "12"    = 12
        1, 3, 1, 1, 2, 3, // "34"    = 34
        3, 3, 1, 1, 2, 1, // "56"    = 56
        2, 4, 1, 1, 1, 2, // "78"    = 78
        1, 3, 3, 1, 2, 1, // check   = 47
        2, 3, 3, 1, 1, 1, // Stop    = 106
        2,                // Stop's 13th module
    };

    Barcode::Encoded e{};
    ASSERT_TRUE(Code128::encode("12345678", e));
    EXPECT_EQ(widthsOf(e), kExpected);
    EXPECT_EQ(e.totalModules, 79); // 7 symbols * 11 + 2, against 123 in B alone
}

/// INVARIANT. Switching may never make a barcode longer than pure subset B
/// would have. This is the property the buffer bound rests on -- kMaxWidths is
/// sized for one symbol per character -- so a rule change that broke it would
/// be a buffer overrun and not just a wasted opportunity.
TEST(Code128SubsetC, NoIdEncodesWiderThanPureSubsetBWould)
{
    for (const char *id : { "A", "A1", "0", "12", "123", "1234", "12345",
                            "A1234567", "12345678", "1A2B3C4D", "999999999999",
                            "0123456789ABCDEF", "1234567890123456",
                            "~ !\"#", "WWWWWWWWWWWW" }) {
        Barcode::Encoded e{};
        ASSERT_TRUE(Code128::encode(id, e)) << id;

        // What subset B alone would have produced: one symbol a character,
        // plus start, check and stop.
        const size_t pureB = (std::string(id).size() + 3) * kSymbolModules + 2;
        EXPECT_LE(e.totalModules, pureB) << id;
        EXPECT_LE(e.count, Barcode::Encoded::kMaxWidths) << id;
    }
}

/// An id with no digit run long enough to pay for a switch must encode exactly
/// as it did before subset C existed -- same start, same symbols, same widths.
/// Most ids people carry are in this set, so this is the test that says the
/// change was additive.
TEST(Code128SubsetC, IdsThatCannotBenefitAreUnchanged)
{
    for (const char *id : { "A", "AB", "parkrun", "A1", "A12", "A123",
                            "~ !\"#", "WWWWWWWWWWWW", "1A2B3C4D" }) {
        Barcode::Encoded e{};
        ASSERT_TRUE(Code128::encode(id, e)) << id;

        const std::vector<uint8_t> v = decodeSymbols(e);
        EXPECT_EQ(v.front(), Code128::kStartB) << id;
        for (size_t i = 1; i + 2 < v.size(); i++) {
            EXPECT_NE(v[i], Code128::kCodeC) << id << " switched at " << i;
            EXPECT_EQ(v[i], static_cast<uint8_t>(id[i - 1] - 32)) << id << " at " << i;
        }
        const size_t pureB = (std::string(id).size() + 3) * kSymbolModules + 2;
        EXPECT_EQ(e.totalModules, pureB) << id;
    }
}

/// Four digits is break-even and the encoder takes it; three is not and it
/// does not. Stated because it is the boundary the rule is written around: a
/// switch costs one symbol and each pair saves one, so four digits are three
/// symbols in C against four in B.
TEST(Code128SubsetC, SwitchesMidIdOnlyOnceARunPaysForIt)
{
    Barcode::Encoded three{}, four{};
    ASSERT_TRUE(Code128::encode("A123", three));
    ASSERT_TRUE(Code128::encode("A1234", four));

    const std::vector<uint8_t> t = decodeSymbols(three);
    EXPECT_EQ(std::count(t.begin(), t.end(), Code128::kCodeC), 0)
        << "a three-digit run does not pay for a switch";

    const std::vector<uint8_t> f = decodeSymbols(four);
    EXPECT_EQ(std::count(f.begin(), f.end(), Code128::kCodeC), 1)
        << "a four-digit run does";
    EXPECT_LT(four.totalModules, three.totalModules + kSymbolModules)
        << "A1234 is one character longer than A123 and no wider";
}

/// An id that is entirely digits starts in subset C, because Start C replaces
/// Start B rather than following it -- so there is no switch to pay for and
/// even two digits are worth pairing.
TEST(Code128SubsetC, AnAllDigitIdStartsInSubsetC)
{
    for (const char *id : { "12", "1234", "12345678", "1234567890123456" }) {
        Barcode::Encoded e{};
        ASSERT_TRUE(Code128::encode(id, e)) << id;
        EXPECT_EQ(decodeSymbols(e).front(), Code128::kStartC) << id;
    }

    // A single digit has nothing to pair with.
    Barcode::Encoded one{};
    ASSERT_TRUE(Code128::encode("7", one));
    EXPECT_EQ(decodeSymbols(one).front(), Code128::kStartB);
}

/// Sixteen digits now cost what eight characters used to. This is the number
/// the geometry tests care about, restated here where the encoder can be held
/// to it.
TEST(Code128SubsetC, SixteenDigitsCostWhatEightCharactersUsedTo)
{
    Barcode::Encoded digits{}, letters{};
    ASSERT_TRUE(Code128::encode("1234567890123456", digits));
    ASSERT_TRUE(Code128::encode("AAAAAAAA", letters));
    EXPECT_EQ(digits.totalModules, 123);
    EXPECT_EQ(letters.totalModules, 123);
}

// ---------------------------------------------------------------------------
// What it refuses
//
// Every one of these is a refusal rather than a repair, and that is the point:
// a truncated or substituted id is a wrong id, and a wrong id scans.
// ---------------------------------------------------------------------------

TEST(Code128Encode, RefusesEmpty)
{
    Barcode::Encoded e{};
    EXPECT_FALSE(Code128::encode("", e));
}

TEST(Code128Encode, RefusesOverlongRatherThanTruncating)
{
    const std::string tooLong(Code128::kMaxDataLength + 1, 'A');
    Barcode::Encoded e{};
    EXPECT_FALSE(Code128::encode(tooLong.c_str(), e));

    const std::string atLimit(Code128::kMaxDataLength, 'A');
    Barcode::Encoded ok{};
    EXPECT_TRUE(Code128::encode(atLimit.c_str(), ok));
}

TEST(Code128Encode, AcceptsTheWholePrintableAsciiRange)
{
    for (int c = 32; c <= 126; c++) {
        const char id[2] = { static_cast<char>(c), '\0' };
        Barcode::Encoded e{};
        EXPECT_TRUE(Code128::encode(id, e)) << "char " << c;
    }
}

TEST(Code128Encode, RefusesOutsidePrintableAscii)
{
    for (int c : { 1, 9, 10, 13, 31, 127 }) {
        const char id[2] = { static_cast<char>(c), '\0' };
        Barcode::Encoded e{};
        EXPECT_FALSE(Code128::encode(id, e)) << "char " << c;
    }
}

TEST(Code128Encode, RefusesHighBytes)
{
    // Signed char on ARM and on most hosts, so 0x80+ arrives negative; the
    // range check has to reject either way.
    const char id[] = { static_cast<char>(0xC3), static_cast<char>(0xA9), '\0' };
    Barcode::Encoded e{};
    EXPECT_FALSE(Code128::encode(id, e));
}

TEST(Code128Encode, ARefusalLeavesNothingPartiallyEncoded)
{
    // The GUI keys "draw nothing" off encode() failing, so a refusal that had
    // already written widths would leave a partial barcode behind if anything
    // ever drew on a false return.
    Barcode::Encoded e{};
    const std::string tooLong(Code128::kMaxDataLength + 1, 'A');
    ASSERT_FALSE(Code128::encode(tooLong.c_str(), e));
    EXPECT_EQ(e.count, 0u);
    EXPECT_EQ(e.totalModules, 0u);
}

// ---------------------------------------------------------------------------
// Round trip
//
// A decoder built the other way round -- pattern back to value, via a reverse
// map rather than by indexing -- then the symbol stream read back and checked
// against the id and the checksum rule. It shares the table, so it cannot
// prove the table right; what it catches is the encoder's own bookkeeping.
// ---------------------------------------------------------------------------

/**
 * @brief Read a symbol stream back to text the way a scanner has to.
 *
 * Subset-aware, which is the whole point: it follows the start character and
 * the switch symbols instead of assuming subset B, so it is evidence about the
 * switching rather than a restatement of them.
 *
 * The trap it has to survive is that **symbol value 99 means two different
 * things**. In subset B it is Code C, the switch; in subset C it is the digit
 * pair "99". An id of `999999999999` is six consecutive 99s that are digits,
 * not six switches, and a decoder that tested the value before the subset
 * would read it as an empty string. Sets @p err rather than asserting, so a
 * failure names the id.
 */
std::string decodeText(const Barcode::Encoded &e, std::string &err)
{
    err.clear();
    const std::vector<uint8_t> v = decodeSymbols(e);
    if (v.empty()) { err = "a symbol did not match any table row"; return ""; }
    if (v.size() < 4) { err = "too few symbols"; return ""; }
    if (v.back() != Code128::kStop) { err = "last symbol is not Stop"; return ""; }
    if (e.widths[e.count - 1] != 2) { err = "Stop is missing its trailing bar"; return ""; }

    bool inC;
    if (v.front() == Code128::kStartB)      { inC = false; }
    else if (v.front() == Code128::kStartC) { inC = true; }
    else { err = "start is neither B nor C"; return ""; }

    // The check is the weighted sum of the start and every data symbol, each
    // by its position. Recomputed here from the stream, so a check written in
    // the wrong place or omitting a switch symbol's weight shows up.
    uint32_t sum = v.front();
    for (size_t i = 1; i + 2 < v.size(); i++) {
        sum += static_cast<uint32_t>(v[i]) * static_cast<uint32_t>(i);
    }
    if (v[v.size() - 2] != sum % 103) {
        err = "check symbol is " + std::to_string(v[v.size() - 2])
            + ", rule gives " + std::to_string(sum % 103);
        return "";
    }

    std::string text;
    for (size_t i = 1; i + 2 < v.size(); i++) {
        const uint8_t value = v[i];
        if (inC) {
            if (value == Code128::kCodeB) { inC = false; continue; }
            if (value > 99) { err = "subset C symbol out of range"; return ""; }
            text += static_cast<char>('0' + value / 10);
            text += static_cast<char>('0' + value % 10);
        } else {
            if (value == Code128::kCodeC) { inC = true; continue; }
            if (value > 94) { err = "subset B symbol out of range"; return ""; }
            text += static_cast<char>(value + 32);
        }
    }
    return text;
}

TEST(Code128RoundTrip, EveryIdReadsBackAsItselfWithAValidCheck)
{
    for (const char *id : { "A", "A1", "A1234567", "parkrun", "~ !\"#",
                            "0123456789ABCDEF", "12345678", "1234567890123456",
                            "0", "12", "123", "1234", "12345", "A123", "A1234",
                            "999999999999", "99", "9999", "1A2B3C4D",
                            " ", "~", "00000000" }) {
        Barcode::Encoded e{};
        ASSERT_TRUE(Code128::encode(id, e)) << id;

        std::string err;
        const std::string back = decodeText(e, err);
        EXPECT_TRUE(err.empty()) << id << ": " << err;
        EXPECT_EQ(back, id) << "round trip changed the id";
    }
}

/// The id an unwary decoder gets wrong, stated on its own because it is the
/// one case where the symbology is genuinely ambiguous without context.
TEST(Code128RoundTrip, NinesInSubsetCAreDigitsAndNotSwitches)
{
    Barcode::Encoded e{};
    ASSERT_TRUE(Code128::encode("999999999999", e));

    const std::vector<uint8_t> v = decodeSymbols(e);
    ASSERT_EQ(v.size(), 9u) << "Start C, six digit pairs, check, Stop";
    EXPECT_EQ(v.front(), Code128::kStartC);
    for (size_t i = 1; i <= 6; i++) {
        EXPECT_EQ(v[i], 99) << "pair " << i << " is the digits 99";
    }

    std::string err;
    EXPECT_EQ(decodeText(e, err), "999999999999");
    EXPECT_TRUE(err.empty()) << err;
}

} // namespace

// ---------------------------------------------------------------------------
// Diffed against an independent implementation
//
// Everything above is, in the end, this table and this encoder checking each
// other: the structural tests hold kPatterns to the symbology's rules, and the
// round trip shares the table it is testing -- as its own comment says, it
// cannot prove the table right. A self-consistently wrong table survives all
// of it.
//
// oracle/generate.cpp records what zint produces for a corpus of ids --
// separate implementation, separate table, separate subset selection -- and
// commits the module patterns as data, so nothing here needs zint at runtime.
// A pattern zint agrees with is one two implementations reached independently.
// ---------------------------------------------------------------------------

namespace {

std::string ourWidthString(const char *id)
{
    Barcode::Encoded e{};
    if (!Code128::encode(id, e)) {
        return "";
    }
    std::string out;
    for (uint8_t i = 0; i < e.count; i++) {
        out += static_cast<char>('0' + e.widths[i]);
    }
    return out;
}

uint16_t ourModules(const char *id)
{
    Barcode::Encoded e{};
    return Code128::encode(id, e) ? e.totalModules : 0;
}

} // namespace

/// INVARIANT, and the strongest claim in this file: for every id in the corpus
/// this encoder produces the same bars, module for module, as an independent
/// implementation. Not "the same length" -- the same bars.
TEST(ZintOracle, EveryVectorMatchesZintExactly)
{
    for (int i = 0; i < ZintVectors::kVectorCount; i++) {
        const ZintVectors::Vector &v = ZintVectors::kVectors[i];
        ASSERT_NE(ourModules(v.id), 0) << "we refuse an id zint encoded: " << v.id;
        EXPECT_EQ(ourModules(v.id), v.totalModules)
            << v.id << ": module count differs from " << ZintVectors::kZintVersion;
        EXPECT_EQ(ourWidthString(v.id), std::string(v.widths))
            << v.id << ": bars differ from " << ZintVectors::kZintVersion;
    }
}

/// The same corpus, read back through the decoder above. Agreeing with zint and
/// decoding to the id are different claims -- two implementations could share a
/// misreading of the standard -- and this is the one that says what a scanner
/// will report.
TEST(ZintOracle, EveryVectorAlsoDecodesToItself)
{
    for (int i = 0; i < ZintVectors::kVectorCount; i++) {
        const ZintVectors::Vector &v = ZintVectors::kVectors[i];
        Barcode::Encoded e{};
        ASSERT_TRUE(Code128::encode(v.id, e)) << v.id;
        std::string err;
        EXPECT_EQ(decodeText(e, err), std::string(v.id)) << v.id << ": " << err;
    }
}

/// Every printable character, on its own, through the round trip -- so a single
/// wrong row cannot hide behind a corpus. The range test above proves the
/// encoder accepts them; this proves each one comes back.
TEST(Code128RoundTrip, EveryPrintableCharacterReadsBackAlone)
{
    for (char c = 32; c < 127; c++) {
        const char id[2] = { c, '\0' };
        Barcode::Encoded e{};
        ASSERT_TRUE(Code128::encode(id, e)) << "char " << static_cast<int>(c);
        std::string err;
        EXPECT_EQ(decodeText(e, err), std::string(id))
            << "char " << static_cast<int>(c) << ": " << err;
    }
}
