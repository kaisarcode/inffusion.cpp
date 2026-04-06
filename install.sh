#!/bin/bash
# install.sh - Production installer for inffusion on Linux
# Summary: Installs the current-architecture binary and shared runtime dependencies.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

APP_ID="inffusion"
REPO_ID="inffusion.cpp"
CORE_REPO_REF="master"
CORE_REPO_ROOT="https://raw.githubusercontent.com/kaisarcode/${REPO_ID}/master"
INSTALLER_URL="${CORE_REPO_ROOT}/install.sh"
SYS_BIN_DIR="/usr/local/bin"
SYS_APP_DIR="/usr/local/lib/kaisarcode/apps"
SYS_DEP_DIR="/usr/local/lib/kaisarcode"

# Prints one error and exits.
# @param $1 Error message.
# @return Does not return.
fail() {
    printf "Error: %s\n" "$1" >&2
    exit 1
}

# Ensures the installer runs with root privileges.
# @param $@ Original script arguments.
# @return Does not return when re-executing.
ensure_root() {
    local installer_path
    local script_name
    local script_path
    local script_fd

    if [ "$(id -u)" -eq 0 ]; then
        return 0
    fi
    command -v sudo >/dev/null 2>&1 || fail "sudo is required."
    script_path="${BASH_SOURCE[0]:-$0}"
    script_name=$(basename "$script_path")
    if [ -f "$0" ] && [ -r "$0" ] && [ "$script_name" != "bash" ] && [ "$script_name" != "sh" ]; then
        if [ -t 0 ] && [ -r /dev/tty ]; then
            exec sudo bash "$0" "$@" </dev/tty
        fi
        exec sudo bash "$0" "$@"
    fi
    if [ -r "$script_path" ] && [ "$script_name" != "bash" ] && [ "$script_name" != "sh" ]; then
        if [ -t 0 ] && [ -r /dev/tty ]; then
            exec sudo bash "$script_path" "$@" </dev/tty
        fi
        exec sudo bash "$script_path" "$@"
    fi
    script_fd="/proc/$$/fd/255"
    if [ -r "$script_fd" ]; then
        installer_path=$(mktemp) || fail "Unable to allocate temporary installer."
        cat "$script_fd" > "$installer_path" || {
            rm -f "$installer_path"
            fail "Unable to stage installer payload."
        }
        chmod 0700 "$installer_path"
        if [ -t 0 ] && [ -r /dev/tty ]; then
            exec sudo bash "$installer_path" "$@" </dev/tty
        fi
        exec sudo bash "$installer_path" "$@"
    fi
    command -v wget >/dev/null 2>&1 || fail "wget is required."
    installer_path=$(mktemp) || fail "Unable to allocate temporary installer."
    if ! wget -qO "$installer_path" "$INSTALLER_URL"; then
        rm -f "$installer_path"
        fail "Unable to download installer payload."
    fi
    chmod 0700 "$installer_path"
    if [ -t 0 ] && [ -r /dev/tty ]; then
        exec sudo bash "$installer_path" "$@" </dev/tty
    fi
    exec sudo bash "$installer_path" "$@"
}

# Reports one unavailable remote asset.
# @param $1 Missing asset identifier.
# @return Does not return.
fail_unavailable() {
    fail "Remote asset is not available yet (repo may still be private): $1"
}

# Downloads one remote asset.
# @param $1 Source URL.
# @param $2 Destination path.
# @return 0 on success.
download_asset() {
    url="$1"
    out="$2"
    if ! wget -qO "$out" "$url"; then
        rm -f "$out"
        fail_unavailable "$url"
    fi
    if [ -s "$out" ] && [ "$(wc -c < "$out")" -lt 1024 ] && grep -q '^version https://git-lfs.github.com/spec/v1' "$out" 2>/dev/null; then
        media_url=$(echo "$url" | sed 's/raw.githubusercontent.com/media.githubusercontent.com\/media/')
        if ! wget -qO "$out" "$media_url"; then
            rm -f "$out"
            fail_unavailable "$media_url"
        fi
    fi
    [ -s "$out" ] || {
        rm -f "$out"
        fail_unavailable "$url"
    }
}

