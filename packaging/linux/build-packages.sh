#!/usr/bin/env bash
# Builds one portable .tar.gz per application from an already built and
# configured CMake tree (Release). See packaging/linux/README.md.
#
# Usage: build-packages.sh <build-directory> <output-directory> <suite-version>
#
# Shared libraries that resolve outside the standard system library
# directories (for example vcpkg-provided Qt or FFmpeg) are copied into the
# package's lib/ directory and the executable's RPATH is set to $ORIGIN/../lib.
# System libraries (glibc, GL/EGL, X11/xcb, ...) are never bundled.
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <build-directory> <output-directory> <suite-version>" >&2
    exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="$(cd "$1" && pwd)"
mkdir -p "$2"
output_dir="$(cd "$2" && pwd)"
suite_version="$3"
versions_file="$repo_root/packaging/app-versions.json"
arch="$(uname -m)"

if [[ ! "$suite_version" =~ ^[0-9]{4}\.[0-9]+\.[0-9]+$ ]]; then
    echo "error: suite version '$suite_version' must look like 2026.11.0" >&2
    exit 2
fi

# Directories whose libraries are provided by the target distribution.
is_system_library() {
    case "$1" in
        /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*|/usr/lib32/*) return 0 ;;
        *) return 1 ;;
    esac
}

app_ids=(hub video-editor image-editor motion-editor)
checksum_file="$output_dir/SHA256SUMS-linux.txt"
: > "$checksum_file"

for app_id in "${app_ids[@]}"; do
    app_version="$(python3 -I - "$versions_file" "$app_id" <<'PY'
import json, sys
print(json.load(open(sys.argv[1], encoding="utf-8"))["applications"][sys.argv[2]]["version"])
PY
)"
    package_name="creative-suite-${app_id}-${app_version}-linux-${arch}"
    stage_root="$(mktemp -d)"
    stage="$stage_root/$package_name"
    trap 'rm -rf "$stage_root"' EXIT

    echo "==> Staging $app_id $app_version"
    cmake --install "$build_dir" --config Release --prefix "$stage" --component "$app_id"

    executable="$(find "$stage/bin" -maxdepth 1 -type f -perm -u+x -name 'creative-suite-*' | head -n 1)"
    if [[ -z "$executable" ]]; then
        echo "error: no creative-suite-* executable was staged for $app_id" >&2
        exit 1
    fi

    # Bundle non-system shared libraries that the executable resolves.
    mkdir -p "$stage/lib"
    while read -r library_path; do
        [[ -n "$library_path" ]] || continue
        if ! is_system_library "$library_path"; then
            cp -L "$library_path" "$stage/lib/"
        fi
    done < <(ldd "$executable" | awk '/=> \// { print $3 }')

    # Qt plugins that CMake did not stage (shared Qt only).
    if [[ -d "$stage/plugins" ]]; then
        :
    elif [[ -n "${QT_PLUGINS_DIRECTORY:-}" && -d "$QT_PLUGINS_DIRECTORY" ]]; then
        cp -a "$QT_PLUGINS_DIRECTORY" "$stage/plugins"
    fi

    if [[ -n "$(ls -A "$stage/lib")" ]]; then
        command -v patchelf >/dev/null || { echo "error: patchelf is required to bundle libraries" >&2; exit 1; }
        patchelf --set-rpath '$ORIGIN/../lib' "$executable"
    else
        rmdir "$stage/lib"
    fi
    # Keep a qt.conf written by Qt's deploy step; otherwise point at plugins/.
    if [[ -d "$stage/plugins" && ! -f "$stage/bin/qt.conf" ]]; then
        printf '[Paths]\nPlugins = ../plugins\n' > "$stage/bin/qt.conf"
    fi

    cp "$repo_root/LICENSE" "$stage/LICENSE"

    archive="$output_dir/$package_name.tar.gz"
    tar --sort=name --owner=0 --group=0 --numeric-owner \
        --mtime='UTC 1970-01-01' -C "$stage_root" -cf - "$package_name" |
        gzip -n -9 > "$archive"
    (cd "$output_dir" && sha256sum "$(basename "$archive")") >> "$checksum_file"
    rm -rf "$stage_root"
    trap - EXIT
    echo "Created $archive"
done

echo "Created four Linux packages and SHA256SUMS-linux.txt in $output_dir (suite $suite_version)"
