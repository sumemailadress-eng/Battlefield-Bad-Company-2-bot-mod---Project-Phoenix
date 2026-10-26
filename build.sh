#!/bin/sh
# Build with a 32-bit MinGW-w64 toolchain (clang or gcc), e.g. i686-w64-mingw32-gcc.
set -e
mkdir -p bin
CC=${CC:-i686-w64-mingw32-gcc}
CF="-O2 -std=gnu11 -Wall -Wextra -Wno-cast-function-type -Wno-unused-parameter -Wno-missing-field-initializers -Wno-misleading-indentation -D_WIN32_WINNT=0x0601 -Isrc -Iruntime -msse2 -mfpmath=sse"
$CC $CF -static -shared -Wl,--kill-at runtime/phoenix_module.c runtime/engine.c runtime/world.c runtime/bots.c runtime/learn.c runtime/indoor.c runtime/turret.c runtime/navglue.c runtime/roads.c runtime/win_common.c src/bc2_observe.c src/bc2_tick_hook.c src/nav.c -lbcrypt -lm -o bin/PhoenixBC2.dll
$CC -O2 -std=gnu11 -D_WIN32_WINNT=0x0601 -municode -static runtime/attach.c runtime/win_common.c -lbcrypt -o bin/PhoenixAttach.exe
# Offline harness (Linux): fake BC2 server + game simulation driving the real runtime logic
gcc -O2 -std=gnu11 -DPHX_HARNESS -Iharness -Isrc -Iruntime -w harness/harness_main.c harness/mock_engine.c runtime/world.c runtime/bots.c runtime/learn.c runtime/indoor.c runtime/turret.c runtime/navglue.c runtime/roads.c src/nav.c src/bc2_observe.c -lm -o harness/phoenix_harness
# Launcher (Windows GUI): paths, bot settings, Start All
$CC -O2 -std=gnu11 -D_WIN32_WINNT=0x0601 -municode -mwindows -static runtime/launcher.c -lcomdlg32 -lshell32 -lws2_32 -ladvapi32 -o PhoenixLauncher.exe
