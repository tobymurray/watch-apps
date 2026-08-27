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

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Code128.hpp"

namespace {

/// Elements per symbol in the table (the Stop symbol's 13th module is carried
/// separately by the encoder, not in the table).
constexpr size_t kElementsPerSymbol = 6;
constexpr size_t kSymbolModules     = 11;

std::vector<uint8_t> widthsOf(const Code128::Encoded &e)
{
    return std::vector<uint8_t>(e.widths, e.widths + e.count);
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

/// The four patterns quoted in every published description of Code 128. If the
/// table were shifted or misaligned anywhere, these are what would move.
TEST(Code128Table, PublishedStartAndStopPatternsMatch)
{
    const std::map<size_t, std::vector<uint8_t>> kPublished = {
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
    EXPECT_EQ(Code128::kStartB, 104);
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

    Code128::Encoded e{};
    ASSERT_TRUE(Code128::encode("A1", e));
    EXPECT_EQ(widthsOf(e), kExpected);
    EXPECT_EQ(e.totalModules, 57); // 5 symbols * 11 + 2
}

/// A parkrun-shaped id, checked on the arithmetic rather than the full widths:
/// check = (104 + 33*1 + 17*2 + 18*3 + 19*4 + 20*5 + 21*6 + 22*7 + 23*8) mod 103
///       = 865 mod 103 = 41.
TEST(Code128Encode, ChecksumForAParkrunShapedId)
{
    Code128::Encoded e{};
    ASSERT_TRUE(Code128::encode("A1234567", e));

    // The check symbol is the second-to-last, immediately before Stop: Start
    // plus eight data symbols come first, so it is the tenth.
    const size_t checkStart = 9 * kElementsPerSymbol;
    std::vector<uint8_t> check(e.widths + checkStart,
                              e.widths + checkStart + kElementsPerSymbol);
    std::vector<uint8_t> expected(Code128::kPatterns[41],
                                 Code128::kPatterns[41] + kElementsPerSymbol);
    EXPECT_EQ(check, expected) << "check symbol should be value 41";
    EXPECT_EQ(e.totalModules, 123); // 11 symbols * 11 + 2
}

TEST(Code128Encode, ChangingOneCharacterChangesTheCheckSymbol)
{
    Code128::Encoded a{}, b{};
    ASSERT_TRUE(Code128::encode("A1234567", a));
    ASSERT_TRUE(Code128::encode("A1234568", b));
    EXPECT_NE(widthsOf(a), widthsOf(b));
}

/// The weighting is positional, so a transposition must not encode the same.
/// A checksum that dropped the position multiplier would pass everything above
/// and fail this.
TEST(Code128Encode, TranspositionDoesNotProduceTheSameSymbols)
{
    Code128::Encoded a{}, b{};
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
        Code128::Encoded e{};
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
        Code128::Encoded e{};
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
    Code128::Encoded e{};
    ASSERT_TRUE(Code128::encode(id.c_str(), e));
    EXPECT_EQ(e.count, Code128::Encoded::kMaxWidths);
    EXPECT_EQ(e.count, 115u);
}

TEST(Code128Encode, FirstElementIsAlwaysABar)
{
    // The renderer relies on this: it alternates starting from "bar", so an
    // encoder that ever led with a space would invert the whole symbol.
    for (const char *id : { "A", "A1", "0", "~", "ABCDEFGHIJKLMNOP" }) {
        Code128::Encoded e{};
        ASSERT_TRUE(Code128::encode(id, e)) << id;
        // Start B's first element, for every id.
        EXPECT_EQ(e.widths[0], Code128::kPatterns[Code128::kStartB][0]) << id;
    }
}

// ---------------------------------------------------------------------------
// What it refuses
//
// Every one of these is a refusal rather than a repair, and that is the point:
// a truncated or substituted id is a wrong id, and a wrong id scans.
// ---------------------------------------------------------------------------

TEST(Code128Encode, RefusesEmpty)
{
    Code128::Encoded e{};
    EXPECT_FALSE(Code128::encode("", e));
}

TEST(Code128Encode, RefusesOverlongRatherThanTruncating)
{
    const std::string tooLong(Code128::kMaxDataLength + 1, 'A');
    Code128::Encoded e{};
    EXPECT_FALSE(Code128::encode(tooLong.c_str(), e));

    const std::string atLimit(Code128::kMaxDataLength, 'A');
    Code128::Encoded ok{};
    EXPECT_TRUE(Code128::encode(atLimit.c_str(), ok));
}

TEST(Code128Encode, AcceptsTheWholePrintableAsciiRange)
{
    for (int c = 32; c <= 126; c++) {
        const char id[2] = { static_cast<char>(c), '\0' };
        Code128::Encoded e{};
        EXPECT_TRUE(Code128::encode(id, e)) << "char " << c;
    }
}

TEST(Code128Encode, RefusesOutsidePrintableAscii)
{
    for (int c : { 1, 9, 10, 13, 31, 127 }) {
        const char id[2] = { static_cast<char>(c), '\0' };
        Code128::Encoded e{};
        EXPECT_FALSE(Code128::encode(id, e)) << "char " << c;
    }
}

TEST(Code128Encode, RefusesHighBytes)
{
    // Signed char on ARM and on most hosts, so 0x80+ arrives negative; the
    // range check has to reject either way.
    const char id[] = { static_cast<char>(0xC3), static_cast<char>(0xA9), '\0' };
    Code128::Encoded e{};
    EXPECT_FALSE(Code128::encode(id, e));
}

TEST(Code128Encode, ARefusalLeavesNothingPartiallyEncoded)
{
    // The GUI keys "draw nothing" off encode() failing, so a refusal that had
    // already written widths would leave a partial barcode behind if anything
    // ever drew on a false return.
    Code128::Encoded e{};
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

std::vector<uint8_t> decodeSymbols(const Code128::Encoded &e)
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

TEST(Code128RoundTrip, SymbolsReadBackAsTheIdWithAValidCheck)
{
    for (const char *id : { "A", "A1234567", "parkrun", "~ !\"#", "0123456789ABCDEF" }) {
        Code128::Encoded e{};
        ASSERT_TRUE(Code128::encode(id, e)) << id;

        const std::vector<uint8_t> values = decodeSymbols(e);
        ASSERT_FALSE(values.empty()) << id << ": a symbol did not match any table row";

        const size_t len = std::string(id).size();
        ASSERT_EQ(values.size(), len + 3) << id;

        EXPECT_EQ(values.front(), Code128::kStartB) << id;
        EXPECT_EQ(values.back(), Code128::kStop) << id;

        // The data symbols are the id, offset by 32.
        for (size_t i = 0; i < len; i++) {
            EXPECT_EQ(values[1 + i], static_cast<uint8_t>(id[i] - 32)) << id << " at " << i;
        }

        // And the check symbol satisfies the rule, computed here independently.
        uint32_t sum = Code128::kStartB;
        for (size_t i = 0; i < len; i++) {
            sum += static_cast<uint32_t>(id[i] - 32) * static_cast<uint32_t>(i + 1);
        }
        EXPECT_EQ(values[len + 1], sum % 103) << id;
    }
}

} // namespace
