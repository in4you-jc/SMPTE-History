#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
export CLANG_MODULE_CACHE_PATH="$PWD/build/module-cache"
export SWIFT_MODULECACHE_PATH="$PWD/build/module-cache"
mkdir -p build/arm64 build/x86_64 "dist/SMPTE History.app/Contents/MacOS" "dist/SMPTE History.app/Contents/Frameworks" "dist/SMPTE History.app/Contents/Resources"
for arch in arm64 x86_64; do
    xcrun clang -arch "$arch" -mmacosx-version-min=13.0 -O2 -dynamiclib \
        -I vendor/libltc/src vendor/libltc/src/{ltc,decoder,encoder,timecode}.c \
        -install_name @rpath/libltc.dylib -o "build/$arch/libltc.dylib"
    xcrun clang -arch "$arch" -mmacosx-version-min=13.0 -O2 -std=c11 \
        -I vendor/libltc/src -c Sources/Bridge.c -o "build/$arch/Bridge.o"
    xcrun swiftc -swift-version 5 -O -target "$arch-apple-macosx13.0" \
        -module-cache-path "$PWD/build/module-cache" \
        -import-objc-header Sources/Bridge.h Sources/History.swift Sources/App.swift \
        "build/$arch/Bridge.o" -L "build/$arch" -lltc \
        -framework AudioToolbox -framework CoreAudio -framework AVFoundation \
        -Xlinker -rpath -Xlinker @executable_path/../Frameworks \
        -o "build/$arch/SMPTEHistory"
done
app="dist/SMPTE History.app/Contents"
xcrun lipo -create build/arm64/SMPTEHistory build/x86_64/SMPTEHistory -output "$app/MacOS/SMPTEHistory"
xcrun lipo -create build/arm64/libltc.dylib build/x86_64/libltc.dylib -output "$app/Frameworks/libltc.dylib"
cp Info.plist "$app/Info.plist"
cp vendor/libltc/COPYING "$app/Resources/libltc-COPYING.txt"
codesign --force --sign - "$app/Frameworks/libltc.dylib"
codesign --force --sign - "dist/SMPTE History.app"
echo "Gotowe: $PWD/dist/SMPTE History.app"
