#!/bin/bash
# make.sh - Multi-architecture build wrapper for inffusion
# Summary: Runs the canonical Makefile targets for the current source tree.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

# Runs the canonical build entry point.
# @param $@ Forwarded script arguments.
# @return 0 on success.
main() {
    make all
}

main "$@"
