#ifndef CRUMB_HPP
#define CRUMB_HPP

#include <cstdarg>
#include <cstdio>
#include <memory>

#include "SDK/Kernel/Kernel.hpp"

/// Appends one line to swatch.log in the app's folder: the crash leaves no dump, so this says how far it got.
inline void crumb(SDK::Kernel &k, const char *fmt, ...)
{
    char    line[96];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n > static_cast<int>(sizeof(line)) - 2) {
        n = sizeof(line) - 2;
    }
    line[n++] = '\n';
    std::unique_ptr<SDK::Interface::IFile> f = k.fs.file("swatch.log");
    if (!f || !f->open(true, false)) {
        return;
    }
    size_t written = 0;
    if (f->seek(f->size())) {
        f->write(line, static_cast<size_t>(n), written);
    }
    f->flush();
    f->close();
}


#endif // CRUMB_HPP
