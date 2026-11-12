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

add_executable(lapse-tui
    tui/lapse-tui.cpp
    src/engine.cpp)

target_compile_features(lapse-tui PRIVATE cxx_std_20)
target_include_directories(lapse-tui PRIVATE src)
target_compile_definitions(lapse-tui PRIVATE LAPSE_TUI_VERSION="${PROJECT_VERSION}")
target_compile_options(lapse-tui PRIVATE -Wall -Wextra)
target_link_libraries(lapse-tui PRIVATE ftxui::component ftxui::dom ftxui::screen)

find_package(Threads REQUIRED)
target_link_libraries(lapse-tui PRIVATE Threads::Threads)

if(LAPSE_TUI_STATIC)
    if(APPLE)
        target_link_options(lapse-tui PRIVATE -static-libstdc++)
    else()
        target_link_options(lapse-tui PRIVATE -static)
    endif()
endif()

if(LAPSE_TUI_SANITIZE)
    target_compile_options(lapse-tui PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(lapse-tui PRIVATE -fsanitize=address,undefined)
endif()

include(GNUInstallDirs)
install(TARGETS lapse-tui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
