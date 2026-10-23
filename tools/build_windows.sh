#!/usr/bin/env bash
# Run inside MSYS2 UCRT64; package a standalone Windows application.
set -euo pipefail
source_dir=$1
stage=$2
export PATH="/ucrt64/bin:/usr/bin:$PATH"
cmake -S "$source_dir" -B "$source_dir/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build "$source_dir/build" --parallel
ctest --test-dir "$source_dir/build" --output-on-failure

mkdir -p "$stage"
cp "$source_dir/build/stash.exe" "$stage/"
cp "$source_dir/assets/stash.ico" "$stage/"
cp "$source_dir/assets/Silkscreen-OFL.txt" "$stage/"
cp "$source_dir/assets/Inter-OFL.txt" "$stage/"
for path in lib/gdk-pixbuf-2.0 etc/fonts share/glib-2.0/schemas \
            share/icons/Adwaita share/icons/hicolor share/licenses; do
  if [[ -d /ucrt64/$path ]]; then
    mkdir -p "$stage/$(dirname "$path")"
    cp -R "/ucrt64/$path" "$stage/$(dirname "$path")/"
  fi
done
mkdir -p "$stage/share/icons/hicolor/scalable/apps"
cp "$source_dir/assets/stash.svg" "$stage/share/icons/hicolor/scalable/apps/"

# Bundle the import closure, including dependencies of dynamically loaded image loaders.
# Windows system DLLs remain supplied by Windows itself.
declare -A copied=()
queue=("$stage/stash.exe")
while IFS= read -r -d '' module; do queue+=("$module"); done < <(find "$stage/lib" -name '*.dll' -print0)
for ((index=0; index<${#queue[@]}; index++)); do
  while IFS= read -r dependency; do
    dependency=${dependency%$'\r'}
    key=${dependency,,}
    [[ -n ${copied[$key]:-} ]] && continue
    copied[$key]=1
    if [[ -f /ucrt64/bin/$dependency ]]; then
      cp "/ucrt64/bin/$dependency" "$stage/"
      queue+=("$stage/$dependency")
    elif [[ ! -f /c/Windows/System32/$dependency && $key != api-ms-* && $key != ext-ms-* ]]; then
      echo "Missing Windows runtime dependency: $dependency" >&2
      exit 1
    fi
  done < <(objdump -p "${queue[$index]}" | sed -n 's/^[[:space:]]*DLL Name: //p')
done
echo "Packaged ${#copied[@]} imported runtime libraries (including Windows system libraries)."
