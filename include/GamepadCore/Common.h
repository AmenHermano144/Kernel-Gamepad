#pragma once

#ifdef _KERNEL_MODE
#include <ntddk.h>
#include <wdf.h>
#else
#include <Windows.h>
#include <initguid.h>
#endif

#define GAMEPADCORE_VERSION 0x0001

// bus driver device interface
// {A6E7B530-F2A0-4D79-B867-7B57B2E33A8A}
DEFINE_GUID(GUID_DEVINTERFACE_GAMEPADBUS,
    0xa6e7b530, 0xf2a0, 0x4d79, 0xb8, 0x67, 0x7b, 0x57, 0xb2, 0xe3, 0x3a, 0x8a);

// filter driver device interface
// {C4D1E8B1-3A92-4F70-A5B3-8E1D6C9F0B2D}
DEFINE_GUID(GUID_DEVINTERFACE_GAMEPADFILTER,
    0xc4d1e8b1, 0x3a92, 0x4f70, 0xa5, 0xb3, 0x8e, 0x1d, 0x6c, 0x9f, 0x0b, 0x2d);

typedef enum _GAMEPAD_TARGET_TYPE {
    GamepadTypeXbox360 = 0,
    GamepadTypeDS4     = 1,
} GAMEPAD_TARGET_TYPE;

#define GAMEPAD_POOL_TAG 'GpCr'
