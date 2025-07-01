
@REM 删除文件中多余的文件
@REM Delete redundant files generated in the project

@echo off
setlocal enabledelayedexpansion

REM 定义列表
set "fileList="
call :appendFileList hello_world
call :appendFileList i2c_tools


REM 遍历列表并输出文件名
for %%i in (%fileList%) do (
    @REM rmdir /s /q examples\%%i\build
    @REM del examples\%%i\sdkconfig
    @REM echo %%i

    IF EXIST "examples\%%i\build" (
        rmdir /s /q examples\%%i\build
        ECHO delete: examples\%%i\build
    )

    IF EXIST "examples\%%i\sdkconfig" (
        del examples\%%i\sdkconfig
        ECHO delete: examples\%%i\sdkconfig
    )
)

endlocal
pause
goto :eof

:appendFileList
if defined fileList (
    set "fileList=!fileList! %1"
) else (
    set "fileList=%1"
)
goto :eof