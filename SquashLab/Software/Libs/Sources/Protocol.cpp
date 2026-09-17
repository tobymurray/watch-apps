/**
 ******************************************************************************
 * @file    Protocol.cpp
 * @brief   Reading the drill protocols. Spec in the header.
 ******************************************************************************
 */

#include "Protocol.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "SDK/JSON/JsonStreamReader.hpp"

#define LOG_MODULE_PRX      "Protocol"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

void Protocols::copyName(char (&dest)[skNameLen], const char* src, size_t len)
{
    const size_t n = (len < skNameLen - 1) ? len : skNameLen - 1;
    std::memcpy(dest, src, n);
    dest[n] = '\0';
}

bool Protocols::load(const SDK::Kernel& kernel, const char* path)
{
    mError = Error::NONE;

    if (!kernel.fs.exist(path)) {
        mError = Error::MISSING;
        return false;
    }

    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(path);
    if (!file || !file->open()) {
        mError = Error::UNREADABLE;
        return false;
    }

    const size_t size = file->size();
    if (size == 0 || size > skMaxFileBytes) {
        file->close();
        mError = (size > skMaxFileBytes) ? Error::TOO_LARGE : Error::MALFORMED;
        return false;
    }

    std::unique_ptr<char[]> buffer(new (std::nothrow) char[size]);
    size_t read = 0;
    const bool ok = buffer && file->read(buffer.get(), size, read) && read == size;
    file->close();
    file.reset();

    if (!ok) {
        mError = Error::UNREADABLE;
        return false;
    }

    SDK::JsonStreamReader json(buffer.get(), size);
    if (!json.validate()) {
        mError = Error::MALFORMED;
        return false;
    }

    // The schema must match exactly rather than be a floor. A file written for
    // a later shape is not one this build can read half of.
    uint8_t schema = 0;
    if (!json.get("schema", schema) || schema != 1u) {
        mError = Error::MALFORMED;
        return false;
    }

    // Parsed into locals and committed only on success, so a bad edit leaves
    // the picker holding whatever last worked instead of emptying it.
    Protocol parsed[skMaxProtocols];
    uint8_t  parsedCount = 0;

    char query[64];
    for (uint8_t p = 0; p < skMaxProtocols; ++p) {
        const char* name = nullptr;
        size_t      nameLen = 0;
        std::snprintf(query, sizeof(query), "protocols[%u].name", static_cast<unsigned>(p));
        if (!json.get(query, name, nameLen)) {
            break;   // Past the last one.
        }
        Protocol& out = parsed[parsedCount];
        copyName(out.name, name, nameLen);
        out.stepCount = 0;

        for (uint8_t s = 0; s < skMaxSteps; ++s) {
            const char* l1 = nullptr;
            size_t      l1Len = 0;
            std::snprintf(query, sizeof(query), "protocols[%u].steps[%u].line1",
                          static_cast<unsigned>(p), static_cast<unsigned>(s));
            if (!json.get(query, l1, l1Len)) {
                break;
            }

            Step& step = out.steps[out.stepCount];
            copyName(step.line1, l1, l1Len);

            const char* l2 = nullptr;
            size_t      l2Len = 0;
            std::snprintf(query, sizeof(query), "protocols[%u].steps[%u].line2",
                          static_cast<unsigned>(p), static_cast<unsigned>(s));
            if (json.get(query, l2, l2Len)) {
                copyName(step.line2, l2, l2Len);
            } else {
                step.line2[0] = '\0';
            }

            std::snprintf(query, sizeof(query), "protocols[%u].steps[%u].kind",
                          static_cast<unsigned>(p), static_cast<unsigned>(s));
            if (!json.get(query, step.kind)) {
                step.kind = 0;
            }

            std::snprintf(query, sizeof(query), "protocols[%u].steps[%u].target",
                          static_cast<unsigned>(p), static_cast<unsigned>(s));
            if (!json.get(query, step.target)) {
                step.target = 0;
            }

            std::snprintf(query, sizeof(query), "protocols[%u].steps[%u].seconds",
                          static_cast<unsigned>(p), static_cast<unsigned>(s));
            if (!json.get(query, step.seconds)) {
                step.seconds = 0;
            }

            ++out.stepCount;
        }

        if (out.stepCount == 0) {
            LOG_WARNING("protocol %u has no steps; skipped\n", static_cast<unsigned>(p));
            continue;
        }
        ++parsedCount;
    }

    if (parsedCount == 0) {
        mError = Error::NO_PROTOCOLS;
        return false;
    }

    for (uint8_t i = 0; i < parsedCount; ++i) {
        mProtocols[i] = parsed[i];
    }
    mCount = parsedCount;

    LOG_INFO("Loaded %u protocol(s)\n", static_cast<unsigned>(mCount));
    return true;
}
