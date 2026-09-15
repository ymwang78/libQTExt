cmake_minimum_required(VERSION 3.22)

# libQTExt 的 GTest。libQTExt 本身由 libQTExt.vcxproj（MSBuild）构建，这里单独起一个
# CMake 工程，把被测的 libQTExt 源码直接编进测试程序，头文件交给 AUTOMOC。
#
# Windows（x64 开发者命令行，单配置生成器；Debug 以外的配置都链 Release 版 libzce）：
#   cmake -S tests -B tests/build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
#         -DCMAKE_PREFIX_PATH="C:/Qt/6.10.3/msvc2022_64;D:/Github/vcpkg/installed/x64-windows-static-md"
#   cmake --build tests/build
#   ctest --test-dir tests/build --output-on-failure
# 运行时 PATH 需带 Qt 的 bin。未设置 QT_QPA_PLATFORM 时测试自己切到 offscreen。
project(libqtext_tests LANGUAGES CXX)

include(CTest)
if(NOT BUILD_TESTING)
    return()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571  # v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

get_filename_component(LIBQTEXT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# 默认按 cxxproj/libsrc/libQTExt 的位置推算；在 git worktree 等别处检出时手动指定。
get_filename_component(_default_cxxproj_root "${LIBQTEXT_DIR}/../.." ABSOLUTE)
set(CXXPROJ_ROOT "${_default_cxxproj_root}" CACHE PATH
    "cxxproj workspace root (provides include/zce and the libzce build)")

# xTableView.cpp / xItemDelegate.cpp 用到 zce::Any，它的 JSON 转换实现在 libzce 里。
if(WIN32)
    # libzce.lib 是 MSBuild 的产物：Debug 是 /MDd，Release 是 /MD。它用到的 libuv / bz2 /
    # libcrypto 取自 CMAKE_PREFIX_PATH 里的 vcpkg 安装树，Debug 配置取其中 debug/lib 下的版本，
    # 免得一个进程里混两种 CRT。
    set(_zce_config Release)
    set(_zce_dep_hints "")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_zce_config Debug)
        foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
            list(APPEND _zce_dep_hints "${_prefix}/debug/lib")
        endforeach()
    endif()
    find_library(LIBQTEXT_ZCE_LIBRARY NAMES libzce
                 PATHS "${CXXPROJ_ROOT}/libsrc/libzce/bin/x64/${_zce_config}" NO_DEFAULT_PATH)
    find_library(LIBQTEXT_UV_LIBRARY NAMES libuv uv HINTS ${_zce_dep_hints})
    find_library(LIBQTEXT_BZ2_LIBRARY NAMES bz2d bz2 HINTS ${_zce_dep_hints})
    find_library(LIBQTEXT_CRYPTO_LIBRARY NAMES libcrypto HINTS ${_zce_dep_hints})
    foreach(_dep LIBQTEXT_UV_LIBRARY LIBQTEXT_BZ2_LIBRARY LIBQTEXT_CRYPTO_LIBRARY)
        if(NOT ${_dep})
            message(FATAL_ERROR "${_dep} not found; add the vcpkg install tree "
                                "(e.g. vcpkg/installed/x64-windows-static-md) to CMAKE_PREFIX_PATH")
        endif()
    endforeach()
    set(LIBQTEXT_ZCE_DEPS
        "${LIBQTEXT_UV_LIBRARY}" "${LIBQTEXT_BZ2_LIBRARY}" "${LIBQTEXT_CRYPTO_LIBRARY}"
        ws2_32 iphlpapi shlwapi psapi crypt32 userenv dbghelp ole32 advapi32 user32 shell32)
else()
    # 与 xRto 的 Linux 构建同一套依赖。
    find_library(LIBQTEXT_ZCE_LIBRARY NAMES zce libzce.a
                 PATHS "${CXXPROJ_ROOT}/lib/linux_x86_64" "${CXXPROJ_ROOT}/libsrc/libzce/build"
                 NO_DEFAULT_PATH)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUV REQUIRED IMPORTED_TARGET libuv)
    find_package(OpenSSL REQUIRED)
    find_package(BZip2 REQUIRED)
    set(LIBQTEXT_ZCE_DEPS
        PkgConfig::LIBUV BZip2::BZip2 OpenSSL::SSL OpenSSL::Crypto uuid dl pthread)
endif()
if(NOT LIBQTEXT_ZCE_LIBRARY)
    message(FATAL_ERROR "libzce not found; build libsrc/libzce first "
                        "(or pass -DLIBQTEXT_ZCE_LIBRARY=<path to libzce>)")
endif()

set(LIBQTEXT_TABLE_SOURCES
    "${LIBQTEXT_DIR}/xItemDelegate.cpp"
    "${LIBQTEXT_DIR}/xItemDelegate.h"
    "${LIBQTEXT_DIR}/xTableEditor.cpp"
    "${LIBQTEXT_DIR}/xTableEditor.h"
    "${LIBQTEXT_DIR}/xTableHeader.cpp"
    "${LIBQTEXT_DIR}/xTableHeader.h"
    "${LIBQTEXT_DIR}/xTableView.cpp"
    "${LIBQTEXT_DIR}/xTableView.h"
)

# 追加模式：往占位行提交空值不新增行（xAbstractTableModel::setData / xTableView::paste）。
add_executable(test_append_mode_placeholder
    test_append_mode_placeholder.cpp
    ${LIBQTEXT_TABLE_SOURCES}
)
target_compile_definitions(test_append_mode_placeholder PRIVATE USE_GTEST_MAIN)
target_include_directories(test_append_mode_placeholder
    PRIVATE
        "${LIBQTEXT_DIR}"
        "${CXXPROJ_ROOT}/include"
)
target_link_libraries(test_append_mode_placeholder
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets
        Qt6::Test
        "${LIBQTEXT_ZCE_LIBRARY}"
        ${LIBQTEXT_ZCE_DEPS}
)
if(MSVC)
    target_compile_options(test_append_mode_placeholder PRIVATE /utf-8 /wd4996)
endif()
add_test(NAME append_mode_placeholder COMMAND test_append_mode_placeholder)
