#!/usr/bin/env bash
# MinGW-w64 编译脚本（Linux 交叉编译，或 Windows 下的 MSYS2 / i686 工具链）
#
#   ./build_mingw.sh          编译 bin/War3Trainer.exe 与 bin/War3Trainer.dll
#   ./build_mingw.sh test     另外编译 build/selftest.exe 与模拟游戏测试程序（tests/）
#
# 必须是 32 位（i686）工具链：魔兽争霸 III 是 32 位程序。
set -e
cd "$(dirname "$0")"

CXX=${CXX:-i686-w64-mingw32-g++}
WINDRES=${WINDRES:-i686-w64-mingw32-windres}
mkdir -p bin build

COMMON="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -DUNICODE -D_UNICODE -static -static-libgcc -static-libstdc++ -s"

echo "[1/3] 注入模块 War3Trainer.dll"
$CXX $COMMON -Iinclude -Isrc/dll -shared -Wl,--exclude-all-symbols \
    -o bin/War3Trainer.dll src/dll/*.cpp -lversion -luser32

echo "[2/3] 资源"
$WINDRES -I src/gui -O coff -o build/app.res.o res/app.rc

echo "[3/3] 界面程序 War3Trainer.exe"
$CXX $COMMON -municode -mwindows -Isrc/gui \
    -o bin/War3Trainer.exe src/gui/*.cpp build/app.res.o -lcomctl32 -luser32 -lgdi32 -ladvapi32

if [ "$1" = "test" ]; then
    echo "[测试] selftest / 模拟游戏 / 链路测试 / 补丁测试"
    mkdir -p build/mock
    $CXX $COMMON -Iinclude -Isrc/dll -o build/selftest.exe tests/selftest.cpp \
        src/dll/SafeCall.cpp src/dll/Offsets.cpp src/dll/Jass.cpp src/gui/Hotkey.cpp src/gui/Rows.cpp
    $WINDRES -O coff -o build/mock/fakegame.res.o tests/mock/FakeGame.rc
    $CXX -O2 -shared -static -s -o build/mock/Game.dll tests/mock/FakeGame.cpp build/mock/fakegame.res.o
    $CXX -O2 -mwindows -static -s -o build/mock/war3.exe tests/mock/MockGame.cpp
    $CXX $COMMON -Isrc/gui -o build/mock/linktest.exe tests/linktest.cpp src/gui/GameLink.cpp -ladvapi32
    $CXX $COMMON -Isrc/gui -o build/mock/patchtest.exe tests/patchtest.cpp src/gui/GameLink.cpp -ladvapi32
    cp bin/War3Trainer.dll build/mock/
fi

echo "完成：bin/War3Trainer.exe  bin/War3Trainer.dll"
