// language: C, file: mouse_filter.c, target: Windows 10/11 x64 kernel, WDK
// Upper filter для mouclass: перехватывает ServiceCallback мышиного класс-драйвера.
// Офсеты вставляются как MOUSE_INPUT_DATA напрямую в стек ввода —
// injected-бит не выставляется, ввод неотличим от физической мыши.
#include "driver.h"

// ObReferenceObjectByName и IoDriverObjectType не экспортируются через ntddk.h,
// но присутствуют в ntoskrnl.exe — объявляем extern явно.
// ntifs.h содержит их, но недоступен в WDK NuGet km-only конфигурации.
NTKERNELAPI NTSTATUS ObReferenceObjectByName(
    PUNICODE_STRING ObjectName,
    ULONG           Attributes,
    PACCESS_STATE   AccessState,
    ACCESS_MASK     DesiredAccess,
    POBJECT_TYPE    ObjectType,
    KPROCESSOR_MODE AccessMode,
    PVOID           ParseContext,
    PVOID*          Object
);
extern POBJECT_TYPE* IoDriverObjectType;

// ─── Структуры mouclass ───────────────────────────────────────────────────
typedef VOID(*PMOUSE_SERVICE_CALLBACK)(
    PDEVICE_OBJECT DevObj,
    PMOUSE_INPUT_DATA InputDataStart,
    PMOUSE_INPUT_DATA InputDataEnd,
    PULONG            InputDataConsumed
);

// Сохраняем оригинальный ServiceCallback при подключении
typedef struct _MOUSE_FILTER_CTX {
    PMOUSE_SERVICE_CALLBACK OriginalCallback;
    PDEVICE_OBJECT          ClassDevObj;
    RC_DEV_CTX*             RcCtx;       // ссылка на основной контекст
} MOUSE_FILTER_CTX;

static MOUSE_FILTER_CTX g_filter = { 0 };

// ─── Наш ServiceCallback (вызывается вместо оригинального) ───────────────
VOID RcMouseServiceCallback(
    PDEVICE_OBJECT DevObj,
    PMOUSE_INPUT_DATA InputDataStart,
    PMOUSE_INPUT_DATA InputDataEnd,
    PULONG InputDataConsumed)
{
    // Сначала передаём оригинальный ввод
    if (g_filter.OriginalCallback) {
        g_filter.OriginalCallback(DevObj, InputDataStart,
                                  InputDataEnd, InputDataConsumed);
    }

    // Теперь применяем накопленные офсеты из кольцевой очереди
    RC_DEV_CTX* ctx = g_filter.RcCtx;
    if (!ctx || !InterlockedCompareExchange(&ctx->active, 1, 1)) return;

    LONG head = ctx->queue.head & RC_QUEUE_MASK;
    LONG tail = ctx->queue.tail & RC_QUEUE_MASK;

    while (tail != head) {
        RC_OFFSET_MSG* msg = &ctx->queue.buf[tail];

        MOUSE_INPUT_DATA synthetic = { 0 };
        synthetic.UnitId    = 0;
        synthetic.Flags     = MOUSE_MOVE_RELATIVE;
        synthetic.LastX     = (LONG)msg->x;
        synthetic.LastY     = (LONG)msg->y;

        ULONG consumed = 0;

        if (g_filter.OriginalCallback) {
            g_filter.OriginalCallback(
                DevObj,
                &synthetic,
                &synthetic + 1,
                &consumed
            );
        }

        tail = (tail + 1) & RC_QUEUE_MASK;
        InterlockedExchange(&ctx->queue.tail, tail);
        InterlockedIncrement((PLONG)&ctx->applied_total);
    }
}

// ─── Установка фильтра через IOCTL_INTERNAL_MOUSE_CONNECT ────────────────
// CONNECT_DATA берём из kbdmou.h (включён через driver.h → kbdmou.h).
// Локальный typedef убран — он дублировал системный и вызывал C4459.
NTSTATUS RcInstallMouseFilter(
    PDEVICE_OBJECT FilterDO,
    PDEVICE_OBJECT LowerDO,
    RC_DEV_CTX*    RcCtx)
{
    CONNECT_DATA connect;
    connect.ClassDeviceObject = FilterDO;
    // C4152: VOID* ← указатель на функцию — явный каст через PVOID
    connect.ClassService      = (PVOID)(ULONG_PTR)RcMouseServiceCallback;

    KEVENT           evt;
    IO_STATUS_BLOCK  iosb;
    KeInitializeEvent(&evt, NotificationEvent, FALSE);

    PIRP irp = IoBuildDeviceIoControlRequest(
        IOCTL_INTERNAL_MOUSE_CONNECT,
        LowerDO,
        &connect, sizeof(connect),
        NULL, 0,
        TRUE, &evt, &iosb
    );
    if (!irp) return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS st = IoCallDriver(LowerDO, irp);
    if (st == STATUS_PENDING)
        KeWaitForSingleObject(&evt, Executive, KernelMode, FALSE, NULL);

    st = iosb.Status;

    if (NT_SUCCESS(st)) {
        g_filter.OriginalCallback = (PMOUSE_SERVICE_CALLBACK)connect.ClassService;
        g_filter.ClassDevObj      = connect.ClassDeviceObject;
        g_filter.RcCtx            = RcCtx;
        RcCtx->lower_mouse = LowerDO;
    }

    return st;
}

// ─── Получить нижнее устройство mouclass ──────────────────────────────────
PDEVICE_OBJECT RcFindMouclassDevice(VOID) {
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Driver\\mouclass");
    PDRIVER_OBJECT drv  = NULL;

    NTSTATUS st = ObReferenceObjectByName(
        &name,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        0,
        *IoDriverObjectType,
        KernelMode,
        NULL,
        (PVOID*)&drv
    );
    if (!NT_SUCCESS(st) || !drv) return NULL;

    PDEVICE_OBJECT dev = drv->DeviceObject;
    if (dev) ObReferenceObject(dev);
    ObDereferenceObject(drv);
    return dev;
}
