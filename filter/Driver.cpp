#include "Driver.h"

extern "C" DRIVER_INITIALIZE DriverEntry;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

extern "C"
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, Filter_EvtDeviceAdd);

    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

extern "C"
NTSTATUS Filter_EvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    NTSTATUS status;

    // mark as filter
    WdfFdoInitSetFilter(DeviceInit);

    // create device
    WDF_OBJECT_ATTRIBUTES deviceAttribs;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttribs, FILTER_DEVICE_CONTEXT);

    WDFDEVICE device;
    status = WdfDeviceCreate(&DeviceInit, &deviceAttribs, &device);
    if (!NT_SUCCESS(status))
        return status;

    auto ctx = FilterGetContext(device);
    ctx->IsMuted = FALSE;
    ctx->LastReportLength = 0;
    KeInitializeSpinLock(&ctx->ReportLock);
    ctx->IoTarget = WdfDeviceGetIoTarget(device);

    // default queue: intercepts internal device control (URBs from function driver)
    WDF_IO_QUEUE_CONFIG defaultQueueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&defaultQueueConfig, WdfIoQueueDispatchParallel);
    defaultQueueConfig.EvtIoInternalDeviceControl = Filter_EvtIoInternalDeviceControl;
    defaultQueueConfig.EvtIoDeviceControl = Filter_EvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &defaultQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
        return status;

    // manual queue for pending IOCTL_FILTER_READ_INPUT requests
    WDF_IO_QUEUE_CONFIG manualConfig;
    WDF_IO_QUEUE_CONFIG_INIT(&manualConfig, WdfIoQueueDispatchManual);

    status = WdfIoQueueCreate(device, &manualConfig, WDF_NO_OBJECT_ATTRIBUTES, &ctx->PendingReadQueue);
    if (!NT_SUCCESS(status))
        return status;

    // expose device interface for usermode sideband control
    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_GAMEPADFILTER, nullptr);
    if (!NT_SUCCESS(status))
        return status;

    return STATUS_SUCCESS;
}

// intercept internal device control (URBs flowing through the stack)
extern "C"
VOID Filter_EvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    auto device = WdfIoQueueGetDevice(Queue);
    auto ctx = FilterGetContext(device);

    if (IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB)
    {
        auto irp = WdfRequestWdmGetIrp(Request);
        auto urb = static_cast<PURB>(URB_FROM_IRP(irp));

        if (urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER)
        {
            auto transfer = &urb->UrbBulkOrInterruptTransfer;

            // only intercept incoming (device-to-host) interrupt transfers
            if (transfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
            {
                // set completion routine to intercept the data
                WDF_OBJECT_ATTRIBUTES reqAttribs;
                WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&reqAttribs, FILTER_REQUEST_CONTEXT);
                WdfObjectAllocateContext(Request, &reqAttribs, nullptr);

                auto reqCtx = FilterRequestGetContext(Request);
                reqCtx->FilterContext = ctx;

                WdfRequestFormatRequestUsingCurrentType(Request);
                WdfRequestSetCompletionRoutine(Request, Filter_UsbInterruptCompletion, nullptr);

                if (WdfRequestSend(Request, ctx->IoTarget, WDF_NO_SEND_OPTIONS))
                    return;

                WdfRequestComplete(Request, WdfRequestGetStatus(Request));
                return;
            }
        }
    }

    // pass through everything else
    WdfRequestFormatRequestUsingCurrentType(Request);

    WDF_REQUEST_SEND_OPTIONS sendOptions;
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    if (!WdfRequestSend(Request, ctx->IoTarget, &sendOptions))
        WdfRequestComplete(Request, WdfRequestGetStatus(Request));
}

