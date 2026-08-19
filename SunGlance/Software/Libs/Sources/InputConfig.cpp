/**
 ******************************************************************************
 * @file    InputConfig.cpp
 * @date    02-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Reader for a user-supplied value written into the app's own folder.
 ******************************************************************************
 *
 * A copy of Barcode's file of the same name. See the header for why it is a
 * copy and not a shared library, and for the rule that a fix belongs in both.
 *
 ******************************************************************************
 */

#include "InputConfig.hpp"

#include <cstring>

#include "SDK/JSON/JsonStreamReader.hpp"

#define LOG_MODULE_PRX      "InputConfig"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace InputConfig
{

Reader::Reader(const SDK::Kernel &kernel, const char *path)
    : mKernel(kernel)
    , mPath(path)
{
    // No read here -- see the class comment. The first refresh() does it.
}

bool Reader::statFile(size_t &size, time_t &utc) const
{
    SDK::Interface::IFileSystem::ObjectInfo info {};
    if (!mKernel.fs.objectInfo(mPath, info) || info.isDir) {
        return false;
    }
    size = info.size;
    utc  = info.utc;
    return true;
}

bool Reader::refresh()
{
    size_t size = 0;
    time_t utc  = 0;
    const bool present = statFile(size, utc);

    if (present == mPresent && size == mStampSize && utc == mStampUtc) {
        return false;
    }

    mPresent   = present;
    mStampSize = size;
    mStampUtc  = utc;
    load();
    return true;
}

void Reader::load()
{
    mJson.reset();
    mJsonLen = 0;

    if (!mPresent) {
        mStatus = Status::Absent;
        return;
    }

    // Both checks happen before any allocation, so an oversized or truncated
    // file costs one stat and nothing else.
    if (mStampSize > kMaxFileBytes) {
        LOG_WARNING("%s is %u bytes, over the %u limit\n",
                    mPath, static_cast<unsigned>(mStampSize),
                    static_cast<unsigned>(kMaxFileBytes));
        mStatus = Status::TooLarge;
        return;
    }
    if (mStampSize == 0) {
        // Most likely a copy that was interrupted, but an empty file is not
        // JSON either way and the user needs the same thing done about it.
        mStatus = Status::NotJson;
        return;
    }

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
    if (!file || !file->open()) {
        mStatus = Status::Unreadable;
        return;
    }

    std::unique_ptr<char[]> buffer(new (std::nothrow) char[mStampSize]);
    if (!buffer) {
        file->close();
        mStatus = Status::Unreadable;
        return;
    }

    size_t read = 0;
    const bool ok = file->read(buffer.get(), mStampSize, read) && read == mStampSize;
    file->close();

    if (!ok) {
        mStatus = Status::Unreadable;
        return;
    }

    SDK::JsonStreamReader reader(buffer.get(), mStampSize);
    if (!reader.validate()) {
        mStatus = Status::NotJson;
        return;
    }

    uint32_t schema = 0;
    if (!reader.get("schema", schema) || schema != kSchemaSupported) {
        mStatus = Status::WrongSchema;
        return;
    }

    mJson    = std::move(buffer);
    mJsonLen = mStampSize;
    mStatus  = Status::Ok;
}

bool Reader::has(const char *query) const
{
    if (mStatus != Status::Ok || !mJson) {
        return false;
    }

    SDK::JsonStreamReader reader(mJson.get(), mJsonLen);
    const char *value = nullptr;
    size_t      len   = 0;
    return reader.get(query, value, len);
}

bool Reader::getString(const char *query, char *out, size_t outSize) const
{
    if (out == nullptr || outSize == 0) {
        return false;
    }
    out[0] = '\0';

    if (mStatus != Status::Ok || !mJson) {
        return false;
    }

    SDK::JsonStreamReader reader(mJson.get(), mJsonLen);
    const char *value = nullptr;
    size_t      len   = 0;
    if (!reader.get(query, value, len) || value == nullptr) {
        return false;
    }

    if (len == 0 || len >= outSize) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        // Printable ASCII only. The backslash goes too: coreJSON returns the
        // raw slice with JSON escapes left undecoded, so an escape sequence
        // would reach the caller as its literal characters. Rejecting is
        // honest; parsing the escape as part of a coordinate is not.
        if (c < 0x20 || c > 0x7E || c == '\\') {
            return false;
        }
    }

    memcpy(out, value, len);
    out[len] = '\0';
    return true;
}

} // namespace InputConfig
