// language: C, file: driver.h, target: Windows 10/11 x64 kernel, WDK/KMDF
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <kbdmou.h>
#include "../usermode/driver_shared.h"

// Размер lock-free кольцевого буфера — степень 2
#define RC_QUEUE_MASK   (1023u)
#define RC_QUEUE_SIZE   (RC_QUEUE_MASK + 1u)

// Кольцевая очередь MPSC (IOCTL-handler пишет, DPC читает)
typedef struct _RC_QUEUE {
    RC_OFFSET_MSG    buf[RC_QUEUE_SIZE];
    volatile LONG    head;      // write index
    volatile LONG    tail;      // read index (DPC)
} RC_QUEUE;

// Контекст WDF-устройства
typedef struct _RC_DEV_CTX {
    RC_QUEUE          queue;
    LONG              active;           // 1 = применять офсеты
    ULONG             applied_total;
    ULONGLONG         last_latency_us;
    PDEVICE_OBJECT    lower_mouse;      // нижнее устройство mouclass
    KTIMER            timer;
    KDPC              dpc;
} RC_DEV_CTX;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RC_DEV_CTX, RcGetCtx)

// ─── Прототипы ────────────────────────────────────────────────────────────
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD   RcEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL RcEvtIoCtrl;
KDEFERRED_ROUTINE           RcDpc;

// evasion.c
VOID RcUnlinkModule(PDRIVER_OBJECT drv);
VOID RcPatchEtw(VOID);
VOID RcSpoofMouDevice(PDEVICE_OBJECT fdo);
