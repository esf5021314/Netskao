# 魔兽争霸 III 修改器（大象修改器 C++ 重制版）
#
# Visual Studio 2019（16.10 以上）/ 2022：文件 -> 打开 -> 文件夹，选择本目录，
#   配置下拉框选 x86-Release（见 CMakePresets.json）；VS 2017 请用 build_msvc.bat
#   或命令行：cmake -B build -A Win32 && cmake --build build --config Release
# MinGW：     cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++
#
# 必须编译为 32 位：魔兽争霸 III 是 32 位程序。
cmake_minimum_required(VERSION 3.15)
project(War3Trainer CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "必须编译为 32 位（Visual Studio 请选择 Win32 / x86）")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_CURRENT_SOURCE_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg} ${CMAKE_CURRENT_SOURCE_DIR}/bin)
endforeach()

add_compile_definitions(UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    # 静态 CRT（/MT），源文件按 UTF-8 解析
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /W3)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I\"${CMAKE_CURRENT_SOURCE_DIR}/res\" /I\"${CMAKE_CURRENT_SOURCE_DIR}/src/gui\"")
else()
    add_compile_options(-fno-strict-aliasing -Wall -Wno-unused-parameter)
    add_link_options(-static -static-libgcc -static-libstdc++)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/res -I${CMAKE_CURRENT_SOURCE_DIR}/src/gui")
endif()

# ---- 注入模块 ----
file(GLOB DLL_SOURCES CONFIGURE_DEPENDS src/dll/*.cpp)
add_library(War3TrainerDll SHARED ${DLL_SOURCES})
# 输出名与界面程序相同（War3Trainer），调试信息另起名字，避免两者都写 bin/War3Trainer.pdb / .ilk
set_target_properties(War3TrainerDll PROPERTIES OUTPUT_NAME War3Trainer PREFIX "" PDB_NAME War3TrainerDll)
target_include_directories(War3TrainerDll PRIVATE include src/dll)
target_link_libraries(War3TrainerDll PRIVATE version user32)
if(MSVC)
    target_link_options(War3TrainerDll PRIVATE /INCREMENTAL:NO)
else()
    target_link_options(War3TrainerDll PRIVATE -Wl,--exclude-all-symbols)
endif()

# ---- 界面程序 ----
file(GLOB GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp)
add_executable(War3Trainer WIN32 ${GUI_SOURCES} res/app.rc)
target_include_directories(War3Trainer PRIVATE src/gui)
target_link_libraries(War3Trainer PRIVATE comctl32 user32 gdi32 advapi32)
if(MSVC)
    # 清单已经在 app.rc 中（requireAdministrator），关闭链接器自动生成的清单
    target_link_options(War3Trainer PRIVATE /MANIFEST:NO)
else()
    target_link_options(War3Trainer PRIVATE -municode)
endif()
add_dependencies(War3Trainer War3TrainerDll)
