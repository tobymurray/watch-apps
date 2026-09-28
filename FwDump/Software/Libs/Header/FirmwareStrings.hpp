/**
 ******************************************************************************
 * @file    FirmwareStrings.hpp
 * @brief   Version strings found in the flash image itself.
 ******************************************************************************
 *
 * Kernel 1.4.0 answers REQUEST_SYSTEM_INFO with FAIL, so the kernel's own
 * account of its version is not available on every firmware this app runs on.
 * The image carries it anyway. In the 1.4.0 dump (whole-image CRC-32
 * 0x14009D03), exactly two whole strings of the form N.N.N lie between NUL
 * bytes: "0.1.4" at 0x08019194, below the kernel's vector table, and "1.4.0" at
 * 0x08168255, above it at 0x08060000. Whether other versions keep that shape
 * is unproven; a dump_context.txt from another version shows it either way,
 * because every match is recorded with its address, not only the one chosen.
 *
 * Free of SDK types, so it is tested on the host.
 *
 ******************************************************************************
 */

#ifndef FIRMWARE_STRINGS_HPP
#define FIRMWARE_STRINGS_HPP

#include <cstddef>
#include <cstdint>

namespace FirmwareStrings
{

/// Room for "255.255.255" and its NUL.
constexpr size_t kMaxText = 12;

/// Matches beyond this are counted but not kept.
constexpr size_t kMaxFound = 6;

struct Match {
    uint32_t address = 0;
    char     text[kMaxText] = {};
};

struct Result {
    Match  matches[kMaxFound];
    size_t kept  = 0; ///< Entries of matches filled.
    size_t total = 0; ///< Matches found, including any past kMaxFound.

    /// The one match at or above @p vtor, i.e. inside the running kernel's
    /// image, or nullptr when there is not exactly one.
    const Match* kernel(uint32_t vtor) const
    {
        const Match* found = nullptr;
        for (size_t i = 0; i < kept; ++i) {
            if (matches[i].address >= vtor) {
                if (found != nullptr) {
                    return nullptr;
                }
                found = &matches[i];
            }
        }
        return total == kept ? found : nullptr;
    }
};

/// Whether [text, text + length) is 1-3 digits, '.', 1-3 digits, '.', 1-3 digits.
inline bool isVersion(const uint8_t* text, size_t length)
{
    size_t parts = 0;
    size_t digits = 0;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t c = text[i];
        if (c >= '0' && c <= '9') {
            if (++digits > 3) {
                return false;
            }
        } else if (c == '.' && digits > 0 && parts < 2) {
            ++parts;
            digits = 0;
        } else {
            return false;
        }
    }
    return parts == 2 && digits > 0;
}

/// Every NUL-bounded version string in @p window, which is mapped at @p base.
inline Result scan(const uint8_t* window, uint32_t base, size_t size)
{
    Result result;
    size_t start = 0;
    for (size_t i = 0; i < size; ++i) {
        if (window[i] != 0) {
            continue;
        }
        const size_t length = i - start;
        const bool bounded = start > 0; // A string at offset 0 has no NUL before it.
        if (bounded && length >= 5 && length < kMaxText && isVersion(window + start, length)) {
            if (result.kept < kMaxFound) {
                Match& match = result.matches[result.kept++];
                match.address = base + static_cast<uint32_t>(start);
                for (size_t k = 0; k < length; ++k) {
                    match.text[k] = static_cast<char>(window[start + k]);
                }
                match.text[length] = '\0';
            }
            ++result.total;
        }
        start = i + 1;
    }
    return result;
}

} // namespace FirmwareStrings

#endif // FIRMWARE_STRINGS_HPP
