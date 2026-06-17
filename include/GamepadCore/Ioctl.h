#pragma once

#include "Common.h"
#include "Reports.h"

// ============================================================
//  bus driver ioctls
// ============================================================

#define FILE_DEVICE_GAMEPAD_BUS 0x2A00

#define IOCTL_GAMEPAD_CHECK_VERSION \
    CTL_CODE(FILE_DEVICE_GAMEPAD_BUS, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GAMEPAD_PLUGIN_TARGET \
    CTL_CODE(FILE_DEVICE_GAMEPAD_BUS, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GAMEPAD_UNPLUG_TARGET \
    CTL_CODE(FILE_DEVICE_GAMEPAD_BUS, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GAMEPAD_WAIT_DEVICE_READY \
    CTL_CODE(FILE_DEVICE_GAMEPAD_BUS, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GAMEPAD_SUBMIT_REPORT \
    CTL_CODE(FILE_DEVICE_GAMEPAD_BUS, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GAMEPAD_REQUEST_NOTIFICATION \
    CTL_CODE(FILE_DEVICE_GAMEPAD_BUS, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GAMEPAD_GET_USER_INDEX \
    CTL_CODE(FILE_DEVICE_GAMEPAD_BUS, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)

// ============================================================
//  filter driver ioctls
// ============================================================

#define FILE_DEVICE_GAMEPAD_FILTER 0x2A01

#define IOCTL_FILTER_GET_DEVICE_INFO \
    CTL_CODE(FILE_DEVICE_GAMEPAD_FILTER, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_FILTER_SET_MUTE \
    CTL_CODE(FILE_DEVICE_GAMEPAD_FILTER, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_FILTER_GET_MUTE \
    CTL_CODE(FILE_DEVICE_GAMEPAD_FILTER, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_FILTER_READ_INPUT \
    CTL_CODE(FILE_DEVICE_GAMEPAD_FILTER, 0x903, METHOD_BUFFERED, FILE_ANY_ACCESS)

// ============================================================
//  bus ioctl structures
// ============================================================

#include <pshpack1.h>

typedef struct _GAMEPAD_CHECK_VERSION {
    ULONG Size;
    ULONG Version;
} GAMEPAD_CHECK_VERSION, *PGAMEPAD_CHECK_VERSION;

typedef struct _GAMEPAD_PLUGIN_TARGET {
    ULONG               Size;
    ULONG               SerialNo;
    GAMEPAD_TARGET_TYPE  TargetType;
    USHORT               VendorId;   // 0 = default
    USHORT               ProductId;  // 0 = default
} GAMEPAD_PLUGIN_TARGET, *PGAMEPAD_PLUGIN_TARGET;

typedef struct _GAMEPAD_UNPLUG_TARGET {
    ULONG Size;
    ULONG SerialNo; // 0 = unplug all owned by session
} GAMEPAD_UNPLUG_TARGET, *PGAMEPAD_UNPLUG_TARGET;

typedef struct _GAMEPAD_WAIT_DEVICE_READY {
    ULONG Size;
    ULONG SerialNo;
} GAMEPAD_WAIT_DEVICE_READY, *PGAMEPAD_WAIT_DEVICE_READY;

typedef struct _GAMEPAD_SUBMIT_REPORT {
    ULONG               Size;
    ULONG               SerialNo;
    GAMEPAD_TARGET_TYPE  TargetType;
    union {
        XUSB_GAMEPAD_REPORT Xusb;
        DS4_GAMEPAD_REPORT  Ds4;
    } Report;
} GAMEPAD_SUBMIT_REPORT, *PGAMEPAD_SUBMIT_REPORT;

typedef struct _GAMEPAD_REQUEST_NOTIFICATION {
    ULONG               Size;
    ULONG               SerialNo;
    GAMEPAD_TARGET_TYPE  TargetType;
} GAMEPAD_REQUEST_NOTIFICATION, *PGAMEPAD_REQUEST_NOTIFICATION;

typedef struct _GAMEPAD_NOTIFICATION {
    ULONG               Size;
    ULONG               SerialNo;
    GAMEPAD_TARGET_TYPE  TargetType;
    union {
        XUSB_OUTPUT_REPORT  Xusb;
        DS4_OUTPUT_DATA     Ds4;
    } Output;
} GAMEPAD_NOTIFICATION, *PGAMEPAD_NOTIFICATION;

typedef struct _GAMEPAD_GET_USER_INDEX {
    ULONG Size;
    ULONG SerialNo;
    ULONG UserIndex; // out
} GAMEPAD_GET_USER_INDEX, *PGAMEPAD_GET_USER_INDEX;

// ============================================================
//  filter ioctl structures
// ============================================================

typedef struct _FILTER_DEVICE_INFO {
    ULONG   Size;
    USHORT  VendorId;
    USHORT  ProductId;
    WCHAR   InstanceId[80];
    BOOLEAN IsMuted;
} FILTER_DEVICE_INFO, *PFILTER_DEVICE_INFO;

typedef struct _FILTER_SET_MUTE {
    ULONG   Size;
    BOOLEAN Mute;
} FILTER_SET_MUTE, *PFILTER_SET_MUTE;

typedef struct _FILTER_GET_MUTE {
    ULONG   Size;
    BOOLEAN IsMuted;
} FILTER_GET_MUTE, *PFILTER_GET_MUTE;

typedef struct _FILTER_INPUT_REPORT {
    ULONG  Size;
    ULONG  ReportLength;
    UCHAR  ReportData[256];
    USHORT VendorId;
    USHORT ProductId;
} FILTER_INPUT_REPORT, *PFILTER_INPUT_REPORT;

#include <poppack.h>
