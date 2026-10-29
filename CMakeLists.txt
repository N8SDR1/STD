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

# This machine has MinGW and MSVC kits side by side. STD links the MSVC
# kit only. A MinGW Qt on PATH must not win find_package or windeployqt.
if(WIN32 AND EXISTS "C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6/Qt6Config.cmake")
  list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")
endif()

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

find_package(Qt6 6.2 QUIET COMPONENTS Network)
if(NOT Qt6Network_FOUND)
  message(FATAL_ERROR
    "Qt 6 Network was not found, so STD cannot reach a logger.\n"
    "Install the Qt 6 base development package and re-run cmake.")
endif()

find_package(Qt6 6.2 QUIET COMPONENTS GuiPrivate)
if(NOT Qt6GuiPrivate_FOUND)
  message(FATAL_ERROR
    "Qt 6 Gui private headers were not found, so the waterfall cannot use Vulkan or Direct3D.\n"
    "Install the Qt 6 base development package and re-run cmake.")
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
  src/WaterfallGpu.cpp
  src/WaterfallGpu.h
  src/ModeProfile.cpp
  src/ModeProfile.h
  src/Ft8Decode.cpp
  src/Ft8Decode.h
  src/StatusStrip.cpp
  src/StatusStrip.h
  src/TciClient.cpp
  src/TciClient.h
  src/TxWave.cpp
  src/TxWave.h
  src/QsoLog.cpp
  src/QsoLog.h
  src/MacroFile.cpp
  src/MacroFile.h
  src/hell/HellEngine.cpp
  src/hell/HellEngine.h
  src/hell/HellRaster.cpp
  src/hell/HellRaster.h
  src/hell/fldigi/filters.cxx
  src/hell/fldigi/fftfilt.cxx
  src/rtty/RttyModem.cpp
  src/rtty/RttyModem.h
  src/psk/PskModem.cpp
  src/psk/PskModem.h
  src/wefax/WefaxModem.cpp
  src/wefax/WefaxModem.h
  src/olivia/OliviaModem.cpp
  src/olivia/OliviaModem.h
  src/mfsk/MfskModem.cpp
  src/mfsk/MfskModem.h
  src/dominoex/DominoModem.cpp
  src/dominoex/DominoModem.h
  src/thor/ThorModem.cpp
  src/thor/ThorModem.h
  src/mt63/Mt63Modem.cpp
  src/mt63/Mt63Modem.h
  src/js8/Js8Modem.cpp
  src/js8/Js8Modem.h
  src/wspr/WsprModem.cpp
  src/wspr/WsprModem.h
  src/spot/PskReporter.cpp
  src/spot/PskReporter.h
  src/UpdateChecker.cpp
  src/UpdateChecker.h
  src/mt63/fldigi/dsp.cxx
  src/mt63/fldigi/mt63base.cxx
  resources/resources.qrc
)

target_compile_definitions(std PRIVATE
  STD_VERSION="${PROJECT_VERSION}"
  STD_REPO_OWNER="N8SDR1"
  STD_REPO_NAME="STD")

# Embed the purple capsule into std.exe so Explorer, the taskbar, and the
# installer shortcuts show that icon. Same pattern as Lyra's lyra.rc.
if(WIN32)
  target_sources(std PRIVATE src/std.rc)
endif()
target_include_directories(std PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/hell/fldigi"
  "${CMAKE_SOURCE_DIR}/src/mt63/fldigi")

target_link_libraries(std PRIVATE Qt6::Widgets Qt6::GuiPrivate Qt6::Network Qt6::WebSockets)

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
      "${CMAKE_SOURCE_DIR}/src/ft8/decoderms_jt65.h"
      "${CMAKE_SOURCE_DIR}/src/ft8/jt65_bridge.cpp"
      "${CMAKE_SOURCE_DIR}/src/ft8/qt6_compat.h"
      "${CMAKE_SOURCE_DIR}/src/ft8/patch_qt_import.py"
      "${CMAKE_SOURCE_DIR}/src/ft8/patch_moc_undef.py"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderft8.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderft8var.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderft4.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvMsPlayer/libsound/HvGenFt4/gen_ft4.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderft2.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvMsPlayer/libsound/HvGenFt2/gen_ft2.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderpom.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderjt65.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvDecoderMs/decoderq65.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvMsPlayer/libsound/HvGen65/gen65.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvMsPlayer/libsound/HvGenQ65/gen_q65.cpp"
      "${CMAKE_SOURCE_DIR}/../MSHV/src/HvMsPlayer/libsound/HvGenQ65/q65_subs.cpp"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building the MSHV FT8, FT4, FT2, JT65, and Q65 decoder"
    VERBATIM)
  add_custom_target(ft8dec DEPENDS "${FT8_DLL}")
  add_dependencies(std ft8dec)
endif()

if(MSVC)
  target_compile_options(std PRIVATE /W4)
  set_source_files_properties(
    src/hell/fldigi/filters.cxx
    src/hell/fldigi/fftfilt.cxx
    src/mt63/fldigi/dsp.cxx
    src/mt63/fldigi/mt63base.cxx
    PROPERTIES COMPILE_OPTIONS "/W1")
else()
  target_compile_options(std PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Deploy from the same kit CMake configured (Qt6::qmake's directory).
# NO_DEFAULT_PATH keeps a MinGW windeployqt on PATH from being chosen.
if(WIN32)
  get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
  find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin_dir}" NO_DEFAULT_PATH)
  if(WINDEPLOYQT_EXE)
    add_custom_command(TARGET std POST_BUILD
      COMMAND "${WINDEPLOYQT_EXE}"
        --no-translations
        "$<TARGET_FILE:std>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt_bin_dir}/../plugins/platforms/qoffscreen.dll"
        "$<TARGET_FILE_DIR:std>/platforms/qoffscreen.dll"
      COMMENT "windeployqt from ${_qt_bin_dir}"
      VERBATIM)
  endif()
endif()

install(TARGETS std RUNTIME DESTINATION bin)
