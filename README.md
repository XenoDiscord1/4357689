# RecoilComp

Usermode recoil compensation + kernel-mode input filter driver.

## Структура

```
usermode/   — C++ приложение (SendInput / IOCTL к драйверу)
driver/     — WDK kernel driver (mouclass upper filter)
```

## Сборка через GitHub Actions

Push в `main` → Actions собирает два артефакта:
- **RecoilComp-usermode** → `RecoilComp.exe`
- **RecoilDriver-sys** → `recoil.sys` + `install.bat`

## Запуск

1. Скачать оба артефакта из Actions → Artifacts
2. Запустить `install.bat` от администратора (устанавливает драйвер)
3. Запустить `RecoilComp.exe`
4. **INSERT** — toggle компенсации
5. Правый клик по иконке в трее → выбор оружия

## Профили

По умолчанию: AK47, M4A1, AWP  
Кастомные: положи `.json` в папку `profiles/` рядом с `.exe`

```json
{
  "weapon": "AK47",
  "offsets": [
    {"x": 0, "y": 8, "delay": 10},
    {"x": 2, "y": 7, "delay": 10}
  ],
  "calibrated_date": "2024-01-20",
  "spray_pattern": "standard"
}
```

## Требования для локальной сборки

- Visual Studio 2022 с C++ Desktop Development
- Windows Driver Kit (WDK) 10.0.26100+
- CMake 3.20+
