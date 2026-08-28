/**
 ******************************************************************************
 * @file    Symbology.hpp
 * @date    28-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   One entry point from a code's format to the widths it draws as.
 ******************************************************************************
 *
 * The seam, and the only file that knows more than one symbology's name. The
 * service validates through it and the widget draws through it, so neither has
 * a reason to include a particular encoder -- which is what keeps adding a
 * symbology to a new header and a new case rather than a change spread across
 * both halves of the app.
 *
 * There is exactly one format today. That is the point of the file rather than
 * an argument against it: the dispatch existing while there is one answer is
 * what made subset C a change to Code128.hpp alone, and the same holds for
 * whatever comes next. Docs/SYMBOLOGIES.md ranks the candidates and says why
 * ITF is not the obvious winner it looks like.
 *
 * @note This handles *linear* symbologies only, which is every candidate in
 *       that document. A matrix symbology such as QR produces a grid of
 *       modules rather than a run of widths, so it fits neither
 *       Barcode::Encoded nor the widget that draws it -- see the same document.
 *
 ******************************************************************************
 */

#ifndef SYMBOLOGY_HPP
#define SYMBOLOGY_HPP

#include "Barcode.hpp"
#include "Code128.hpp"
#include "Encoded.hpp"

namespace Barcode
{

/**
 * @brief Encode @p text in @p format.
 * @retval true  Encoded; @p out is what to draw.
 * @retval false @p format cannot carry @p text. The caller has no fallback --
 *               see Barcode.hpp on why a barcode that scans as something else
 *               is worse than none.
 *
 * The switch has no default, deliberately: adding a Format without an encoder
 * is then a compiler warning here rather than a silent refusal on the watch.
 */
inline bool encode(Format format, const char *text, Encoded &out)
{
    switch (format) {
    case Format::Code128:
        return Code128::encode(text, out);
    }
    return false;
}

} // namespace Barcode

#endif // SYMBOLOGY_HPP
