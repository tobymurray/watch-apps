/**
 ******************************************************************************
 * @file    Symbology.hpp
 * @date    28-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   One entry point from a code's format to what it draws as.
 ******************************************************************************
 *
 * The seam, and the only file that knows more than one symbology's name. The
 * service validates through it and the widgets draw through it, so none of
 * them has a reason to include a particular encoder -- which is what keeps
 * adding a symbology to a new header and a new case rather than a change
 * spread across both halves of the app.
 *
 * There are two intermediate forms, because there are two *kinds* of
 * symbology and no single form describes both:
 *
 *   Barcode::Encoded  a run of alternating bar and space widths. Complete for
 *                     any linear symbology; cannot carry a grid.
 *   Barcode::Matrix   a square grid of light and dark modules. Complete for a
 *                     matrix symbology; has no left-to-right run structure.
 *
 * An earlier draft of this file said a matrix symbology "fits neither", which
 * was true while there was one form. The fix was not to make Encoded a variant
 * -- that would have put a discriminant and a branch on the path that draws
 * every parkrun barcode, to serve a format that path cannot draw -- but to give
 * the matrix case its own form and its own widget. The linear path is byte for
 * byte what it was. Docs/QR.md has the argument.
 *
 * isMatrix() is how a caller that does not care -- the service, deciding
 * whether an id is drawable at all -- picks which overload to ask.
 *
 ******************************************************************************
 */

#ifndef SYMBOLOGY_HPP
#define SYMBOLOGY_HPP

#include "Barcode.hpp"
#include "Code128.hpp"
#include "Encoded.hpp"
#include "Itf.hpp"
#include "Matrix.hpp"
#include "Qr.hpp"

