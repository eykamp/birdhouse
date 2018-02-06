@echo off

rem Requires WinSCP and Arduino IDE to be installed



:::::
:: Parse command line parameters
:::::


:: From https://stackoverflow.com/questions/3973824/windows-bat-file-optional-argument-parsing

setlocal enableDelayedExpansion

:: Define the option names along with default values, using a <space>
:: delimiter between options. I'm using some generic option names, but 
:: normally each option would have a meaningful name.
::
:: Each option has the format -name:[default]
::
:: The option names are NOT case sensitive.
::
:: Options that have a default value expect the subsequent command line
:: argument to contain the value. If the option is not provided then the
:: option is set to the default. If the default contains spaces, contains
:: special characters, or starts with a colon, then it should be enclosed
:: within double quotes. The default can be undefined by specifying the
:: default as empty quotes "".
:: NOTE - defaults cannot contain * or ? with this solution.
::
:: Options that are specified without any default value are simply flags
:: that are either defined or undefined. All flags start out undefined by
:: default and become defined if the option is supplied.
::
:: The order of the definitions is not important.
::
set "options=-winscpprofile:"" -clean: -noup: -help:"
::-option2:"" -option3:"three word default" -flag1: -flag2:"

:: Set the default option values
for %%O in (%options%) do for /f "tokens=1,* delims=:" %%A in ("%%O") do set "%%A=%%~B"

:loop
:: Validate and store the options, one at a time, using a loop.
:: Options start at arg 3 in this example. Each SHIFT is done starting at
:: the first option so required args are preserved.
::
if not "%~1"=="" (
  set "test=!options:*%~1:=! "
  if "!test!"=="!options! " (
    rem No substitution was made so this is an invalid option.
    rem Error handling goes here.
    rem I will simply echo an error message.
    echo Error: Invalid option %~1
  ) else if "!test:~0,1!"==" " (
    rem Set the flag option using the option name.
    rem The value doesn't matter, it just needs to be defined.
    set "%~1=1"
  ) else (
    rem Set the option value using the option as the name.
    rem and the next arg as the value
    setlocal disableDelayedExpansion
      set "val=%~2"
      call :escapeVal
      setlocal enableDelayedExpansion
      for /f delims^=^ eol^= %%A in ("!val!") do endlocal&endlocal&set "%~1=%%A" !
    shift /1
  )
  shift /1
  goto :loop
)
goto :endArgs

:escapeVal
set "val=%val:^=^^%"
set "val=%val:!=^!%"
exit /b

:endArgs

:: Now all supplied options are stored in variables whose names are the
:: option names. Missing options have the default value, or are undefined if
:: there is no default.
:: The required args are still available in %1 and %2 (and %0 is also preserved)
:: For this example I will simply echo all the option values,
:: assuming any variable starting with - is an option.
::

:: To get the value of a single parameter, just remember to include the `-`
rem echo The value of -winscpprofile is: !-winscpprofile!



:: Validate our command line parameters... only one at the moment!
if not defined -winscpprofile (
    call :help
    exit /b 1
) 

if defined -help (
    call :help
    exit /b 0
)

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
IF NOT EXIST "%WINSCP_PROGRAM_LOCATION%" (
    echo Could not find WinScp -- is it installed?  If so, check the location and modify this file, changing WINSCP_PROGRAM_LOCATION accordingly.
    goto Error
)

IF EXIST %BUILD_TARGET% (
    if defined -clean ( 
        echo Deleting %BUILD_TARGET%
        del %BUILD_TARGET% 
        IF EXIST %BUILD_TARGET% (
            echo Could not delete %BUILD_TARGET%... aborting.
            goto Error
        )
    )
)
IF EXIST %BUILD_TARGET% (
    echo Target %BUILD_TARGET% already exists! ^(use -clean option to overwrite^)
    goto Error
)


rem Build
echo Building %BUILD_TARGET%
"%ARDUINO_BUILD_EXE_LOCATION%" --verify --pref build.path=%BUILD_FOLDER% %SOURCE_FILE%
IF ERRORLEVEL 1 goto Error

if defined -noup (
    del %BUILD_FOLDER%\%SOURCE_FILE%.bin
    echo Successfully built %SOURCE_FILE%!
    goto Success
)

REM Rename file, will fail on overwrite
ren %BUILD_FOLDER%\%SOURCE_FILE%.bin %BUILD_TARGET_FILE%
IF ERRORLEVEL 1 (
    echo Error renaming %BUILD_FOLDER%\%SOURCE_FILE%.bin to %BUILD_TARGET_FILE%
    goto Error
)




SET /p ARE_YOU_SURE=Proceed with upload? [Y/N]
IF /I %ARE_YOU_SURE% NEQ Y GOTO End

echo Uploading %BUILD_TARGET%
"%WINSCP_PROGRAM_LOCATION%" "%-winscpprofile%" /command "cd %REMOTE_DIR%" "put %BUILD_TARGET%" "exit"

:Success
echo Success!
color 2F
pause
color
exit /b

:Error
color CF
pause
color
exit /b 1

:Help
echo Syntax: %~nx0 -winscpprofile=^<WinSCP profile^> [-clean] [-noup] [-help]
exit /b
