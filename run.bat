@echo off
REM ---------------------------------------------------------------------------
REM 启动 ComicReader（Qt 6.9.1 来自 E:\vcpkg-export）
REM 需要 Qt DLL、QML 模块与平台插件的运行时路径。
REM ---------------------------------------------------------------------------
setlocal

set "QTDIR=E:\vcpkg-export\installed\x64-windows"

if not exist "%QTDIR%" (
    echo [ERROR] 未找到 Qt 目录: %QTDIR%
    echo 请修改本脚本中的 QTDIR，或安装 Qt 到默认 vcpkg 目录。
    pause
    exit /b 1
)

REM Qt DLL（Qt6Core.dll 等）
set "PATH=%QTDIR%\bin;%QTDIR%\tools\Qt6\bin;%PATH%"
REM QML 模块（QtQuick / QtQml / QtQuick.Controls ...）
set "QML_IMPORT_PATH=%QTDIR%\Qt6\qml"
REM 平台插件（qwindows.dll 等）
set "QT_PLUGIN_PATH=%QTDIR%\Qt6\plugins"

if not exist "build\ComicReader.exe" (
    echo [ERROR] 未找到 build\ComicReader.exe，请先执行 CMake 配置与编译。
    pause
    exit /b 1
)

start "" "build\ComicReader.exe"
endlocal
