#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
app="dist/SMPTE History.app"
version=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$app/Contents/Info.plist")
mkdir -p build dist
stage=$(mktemp -d "$PWD/build/dmg-stage.XXXXXX")
ditto "$app" "$stage/SMPTE History.app"
ln -s /Applications "$stage/Applications"
codesign --verify --deep --strict "$stage/SMPTE History.app"
image="dist/SMPTE-History-$version.dmg"
hdiutil create -volname "SMPTE History" -srcfolder "$stage" -format UDZO -ov "$image"
hdiutil verify "$image"
(
    cd dist
    shasum -a 256 "SMPTE-History-$version.dmg" > "SMPTE-History-$version.dmg.sha256"
)
echo "Gotowe: $PWD/$image"
