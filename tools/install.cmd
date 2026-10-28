@echo off
chcp 65001 >nul
rem Регистрация оснастки «Редактор INF» и создание консоли inf_editor.msc.
rem Запускать от имени администратора. Параметр — путь к inf_snapin.dll
rem (по умолчанию build\Release\inf_snapin.dll).
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "DLL=%~1"
if "%DLL%"=="" set "DLL=%ROOT%\build\Release\inf_snapin.dll"

for %%I in ("%DLL%") do set "DLL=%%~fI"
if not exist "%DLL%" (
    echo Не найден %DLL%
    exit /b 1
)

regsvr32 /s "%DLL%"
if errorlevel 1 (
    echo Не удалось зарегистрировать %DLL%. Требуются права администратора.
    exit /b 1
)
echo Оснастка зарегистрирована: %DLL%

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_msc.ps1" -Out "%ROOT%\inf_editor.msc"
if errorlevel 1 (
    echo Не удалось создать консоль %ROOT%\inf_editor.msc.
    exit /b 1
)
echo Создана консоль %ROOT%\inf_editor.msc

rem запуск по имени из «Пуск → Выполнить»: inf_editor.msc
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\App Paths\inf_editor.msc" /ve /d "%ROOT%\inf_editor.msc" /f >nul
if errorlevel 1 (
    echo Не удалось зарегистрировать запуск inf_editor.msc по имени.
    exit /b 1
)
echo Консоль запускается командой inf_editor.msc из окна «Выполнить»
