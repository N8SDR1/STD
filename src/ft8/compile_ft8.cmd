@echo off
setlocal
set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.1\mingw_64\bin;%PATH%
set GPP=C:\Qt\Tools\mingw1310_64\bin\g++.exe
set OUT=Y:\Claude local\SDRProject\MSHV\build-ft8
set MSHV=Y:\Claude local\SDRProject\MSHV
if not exist "%OUT%" mkdir "%OUT%"
set INC=-I"Y:\Claude local\SDRProject\STD\src\ft8\qt5" -I"C:\Qt\6.11.1\mingw_64\include" -I"C:\Qt\6.11.1\mingw_64\include\QtCore" -I"%MSHV%\src" -I"%MSHV%\src\HvDecoderMs" -I"%MSHV%\src\HvMsPlayer\libsound" -I"%MSHV%\src\Hv_Lib_fftw"
set FLAGS=-std=gnu++17 -DQT_CORE_LIB -DQT_NO_DEBUG -D_WIN64_ -c -O2 -w -include "Y:\Claude local\SDRProject\STD\src\ft8\qt6_compat.h"
echo COMPILE decoderpom
"%GPP%" %FLAGS% %INC% -o "%OUT%\decoderpom.o" "%MSHV%\src\HvDecoderMs\decoderpom.cpp"
if errorlevel 1 exit /b 1
echo COMPILE genpom
"%GPP%" %FLAGS% %INC% -o "%OUT%\genpom.o" "%MSHV%\src\HvMsPlayer\libsound\genpom.cpp"
if errorlevel 1 exit /b 1
echo COMPILE pack
"%GPP%" %FLAGS% %INC% -o "%OUT%\pack.o" "%MSHV%\src\HvMsPlayer\libsound\HvPackUnpackMsg\pack_unpack_msg77.cpp"
if errorlevel 1 exit /b 1
echo COMPILE gen_ft8
"%GPP%" %FLAGS% %INC% -o "%OUT%\gen_ft8.o" "%MSHV%\src\HvMsPlayer\libsound\HvGenFt8\gen_ft8.cpp"
if errorlevel 1 exit /b 1
echo COMPILE decoderft8
"%GPP%" %FLAGS% -include "Y:\Claude local\SDRProject\STD\src\ft8\decoderms_ft8.h" %INC% -o "%OUT%\decoderft8.o" "%MSHV%\src\HvDecoderMs\decoderft8.cpp"
if errorlevel 1 exit /b 1
echo COMPILE decoderft8var
"%GPP%" %FLAGS% -include "Y:\Claude local\SDRProject\STD\src\ft8\decoderms_ft8.h" %INC% -o "%OUT%\decoderft8var.o" "%MSHV%\src\HvDecoderMs\decoderft8var.cpp"
if errorlevel 1 exit /b 1
echo OK
exit /b 0
