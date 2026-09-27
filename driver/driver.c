// language: C, file: driver.c, target: Windows 10/11 x64 kernel, WDK/KMDF
// DriverEntry → WDF init → IOCTL handler → DPC таймер применения офсетов
// Компиляция: Visual Studio 2022 + WDK 10.0.26100+
// sc create RecoilDrv type= kernel start= demand binPath= C:\path\recoil.sys
#include "driver.h"

// ─── DriverEntry ──────────────────────────────────────────────────────────
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG cfg;
    WDF_DRIVER_CONFIG_INIT(&cfg, RcEvtDeviceAdd);

    NTSTATUS st = WdfDriverCreate(
        DriverObject, RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &cfg,
        WDF_NO_HANDLE
    );
    if (!NT_SUCCESS(st)) return st;

    // Эвазия: отвязать из PsLoadedModuleList + патч ETW
    RcUnlinkModule(DriverObject);
    RcPatchEtw();

    return STATUS_SUCCESS;
}

// ─── DeviceAdd ────────────────────────────────────────────────────────────
NTSTATUS RcEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, RC_DEV_CTX);

    WDFDEVICE device;
    NTSTATUS st = WdfDeviceCreate(&DeviceInit, &attrs, &device);
    if (!NT_SUCCESS(st)) return st;

    // Символическая ссылка → user-mode видит \\.\RecoilDriver
    UNICODE_STRING link = RTL_CONSTANT_STRING(RECOIL_SYMLINK_NAME);
    st = WdfDeviceCreateSymbolicLink(device, &link);
    if (!NT_SUCCESS(st)) return st;

    // Очередь IOCTL
    WDF_IO_QUEUE_CONFIG qcfg;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchParallel);
    qcfg.EvtIoDeviceControl = RcEvtIoCtrl;

    WDFQUEUE q;
    st = WdfIoQueueCreate(device, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, &q);
    if (!NT_SUCCESS(st)) return st;

    // Контекст + DPC таймер (интервал 1ms)
    RC_DEV_CTX* ctx = RcGetCtx(device);
    RtlZeroMemory(ctx, sizeof(*ctx));
    InterlockedExchange(&ctx->active, 1);

    KeInitializeTimer(&ctx->timer);
    KeInitializeDpc(&ctx->dpc, RcDpc, ctx);

    LARGE_INTEGER interval;
    interval.QuadPart = -10000LL; // 1ms в 100ns тиках
    KeSetTimerEx(&ctx->timer, interval, 1, &ctx->dpc);

    // Спуфинг: маскируем как обычный мышиный фильтр
    RcSpoofMouDevice(WdfDeviceWdmGetDeviceObject(device));

    return STATUS_SUCCESS;
}

// ─── IOCTL handler ────────────────────────────────────────────────────────
VOID RcEvtIoCtrl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputLen,
    _In_ size_t     InputLen,
    _In_ ULONG      IoControlCode)
{
    UNREFERENCED_PARAMETER(OutputLen);
    UNREFERENCED_PARAMETER(InputLen);

    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    RC_DEV_CTX* ctx  = RcGetCtx(device);
    NTSTATUS  st     = STATUS_SUCCESS;

    switch (IoControlCode) {

    // ── Добавить офсет в кольцевую очередь ─────────────────────────────
    case IOCTL_RC_PUSH_OFFSET: {
        RC_OFFSET_MSG* in = NULL;
        st = WdfRequestRetrieveInputBuffer(
            Request, sizeof(RC_OFFSET_MSG), (PVOID*)&in, NULL);
        if (!NT_SUCCESS(st)) break;

        // Lock-free MPSC: атомарный increment head, пишем в слот
        LONG idx = InterlockedIncrement(&ctx->queue.head) & RC_QUEUE_MASK;
        RtlCopyMemory(&ctx->queue.buf[idx], in, sizeof(RC_OFFSET_MSG));
        break;
    }

    // ── Включить / выключить ─────────────────────────────────────────────
    case IOCTL_RC_SET_ACTIVE: {
        int* flag = NULL;
        st = WdfRequestRetrieveInputBuffer(
            Request, sizeof(int), (PVOID*)&flag, NULL);
        if (!NT_SUCCESS(st)) break;
        InterlockedExchange(&ctx->active, *flag ? 1 : 0);
        break;
    }

    // ── Статус ────────────────────────────────────────────────────────────
    case IOCTL_RC_GET_STATUS: {
        RC_STATUS* out = NULL;
        st = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(RC_STATUS), (PVOID*)&out, NULL);
        if (!NT_SUCCESS(st)) break;

        out->queue_depth    = (ULONG)((ctx->queue.head - ctx->queue.tail)
                                      & RC_QUEUE_MASK);
        out->applied_total  = ctx->applied_total;
        out->last_latency_us = ctx->last_latency_us;
        out->driver_active  = (int)ctx->active;

        WdfRequestSetInformation(Request, sizeof(RC_STATUS));
        break;
    }

    default:
        st = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestComplete(Request, st);
}

// ─── DPC: применить офсеты из очереди через mouse class ──────────────────
VOID RcDpc(
    _In_ PKDPC   Dpc,
    _In_ PVOID   Context,
    _In_ PVOID   Arg1,
    _In_ PVOID   Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    RC_DEV_CTX* ctx = (RC_DEV_CTX*)Context;
    if (!ctx || !InterlockedCompareExchange(&ctx->active, 1, 1)) return;

    // Читаем очередь, применяем накопившиеся офсеты
    LONG head = ctx->queue.head & RC_QUEUE_MASK;
    LONG tail = ctx->queue.tail & RC_QUEUE_MASK;

    while (tail != head) {
        RC_OFFSET_MSG* msg = &ctx->queue.buf[tail];

        // Инжект идёт через RcMouseServiceCallback в mouse_filter.c —
        // DPC только дренирует очередь, фактический вызов там.
        UNREFERENCED_PARAMETER(msg);

        // Обновляем tail
        tail = (tail + 1) & RC_QUEUE_MASK;
        InterlockedExchange(&ctx->queue.tail, tail);
        InterlockedIncrement((PLONG)&ctx->applied_total);
    }
}
