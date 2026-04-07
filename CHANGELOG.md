# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [KaisarCode Standards](https://kaisarcode.com).

## [1.0.2] - 2026-04-07

### Changed
- Standardized the binary output directory layout to use strictly `bin/<arch>/<platform>`.
- Standardized the object output directory layout to use strictly `obj/<arch>/<platform>`.
- Flattened the library vendor architecture paths to avoid redundant nested directories (`lib/<proj>/<arch>/<platform>`).
- Implemented Git LFS tracking for all shared libraries, static libraries, and binaries.
- Removed legacy architecture root targets (`arm64-v8a` and `win64`).

## [1.0.1] - 2026-04-05

### Added
- Modular `ggml` runtime packaging separated from `stable-diffusion.cpp`.
- Multi-architecture runtime support for `x86_64`, `aarch64`, `arm64-v8a`, and `win64`.

### Changed
- Refactored the application build to link against `stable-diffusion.cpp` and `ggml` as separate runtime components.
- Updated installers and runtime packaging to deploy and resolve both vendored library trees.
- Standardized linker flags and runtime search paths for portable vendored execution.
- Updated the project release metadata to `v1.0.1`.

### Notes
- `win64` packaging and CLI startup are available, but full image generation under `wine` is still pending validation.
