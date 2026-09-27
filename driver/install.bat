@echo off
setlocal EnableDelayedExpansion
title RecoilDriver Installer

:: ── Проверка прав администратора ────────────────────────────────────────
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Нужны права администратора. Перезапуск с UAC...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

:: ── Проверка версии Windows (только Win10/11) ────────────────────────────
for /f "tokens=4-5 delims=. " %%i in ('ver') do set VER=%%i.%%j
echo [*] Windows версия: %VER%
if "%VER%" lss "10.0" (
    echo [!] Требуется Windows 10 или 11.
    pause & exit /b 1
)

:: ── Архитектура ──────────────────────────────────────────────────────────
if "%PROCESSOR_ARCHITECTURE%" neq "AMD64" (
    echo [!] Требуется 64-битная система.
    pause & exit /b 1
)

set DRIVER_NAME=RecoilDrv
set DRIVER_FILE=%~dp0recoil.sys
set DRIVER_DEST=%SystemRoot%\System32\drivers\recoil.sys

:: ── Включить тестовую подпись (если нет EV сертификата) ──────────────────
:: В production — подписать через EV + attestation signing на Microsoft Hardware Portal
echo [*] Включаем тестовую подпись (требует перезагрузку один раз)...
bcdedit /set testsigning on >nul 2>&1

:: ── Остановить и удалить предыдущий экземпляр ────────────────────────────
sc query %DRIVER_NAME% >nul 2>&1
if %errorlevel% equ 0 (
    echo [*] Останавливаем предыдущий драйвер...
    sc stop  %DRIVER_NAME% >nul 2>&1
    timeout /t 1 /nobreak >nul
    sc delete %DRIVER_NAME% >nul 2>&1
    timeout /t 1 /nobreak >nul
)

:: ── Копируем .sys ─────────────────────────────────────────────────────────
if not exist "%DRIVER_FILE%" (
    echo [!] Файл recoil.sys не найден рядом с install.bat
    pause & exit /b 1
)
echo [*] Копируем recoil.sys → %DRIVER_DEST%
copy /Y "%DRIVER_FILE%" "%DRIVER_DEST%" >nul
if %errorlevel% neq 0 (
    echo [!] Не удалось скопировать файл.
    pause & exit /b 1
)

:: ── Верификация SHA256 хэша (замени на реальный хэш своей сборки) ─────────
:: for /f %%H in ('certutil -hashfile "%DRIVER_DEST%" SHA256 ^| find /v ":"') do set HASH=%%H
:: if "!HASH!" neq "ТВОЙ_SHA256_ХЭШ_ЗДЕСЬ" (echo [!] Хэш не совпадает! & exit /b 1)

:: ── Регистрируем сервис ────────────────────────────────────────────────────
echo [*] Регистрируем kernel service...
sc create %DRIVER_NAME% ^
    type= kernel ^
    start= demand ^
    error= normal ^
    binPath= "%DRIVER_DEST%" ^
    displayname= "HID Upper Filter Service" ^
    group= "Pointer Class"

if %errorlevel% neq 0 (
    echo [!] sc create завершился с ошибкой %errorlevel%
    pause & exit /b 1
)

:: ── Описание сервиса (маскировка под системный компонент) ─────────────────
sc description %DRIVER_NAME% "Manages upper filter for HID-compatible pointing devices."

:: ── Запуск ────────────────────────────────────────────────────────────────
echo [*] Запускаем драйвер...
sc start %DRIVER_NAME%
if %errorlevel% neq 0 (
    echo [!] sc start вернул %errorlevel%. Проверьте Event Viewer ^> System.
    pause & exit /b 1
)

echo.
echo [+] RecoilDriver успешно установлен и запущен.
echo [+] User-mode app: запусти RecoilComp.exe — INSERT для toggle.
echo.
pause