// completion routine: fires after real hardware filled the transfer buffer
extern "C"
VOID Filter_UsbInterruptCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    _In_ WDFCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Context);

    auto status = CompletionParams->IoStatus.Status;
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    auto reqCtx = FilterRequestGetContext(Request);
    auto ctx = reqCtx->FilterContext;

    auto irp = WdfRequestWdmGetIrp(Request);
    auto urb = static_cast<PURB>(URB_FROM_IRP(irp));
    auto transfer = &urb->UrbBulkOrInterruptTransfer;

    if (transfer->TransferBuffer && transfer->TransferBufferLength > 0)
    {
        KIRQL oldIrql;
        KeAcquireSpinLock(&ctx->ReportLock, &oldIrql);

        // cache the intercepted report
        ULONG copyLen = min(transfer->TransferBufferLength, (ULONG)sizeof(ctx->LastReport));
        RtlCopyMemory(ctx->LastReport, transfer->TransferBuffer, copyLen);
        ctx->LastReportLength = copyLen;

        KeReleaseSpinLock(&ctx->ReportLock, oldIrql);

        // complete pending usermode read requests
        WDFREQUEST readReq;
        while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(ctx->PendingReadQueue, &readReq)))
        {
            PFILTER_INPUT_REPORT output = nullptr;
            NTSTATUS readStatus = WdfRequestRetrieveOutputBuffer(readReq, sizeof(FILTER_INPUT_REPORT),
                reinterpret_cast<PVOID*>(&output), nullptr);
            if (NT_SUCCESS(readStatus))
            {
                output->Size = sizeof(FILTER_INPUT_REPORT);
                output->ReportLength = copyLen;
                RtlCopyMemory(output->ReportData, ctx->LastReport, copyLen);
                output->VendorId = ctx->VendorId;
                output->ProductId = ctx->ProductId;
                WdfRequestCompleteWithInformation(readReq, STATUS_SUCCESS, sizeof(FILTER_INPUT_REPORT));
            }
            else
            {
                WdfRequestComplete(readReq, readStatus);
            }
        }

        // if muted, zero out the transfer buffer so function driver sees neutral input
        if (ctx->IsMuted)
        {
            RtlZeroMemory(transfer->TransferBuffer, transfer->TransferBufferLength);
        }
    }

    WdfRequestComplete(Request, status);
}

// sideband ioctls from usermode
extern "C"
VOID Filter_EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    auto device = WdfIoQueueGetDevice(Queue);
    auto ctx = FilterGetContext(device);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    size_t bytesReturned = 0;

    switch (IoControlCode)
    {
    case IOCTL_FILTER_SET_MUTE:
    {
        PFILTER_SET_MUTE pMute = nullptr;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(FILTER_SET_MUTE),
            reinterpret_cast<PVOID*>(&pMute), nullptr);
        if (NT_SUCCESS(status))
            InterlockedExchange8(reinterpret_cast<CHAR*>(&ctx->IsMuted), pMute->Mute ? TRUE : FALSE);
        break;
    }

    case IOCTL_FILTER_GET_MUTE:
    {
        PFILTER_GET_MUTE pMute = nullptr;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(FILTER_GET_MUTE),
            reinterpret_cast<PVOID*>(&pMute), nullptr);
        if (NT_SUCCESS(status))
        {
            pMute->Size = sizeof(FILTER_GET_MUTE);
            pMute->IsMuted = ctx->IsMuted;
            bytesReturned = sizeof(FILTER_GET_MUTE);
        }
        break;
    }

    case IOCTL_FILTER_GET_DEVICE_INFO:
    {
        PFILTER_DEVICE_INFO pInfo = nullptr;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(FILTER_DEVICE_INFO),
            reinterpret_cast<PVOID*>(&pInfo), nullptr);
        if (NT_SUCCESS(status))
        {
            pInfo->Size = sizeof(FILTER_DEVICE_INFO);
            pInfo->VendorId = ctx->VendorId;
            pInfo->ProductId = ctx->ProductId;
            pInfo->IsMuted = ctx->IsMuted;
            bytesReturned = sizeof(FILTER_DEVICE_INFO);
        }
        break;
    }

    case IOCTL_FILTER_READ_INPUT:
    {
        // queue this request - it gets completed when input arrives
        status = WdfRequestForwardToIoQueue(Request, ctx->PendingReadQueue);
        if (NT_SUCCESS(status))
            return; // pending
        break;
    }

    default:
    {
        // pass through unknown ioctls to next driver
        WdfRequestFormatRequestUsingCurrentType(Request);
        WDF_REQUEST_SEND_OPTIONS sendOptions;
        WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
        if (!WdfRequestSend(Request, ctx->IoTarget, &sendOptions))
            WdfRequestComplete(Request, WdfRequestGetStatus(Request));
        return;
    }
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
