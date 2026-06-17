#pragma once

#pragma warning(disable:4200) // zero-length array
#pragma warning(disable:4201) // nameless struct/union

#include <ntddk.h>
#include <wdf.h>
#include <ntintsafe.h>
#include <initguid.h>
#include <usb.h>
#include <usbioctl.h>
#include <usbbusif.h>
#include <wdmguid.h>

#include "CppRuntime.h"
#include <GamepadCore/Common.h>
#include <GamepadCore/Ioctl.h>

// forward declare
class EmulationTarget;

// fdo context (bus device)
typedef struct _FDO_DEVICE_DATA {
    LONG InterfaceReferenceCounter;
    LONG NextSessionId;
} FDO_DEVICE_DATA, *PFDO_DEVICE_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FDO_DEVICE_DATA, FdoGetData)

// file object context (one per usermode handle)
typedef struct _FDO_FILE_DATA {
    LONG SessionId;
} FDO_FILE_DATA, *PFDO_FILE_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FDO_FILE_DATA, FileObjectGetData)

// pdo identification for child list
typedef struct _PDO_IDENTIFICATION_DESCRIPTION {
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;
    ULONG SerialNo;
    LONG SessionId;
    EmulationTarget* Target;
} PDO_IDENTIFICATION_DESCRIPTION, *PPDO_IDENTIFICATION_DESCRIPTION;

// pdo device context
typedef struct _EMULATION_TARGET_CONTEXT {
    EmulationTarget* Target;
} EMULATION_TARGET_CONTEXT, *PEMULATION_TARGET_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(EMULATION_TARGET_CONTEXT, PdoGetTarget)

extern "C" {

// driver callbacks
EVT_WDF_DRIVER_DEVICE_ADD           Bus_EvtDeviceAdd;
EVT_WDF_DEVICE_FILE_CREATE          Bus_DeviceFileCreate;
EVT_WDF_FILE_CLOSE                  Bus_FileClose;
EVT_WDF_CHILD_LIST_CREATE_DEVICE    Bus_EvtDeviceListCreatePdo;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_COMPARE Bus_EvtChildListCompare;

// queue callback
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL  Bus_EvtIoDeviceControl;

} // extern "C"

// bus enumeration
NTSTATUS Bus_PlugInDevice(
    _In_ WDFDEVICE Device,
    _In_ PGAMEPAD_PLUGIN_TARGET PlugIn,
    _In_ LONG SessionId
);

NTSTATUS Bus_UnPlugDevice(
    _In_ WDFDEVICE Device,
    _In_ ULONG SerialNo,
    _In_ LONG SessionId
);