namespace Barcode
{

/**
 * @brief Does @p format produce a module grid rather than a run of widths?
 *
 * The switch has no default, deliberately, for the same reason the encoders
 * below have none: adding a Format is then a compiler warning in this file
 * rather than a silent misclassification on the watch.
 */
inline bool isMatrix(Format format)
{
    switch (format) {
    case Format::Code128:
    case Format::Itf:
        return false;
    case Format::Qr:
        return true;
    }
    return false;
}

/**
 * @brief Encode @p text in @p format as a linear symbology.
 * @retval true  Encoded; @p out is what to draw.
 * @retval false @p format cannot carry @p text, or is not linear. The caller
 *               has no fallback -- see Barcode.hpp on why a barcode that scans
 *               as something else is worse than none.
 */
inline bool encode(Format format, const char *text, Encoded &out)
{
    switch (format) {
    case Format::Code128:
        return Code128::encode(text, out);
    case Format::Itf:
        return Itf::encode(text, out);
    case Format::Qr:
        // Asked for the wrong shape. Refusing rather than asserting, because
        // the only way here is a caller that skipped isMatrix(), and a blank
        // screen is the correct outcome of that mistake.
        return false;
    }
    return false;
}

/**
 * @brief Encode @p text in @p format as a matrix symbology.
 * @retval true  Encoded; @p out is the module grid, quiet zone excluded.
 * @retval false @p format cannot carry @p text, or is not a matrix symbology.
 */
inline bool encode(Format format, const char *text, Matrix &out)
{
    switch (format) {
    case Format::Qr:
        return Qr::encode(text, out);
    case Format::Code128:
    case Format::Itf:
        return false;
    }
    return false;
}

/**
 * @brief Can @p format draw @p text at all?
 *
 * What the service asks before it adopts a code, and the reason it does not
 * have to know which kind of symbology it was handed. Every rule about what an
 * id may contain lives in an encoder, and nothing here has an opinion of its
 * own -- a second one would be a second thing to keep in step.
 *
 * The two branches answer differently, and the asymmetry is measured rather
 * than stylistic. Code 128 answers by encoding, because its encoder is a
 * 642-byte table and a loop that the service links anyway. QR answers through
 * Qr::accepts(), because encoding a probe would drag Reed-Solomon, eight mask
 * patterns and the penalty scoring into the service binary -- 2.3 KB, in a
 * process that never draws anything. That is not a second rule: Qr::encode()
 * begins by calling the same function, so there is exactly one definition of
 * what QR accepts and both callers use it.
 */
inline bool isDrawable(Format format, const char *text)
{
    switch (format) {
    case Format::Code128: {
        Encoded probe {};
        return Code128::encode(text, probe);
    }
    case Format::Itf:
        // Answers without the table, like QR and for a milder version of the
        // same reason: the rule is "an even number of digits" and encoding a
        // probe to discover that would be work for no extra certainty.
        return Itf::accepts(text);
    case Format::Qr:
        return Qr::accepts(text);
    }
    return false;
}

/**
 * @brief Read a format out of a configuration value.
 * @param text  The `fmtN` field, as the file carried it.
 * @param out   Set only when this returns true.
 * @retval true  Recognised; @p out is the format.
 * @retval false Not a format this app draws.
 *
 * Two words, `Code128` and `QRCode`, matched **without regard to case**. The
 * phone renders a string field as a plain text box, so the wearer is typing;
 * refusing `qrcode` because it wanted `QRCode` would be a spelling test rather
 * than a safety check, and nothing about which symbology to draw is
 * case-sensitive. The manifest's pattern spells both cases out per letter,
 * since the SDK's pattern dialect has no inline case-insensitive flag -- so the
 * phone accepts exactly what this does.
 *
 * **An empty or absent value means Code 128.** The declared default is the
 * literal "Code128", so a key missing from the file reads as that; this handles
 * the other route, a file hand-edited to clear the value. Both keep an
 * input.json written before this field existed meaning exactly what it meant,
 * and both have tests.
 *
 * `qr` on its own is *not* accepted. It is the obvious thing to type and it was
 * the spelling in an earlier draft, but two words for one format is a wart, and
 * refusing it is safe: nothing gets drawn wrongly, the screen says what to use,
 * and the phone's pattern means only a hand-edited file can produce it.
 */
inline bool parseFormat(const char *text, Format &out)
{
    auto equals = [](const char *a, const char *b) {
        for (size_t i = 0;; i++) {
            char ca = a[i];
            if (ca >= 'A' && ca <= 'Z') {
                ca = static_cast<char>(ca - 'A' + 'a');
            }
            if (ca != b[i]) {
                return false;
            }
            if (ca == '\0') {
                return true;
            }
        }
    };

    if (text == nullptr || text[0] == '\0' || equals(text, "code128")) {
        out = Format::Code128;
        return true;
    }
    if (equals(text, "qrcode")) {
        out = Format::Qr;
        return true;
    }
    if (equals(text, "itf")) {
        out = Format::Itf;
        return true;
    }
    return false;
}

/**
 * @brief How a linear symbol should be laid out on this panel.
 *
 * Not a symbology and not a style: it is which of two ways of turning element
 * widths into pixels a format is drawn with, and the difference is worth a
 * name because it is the difference between a bar edge that steps and one that
 * does not.
 *
 *   Scaled      The run is stretched to fill the widget, so an element is a
 *               fractional number of pixels and its edges are anti-aliased.
 *   WholePixel  An element is a whole number of pixels and the symbol is
 *               centred in whatever that leaves. No edge lands mid-pixel, so
 *               nothing is anti-aliased and the wide:narrow ratio is exact.
 *
 * WholePixel is better on this panel and it is not free: rounding the element
 * width down costs up to a fifth of the symbol's width, which is width the
 * quiet zone then absorbs. Code 128 does not take that trade, because its
 * modules are already near one pixel at the lengths people use and rounding
 * down would halve them. ITF at 3:1 has pixels to spare and buys crisp edges
 * with them. See Docs/ITF.md.
 *
 * Lives here rather than in the widget so that the widget still does not know
 * one symbology from another -- it asks this, the way MainView asks
 * isMatrix(). A third linear format needs a case here and nothing in the GUI.
 */
enum class Render : uint8_t {
    Scaled,
    WholePixel,
};

inline Render renderStyle(Format format)
{
    switch (format) {
    case Format::Code128:
        return Render::Scaled;
    case Format::Itf:
        return Render::WholePixel;
    case Format::Qr:
        // Not drawn by the linear widget at all; QrWidget is whole-pixel by
        // construction. Answered rather than left to fall through so the
        // switch stays exhaustive.
        return Render::WholePixel;
    }
    return Render::Scaled;
}

} // namespace Barcode

#endif // SYMBOLOGY_HPP
