/**
 ******************************************************************************
 * @file    Crc32.cpp
 * @brief   CRC-32/ISO-HDLC table and update loop.
 ******************************************************************************
 */

#include "Crc32.hpp"

#include <array>

namespace {

/// Built on first use rather than written out as a literal table: a
/// hand-pasted 256-entry table is 256 more chances to typo a constant that
/// nothing but a host test would ever catch, and the generator loop below is
/// the definition of the polynomial rather than a copy of its consequences.
///
/// Function-local static, so this is thread-safe initialisation under the
/// C++11 rules the SDK builds with, and costs nothing until the first hash.
const uint32_t* table()
{
    static const std::array<uint32_t, 256> kTable = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return kTable.data();
}

} // namespace

namespace Crc32
{

uint32_t update(uint32_t crc, const uint8_t* data, size_t length)
{
    const uint32_t* t = table();
    for (size_t i = 0; i < length; ++i) {
        crc = t[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

} // namespace Crc32
