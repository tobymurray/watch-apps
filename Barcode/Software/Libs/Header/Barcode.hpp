/**
 ******************************************************************************
 * @file    Barcode.hpp
 * @date    25-07-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Barcode id state shared by service and GUI.
 ******************************************************************************
 *
 * The id is the only state: unlike a stopwatch there is nothing to time, so
 * the service's whole job is to hold this string and hand it to the GUI.
 *
 ******************************************************************************
 */

#ifndef BARCODE_HPP
#define BARCODE_HPP

#include <cstddef>
#include <cstring>

namespace Barcode
{

constexpr size_t kMaxIdLength = 16; ///< Matches Code128::kMaxDataLength

/// Parkrun-style default: a letter followed by a numeric athlete id.
constexpr char kDefaultId[] = "A123456";

struct State
{
    char id[kMaxIdLength + 1]; ///< Null-terminated, printable ASCII
};

inline State makeDefaultState()
{
    State state{};
    std::strncpy(state.id, kDefaultId, kMaxIdLength);
    state.id[kMaxIdLength] = '\0';
    return state;
}

} // namespace Barcode

#endif // BARCODE_HPP
