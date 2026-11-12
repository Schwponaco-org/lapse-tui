# lapse-tui - a terminal interface for the LAPSE subtitle sync engine
# Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.22)

project(lapse-tui
    VERSION 0.1.0
    DESCRIPTION "Terminal interface for the LAPSE subtitle sync engine"
    LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "build type" FORCE)
endif()

option(LAPSE_TUI_STATIC   "link everything statically, for a binary to copy onto a server" OFF)
option(LAPSE_TUI_SANITIZE "build with AddressSanitizer and UBSan, slow" OFF)

include(FetchContent)
find_package(ftxui 6 QUIET CONFIG)
if(ftxui_FOUND)
    message(STATUS "FTXUI ${ftxui_VERSION} from the system")
else()
    set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(FTXUI_QUIET          ON  CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
        GIT_TAG        5cfed50702f52d51c1b189b5f97f8beaf5eaa2a6 # v6.1.9
        GIT_SHALLOW    FALSE)
    message(STATUS "FTXUI not found, building v6.1.9 in the build tree")
    FetchContent_MakeAvailable(ftxui)
endif()

# everything but the screen, so the tests can link it
add_library(lapse_tui_core STATIC
    src/config.cpp
    src/engine.cpp
    src/library.cpp
    src/match.cpp
    src/names.cpp)
target_compile_features(lapse_tui_core PUBLIC cxx_std_20)
target_include_directories(lapse_tui_core PUBLIC src)
target_compile_options(lapse_tui_core PUBLIC -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(lapse_tui_core PUBLIC Threads::Threads)

if(LAPSE_TUI_SANITIZE)
    target_compile_options(lapse_tui_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(lapse_tui_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(lapse-tui tui/lapse-tui.cpp)
target_compile_definitions(lapse-tui PRIVATE LAPSE_TUI_VERSION="${PROJECT_VERSION}")
target_link_libraries(lapse-tui PRIVATE lapse_tui_core ftxui::component ftxui::dom ftxui::screen)

if(LAPSE_TUI_STATIC)
    # macOS has no static libc, and its libc++ is always there
    if(NOT APPLE)
        target_link_options(lapse-tui PRIVATE -static)
    endif()
endif()

enable_testing()
foreach(name library match)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE lapse_tui_core)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()

include(GNUInstallDirs)
install(TARGETS lapse-tui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
