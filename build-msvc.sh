#!/bin/sh
# Build PhoenixBC2.dll on Windows with LLVM clang + the Visual Studio Build Tools headers/libs (no MinGW needed).
# Run from Git Bash in this folder. Adjust the three paths below if your versions differ.
set -e
CLANG=${CLANG:-"/c/Program Files/LLVM/bin/clang.exe"}
VC=${VC:-"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207"}
SDK=${SDK:-"C:/Program Files (x86)/Windows Kits/10"}; V=${SDKVER:-10.0.26100.0}
export INCLUDE="$VC/include;$SDK/Include/$V/ucrt;$SDK/Include/$V/um;$SDK/Include/$V/shared"
export LIB="$VC/lib/x86;$SDK/Lib/$V/ucrt/x86;$SDK/Lib/$V/um/x86"
mkdir -p bin
"$CLANG" --target=i686-pc-windows-msvc -fms-runtime-lib=static -O2 -std=gnu11 -Wall -Wextra -Wno-cast-function-type -Wno-unused-parameter \
  -Wno-missing-field-initializers -Wno-misleading-indentation -D_WIN32_WINNT=0x0601 -D_CRT_SECURE_NO_WARNINGS -Isrc -Iruntime -msse2 -mfpmath=sse \
  -shared -fuse-ld=lld -Wl,/EXPORT:PhoenixStart=_PhoenixStart@4 -Wl,/EXPORT:PhoenixStop=_PhoenixStop@4 \
  runtime/phoenix_module.c runtime/engine.c runtime/world.c runtime/bots.c runtime/learn.c runtime/indoor.c runtime/turret.c runtime/navglue.c \
  runtime/roads.c runtime/win_common.c src/bc2_observe.c src/bc2_tick_hook.c src/nav.c -lbcrypt -luser32 -lkernel32 -ladvapi32 -o bin/PhoenixBC2.dll
rm -f bin/PhoenixBC2.lib bin/PhoenixBC2.exp
echo "built bin/PhoenixBC2.dll"
