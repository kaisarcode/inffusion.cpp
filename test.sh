#!/bin/bash
# test.sh - Automated test suite for inffusion
# Summary: Validates the compact infer public surface.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
APP_ROOT="$SCRIPT_DIR"
MODEL_PATH="${INFFUSION_MODEL:-}"

# Returns success when one version string matches the public CLI format.
# @param $1 Version output.
# @return 0 on success.
is_valid_version_output() {
    printf '%s\n' "$1" | grep -Eq '^inffusion [0-9]+\.[0-9]+\.[0-9]+$'
}

# Prints one failure and exits.
# @param $1 Failure message.
# @return Does not return.
fail() {
    printf "\033[31m[FAIL]\033[0m %s\n" "$1"
    exit 1
}

# Prints one success line.
# @param $1 Success message.
# @return 0 on success.
pass() {
    printf "\033[32m[PASS]\033[0m %s\n" "$1"
}

# Resolves the current build artifact.
# @return 0 on success.
test_setup() {
    ARCH=$(uname -m)
    [ "$ARCH" = "x86_64" ] || [ "$ARCH" = "aarch64" ] || ARCH="arm64-v8a"
    case "$ARCH" in
        x86_64) EXT="" ;;
        aarch64) EXT="" ;;
        arm64-v8a) EXT="" ;;
        *) EXT="" ;;
    esac
    PLATFORM=$(uname -s | tr '[:upper:]' '[:lower:]')
    [ "$PLATFORM" = "linux" ] || PLATFORM="windows"
    export INFFUSION_BIN="$APP_ROOT/bin/$ARCH/$PLATFORM/inffusion$EXT"
    [ -x "$INFFUSION_BIN" ] || fail "Binary not found at $INFFUSION_BIN."
    VERSION_OUT=$("$INFFUSION_BIN" --version)
    is_valid_version_output "$VERSION_OUT" || fail "Direct binary runtime resolution failed."
    if [ "$(uname -s)" = "Linux" ]; then
        export LD_LIBRARY_PATH="$APP_ROOT/lib/stable-diffusion.cpp/$ARCH/linux:$APP_ROOT/lib/ggml/$ARCH/linux${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        if ldd "$INFFUSION_BIN" | grep -q 'not found'; then
            ldd "$INFFUSION_BIN"
            fail "Shared runtime dependencies are missing."
        fi
    fi
    [ -n "$MODEL_PATH" ] || pass "Environment note: INFFUSION_MODEL is not set; only CLI validation will run."
    pass "Environment verified: using $INFFUSION_BIN"
}

# Verifies command help and fail-fast parsing.
# @return 0 on success.
test_general() {
    HELP_OUT=$("$INFFUSION_BIN" --help)
    printf '%s\n' "$HELP_OUT" | grep -q 'inffusion infer' || fail "Help output missing infer command."
    printf '%s\n' "$HELP_OUT" | grep -q -- '--ref' || fail "Help output missing --ref."
    printf '%s\n' "$HELP_OUT" | grep -q -- '--output' || fail "Help output missing --output."
    printf '%s\n' "$HELP_OUT" | grep -q -- '--version, -v' || fail "Help output missing --version."
    pass "General: Help output verified."

    VERSION_OUT=$("$INFFUSION_BIN" --version)
    is_valid_version_output "$VERSION_OUT" || fail "Version output failed."
    VERSION_OUT=$("$INFFUSION_BIN" infer --version)
    is_valid_version_output "$VERSION_OUT" || fail "Command-level version output failed."
    pass "General: Version output verified."

    if "$INFFUSION_BIN" >/dev/null 2>&1; then fail "Missing command should fail."; fi
    if "$INFFUSION_BIN" unknown >/dev/null 2>&1; then fail "Unknown command should fail."; fi
    if env -u INFFUSION_MODEL "$INFFUSION_BIN" infer >/dev/null 2>&1; then fail "Missing model should fail when INFFUSION_MODEL is absent."; fi
    if "$INFFUSION_BIN" infer --model >/dev/null 2>&1; then fail "Missing --model value should fail."; fi
    if "$INFFUSION_BIN" infer --model /tmp/x --type video <<< 'x' >/dev/null 2>&1; then fail "Unsupported --type should fail."; fi
    if "$INFFUSION_BIN" infer --model /tmp/x --type image <<< 'x' >/dev/null 2>&1; then fail "infer --type image should require --ref."; fi
    if "$INFFUSION_BIN" infer --model /tmp/x --lora-scale 1.0 <<< 'x' >/dev/null 2>&1; then fail "--lora-scale without --lora should fail."; fi
    pass "General: Fail-fast CLI verified."
}

# Runs the full test suite.
# @return 0 on success.
run_tests() {
    test_setup
    test_general
    pass "All tests passed successfully for inffusion."
}

run_tests
