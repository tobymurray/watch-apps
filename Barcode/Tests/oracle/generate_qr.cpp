/**
 ******************************************************************************
 * @file    generate_qr.cpp
 * @brief   Regenerate zint_qr_vectors.hpp: QR module grids from zint.
 ******************************************************************************
 *
 * The companion to generate.cpp, and for a stronger reason. Code 128's table
 * can be held to the symbology's own structure and read against a published
 * table; Reed-Solomon over GF(256), the format information's BCH and the eight
 * mask patterns cannot be checked by eye at all. A mis-encoded symbol whose
 * error correction is computed consistently over the mistake is not caught by
 * its own checksum -- it decodes to a valid, wrong value, which is the one
 * outcome README.md says must never happen. So the encoder is diffed against
 * an independent implementation rather than argued about.
 *
 * Not part of the test build: it needs libzint, the tests must build without
 * it, and the vectors are data rather than something to recompute on every run.
 *
 *   apt-get install -y libzint-dev
 *   g++ -std=c++17 -o generate_qr generate_qr.cpp -lzint && ./generate_qr > zint_qr_vectors.hpp
 *
 * ## The mask is forced, and why that makes this a better test
 *
 * Every one of the eight masks yields a decodable symbol -- the mask is
 * recorded in the format information -- so which one an encoder picks is a
 * matter of judgement and not of correctness. Diffing an auto-chosen mask
 * would therefore test two penalty-scoring implementations against each other
 * and call a legal difference a bug. Forcing the mask instead tests the part
 * that *is* correctness -- the codewords, the error correction, the placement,
 * the function patterns and the format strings -- across all eight, which is
 * far more of the encoder than one auto-chosen mask would reach.
 *
 * ## Why the corpus looks like this
 *
 * This app's encoder emits a **single byte-mode segment**, always: the version
 * is fixed, so a denser mode would only buy a smaller symbol that would not be
 * drawn, and having no mode selection is one less thing to get wrong. zint
 * optimises across modes and will split a payload into byte and numeric
 * segments where that is shorter -- for "a1234567" it encodes `a` as a byte
 * and `1234567` as numerics -- which is a different, equally legal encoding
 * and a different grid.
 *
 * So the corpus is restricted to payloads where a single byte segment *is* the
 * optimal encoding, by a rule rather than by trial and error: each contains at
 * least one character outside QR's alphanumeric set (0-9, A-Z, space, $%*+-./:)
 * and no digit run long enough to pay for a numeric segment. Payloads outside
 * that rule -- notably the parkrun shapes, which are pure alphanumerics -- are
 * covered instead by decoding this encoder's own output with zbar, which does
 * not care which mode produced the grid. See Tests/README.md.
 *
 * The zint version is recorded, because a change in its mask or placement
 * behaviour would otherwise read as a change in ours.
 *
 ******************************************************************************
 */

#include <zint.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{

/// Chosen by the rule in the header comment: a character outside the
/// alphanumeric set, and no digit run worth a numeric segment. Spread over the
/// lengths and shapes that decide a byte-mode encoding -- one character, the
/// longest id this app accepts, and both sides of the version's capacity.
const char *kCorpus[] = {
    // shortest, and single characters at both ends of the printable range
    "a", "x", "~", "!",
    // ordinary ids of the kind this format is for
    "abc", "b12", "gym-card", "tobym", "member/9f2", "q1w2e3r4t5",
    // spaces and punctuation
    "hello world", "Sam's card", "London Bridge", "!\"#$%&'()*+,-./",
    // the longest id this app accepts, in two flavours
    "zzzzzzzzzzzzzzzz", "aB3!aB3!aB3!aB3!",
    // the version's own capacity, and one below it
    "abcdefghijklmnopqrstuvwxy", "abcdefghijklmnopqrstuvwxyz",
};

std::string escape(const char *s)
{
    std::string out;
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\\') { out += '\\'; }
        out += *p;
    }
    return out;
}

/// The grid as hex, row-major, most significant bit first. 625 modules is 79
/// bytes and 158 characters -- compact enough to commit, and it is the grid
/// itself rather than a digest, so a failing test can say *where* it differs.
bool grid(const char *data, int mask, std::string &out)
{
    zint_symbol *s = ZBarcode_Create();
    s->symbology = BARCODE_QRCODE;
    s->option_1  = 2;                  // error-correction level M
    s->option_2  = 2;                  // version 2
    s->option_3  = (mask + 1) << 8;    // force the mask rather than let zint choose
    s->show_hrt  = 0;

    if (ZBarcode_Encode(s, (const unsigned char *)data, (int)strlen(data)) >= ZINT_ERROR) {
        fprintf(stderr, "zint refused \"%s\" mask %d: %s\n", data, mask, s->errtxt);
        ZBarcode_Delete(s);
        return false;
    }
    if (s->width != 25 || s->rows != 25) {
        fprintf(stderr, "zint gave \"%s\" a %dx%d symbol, wanted 25x25\n",
                data, s->width, s->rows);
        ZBarcode_Delete(s);
        return false;
    }

    static const char *kHex = "0123456789abcdef";
    out.clear();
    unsigned acc = 0;
    int bits = 0;
    for (int y = 0; y < s->rows; y++) {
        for (int x = 0; x < s->width; x++) {
            acc = (acc << 1) | ((s->encoded_data[y][x >> 3] >> (x & 7)) & 1u);
            if (++bits == 4) { out += kHex[acc & 0xF]; acc = 0; bits = 0; }
        }
    }
    if (bits) { out += kHex[(acc << (4 - bits)) & 0xF]; }

    ZBarcode_Delete(s);
    return true;
}

} // namespace

int main()
{
    const int v = ZBarcode_Version();
    char version[32];
    snprintf(version, sizeof version, "zint %d.%d.%d", v / 10000, (v / 100) % 100, v % 100);

    printf("/* GENERATED by oracle/generate_qr.cpp -- do not edit.\n");
    printf(" *\n");
    printf(" * QR version 2, error-correction level M, module grids from %s, an\n", version);
    printf(" * independent implementation. One entry per payload per mask: the grid as\n");
    printf(" * hex, row-major, most significant bit first, 625 modules in 158 nibbles.\n");
    printf(" *\n");
    printf(" * The mask is forced rather than chosen, so this tests the encoding and not\n");
    printf(" * two penalty scores against each other. See generate_qr.cpp for why the\n");
    printf(" * corpus is what it is.\n");
    printf(" */\n\n");
    printf("#ifndef ZINT_QR_VECTORS_HPP\n#define ZINT_QR_VECTORS_HPP\n\n");
    printf("namespace ZintQrVectors\n{\n\n");
    printf("constexpr char kZintVersion[] = \"%s\";\n\n", version);
    printf("constexpr int kSize = 25;\n\n");
    printf("struct Vector\n{\n    const char *id;\n    int         mask;\n"
           "    const char *grid;\n};\n\n");
    printf("constexpr Vector kVectors[] = {\n");

    for (const char *d : kCorpus) {
        for (int mask = 0; mask < 8; mask++) {
            std::string hex;
            if (!grid(d, mask, hex)) {
                return 1;
            }
            printf("    { \"%s\", %d, \"%s\" },\n", escape(d).c_str(), mask, hex.c_str());
        }
    }

    printf("};\n\nconstexpr int kVectorCount = sizeof(kVectors) / sizeof(kVectors[0]);\n\n");
    printf("} // namespace ZintQrVectors\n\n#endif // ZINT_QR_VECTORS_HPP\n");
    return 0;
}
