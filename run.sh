#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f /etc/debian_version ]] && command -v python3 >/dev/null; then
  python3 "$project_dir/tools/setup_audio.py" "$project_dir/build/audio-runtime" || true
fi
cmake -S "$project_dir" -B "$project_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/build" --parallel
exec "$project_dir/build/pomodoro" "$@"
