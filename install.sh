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

    if [ "$(id -u)" -eq 0 ]; then
        return 0
    fi
    command -v sudo >/dev/null 2>&1 || fail "sudo is required."
    script_name=$(basename "$0")
    if [ -f "$0" ] && [ -r "$0" ] && [ "$script_name" != "bash" ] && [ "$script_name" != "sh" ]; then
        if [ -r /dev/tty ]; then
            exec sudo bash "$0" "$@" </dev/tty
        fi
        exec sudo bash "$0" "$@"
    fi
    command -v wget >/dev/null 2>&1 || fail "wget is required."
    installer_path=$(mktemp) || fail "Unable to allocate temporary installer."
    if ! wget -qO "$installer_path" "$INSTALLER_URL"; then
        rm -f "$installer_path"
        fail "Unable to download installer payload."
    fi
    chmod 0700 "$installer_path"
    if [ -r /dev/tty ]; then
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
    find "$SYS_DEP_DIR/obj/$stack/$arch" -maxdepth 1 -type f -delete
    find "$src_dir/$stack/$arch" -maxdepth 1 -type f | while IFS= read -r dep_path; do
        install -m 0644 "$dep_path" "$SYS_DEP_DIR/obj/$stack/$arch/$(basename "$dep_path")"
    done
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
        "export LD_LIBRARY_PATH=\"$SYS_DEP_DIR/obj/llama.cpp/$arch:$SYS_DEP_DIR/obj/ggml/$arch\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}\"" \
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
    stage_local_asset "./lib/obj/llama.cpp/$arch/libllama.so" "$stage_dir/llama.cpp/$arch/libllama.so"
    stage_local_asset "./lib/obj/llama.cpp/$arch/libmtmd.so" "$stage_dir/llama.cpp/$arch/libmtmd.so"
    stage_local_asset "./lib/obj/ggml/$arch/libggml.so" "$stage_dir/ggml/$arch/libggml.so"
    stage_local_asset "./lib/obj/ggml/$arch/libggml-base.so" "$stage_dir/ggml/$arch/libggml-base.so"
    stage_local_asset "./lib/obj/ggml/$arch/libggml-cpu.so" "$stage_dir/ggml/$arch/libggml-cpu.so"
    if [ "$arch" = "x86_64" ] && [ "$cuda_enabled" = true ]; then
        stage_local_asset "./lib/obj/ggml/$arch/libggml-cuda.so" "$stage_dir/ggml/$arch/libggml-cuda.so"
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

    mkdir -p "$stage_dir/bin" "$stage_dir/llama.cpp/$arch" "$stage_dir/ggml/$arch"
    download_asset "$CORE_REPO_ROOT/bin/$arch/$APP_ID" "$stage_dir/bin/$APP_ID"
    download_asset "$CORE_REPO_ROOT/lib/obj/llama.cpp/$arch/libllama.so" "$stage_dir/llama.cpp/$arch/libllama.so"
    download_asset "$CORE_REPO_ROOT/lib/obj/llama.cpp/$arch/libmtmd.so" "$stage_dir/llama.cpp/$arch/libmtmd.so"
    download_asset "$CORE_REPO_ROOT/lib/obj/ggml/$arch/libggml.so" "$stage_dir/ggml/$arch/libggml.so"
    download_asset "$CORE_REPO_ROOT/lib/obj/ggml/$arch/libggml-base.so" "$stage_dir/ggml/$arch/libggml-base.so"
    download_asset "$CORE_REPO_ROOT/lib/obj/ggml/$arch/libggml-cpu.so" "$stage_dir/ggml/$arch/libggml-cpu.so"
    if [ "$arch" = "x86_64" ] && [ "$cuda_enabled" = true ]; then
        download_asset "$CORE_REPO_ROOT/lib/obj/ggml/$arch/libggml-cuda.so" "$stage_dir/ggml/$arch/libggml-cuda.so"
    fi
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
    printf "  llama.cpp path: %s/obj/llama.cpp/%s\n" "$SYS_DEP_DIR" "$arch"
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
        [ -d "./lib/obj/llama.cpp/$arch" ] || fail "Local llama.cpp dependencies not found for $arch."
        [ -d "./lib/obj/ggml/$arch" ] || fail "Local ggml dependencies not found for $arch."
        stage_local_assets "$arch" "$stage_dir" "$cuda_enabled"
    else
        stage_remote_assets "$arch" "$stage_dir" "$cuda_enabled"
    fi

    print_install_plan "$arch" "$mode" "$stage_dir" "$cuda_enabled"
    confirm_install
    printf ">>> Installing %s runtime dependencies...\n" "$APP_ID"
    install_runtime_deps "$stage_dir" "llama.cpp" "$arch"
    install_runtime_deps "$stage_dir" "ggml" "$arch"
    printf ">>> Installing %s binary...\n" "$APP_ID"
    chmod 0755 "$stage_dir/bin/$APP_ID"
    install_runtime_binary "$stage_dir/bin" "$arch"
    install_runtime_wrapper "$arch"
    printf "\033[1;32m[SUCCESS]\033[0m %s installed.\n" "$APP_ID"
}

main "$@"
