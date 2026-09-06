"""Package tracked sources and the built application for GitHub Releases."""
from pathlib import Path
import subprocess
import zipfile

root = Path(__file__).resolve().parent
app = root / "dist" / "SMPTE History.app"
if not (app / "Contents" / "MacOS" / "SMPTEHistory").is_file():
    raise SystemExit("Run bash build.sh first.")
tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
files = [root / name for name in tracked if name]
files.extend(p for p in app.rglob("*") if p.is_file())
archive = root / "dist" / "SMPTE-History-macOS.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
    for path in sorted(files):
        output.write(path, Path("SMPTE-History") / path.relative_to(root))
with zipfile.ZipFile(archive) as output:
    if output.testzip() is not None:
        raise SystemExit("Archive verification failed.")
print(archive)
