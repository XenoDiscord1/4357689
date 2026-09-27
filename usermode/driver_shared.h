// language: C, file: driver_shared.h
// Shared между user-mode и kernel driver — IOCTL интерфейс
#pragma once

#ifndef _KERNEL_MODE
#include <windows.h>
#include <winioctl.h>
#endif

#define RECOIL_DEVICE_NAME   L"\\Device\\RecoilDriver"
#define RECOIL_SYMLINK_NAME  L"\\DosDevices\\RecoilDriver"
#define RECOIL_USER_PATH     L"\\\\.\\RecoilDriver"
#define RECOIL_DEVICE_TYPE   0x8001

#define IOCTL_RC_PUSH_OFFSET CTL_CODE(RECOIL_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_RC_SET_ACTIVE  CTL_CODE(RECOIL_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_RC_GET_STATUS  CTL_CODE(RECOIL_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_READ_DATA)

#pragma pack(push, 1)
typedef struct _RC_OFFSET_MSG {
    unsigned int       offset_id;
    short              x;
    short              y;
    unsigned int       delay_us;
    unsigned long long ts_perfcount;
} RC_OFFSET_MSG;

typedef struct _RC_STATUS {
    unsigned int       queue_depth;
    unsigned int       applied_total;
    unsigned long long last_latency_us;
    int                driver_active;
} RC_STATUS;
#pragma pack(pop)