# Downloads one Git blob payload as text.
# @param $1 Blob API URL.
# @return Writes the decoded blob payload to stdout.
download_blob_text() {
    blob_url="$1"
    command -v python3 >/dev/null 2>&1 || fail "python3 is required."
    response_path=$(mktemp) || fail "Unable to allocate temporary response file."
    if ! wget -qO "$response_path" "$blob_url"; then
        rm -f "$response_path"
        fail_unavailable "$blob_url"
    fi
    python3 - "$response_path" <<'PY'
import base64
import json
import sys

path = sys.argv[1]
with open(path, "r", encoding="utf-8") as fh:
    data = json.load(fh)
content = data.get("content", "")
encoding = data.get("encoding")
if encoding != "base64" or not content:
    sys.exit(1)
sys.stdout.write(base64.b64decode(content).decode("utf-8"))
PY
    rm -f "$response_path"
}

# Detects the current machine architecture.
# @return Writes the resolved architecture to stdout.
detect_arch() {
    case "$(uname -m)" in
        x86_64) printf "x86_64" ;;
        aarch64|arm64) printf "aarch64" ;;
        armv8*|arm64-v8a) printf "arm64-v8a" ;;
        *) fail "Unsupported architecture: $(uname -m)" ;;
    esac
}

# Returns success when the host exposes the CUDA runtime needed by ggml-cuda.
# @return 0 when CUDA runtime is available.
has_cuda_runtime() {
    if ! command -v ldconfig >/dev/null 2>&1; then
        return 1
    fi
    ldconfig -p 2>/dev/null | grep -q 'libcuda\.so\.1' || return 1
    ldconfig -p 2>/dev/null | grep -q 'libcudart\.so\.12' || return 1
    ldconfig -p 2>/dev/null | grep -q 'libcublas\.so\.12' || return 1
    return 0
}

# Installs one runtime binary payload.
# @param $1 Source directory path.
# @param $2 Architecture name.
# @return 0 on success.
install_runtime_binary() {
    src_dir="$1"
    arch="$2"
    mkdir -p "$SYS_APP_DIR/$APP_ID/$arch"
    install -m 0755 "$src_dir/$APP_ID" "$SYS_APP_DIR/$APP_ID/$arch/$APP_ID"
}

# Installs one runtime dependency payload.
# @param $1 Source directory path.
# @param $2 Stack name.
# @param $3 Architecture name.
# @return 0 on success.
install_runtime_deps() {
    src_dir="$1"
    stack="$2"
    arch="$3"
    mkdir -p "$SYS_DEP_DIR/obj/$stack/$arch"
    find "$SYS_DEP_DIR/obj/$stack/$arch" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
    cp -a "$src_dir/$stack/$arch/." "$SYS_DEP_DIR/obj/$stack/$arch/"
}

# Installs one runtime wrapper in the global bin directory.
# @param $1 Architecture name.
# @return 0 on success.
install_runtime_wrapper() {
    arch="$1"
    wrapper_path="$SYS_BIN_DIR/$APP_ID"
    mkdir -p "$SYS_BIN_DIR"
    printf '%s\n' \
        '#!/bin/bash' \
        'set -e' \
        "export LD_LIBRARY_PATH=\"$SYS_DEP_DIR/obj/stable-diffusion.cpp/$arch:$SYS_DEP_DIR/obj/ggml/$arch\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}\"" \
        "exec \"$SYS_APP_DIR/$APP_ID/$arch/$APP_ID\" \"\$@\"" \
        | tee "$wrapper_path" >/dev/null
    chmod 0755 "$wrapper_path"
}

# Formats one byte count for display.
# @param $1 Byte count.
# @return 0 on success.
format_size() {
    bytes="$1"
    awk -v bytes="$bytes" '
        BEGIN {
            split("B KiB MiB GiB TiB", units, " ");
            value = bytes + 0;
            unit = 1;
            while (value >= 1024 && unit < 5) {
                value /= 1024;
                unit++;
            }
            if (unit == 1) {
                printf "%d %s", value, units[unit];
            } else {
                printf "%.2f %s", value, units[unit];
            }
        }
    '
}

# Computes the total size of one staged payload tree.
# @param $1 Staging directory path.
# @return Writes the total byte count to stdout.
compute_stage_size() {
    find "$1" -type f -printf '%s\n' | awk '{ total += $1 } END { printf "%d", total + 0 }'
}

