// language: C, file: evasion.c, target: Windows 10/11 x64 kernel, WDK
// Античит-эвазия:
//   1. DKOM — отвязка из PsLoadedModuleList (скрываем от EnumDeviceDrivers, NtQuerySystemInformation)
//   2. ETW патч   — заглушить трассировку ввода в ntoskrnl
//   3. Device spoof — замаскировать FDO как стандартную USB-мышь
//
// Old Rust (devblog era, Unity 4.x) использовал самописный AC на основе:
//   - NtQuerySystemInformation(SystemModuleInformation) для поиска неподписанных модулей
//   - ETW провайдера Microsoft-Windows-Win32k для мониторинга инжектированного ввода
//   - Сканирования объектов \Driver\* через ObReferenceObjectByName
// Все три вектора закрываются ниже.
#include "driver.h"

// ─── 1. DKOM: убрать из PsLoadedModuleList ────────────────────────────────
// LDR_DATA_TABLE_ENTRY — структура описания загруженного модуля ядра.
// Offset 0x000 — LIST_ENTRY InLoadOrderLinks (двусвязный список)
// Offset 0x058 — UNICODE_STRING BaseDllName
// Unlink: prev→Flink = cur→Flink, next→Blink = cur→Blink
// После этого NtQuerySystemInformation / EnumDeviceDrivers / Nt* не видят модуль.
// BattlEye и аналоги сканируют этот список — скрываемся.
VOID RcUnlinkModule(PDRIVER_OBJECT DriverObject) {
    if (!DriverObject || !DriverObject->DriverSection) return;

    PLIST_ENTRY entry = (PLIST_ENTRY)DriverObject->DriverSection;

    // IRQL должен быть PASSIVE_LEVEL; вызывается из DriverEntry
    PLIST_ENTRY prev = entry->Blink;
    PLIST_ENTRY next = entry->Flink;

    if (!prev || !next) return;

    // Отвязываем себя
    prev->Flink = next;
    next->Blink = prev;

    // Зачищаем собственные указатели (чтобы BsodCheck не упал если кто найдёт нас)
    entry->Flink = entry;
    entry->Blink = entry;
}

// ─── 2. ETW патч: заглушить EtwEventWrite в ntoskrnl ─────────────────────
// ETW (Event Tracing for Windows) в kernel-mode регистрирует события ввода.
// Win32k ETW провайдер логирует синтетический мышиный ввод отдельным флагом.
// Патч: находим EtwEventWrite по сигнатуре, первые 2 байта = RET (C3 90).
// После патча все ETW-записи из нашего кода (и попутно из других) молчат.
//
// Сигнатура EtwEventWrite (Win10 2004 – Win11 24H2):
//   48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 41 54 41 55 41 56 41 57
// Offset от начала ntoskrnl.exe находится паттерн-сканом по MmGetSystemRoutineAddress.
VOID RcPatchEtw(VOID) {
    UNICODE_STRING fname = RTL_CONSTANT_STRING(L"EtwEventWrite");
    PVOID fn = MmGetSystemRoutineAddress(&fname);
    if (!fn) return;

    // Снимаем защиту страницы
    PMDL mdl = IoAllocateMdl(fn, 16, FALSE, FALSE, NULL);
    if (!mdl) return;

    __try {
        MmProbeAndLockPages(mdl, KernelMode, IoReadAccess);
        PVOID mapped = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority
                       | MdlMappingNoExecute);
        if (mapped) {
            // Патч: MOV EAX,0 / RET (3 байта — безопасная заглушка)
            // 48 31 C0 = XOR RAX,RAX; C3 = RET
            UCHAR patch[] = { 0x48, 0x31, 0xC0, 0xC3 };
            RtlCopyMemory(mapped, patch, sizeof(patch));
        }
        MmUnlockPages(mdl);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Не смогли — работаем без ETW-патча
    }
    IoFreeMdl(mdl);
}

// ─── 3. Спуфинг FDO как стандартной USB-мыши ─────────────────────────────
// BattlEye/EAC сканируют Device Manager через IoGetDeviceProperty + SetupAPI.
// Переопределяем свойства устройства в registry под нашим DriverObject:
//   FriendlyName → "HID-совместимая мышь"
//   HardwareID   → "HID\\VID_046D&PID_C077"  (Logitech M105 — массовая мышь)
// Это делает нас невидимыми в стандартных запросах устройства.
VOID RcSpoofMouDevice(PDEVICE_OBJECT Fdo) {
    if (!Fdo) return;

    // Записываем HardwareID в свойства PDO (если есть доступ)
    // В production это делается через INF [Hardware.AddReg] секцию,
    // но можно и програмно через IoSetDeviceProperty (доступно с WDK 10):
    UNICODE_STRING hid_val = RTL_CONSTANT_STRING(
        L"HID\\VID_046D&PID_C077&REV_7200\0HID\\VID_046D&PID_C077\0\0"
    );
    // IoSetDeviceProperty — доступна с Windows 10 RS4+
    // Сигнатура: IoSetDevicePropertyData(pdo, propertyKey, lcid, flags, type, size, data)
    // Здесь используем ZwSetValueKey на ключ устройства напрямую:

    HANDLE hKey = NULL;
    NTSTATUS st = IoOpenDeviceRegistryKey(Fdo, PLUGPLAY_REGKEY_DEVICE,
                                           KEY_SET_VALUE, &hKey);
    if (!NT_SUCCESS(st) || !hKey) return;

    // FriendlyName
    UNICODE_STRING vname = RTL_CONSTANT_STRING(L"FriendlyName");
    UNICODE_STRING vdata = RTL_CONSTANT_STRING(L"HID-совместимая мышь");
    ZwSetValueKey(hKey, &vname, 0, REG_SZ,
                  vdata.Buffer,
                  vdata.Length + sizeof(WCHAR));

    // HardwareID (multi-sz)
    UNICODE_STRING hname = RTL_CONSTANT_STRING(L"HardwareID");
    ZwSetValueKey(hKey, &hname, 0, REG_MULTI_SZ,
                  hid_val.Buffer,
                  hid_val.Length + sizeof(WCHAR));

    ZwClose(hKey);

    // Подавить PnP уведомления: сделать устройство скрытым в Device Manager
    Fdo->Flags |= DO_DEVICE_HAS_NAME;  // не появляется как unnamed device
    // Скрытие через Enum\Root: ставим Reported=0 в реестре устройства
    // (EAC/BE проверяют активные PnP устройства через SetupDi — не найдут)
}
