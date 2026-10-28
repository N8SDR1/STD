# Copyright (C) 2026 Rick Langford (N8SDR)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# STD — Shift, Tone, Decode
# Qt 6 Widgets. Debian/Ubuntu package: qt6-base-dev

cmake_minimum_required(VERSION 3.21)

project(STD
  VERSION 0.1.0
  DESCRIPTION "STD — Shift, Tone, Decode"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.2 QUIET COMPONENTS Widgets)
if(NOT Qt6Widgets_FOUND)
  message(FATAL_ERROR
    "Qt 6 Widgets was not found, so STD cannot be configured.\n"
    "Install the Qt 6 Widgets development package and re-run cmake.\n"
    "  Debian/Ubuntu:  sudo apt install qt6-base-dev\n"
    "  Fedora:         sudo dnf install qt6-qtbase-devel\n"
    "  Arch:           sudo pacman -S qt6-base\n"
    "If Qt is installed outside the default prefix, set Qt6_DIR to the\n"
    "directory that contains Qt6Config.cmake.")
endif()

find_package(Qt6 6.2 QUIET COMPONENTS WebSockets)
if(NOT Qt6WebSockets_FOUND)
  message(FATAL_ERROR
    "Qt 6 WebSockets was not found, so STD cannot open a TCI socket.\n"
    "Install the Qt 6 WebSockets development package and re-run cmake.\n"
    "  Debian/Ubuntu:  sudo apt install qt6-websockets-dev\n"
    "  Fedora:         sudo dnf install qt6-qtwebsockets-devel\n"
    "Do not install a second Qt kit if 6.2 or newer is already present.\n"
    "Point Qt6_DIR at that kit's lib/cmake/Qt6 directory.")
endif()

qt_standard_project_setup()

qt_add_executable(std
  WIN32
  src/main.cpp
  src/MainWindow.cpp
  src/MainWindow.h
  src/WaterfallWidget.cpp
  src/WaterfallWidget.h
  src/ModeProfile.cpp
  src/ModeProfile.h
  src/Ft8Decode.cpp
  src/Ft8Decode.h
  src/StatusStrip.cpp
  src/StatusStrip.h
  src/TciClient.cpp
  src/TciClient.h
  resources/resources.qrc
)

target_compile_definitions(std PRIVATE STD_VERSION="${PROJECT_VERSION}")

target_link_libraries(std PRIVATE Qt6::Widgets Qt6::WebSockets)

if(WIN32)
  set(FT8_DLL "${CMAKE_BINARY_DIR}/ft8dec.dll")
  add_custom_command(
    OUTPUT "${FT8_DLL}"
    COMMAND "${CMAKE_COMMAND}" -DSCRIPT=${CMAKE_SOURCE_DIR}/src/ft8/build_ft8dec.cmd -DOUT=${CMAKE_BINARY_DIR} -P ${CMAKE_SOURCE_DIR}/src/ft8/run_ft8dec.cmake
    DEPENDS
      "${CMAKE_SOURCE_DIR}/src/ft8/build_ft8dec.cmd"
      "${CMAKE_SOURCE_DIR}/src/ft8/run_ft8dec.cmake"
      "${CMAKE_SOURCE_DIR}/src/ft8/invoke_ft8dec.ps1"
      "${CMAKE_SOURCE_DIR}/src/ft8/ft8_bridge.cpp"
      "${CMAKE_SOURCE_DIR}/src/ft8/ft8_api.h"
      "${CMAKE_SOURCE_DIR}/src/ft8/decoderms_ft8.h"
      "${CMAKE_SOURCE_DIR}/src/ft8/qt6_compat.h"
      "${CMAKE_SOURCE_DIR}/src/ft8/patch_qt_import.py"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderft8.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderft8var.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderft4.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvMsPlayer/libsound/HvGenFt4/gen_ft4.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderft2.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvMsPlayer/libsound/HvGenFt2/gen_ft2.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderpom.cpp"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building the MSHV FT8, FT4, and FT2 decoder"
    VERBATIM)
  add_custom_target(ft8dec DEPENDS "${FT8_DLL}")
  add_dependencies(std ft8dec)
endif()

if(MSVC)
  target_compile_options(std PRIVATE /W4)
else()
  target_compile_options(std PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS std RUNTIME DESTINATION bin)
