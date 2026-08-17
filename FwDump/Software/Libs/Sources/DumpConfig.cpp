/**
 ******************************************************************************
 * @file    DumpConfig.cpp
 * @brief   Reading the optional region override.
 ******************************************************************************
 */

#include "DumpConfig.hpp"

#include <memory>
#include <string_view>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/JSON/JsonStreamReader.hpp"

// Deliberately no logger in this file.
//
// load() is reachable from Service's constructor, and on a simulator build the
// service is constructed before the TouchGFX HAL is set up -- while the SDK's
// mock logger routes LOG_INFO through touchgfx_printf, which dereferences the
// HAL singleton. A log line here segfaults the simulator during startup with no
// output to say why (confirmed: it is exactly how the first simulator run of
// this app died). So this reader stays pure and reports a Status instead; the
// one caller logs it from run(), once the HAL exists.

namespace {

/// Parses a bare hex string ("08000000", or "8000000", or "8ABCdef0") into a
/// 32-bit value. Hand-rolled rather than strtoul: strtoul accepts a leading
/// sign, leading whitespace and a "0x" prefix, silently stops at the first
/// invalid character, and reports overflow through errno -- so "0x8000000z"
/// would parse as a plausible-looking address. Here anything that is not
/// entirely hex digits is rejected, which is the only safe reading of a field
/// that names an address to dereference.
bool parseHex32(std::string_view text, uint32_t& out)
{
    if (text.empty() || text.size() > 8) {
        return false;
    }

    uint32_t value = 0;
    for (const char c : text) {
        uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<uint32_t>(c - 'a') + 10u;
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<uint32_t>(c - 'A') + 10u;
        } else {
            return false;
        }
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

/// Reads one optional hex field. Absent leaves @p out untouched (so the
/// default survives); present-but-malformed is an error, because a field
/// somebody bothered to write and got wrong must not be silently ignored.
enum class FieldResult : uint8_t { Absent, Read, Malformed };

FieldResult readHexField(const SDK::JsonStreamReader& reader, const char* key, uint32_t& out)
{
    std::string_view text;
    if (!reader.get(key, text)) {
        return FieldResult::Absent;
    }
    if (!parseHex32(text, out)) {
        return FieldResult::Malformed;
    }
    return FieldResult::Read;
}

} // namespace

namespace DumpConfig
{

const char* describe(Status status)
{
    switch (status) {
        case Status::Default:     return "default region";
        case Status::Ok:          return "config applied";
        case Status::TooLarge:    return "config too large";
        case Status::NotJson:     return "config not JSON";
        case Status::WrongSchema: return "config schema unknown";
        case Status::BadField:    return "config field invalid";
        case Status::BadGeometry: return "config geometry invalid";
    }
    return "config unknown";
}

Result load(const SDK::Kernel& kernel)
{
    Result result; // Default-constructed: the flash region, Status::Default.

    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(kPath);
    if (!file || !file->open(false, false)) {
        return result; // No config. The overwhelmingly common case.
    }

    const size_t size = file->size();
    if (size == 0 || size > kMaxFileBytes) {
        file->close();
        result.status = Status::TooLarge;
        return result;
    }

    // +1 for the NUL: JsonStreamReader takes a length, but a buffer that is not
    // terminated invites every later change to this function to be a bug.
    std::unique_ptr<char[]> buffer(new char[size + 1]);
    size_t br = 0;
    const bool read = file->read(buffer.get(), size, br);
    file->close();

    if (!read || br != size) {
        result.status = Status::NotJson;
        return result;
    }
    buffer[br] = '\0';

    SDK::JsonStreamReader reader(buffer.get(), br);
    if (!reader.validate()) {
        result.status = Status::NotJson;
        return result;
    }

    uint32_t schema = 0;
    if (!reader.get("schema", schema) || schema != kSchemaSupported) {
        result.status = Status::WrongSchema;
        return result;
    }

    // Read into a candidate, not into result.region: a config that turns out
    // incoherent must leave the default entirely intact rather than contribute
    // whichever of its fields happened to parse.
    DumpRegion candidate;
    const char* const keys[] = {"base", "size", "chunk", "subwrite"};
    uint32_t* const   fields[] = {&candidate.base, &candidate.size, &candidate.chunk,
                                  &candidate.subwrite};

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (readHexField(reader, keys[i], *fields[i]) == FieldResult::Malformed) {
            result.status = Status::BadField;
            return result;
        }
    }

    if (!candidate.valid()) {
        result.status = Status::BadGeometry;
        return result;
    }

    result.region = candidate;
    result.status = Status::Ok;
    return result;
}

} // namespace DumpConfig
