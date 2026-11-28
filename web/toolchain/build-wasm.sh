#!/bin/sh -e
# Build the pi32v2 LLVM toolchain (clang, ld.lld, llvm-objcopy) as one WebAssembly (WASI) program for the web
# builder (web/, docs/WEB-FLASHER.md). Runs on Linux or WSL; takes about an hour on 24 cores.
#
#   sh web/toolchain/build-wasm.sh [WORKDIR]       # default ~/wasm
#
# Output: WORKDIR/out/llvm.wasm (one "multicall" program: argv[0] = clang | ld.lld | llvm-objcopy) and
# WORKDIR/out/clang-include/ (the compiler's own headers). The method follows YoWASP's clang build
# (github.com/YoWASP/clang, Apache-2.0): a native tblgen first, then LLVM cross-built with wasi-sdk.
set -x
W=${1:-$HOME/wasm}
HERE=$(cd "$(dirname "$0")" && pwd)
LLVM_TAG=${LLVM_TAG:-pi32v2-23.1.6-2}
WASI_VER=34
mkdir -p "$W" && cd "$W"

if [ ! -d wasi-sdk-$WASI_VER.0-x86_64-linux ]; then
  curl -sL https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$WASI_VER/wasi-sdk-$WASI_VER.0-x86_64-linux.tar.gz | tar xzf -
fi
WASI_SDK=$W/wasi-sdk-$WASI_VER.0-x86_64-linux
if [ ! -d src ]; then
  git clone --depth 1 --branch "$LLVM_TAG" https://github.com/jvitkauskas/pi32v2-llvm.git src
  # YoWASP's one patch that makes LLVM build and run on WASI (no signals, processes, sockets, ...), made for LLVM
  # 22.1.0; it applies to pi32v2-llvm (23.1.x) with small offsets. Apache-2.0 with LLVM Exceptions, as LLVM.
  YP=97196c8eeb1d495fa43bb8af2fb26af5ef5b89fb
  curl -sL https://github.com/YoWASP/llvm-project/commit/$YP.patch -o yowasp-wasi.patch
  (cd src && patch -p1 -F3 < ../yowasp-wasi.patch)
  # Small fix-ups where LLVM 23 differs from 22
  perl -0pi "$HERE/wasi-fixups.pl" src/llvm/lib/Support/Signals.cpp src/llvm/lib/Support/CrashRecoveryContext.cpp \
    src/llvm/lib/Support/Unix/Path.inc src/llvm/lib/Support/Unix/Process.inc
  perl -0pi "$HERE/driver-fixups.pl" src/clang/tools/driver/driver.cpp
fi

# LLVM has calls to mmap it never makes in this set-up; clang and lld may need lots of memory and stack.
CF="--sysroot $WASI_SDK/share/wasi-sysroot -D_WASI_EMULATED_MMAN -flto=thin"
LF="--sysroot $WASI_SDK/share/wasi-sysroot -lwasi-emulated-mman -flto=thin -Wl,--strip-all -Wl,--max-memory=4294967296 -Wl,-z,stack-size=8388608,--stack-first"
# CMake 3.x has no WASI platform: ours says it is Unix-like (what LLVM's cmake expects of WASI).
mkdir -p cmake/Platform
printf 'set(WASI 1)\nset(UNIX 1)\nset(CMAKE_EXECUTABLE_SUFFIX "")\n' > cmake/Platform/WASI.cmake
cat > wasi.cmake <<END
list(APPEND CMAKE_MODULE_PATH $W/cmake)
set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_COMPILER $WASI_SDK/bin/clang)
set(CMAKE_C_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_CXX_COMPILER $WASI_SDK/bin/clang++)
set(CMAKE_CXX_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_LINKER $WASI_SDK/bin/wasm-ld)
set(CMAKE_AR $WASI_SDK/bin/ar)
set(CMAKE_RANLIB $WASI_SDK/bin/ranlib)
set(CMAKE_C_FLAGS "$CF")
set(CMAKE_CXX_FLAGS "$CF")
set(CMAKE_EXE_LINKER_FLAGS "$LF")
END

# 1. Native table generators (run at build time on this PC)
if [ ! -x tblgen/bin/clang-tblgen ]; then
  cmake -G Ninja -B tblgen -S src/llvm -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang \
    -DLLVM_TARGETS_TO_BUILD="" -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=PI32V2 -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF -DCLANG_INCLUDE_TESTS=OFF
  cmake --build tblgen --target llvm-tblgen clang-tblgen
fi

