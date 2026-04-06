#!/bin/bash
# uninstall.sh - Production uninstaller for inffusion on Linux
# Summary: Removes the installed inffusion binary and optionally its shared runtime dependencies.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

APP_ID="inffusion"
REPO_ID="inffusion.cpp"
CORE_REPO_ROOT="https://raw.githubusercontent.com/kaisarcode/${REPO_ID}/master"
UNINSTALLER_URL="${CORE_REPO_ROOT}/uninstall.sh"
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

# Ensures the uninstaller runs with root privileges.
# @param $@ Original script arguments.
# @return Does not return when re-executing.
ensure_root() {
    local uninstaller_path
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
        uninstaller_path=$(mktemp) || fail "Unable to allocate temporary uninstaller."
        cat "$script_fd" > "$uninstaller_path" || {
            rm -f "$uninstaller_path"
            fail "Unable to stage uninstaller payload."
        }
        chmod 0700 "$uninstaller_path"
        if [ -t 0 ] && [ -r /dev/tty ]; then
            exec sudo bash "$uninstaller_path" "$@" </dev/tty
        fi
        exec sudo bash "$uninstaller_path" "$@"
    fi
    command -v wget >/dev/null 2>&1 || fail "wget is required."
    uninstaller_path=$(mktemp) || fail "Unable to allocate temporary uninstaller."
    if ! wget -qO "$uninstaller_path" "$UNINSTALLER_URL"; then
        rm -f "$uninstaller_path"
        fail "Unable to download uninstaller payload."
    fi
    chmod 0700 "$uninstaller_path"
    if [ -t 0 ] && [ -r /dev/tty ]; then
        exec sudo bash "$uninstaller_path" "$@" </dev/tty
    fi
    exec sudo bash "$uninstaller_path" "$@"
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

# Confirms the uninstall plan with the user.
# @return 0 on approval.
confirm_uninstall() {
    local answer

    [ -r /dev/tty ] || fail "Interactive confirmation requires a tty."
    printf "Continue? [Y/n] " >/dev/tty
    IFS= read -r answer </dev/tty || fail "Unable to read confirmation."
    case "$answer" in
        ""|Y|y|yes|YES) return 0 ;;
        *) fail "Uninstall cancelled." ;;
    esac
}

# Removes one path when it exists.
# @param $1 Path to remove.
# @return 0 on success.
remove_if_exists() {
    target_path="$1"
    if [ -e "$target_path" ] || [ -L "$target_path" ]; then
        rm -rf "$target_path"
    fi
}

# Removes one empty parent directory when possible.
# @param $1 Directory path.
# @return 0 on success.
remove_dir_if_empty() {
    target_dir="$1"
    if [ -d "$target_dir" ] && [ -z "$(find "$target_dir" -mindepth 1 -maxdepth 1 2>/dev/null)" ]; then
        rmdir "$target_dir"
    fi
}

# Prints the resolved uninstall plan.
# @param $1 Architecture name.
# @param $2 Whether shared dependencies will be removed.
# @return 0 on success.
print_uninstall_plan() {
    arch="$1"
    remove_deps="$2"

    printf "Summary:\n"
    printf "  App: %s\n" "$APP_ID"
    printf "  Architecture: %s\n" "$arch"
    printf "  Remove app: yes\n"
    printf "  Remove deps: %s\n" "$remove_deps"
    printf "  Wrapper path: %s/%s\n" "$SYS_BIN_DIR" "$APP_ID"
    printf "  App path: %s/%s/%s\n" "$SYS_APP_DIR" "$APP_ID" "$arch"
    if [ "$remove_deps" = true ]; then
        printf "  stable-diffusion.cpp path: %s/obj/stable-diffusion.cpp/%s\n" "$SYS_DEP_DIR" "$arch"
        printf "  ggml path: %s/obj/ggml/%s\n" "$SYS_DEP_DIR" "$arch"
    fi
}

# Removes the installed app files.
# @param $1 Architecture name.
# @return 0 on success.
uninstall_app() {
    arch="$1"

    remove_if_exists "$SYS_BIN_DIR/$APP_ID"
    remove_if_exists "$SYS_APP_DIR/$APP_ID/$arch"
    remove_dir_if_empty "$SYS_APP_DIR/$APP_ID"
}

# Removes the installed shared dependency files for one architecture.
# @param $1 Architecture name.
# @return 0 on success.
uninstall_deps() {
    arch="$1"

    remove_if_exists "$SYS_DEP_DIR/obj/stable-diffusion.cpp/$arch"
    remove_if_exists "$SYS_DEP_DIR/obj/ggml/$arch"
    remove_dir_if_empty "$SYS_DEP_DIR/obj/stable-diffusion.cpp"
    remove_dir_if_empty "$SYS_DEP_DIR/obj/ggml"
    remove_dir_if_empty "$SYS_DEP_DIR/obj"
}

# Runs the uninstaller entry point.
# @param $@ Script arguments.
# @return 0 on success.
main() {
    local arch
    local remove_deps=false

    while [ $# -gt 0 ]; do
        case "$1" in
            --deps) remove_deps=true; shift ;;
            *) fail "Unknown argument: $1" ;;
        esac
    done
    ensure_root "$@"
    [ "$(uname -s)" = "Linux" ] || fail "Only Linux is supported."
    [ "$(id -u)" -eq 0 ] || fail "Root privileges are required."
    arch=$(detect_arch)
    print_uninstall_plan "$arch" "$remove_deps"
    confirm_uninstall
    printf ">>> Removing %s application files...\n" "$APP_ID"
    uninstall_app "$arch"
    if [ "$remove_deps" = true ]; then
        printf ">>> Removing %s runtime dependencies...\n" "$APP_ID"
        uninstall_deps "$arch"
    fi
    printf "\033[1;32m[SUCCESS]\033[0m %s removed.\n" "$APP_ID"
}

main "$@"
