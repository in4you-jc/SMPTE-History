#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build/tests
export CLANG_MODULE_CACHE_PATH="$PWD/build/module-cache"
xcrun clang -std=c11 -O2 -I Sources -I vendor/libltc/src \
    Tests/decoder_test.c Sources/Bridge.c vendor/libltc/src/{ltc,decoder,encoder,timecode}.c \
    -framework AudioToolbox -framework CoreAudio -framework CoreFoundation -o build/tests/decoder_test
build/tests/decoder_test
xcrun swiftc -swift-version 5 -module-cache-path "$PWD/build/module-cache" \
    Sources/History.swift Tests/history_test.swift -o build/tests/history_test
build/tests/history_test
