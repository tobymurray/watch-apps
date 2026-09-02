#!/bin/bash
set -x
for app in Barcode Spin NotifyToggle; do
  docker run --rm -v /Users/tobymurray/git/watch-apps:/apps -v /Users/tobymurray/git/una-sdk:/sdk -e UNA_SDK=/sdk \
    -w /apps/$app/Software/Apps/$app-CMake f5689e6804e6 bash -lc \
    "rm -rf build-fontdoc && cmake -B build-fontdoc -G 'Unix Makefiles' -DBUILD_VERSION=0.0.1 . && cmake --build build-fontdoc -j\$(nproc) && arm-none-eabi-size -A -x build-fontdoc/*GUI.elf* 2>/dev/null; ls -la /apps/$app/Output/ | head -20; find build-fontdoc -name '*GUI*.elf' -o -name '*.map' | head" 
  echo "=== $app exit $?"
done
