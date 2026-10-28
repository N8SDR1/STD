@echo off
setlocal
rem Build ft8dec.dll from the sibling MSHV tree. The algorithm sources
rem stay there, with their copyright headers. This script does not edit them.
set OUT=%~1
if "%OUT%"=="" set OUT=%~dp0..\..\build
set MSHV=%~dp0..\..\..\MSHV
set STD=%~dp0..\..
set GPP=C:\Qt\Tools\mingw1310_64\bin\g++.exe
set MOC=C:\Qt\6.11.1\mingw_64\bin\moc.exe
set QTINC=C:\Qt\6.11.1\mingw_64\include
set QTLIB=C:\Qt\6.11.1\mingw_64\lib
set QTBIN=C:\Qt\6.11.1\mingw_64\bin
set MINGW=C:\Qt\Tools\mingw1310_64\bin
if not exist "%MSHV%\src\HvDecoderMs\decoderft8.cpp" (
  echo MSHV FT8 sources were not found at "%MSHV%"
  exit /b 1
)
if not exist "%GPP%" (
  echo MinGW g++ was not found
  exit /b 1
)
set PATH=%MINGW%;%QTBIN%;%PATH%
set OBJ=%OUT%\ft8obj
if not exist "%OBJ%" mkdir "%OBJ%"
set INC=-I"%STD%\src\ft8\qt5" -I"%STD%\src\ft8" -I"%QTINC%" -I"%QTINC%\QtCore" -I"%MSHV%\src" -I"%MSHV%\src\HvDecoderMs" -I"%MSHV%\src\HvMsPlayer\libsound" -I"%MSHV%\src\Hv_Lib_fftw"
set FLAGS=-std=gnu++17 -DQT_CORE_LIB -DQT_NO_DEBUG -D_WIN64_ -c -O2 -w -include "%STD%\src\ft8\qt6_compat.h"

echo MOC decoderms_ft8.h
"%MOC%" %INC% -o "%OBJ%\moc_decoderms_ft8.cpp" "%STD%\src\ft8\decoderms_ft8.h"
if errorlevel 1 exit /b 1

echo COMPILE decoderpom
"%GPP%" %FLAGS% %INC% -o "%OBJ%\decoderpom.o" "%MSHV%\src\HvDecoderMs\decoderpom.cpp"
if errorlevel 1 exit /b 1
echo COMPILE genpom
"%GPP%" %FLAGS% %INC% -o "%OBJ%\genpom.o" "%MSHV%\src\HvMsPlayer\libsound\genpom.cpp"
if errorlevel 1 exit /b 1
echo COMPILE pack
"%GPP%" %FLAGS% %INC% -o "%OBJ%\pack.o" "%MSHV%\src\HvMsPlayer\libsound\HvPackUnpackMsg\pack_unpack_msg77.cpp"
if errorlevel 1 exit /b 1
echo COMPILE gen_ft8
"%GPP%" %FLAGS% %INC% -o "%OBJ%\gen_ft8.o" "%MSHV%\src\HvMsPlayer\libsound\HvGenFt8\gen_ft8.cpp"
if errorlevel 1 exit /b 1
echo COMPILE decoderft8
"%GPP%" %FLAGS% -include "%STD%\src\ft8\decoderms_ft8.h" %INC% -o "%OBJ%\decoderft8.o" "%MSHV%\src\HvDecoderMs\decoderft8.cpp"
if errorlevel 1 exit /b 1
echo COMPILE decoderft8var
"%GPP%" %FLAGS% -include "%STD%\src\ft8\decoderms_ft8.h" %INC% -o "%OBJ%\decoderft8var.o" "%MSHV%\src\HvDecoderMs\decoderft8var.cpp"
if errorlevel 1 exit /b 1
echo COMPILE gen_ft4
"%GPP%" %FLAGS% %INC% -o "%OBJ%\gen_ft4.o" "%MSHV%\src\HvMsPlayer\libsound\HvGenFt4\gen_ft4.cpp"
if errorlevel 1 exit /b 1
echo COMPILE decoderft4
"%GPP%" %FLAGS% -include "%STD%\src\ft8\decoderms_ft8.h" %INC% -o "%OBJ%\decoderft4.o" "%MSHV%\src\HvDecoderMs\decoderft4.cpp"
if errorlevel 1 exit /b 1
echo COMPILE gen_ft2
"%GPP%" %FLAGS% %INC% -o "%OBJ%\gen_ft2.o" "%MSHV%\src\HvMsPlayer\libsound\HvGenFt2\gen_ft2.cpp"
if errorlevel 1 exit /b 1
echo COMPILE decoderft2
"%GPP%" %FLAGS% -include "%STD%\src\ft8\decoderms_ft8.h" %INC% -o "%OBJ%\decoderft2.o" "%MSHV%\src\HvDecoderMs\decoderft2.cpp"
if errorlevel 1 exit /b 1
echo COMPILE moc
"%GPP%" -std=gnu++17 -DQT_CORE_LIB -DQT_NO_DEBUG -D_WIN64_ -DSTD_FT8_MOC -c -O2 -w %INC% -o "%OBJ%\moc_decoderms_ft8.o" "%OBJ%\moc_decoderms_ft8.cpp"
if errorlevel 1 exit /b 1
echo COMPILE bridge
"%GPP%" -std=gnu++17 -DQT_CORE_LIB -DQT_NO_DEBUG -D_WIN64_ -c -O2 %INC% -o "%OBJ%\ft8_bridge.o" "%STD%\src\ft8\ft8_bridge.cpp"
if errorlevel 1 exit /b 1

echo LINK ft8dec.dll
"%GPP%" -shared -static-libgcc -static-libstdc++ -o "%OUT%\ft8dec.dll" "%OBJ%\decoderpom.o" "%OBJ%\genpom.o" "%OBJ%\pack.o" "%OBJ%\gen_ft8.o" "%OBJ%\gen_ft4.o" "%OBJ%\decoderft8.o" "%OBJ%\decoderft8var.o" "%OBJ%\decoderft4.o" "%OBJ%\gen_ft2.o" "%OBJ%\decoderft2.o" "%OBJ%\moc_decoderms_ft8.o" "%OBJ%\ft8_bridge.o" "%MSHV%\src\Hv_Lib_fftw\win\libfftw3v3310_gcc730_64bit.a" -L"%QTLIB%" -lQt6Core
if errorlevel 1 exit /b 1

python "%STD%\src\ft8\patch_qt_import.py" "%OUT%\ft8dec.dll"
if errorlevel 1 exit /b 1

copy /Y "%QTBIN%\Qt6Core.dll" "%OUT%\Ft8Core.dll" >nul
copy /Y "%MINGW%\libgcc_s_seh-1.dll" "%OUT%\libgcc_s_seh-1.dll" >nul
copy /Y "%MINGW%\libstdc++-6.dll" "%OUT%\libstdc++-6.dll" >nul
copy /Y "%MINGW%\libwinpthread-1.dll" "%OUT%\libwinpthread-1.dll" >nul
echo FT8DLL_OK
exit /b 0
