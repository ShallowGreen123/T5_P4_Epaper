
@REM 删除文件中多余的文件
@REM Delete redundant files generated in the project

@echo off
setlocal enabledelayedexpansion

REM 定义列表
set "fileList="
call :appendFileList hello_world
call :appendFileList i2c_tools
call :appendFileList bq25896
call :appendFileList bq25896_shutdown
call :appendFileList bq27220

REM 遍历列表并输出文件名
for %%i in (%fileList%) do (
    @REM rmdir /s /q examples\%%i\build
    @REM del examples\%%i\sdkconfig
    @REM echo %%i

    IF EXIST "examples\%%i\build" (
        rmdir /s /q examples\%%i\build
        ECHO delete: examples\%%i\build
    )

    IF EXIST "examples\%%i\managed_components" (
        rmdir /s /q examples\%%i\managed_components
        ECHO delete: examples\%%i\managed_components
    )

    IF EXIST "examples\%%i\sdkconfig" (
        del examples\%%i\sdkconfig
        ECHO delete: examples\%%i\sdkconfig
    )

    IF EXIST "examples\%%i\sdkconfig.old" (
        del examples\%%i\sdkconfig.old
        ECHO delete: examples\%%i\sdkconfig.old
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