# Copies one local asset into the staging directory.
# @param $1 Source file path.
# @param $2 Destination file path.
# @return 0 on success.
stage_local_asset() {
    src="$1"
    dst="$2"
    [ -f "$src" ] || fail "Missing local asset: $src"
    mkdir -p "$(dirname "$dst")"
    cp "$src" "$dst"
}

# Resolves local assets into a staging directory.
# @param $1 Architecture name.
# @param $2 Staging directory path.
# @return 0 on success.
stage_local_assets() {
    arch="$1"
    stage_dir="$2"
    cuda_enabled="$3"

    stage_local_asset "./bin/$arch/$APP_ID" "$stage_dir/bin/$APP_ID"
    mkdir -p "$stage_dir/stable-diffusion.cpp/$arch" "$stage_dir/ggml/$arch"
    cp -a "./lib/obj/stable-diffusion.cpp/$arch/." "$stage_dir/stable-diffusion.cpp/$arch/"
    cp -a "./lib/obj/ggml/$arch/." "$stage_dir/ggml/$arch/"
    if [ "$arch" = "x86_64" ] && [ "$cuda_enabled" != true ]; then
        rm -f "$stage_dir/ggml/$arch/libggml-cuda.so"
    fi
}

# Downloads remote assets into a staging directory.
# @param $1 Architecture name.
# @param $2 Staging directory path.
# @return 0 on success.
stage_remote_assets() {
    arch="$1"
    stage_dir="$2"
    cuda_enabled="$3"

    mkdir -p "$stage_dir/bin" "$stage_dir/stable-diffusion.cpp/$arch" "$stage_dir/ggml/$arch"
    download_asset "$CORE_REPO_ROOT/bin/$arch/$APP_ID" "$stage_dir/bin/$APP_ID"
    find_remote_assets "stable-diffusion.cpp" "$arch" | while IFS="$(printf '\t')" read -r asset_mode asset_name asset_blob_url; do
        [ -n "$asset_name" ] || continue
        if [ "$asset_mode" = "120000" ]; then
            ln -s "$(download_blob_text "$asset_blob_url")" "$stage_dir/stable-diffusion.cpp/$arch/$asset_name"
            continue
        fi
        download_asset "$CORE_REPO_ROOT/lib/obj/stable-diffusion.cpp/$arch/$asset_name" \
            "$stage_dir/stable-diffusion.cpp/$arch/$asset_name"
    done
    find_remote_assets "ggml" "$arch" | while IFS="$(printf '\t')" read -r asset_mode asset_name asset_blob_url; do
        [ -n "$asset_name" ] || continue
        if [ "$arch" = "x86_64" ] && [ "$cuda_enabled" != true ] && [ "$asset_name" = "libggml-cuda.so" ]; then
            continue
        fi
        if [ "$asset_mode" = "120000" ]; then
            ln -s "$(download_blob_text "$asset_blob_url")" "$stage_dir/ggml/$arch/$asset_name"
            continue
        fi
        download_asset "$CORE_REPO_ROOT/lib/obj/ggml/$arch/$asset_name" \
            "$stage_dir/ggml/$arch/$asset_name"
    done
}

# Lists one remote vendor directory using the Git tree API.
# @param $1 Stack name.
# @param $2 Architecture name.
# @return Writes mode, filename, and blob URL separated by tabs.
find_remote_assets() {
    stack="$1"
    arch="$2"
    api_url="https://api.github.com/repos/kaisarcode/${REPO_ID}/git/trees/${CORE_REPO_REF}:lib/obj/${stack}/${arch}"
    command -v python3 >/dev/null 2>&1 || fail "python3 is required."
    response_path=$(mktemp) || fail "Unable to allocate temporary response file."
    if ! wget -qO "$response_path" "$api_url"; then
        rm -f "$response_path"
        fail_unavailable "$api_url"
    fi
    python3 - "$response_path" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, "r", encoding="utf-8") as fh:
    data = json.load(fh)
tree = data.get("tree")
if not isinstance(tree, list):
    sys.exit(1)
for item in tree:
    if item.get("type") == "blob":
        print(f'{item["mode"]}\t{item["path"]}\t{item["url"]}')
PY
    rm -f "$response_path"
}

