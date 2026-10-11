#!/usr/bin/env bash
# Builds a self-contained OpenDraw folder for Windows from an MSYS2 UCRT64 build.
# Usage (from an MSYS2 UCRT64 shell): packaging/windows/deploy.sh <build-dir> <output-dir>
set -euo pipefail

build_dir=$1
out_dir=$2
source_dir=$(cd "$(dirname "$0")/../.." && pwd)

rm -rf "$out_dir"
mkdir -p "$out_dir"
cp "$build_dir/opendraw.exe" "$out_dir/"

# Qt libraries and plugins. OpenDraw does no networking, so network plugins are left out.
windeployqt6 --release --no-translations --no-system-d3d-compiler --no-opengl-sw \
    --no-compiler-runtime --skip-plugin-types tls,networkinformation "$out_dir/opendraw.exe"
rm -f "$out_dir/Qt6Network.dll"

# Qt's own strings (standard buttons and dialogs) for each language OpenDraw is translated into.
# OpenDraw's translations are compiled into the program.
qt_translations=$(qtpaths6 --query QT_INSTALL_TRANSLATIONS 2>/dev/null || echo /ucrt64/share/qt6/translations)
for ts in "$source_dir"/translations/opendraw_*.ts; do
    [ -e "$ts" ] || continue
    lang=$(basename "$ts" .ts)
    lang=${lang#opendraw_}
    if [ -e "$qt_translations/qtbase_$lang.qm" ]; then
        mkdir -p "$out_dir/translations"
        cp "$qt_translations/qtbase_$lang.qm" "$out_dir/translations/"
    fi
done

# Everything else the executable and the Qt plugins load from the MSYS2 prefix
# (GCC runtime, ICU, FreeType, HarfBuzz, ...). ldd lists dependencies recursively.
find "$out_dir" -name '*.exe' -o -name '*.dll' | while read -r binary; do
    ldd "$binary" | awk '$3 ~ /^\/ucrt64\/bin\// { print $3 }'
done | sort -u | while read -r dll; do
    [ -e "$out_dir/$(basename "$dll")" ] || cp "$dll" "$out_dir/"
done

cp "$source_dir/LICENSE" "$out_dir/LICENSE.txt"
cp "$source_dir/README.md" "$out_dir/README.md"
echo "Packaged $(find "$out_dir" -type f | wc -l) files in $out_dir"
