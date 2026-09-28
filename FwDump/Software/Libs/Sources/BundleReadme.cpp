/**
 ******************************************************************************
 * @file    BundleReadme.cpp
 * @brief   Writing README.txt beside the dump.
 ******************************************************************************
 */

#include "BundleReadme.hpp"

#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

namespace {

// The python3 command is extracted and run over a synthetic dump by the host
// tests, which is what keeps it correct.
constexpr char kText[] = R"TXT(FW Dump
=======

FW Dump copies this watch's internal flash into this folder, so it can be
read on a computer. It reads memory and writes only into this folder. It
never writes to flash, to the watch's settings, or to any hardware register.
It is not made by, or affiliated with, UNA Watch Ltd.


WHAT THE FILES ARE

dump_000000.bin ... dump_3E0000.bin
    The 4 MB of internal flash at 0x08000000, in 32 pieces of 128 KB. Each
    is named by its offset from 0x08000000, so joined in name order they are
    the whole image.

dump_manifest.txt
    A CRC-32 for each piece and one for the whole image. Rewritten after
    every piece, so it only ever lists pieces that are finished.

dump_context.txt
    What the image cannot say about itself: which chip, which firmware,
    whether the watch allowed the read, and a snapshot of hardware
    registers. Rewritten every time the app opens.

region_XXXXXXXX/
    The same files for a different window of flash, if one was chosen in
    the app's settings.


BEFORE YOU SHARE ANY OF IT

dump_context.txt contains this watch's 96-bit unique ID, on the line that
starts "CTX uid=". It identifies this one watch. Delete that line from any
copy you share.

The .bin files are a copy of the watch's firmware, which UNA Watch Ltd
wrote.


COPYING IT OFF THE WATCH

1. Wait for the watch to say DONE. Connecting USB stops the app. Nothing
   is lost, but the dump stops until you open the app again.
2. Turn off phone sync before connecting USB. The phone writing to the
   watch while a computer reads it has corrupted files.
3. Copy the whole Apps/FwDump folder to the computer.
4. Check the copy on the computer, not the files still on the watch.
   Straight after a copy, the computer may be showing you its own cached
   version of the watch's files rather than what the watch holds.


CHECKING THE COPY

In the copied folder, with Python 3 (on Windows the command is "python"):

python3 -c "import re,zlib
m=open('dump_manifest.txt').read();crc=0;ok=0;n=0
for o,s,c in re.findall(r'off=(\w+) size=(\w+) crc32=(\w+)',m):
  d=open('dump_%06X.bin'%int(o,16),'rb').read();crc=zlib.crc32(d,crc);n+=1
  ok+=len(d)==int(s,16) and zlib.crc32(d)==int(c,16)
w=re.search(r'whole_image_crc32=(\w+)',m)
print('ok %d/%d whole_image_crc32 %08X %s'%(ok,n,crc,'matches' if w and int(w[1],16)==crc else 'DIFFERS'))"

A good copy prints "ok 32/32", then the CRC the watch showed on its DONE
screen, then "matches". Anything else means a piece is missing or damaged,
so copy it again. A different CRC from someone else's dump is expected
when the firmware differs.
)TXT";

} // namespace

namespace BundleReadme
{

bool write(const SDK::Kernel& kernel)
{
    std::unique_ptr<SDK::Interface::IFile> f = kernel.fs.file(kPath);
    if (!f || !f->open(true, true)) {
        return false;
    }

    const size_t length = sizeof(kText) - 1;
    size_t bw = 0;
    f->write(kText, length, bw);
    f->flush();
    f->close();
    return bw == length;
}

} // namespace BundleReadme