# Prints the resolved installation plan from the staged payload.
# @param $1 Architecture name.
# @param $2 Install mode.
# @param $3 Staging directory path.
# @return 0 on success.
print_install_plan() {
    arch="$1"
    mode="$2"
    stage_dir="$3"
    cuda_enabled="$4"
    total_size=$(compute_stage_size "$stage_dir")

    printf "Summary:\n"
    printf "  App: %s\n" "$APP_ID"
    printf "  Architecture: %s\n" "$arch"
    printf "  Source: %s\n" "$mode"
    if [ "$arch" = "x86_64" ]; then
        if [ "$cuda_enabled" = true ]; then
            printf "  CUDA backend: enabled\n"
        else
            printf "  CUDA backend: disabled\n"
        fi
    fi
    find "$stage_dir" -type f | sort | while IFS= read -r asset_path; do
        asset_name="${asset_path#"$stage_dir"/}"
        asset_size=$(wc -c < "$asset_path")
        printf "  File: %s (%s)\n" "$asset_name" "$(format_size "$asset_size")"
    done
    printf "  Total size: %s\n" "$(format_size "$total_size")"
    printf "  Install path: %s/%s/%s/%s\n" "$SYS_APP_DIR" "$APP_ID" "$arch" "$APP_ID"
    printf "  Wrapper path: %s/%s\n" "$SYS_BIN_DIR" "$APP_ID"
    printf "  stable-diffusion.cpp path: %s/obj/stable-diffusion.cpp/%s\n" "$SYS_DEP_DIR" "$arch"
    printf "  ggml path: %s/obj/ggml/%s\n" "$SYS_DEP_DIR" "$arch"
}

# Confirms the installation plan with the user.
# @return 0 on approval.
confirm_install() {
    local answer

    [ -r /dev/tty ] || fail "Interactive confirmation requires a tty."
    printf "Continue? [Y/n] " >/dev/tty
    IFS= read -r answer </dev/tty || fail "Unable to read confirmation."
    case "$answer" in
        ""|Y|y|yes|YES) return 0 ;;
        *) fail "Installation cancelled." ;;
    esac
}

# Runs the installer entry point.
# @param $@ Script arguments.
# @return 0 on success.
main() {
    local local_mode=false
    local mode="remote"
    local arch
    local stage_dir
    local cuda_enabled=false

    while [ $# -gt 0 ]; do
        case "$1" in
            --local) local_mode=true; mode="local"; shift ;;
            *) fail "Unknown argument: $1" ;;
        esac
    done
    ensure_root "$@"
    [ "$(uname -s)" = "Linux" ] || fail "Only Linux is supported."
    [ "$(id -u)" -eq 0 ] || fail "Root privileges are required."
    printf "Preparing installation. Please wait.\n"
    arch=$(detect_arch)
    if [ "$arch" = "x86_64" ] && has_cuda_runtime; then
        cuda_enabled=true
    fi
    stage_dir=$(mktemp -d) || fail "Unable to allocate temporary staging area."
    trap 'rm -rf "$stage_dir"' EXIT

    if [ "$local_mode" = true ]; then
        [ -d "./lib/obj/stable-diffusion.cpp/$arch" ] || fail "Local stable-diffusion.cpp dependencies not found for $arch."
        [ -d "./lib/obj/ggml/$arch" ] || fail "Local ggml dependencies not found for $arch."
        stage_local_assets "$arch" "$stage_dir" "$cuda_enabled"
    else
        stage_remote_assets "$arch" "$stage_dir" "$cuda_enabled"
    fi

    print_install_plan "$arch" "$mode" "$stage_dir" "$cuda_enabled"
    confirm_install
    printf ">>> Installing %s runtime dependencies...\n" "$APP_ID"
    install_runtime_deps "$stage_dir" "stable-diffusion.cpp" "$arch"
    install_runtime_deps "$stage_dir" "ggml" "$arch"
    printf ">>> Installing %s binary...\n" "$APP_ID"
    chmod 0755 "$stage_dir/bin/$APP_ID"
    install_runtime_binary "$stage_dir/bin" "$arch"
    install_runtime_wrapper "$arch"
    printf "\033[1;32m[SUCCESS]\033[0m %s installed.\n" "$APP_ID"
}

main "$@"
