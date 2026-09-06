#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
app="dist/SMPTE History.app"
version=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$app/Contents/Info.plist")
mkdir -p build dist
stage=$(mktemp -d "$PWD/build/pkg-stage.XXXXXX")
mkdir -p "$stage/root/Applications"
ditto "$app" "$stage/root/Applications/SMPTE History.app"
codesign --verify --deep --strict "$stage/root/Applications/SMPTE History.app"
pkgbuild --analyze --root "$stage/root" "$stage/components.plist"
python3 - "$stage/components.plist" <<'PY'
import plistlib, sys
from pathlib import Path
p = Path(sys.argv[1])
components = plistlib.loads(p.read_bytes())
for component in components:
    component['BundleIsRelocatable'] = False
    component['BundleOverwriteAction'] = 'upgrade'
    component['BundleHasStrictIdentifier'] = True
p.write_bytes(plistlib.dumps(components))
PY
pkgbuild --root "$stage/root" --component-plist "$stage/components.plist" \
    --identifier pl.stageutils.smptehistory.installer --version "$version" \
    --install-location / --ownership recommended "$stage/component.pkg"
cat > "$stage/Distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>SMPTE History $version</title>
  <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
  <volume-check><allowed-os-versions><os-version min="13.0"/></allowed-os-versions></volume-check>
  <choices-outline><line choice="main"/></choices-outline>
  <choice id="main" title="SMPTE History" visible="false">
    <pkg-ref id="pl.stageutils.smptehistory.installer"/>
  </choice>
  <pkg-ref id="pl.stageutils.smptehistory.installer" version="$version" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
XML
output="dist/SMPTE-History-$version.pkg"
productbuild --distribution "$stage/Distribution.xml" --package-path "$stage" "$output"
(
    cd dist
    shasum -a 256 "SMPTE-History-$version.pkg" > "SMPTE-History-$version.pkg.sha256"
)
echo "Gotowe: $PWD/$output"
