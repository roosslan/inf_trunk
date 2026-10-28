@echo off
chcp 65001 >nul
rem Отмена регистрации оснастки «Редактор INF». Запускать от имени администратора.
setlocal
set "DLL=%~1"
if "%DLL%"=="" set "DLL=%~dp0..\build\Release\inf_snapin.dll"
reg delete "HKLM\Software\Microsoft\Windows\CurrentVersion\App Paths\inf_editor.msc" /f >nul 2>nul
regsvr32 /s /u "%DLL%"
if errorlevel 1 (
    echo Не удалось отменить регистрацию %DLL%.
    exit /b 1
)
echo Регистрация оснастки отменена.