# 2. clang + lld + objcopy for WASI, one program
OFF=""
for t in BUGPOINT DSYMUTIL GOLD LLC LLI LLVM_AS LLVM_BCANALYZER LLVM_CAT LLVM_CFI_VERIFY LLVM_CONFIG LLVM_COV \
         LLVM_CVTRES LLVM_CXXDUMP LLVM_CXXFILT LLVM_CXXMAP LLVM_DEBUGINFOD LLVM_DEBUGINFOD_FIND LLVM_DIFF LLVM_DIS \
         LLVM_DWARFDUMP LLVM_DWARFUTIL LLVM_DWP LLVM_EXEGESIS LLVM_EXTRACT LLVM_GSYMUTIL LLVM_IFS LLVM_JITLINK \
         LLVM_LIBTOOL_DARWIN LLVM_LINK LLVM_LIPO LLVM_LTO2 LLVM_LTO LLVM_MCA LLVM_MC LLVM_ML LLVM_MODEXTRACT LLVM_MT \
         LLVM_NM LLVM_OBJDUMP LLVM_PDBUTIL LLVM_PROFDATA LLVM_PROFGEN LLVM_RC LLVM_READOBJ LLVM_READTAPI LLVM_REDUCE \
         LLVM_REMARKUTIL LLVM_RTDYLD LLVM_SIM LLVM_SIZE LLVM_SPLIT LLVM_STRESS LLVM_STRINGS LLVM_SYMBOLIZER \
         LLVM_TLI_CHECKER LLVM_UNDNAME LLVM_XRAY LTO OBJ2YAML OPT REMARKS_SHLIB SANCOV SANSTATS VERIFY_USELISTORDER \
         YAML2OBJ LLVM_AR LLVM_DLANG_DEMANGLE_FUZZER LLVM_ITANIUM_DEMANGLE_FUZZER LLVM_MICROSOFT_DEMANGLE_FUZZER \
         LLVM_RUST_DEMANGLE_FUZZER LLVM_DIS_FUZZER LLVM_AS_FUZZER LLVM_ISEL_FUZZER LLVM_OPT_FUZZER \
         LLVM_MC_ASSEMBLE_FUZZER LLVM_MC_DISASSEMBLE_FUZZER LLVM_SPECIAL_CASE_LIST_FUZZER LLVM_YAML_NUMERIC_PARSER_FUZZER \
         LLVM_YAML_PARSER_FUZZER VFABI_DEMANGLE_FUZZER LLVM_DEBUGINFO_ANALYZER LLVM_JITLISTENER LLVM_OPT_REPORT \
         LLVM_SHLIB DXIL_DIS SPIRV_TOOLS REDUCE_CHUNK_LIST XCODE_TOOLCHAIN OPT_VIEWER BUGPOINT_PASSES LLVM_C_TEST; do
  OFF="$OFF -DLLVM_TOOL_${t}_BUILD=OFF"
done
cmake -G Ninja -B wasm -S src/llvm -DCMAKE_TOOLCHAIN_FILE="$W/wasi.cmake" -DLLVM_NATIVE_TOOL_DIR="$W/tblgen/bin" \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_BUILD_SHARED_LIBS=OFF -DLLVM_ENABLE_PIC=OFF \
  -DLLVM_BUILD_STATIC=ON -DLLVM_ENABLE_THREADS=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
  -DLLVM_BUILD_RUNTIME=OFF -DLLVM_BUILD_TOOLS=OFF -DLLVM_INCLUDE_UTILS=OFF -DLLVM_BUILD_UTILS=OFF \
  -DLLVM_INCLUDE_RUNTIMES=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_DOCS=OFF -DLLVM_TARGETS_TO_BUILD="" -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=PI32V2 -DLLVM_DEFAULT_TARGET_TRIPLE=pi32v2 \
  -DLLVM_TOOL_LLVM_DRIVER_BUILD=ON -DLLVM_TOOL_LLVM_OBJCOPY_BUILD=ON $OFF \
  -DLLVM_ENABLE_PROJECTS="clang;lld" -DCLANG_ENABLE_ARCMT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
  -DCLANG_INCLUDE_TESTS=OFF -DCLANG_BUILD_TOOLS=OFF -DCLANG_TOOL_CLANG_SCAN_DEPS_BUILD=OFF \
  -DCLANG_TOOL_CLANG_INSTALLAPI_BUILD=OFF -DCLANG_BUILD_EXAMPLES=OFF -DCLANG_INCLUDE_DOCS=OFF \
  -DCLANG_LINKS_TO_CREATE=clang -DLLD_BUILD_TOOLS=OFF -DCLANG_RESOURCE_DIR=/clang
cmake --build wasm -j 14 --target llvm-driver clang-resource-headers

mkdir -p out
cp wasm/bin/llvm out/llvm.wasm
rm -rf out/clang-include && cp -r wasm/clang/include out/clang-include 2>/dev/null || cp -r wasm/lib/clang/*/include out/clang-include
ls -la out
