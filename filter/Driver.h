#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbioctl.h>
#include <usbbusif.h>
#include <initguid.h>

#include <GamepadCore/Common.h>
#include <GamepadCore/Ioctl.h>

// filter device context
typedef struct _FILTER_DEVICE_CONTEXT {
    WDFIOTARGET IoTarget;
    BOOLEAN     IsMuted;
    KSPIN_LOCK  ReportLock;
    UCHAR       LastReport[256];
    ULONG       LastReportLength;
    WDFQUEUE    PendingReadQueue;
    USHORT      VendorId;
    USHORT      ProductId;
} FILTER_DEVICE_CONTEXT, *PFILTER_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILTER_DEVICE_CONTEXT, FilterGetContext)

// completion context for tracking intercepted urbs
typedef struct _FILTER_REQUEST_CONTEXT {
    PFILTER_DEVICE_CONTEXT FilterContext;
} FILTER_REQUEST_CONTEXT, *PFILTER_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILTER_REQUEST_CONTEXT, FilterRequestGetContext)

extern "C" {

// driver callbacks
EVT_WDF_DRIVER_DEVICE_ADD           Filter_EvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL Filter_EvtIoInternalDeviceControl;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL         Filter_EvtIoDeviceControl;
EVT_WDF_REQUEST_COMPLETION_ROUTINE          Filter_UsbInterruptCompletion;

} // extern "C"
