@echo off
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

set DRIVER_NAME=RecoilDrv
echo [*] Останавливаем и удаляем %DRIVER_NAME%...

sc stop   %DRIVER_NAME% >nul 2>&1
timeout /t 2 /nobreak >nul
sc delete %DRIVER_NAME% >nul 2>&1

del /F /Q "%SystemRoot%\System32\drivers\recoil.sys" >nul 2>&1

:: Отключить тестовую подпись если больше не нужна
:: bcdedit /set testsigning off

echo [+] Готово. Перезагрузка рекомендуется.
pause
