@echo off
set CURRENT_DISK=%~d0
set CURRENT_DIR=%~dp0
set ROOT_DIR=%CURRENT_DIR%../..
set BEGIN_DIR=%CURRENT_DIR%
cd %CURRENT_DISK%
cd %ROOT_DIR%

rm -rf out
gn gen out
ninja -C out

cd %ROOT_DIR%