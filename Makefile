# inffusion - Multi-architecture Makefile
# Summary: Builds the raw inffusion binary and installer payloads.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

NAME = inffusion
REPO_ID = inffusion.cpp
INSTALLER_SRC = src/win/install.cpp
UNINSTALLER_SRC = src/win/uninstall.cpp
BIN_ROOT = bin
DEPS_ROOT = lib
TOOLCHAIN_ROOT = /usr/local/share/kaisarcode/toolchains
SRC = src/main.cpp src/pal.h

INC_DIR = $(DEPS_ROOT)/inc/stable-diffusion.cpp
LIB_ROOT = $(DEPS_ROOT)/obj/stable-diffusion.cpp/$(ARCH)
SHARED_SD = $(LIB_ROOT)/libstable-diffusion.so
WIN_SD = $(LIB_ROOT)/libstable-diffusion.dll.a

CXX_x86_64 = g++
CXX_aarch64 = aarch64-linux-gnu-g++
NDK_VER = android-ndk-r27c
NDK_HOST = linux-x86_64
NDK_ROOT = $(TOOLCHAIN_ROOT)/ndk/$(NDK_VER)
NDK_BIN = $(NDK_ROOT)/toolchains/llvm/prebuilt/$(NDK_HOST)/bin
NDK_API = 24
CXX_arm64_v8a = $(NDK_BIN)/aarch64-linux-android$(NDK_API)-clang++
CXX_win64 = x86_64-w64-mingw32-g++

CXXFLAGS = -Wall -Wextra -Werror -O3 -std=c++17 -I$(INC_DIR) $(EXTRA_CXXFLAGS)
SYSLIBS_UNIX = -pthread -lm
SYSLIBS_WIN = -lws2_32 -ladvapi32 -Wl,--no-insert-timestamp
WININSTALL = -lurlmon -lshell32 -ladvapi32 -lshlwapi -lcomctl32 -Wl,--no-insert-timestamp

.PHONY: all clean build_arch x86_64 aarch64 arm64-v8a win64

all: x86_64 aarch64 arm64-v8a win64

x86_64: $(BIN_ROOT)/x86_64/$(NAME)

$(BIN_ROOT)/x86_64/$(NAME): $(SRC)
	$(MAKE) build_arch ARCH=x86_64 CXX="$(CXX_x86_64)" EXT=""

aarch64: $(BIN_ROOT)/aarch64/$(NAME)

$(BIN_ROOT)/aarch64/$(NAME): $(SRC)
	$(MAKE) build_arch ARCH=aarch64 CXX="$(CXX_aarch64)" EXT=""

arm64-v8a: $(BIN_ROOT)/arm64-v8a/$(NAME)

$(BIN_ROOT)/arm64-v8a/$(NAME): $(SRC)
	@if [ ! -f "$(CXX_arm64_v8a)" ]; then \
		echo "[ERROR] NDK Compiler not found at: $(CXX_arm64_v8a)"; \
		exit 1; \
	fi
	$(MAKE) build_arch ARCH=arm64-v8a CXX="$(CXX_arm64_v8a)" EXT=""

win64: $(BIN_ROOT)/win64/$(NAME).exe install.exe uninstall.exe

$(BIN_ROOT)/win64/$(NAME).exe: $(SRC)
	$(MAKE) build_arch ARCH=win64 CXX="$(CXX_win64)" EXT=".exe" EXTRA_CXXFLAGS="-D_WIN32_WINNT=0x0601"

install.exe: $(INSTALLER_SRC)
	$(CXX_win64) $(CXXFLAGS) -D_WIN32_WINNT=0x0601 -mwindows $(INSTALLER_SRC) -o install.exe $(WININSTALL)

uninstall.exe: $(UNINSTALLER_SRC)
	$(CXX_win64) $(CXXFLAGS) -D_WIN32_WINNT=0x0601 -mwindows $(UNINSTALLER_SRC) -o uninstall.exe $(WININSTALL)

build_arch:
	mkdir -p $(BIN_ROOT)/$(ARCH)
	$(eval UNIX_DEPS = $(SHARED_SD))
	$(eval WIN_DEPS = $(WIN_SD))
	$(eval DEPS = $(if $(findstring win64,$(ARCH)),$(WIN_DEPS),$(UNIX_DEPS)))
	@for dep in $(DEPS); do \
		test -f "$$dep" || { echo "[ERROR] Missing $$dep."; exit 1; }; \
	done
	$(eval OBJS = $(BIN_ROOT)/$(ARCH)/main.o)
	$(MAKE) $(OBJS) ARCH=$(ARCH) CXX="$(CXX)" EXT="$(EXT)" EXTRA_CXXFLAGS="$(EXTRA_CXXFLAGS)"
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(BIN_ROOT)/$(ARCH)/$(NAME)$(EXT) $(if $(findstring win64,$(ARCH)),$(WIN_DEPS) $(SYSLIBS_WIN),$(UNIX_DEPS) $(SYSLIBS_UNIX))
	@if [ "$(ARCH)" != "win64" ] && command -v patchelf >/dev/null 2>&1; then \
		patchelf --remove-rpath $(BIN_ROOT)/$(ARCH)/$(NAME)$(EXT) || true; \
	fi

$(BIN_ROOT)/$(ARCH)/%.o: src/%.cpp
	mkdir -p $(BIN_ROOT)/$(ARCH)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BIN_ROOT) install.exe uninstall.exe
