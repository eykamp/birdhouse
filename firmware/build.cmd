@echo off

rem Requires WinSCP and Arduino IDE to be installed

rem Validate our command line parameters... only one at the moment!
if "%~1"=="" (
    echo Syntax: %~nx0 ^<WinSCP profile^>
    exit /b 
) 


setlocal
rem Important: Use the .com version here, not the .exe
SET WINSCP_PROGRAM_LOCATION=c:\Program Files (x86)\WinSCP\WinSCP.com
SET ARDUINO_BUILD_EXE_LOCATION=c:\Program Files (x86)\Arduino\arduino_debug.exe

SET SOURCE_NAME=firmware
SET SOURCE_FILE=%SOURCE_NAME%.ino



SET WINSCP_PROFILE=%1

rem Extract our version from the source, store it in VERSION_TOKEN_WITH_QUOTES
FOR /F "tokens=3 USEBACKQ" %%F IN (`findstr /B /C:"#define FIRMWARE_VERSION" %SOURCE_FILE%`) DO (
    set VERSION_TOKEN_WITH_QUOTES=%%F
)
rem Strip of leading and trailing quotes
SET VERSION=%VERSION_TOKEN_WITH_QUOTES:~1,-1%

SET BUILD_FOLDER=C:\Temp\BirdhouseFirmwareBuildFolder
SET REMOTE_DIR=/tmp
SET BUILD_TARGET_FILE=%SOURCE_NAME%_%VERSION%.bin
SET BUILD_TARGET=%BUILD_FOLDER%\%BUILD_TARGET_FILE%


rem Verify things are ready
IF NOT EXIST "%WINSCP_PROGRAM_LOCATION%" goto WinscpNotFound

IF EXIST %BUILD_TARGET% goto CollisionError

rem Build
echo Building %BUILD_TARGET%
"%ARDUINO_BUILD_EXE_LOCATION%" --verify --pref build.path=%BUILD_FOLDER% %SOURCE_FILE%
IF ERRORLEVEL 1 goto BuildError

REM Rename file, will fail on overwrite
ren %BUILD_FOLDER%\%SOURCE_FILE%.bin %BUILD_TARGET_FILE%
IF ERRORLEVEL 1 goto CollisionError


SET /p ARE_YOU_SURE=Proceed with upload? [Y/N]
IF /I %ARE_YOU_SURE% NEQ Y GOTO End

echo Uploading %BUILD_TARGET%
"%WINSCP_PROGRAM_LOCATION%" "%WINSCP_PROFILE%" /command "cd %REMOTE_DIR%" "put %BUILD_TARGET%" "exit"

echo Success!
GOTO End

:WinscpNotFound
echo Could not find WinScp -- is it installed?  If so, check the location and modify this file, changing WINSCP_PROGRAM_LOCATION accordingly.
GOTO End

:CollisionError
echo Target %BUILD_TARGET% already exists!
GOTO End

:BuildError
echo Build Error!
GOTO End

:End
