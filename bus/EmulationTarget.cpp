#include "EmulationTarget.h"
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

EmulationTarget::EmulationTarget(ULONG Serial, LONG SessionId, USHORT VendorId, USHORT ProductId)
    : _serialNo(Serial)
    , _sessionId(SessionId)
    , _vendorId(VendorId)
    , _productId(ProductId)
    , _targetType(GamepadTypeXbox360)
{
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&_pnpCapabilities);
    WDF_DEVICE_POWER_CAPABILITIES_INIT(&_powerCapabilities);
    KeInitializeEvent(&_pdoBootNotificationEvent, NotificationEvent, FALSE);
}

NTSTATUS EmulationTarget::Prepare(WDFDEVICE ParentDevice)
{
    UNREFERENCED_PARAMETER(ParentDevice);

    // capture owner pid
    _ownerProcessId = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(PsGetCurrentProcessId()));
    return STATUS_SUCCESS;
}

bool EmulationTarget::IsOwnerProcess() const
{
    return _ownerProcessId == static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(PsGetCurrentProcessId()));
}

NTSTATUS EmulationTarget::CreatePdoDevice(WDFDEVICE ParentDevice, PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS status;
    DECLARE_UNICODE_STRING_SIZE(deviceId, MAX_HARDWARE_ID_LENGTH);
    DECLARE_UNICODE_STRING_SIZE(deviceDesc, MAX_HARDWARE_ID_LENGTH);

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);

    // let derived class set hardware/compatible ids
    status = PdoPrepareDevice(DeviceInit, &deviceId, &deviceDesc);
    if (!NT_SUCCESS(status))
        return status;

    // set instance id
    DECLARE_UNICODE_STRING_SIZE(instanceId, 4);
    status = RtlUnicodeStringPrintf(&instanceId, L"%04d", _serialNo);
    if (!NT_SUCCESS(status))
        return status;

    status = WdfPdoInitAssignInstanceID(DeviceInit, &instanceId);
    if (!NT_SUCCESS(status))
        return status;

    // set device description
    status = WdfPdoInitAddDeviceText(DeviceInit, &deviceDesc, &deviceId, 0x0409);
    if (!NT_SUCCESS(status))
        return status;
    WdfPdoInitSetDefaultLocale(DeviceInit, 0x0409);

    // prepare hardware callback
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    // create pdo device
    WDF_OBJECT_ATTRIBUTES pdoAttribs;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pdoAttribs, EMULATION_TARGET_CONTEXT);
    pdoAttribs.EvtCleanupCallback = EvtDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &pdoAttribs, &_pdoDevice);
    if (!NT_SUCCESS(status))
        return status;

    // store self in device context
    PdoGetTarget(_pdoDevice)->Target = this;

    // expose usb device interface so windows loads xusb/hid class drivers
    status = WdfDeviceCreateDeviceInterface(_pdoDevice, &GUID_DEVINTERFACE_USB_DEVICE, nullptr);
    if (!NT_SUCCESS(status))
        return status;

    // set pnp/power capabilities
    WdfDeviceSetPnpCapabilities(_pdoDevice, &_pnpCapabilities);
    WdfDeviceSetPowerCapabilities(_pdoDevice, &_powerCapabilities);

    // default queue for internal device control (URBs from class driver)
    WDF_IO_QUEUE_CONFIG defaultQueueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&defaultQueueConfig, WdfIoQueueDispatchParallel);
    defaultQueueConfig.EvtIoInternalDeviceControl = EvtIoInternalDeviceControl;

    status = WdfIoQueueCreate(_pdoDevice, &defaultQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
        return status;

    // manual queue for pending usb interrupt in requests
    WDF_IO_QUEUE_CONFIG manualConfig;
    WDF_IO_QUEUE_CONFIG_INIT(&manualConfig, WdfIoQueueDispatchManual);

    status = WdfIoQueueCreate(_pdoDevice, &manualConfig, WDF_NO_OBJECT_ATTRIBUTES, &_pendingUsbInRequests);
    if (!NT_SUCCESS(status))
        return status;

    // manual queue for pending notification requests
    status = WdfIoQueueCreate(_pdoDevice, &manualConfig, WDF_NO_OBJECT_ATTRIBUTES, &_pendingNotificationRequests);
    if (!NT_SUCCESS(status))
        return status;

    // manual queue for wait device ready requests
    status = WdfIoQueueCreate(_pdoDevice, &manualConfig, WDF_NO_OBJECT_ATTRIBUTES, &_waitDeviceReadyRequests);
    if (!NT_SUCCESS(status))
        return status;

    // device-specific init
    status = PdoInitContext();
    if (!NT_SUCCESS(status))
        return status;

    return STATUS_SUCCESS;
}

NTSTATUS EmulationTarget::EnqueueWaitDeviceReady(WDFREQUEST Request)
{
    return WdfRequestForwardToIoQueue(Request, _waitDeviceReadyRequests);
}

NTSTATUS EmulationTarget::SubmitReport(PVOID NewReport)
{
    if (!IsOwnerProcess())
        return STATUS_ACCESS_DENIED;

    return SubmitReportImpl(NewReport);
}

NTSTATUS EmulationTarget::EnqueueNotification(WDFREQUEST Request) const
{
    return WdfRequestForwardToIoQueue(Request, _pendingNotificationRequests);
}

bool EmulationTarget::FindByTypeAndSerial(
    WDFDEVICE ParentDevice,
    GAMEPAD_TARGET_TYPE Type,
    ULONG SerialNo,
    EmulationTarget** OutTarget)
{
    WDFCHILDLIST childList = WdfFdoGetDefaultChildList(ParentDevice);
    WDF_CHILD_LIST_ITERATOR iterator;
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator, WdfRetrievePresentChildren);

    WdfChildListBeginIteration(childList, &iterator);

    WDF_CHILD_RETRIEVE_INFO childInfo;
    PDO_IDENTIFICATION_DESCRIPTION description;

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(PDO_IDENTIFICATION_DESCRIPTION));
    WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &description.Header);

    while (NT_SUCCESS(WdfChildListRetrieveNextDevice(childList, &iterator, nullptr, &childInfo)))
    {
        if (childInfo.Status == WdfChildListRetrieveDeviceSuccess)
        {
            if (description.Target &&
                description.Target->GetType() == Type &&
                description.SerialNo == SerialNo)
            {
                *OutTarget = description.Target;
                WdfChildListEndIteration(childList, &iterator);
                return true;
            }
        }

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(PDO_IDENTIFICATION_DESCRIPTION));
        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &description.Header);
    }

    WdfChildListEndIteration(childList, &iterator);
    return false;
}

// wdf callback: prepare hardware on pdo
NTSTATUS EmulationTarget::EvtDevicePrepareHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesRaw,
    WDFCMRESLIST ResourcesTranslated)
{
    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    auto target = PdoGetTarget(Device)->Target;
    if (!target) return STATUS_INVALID_DEVICE_STATE;

    return target->PdoPrepareHardware();
}

// wdf callback: cleanup pdo context (delete c++ object)
VOID EmulationTarget::EvtDeviceContextCleanup(WDFOBJECT Object)
{
    auto target = PdoGetTarget(static_cast<WDFDEVICE>(Object))->Target;
    if (target)
    {
        delete target;
        PdoGetTarget(static_cast<WDFDEVICE>(Object))->Target = nullptr;
    }
}

// the core urb dispatch - this makes the virtual pdo look like a real usb device
VOID EmulationTarget::EvtIoInternalDeviceControl(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t InputBufferLength,
    ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    auto device = WdfIoQueueGetDevice(Queue);
    auto target = PdoGetTarget(device)->Target;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    if (!target)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
        return;
    }

    switch (IoControlCode)
    {
    case IOCTL_INTERNAL_USB_SUBMIT_URB:
    {
        auto irp = WdfRequestWdmGetIrp(Request);
        auto urb = static_cast<PURB>(URB_FROM_IRP(irp));

        switch (urb->UrbHeader.Function)
        {
        case URB_FUNCTION_CONTROL_TRANSFER:
            status = target->UsbControlTransfer(urb);
            break;

        case URB_FUNCTION_CONTROL_TRANSFER_EX:
            status = target->UsbControlTransfer(urb);
            break;

        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
            status = target->UsbBulkOrInterruptTransfer(&urb->UrbBulkOrInterruptTransfer, Request);
            if (status == STATUS_PENDING)
                return; // don't complete - request is queued
            break;

        case URB_FUNCTION_SELECT_CONFIGURATION:
            status = target->UsbSelectConfiguration(urb);
            break;

        case URB_FUNCTION_SELECT_INTERFACE:
            status = target->UsbSelectInterface(urb);
            break;

        case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
        {
            auto descType = urb->UrbControlDescriptorRequest.DescriptorType;

            switch (descType)
            {
            case USB_DEVICE_DESCRIPTOR_TYPE:
            {
                auto desc = static_cast<PUSB_DEVICE_DESCRIPTOR>(urb->UrbControlDescriptorRequest.TransferBuffer);
                status = target->UsbGetDeviceDescriptor(desc);
                break;
            }
            case USB_CONFIGURATION_DESCRIPTOR_TYPE:
            {
                auto length = urb->UrbControlDescriptorRequest.TransferBufferLength;
                auto buffer = static_cast<PUCHAR>(urb->UrbControlDescriptorRequest.TransferBuffer);

                // windows asks for 9 bytes first (just the config header), then the full thing
                auto totalLength = target->_usbConfigDescriptorSize;
                auto transferSize = (length < totalLength) ? length : totalLength;

                target->GetConfigurationDescriptor(buffer, transferSize);
                urb->UrbControlDescriptorRequest.TransferBufferLength = transferSize;
                status = STATUS_SUCCESS;
                break;
            }
            case USB_STRING_DESCRIPTOR_TYPE:
                status = target->UsbGetStringDescriptor(urb);
                break;
            default:
                status = STATUS_NOT_IMPLEMENTED;
                break;
            }
            break;
        }

        case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
            status = target->UsbGetDescriptorFromInterface(urb);
            break;

        case URB_FUNCTION_CLASS_INTERFACE:
            status = target->UsbClassInterface(urb);
            break;

        case URB_FUNCTION_ABORT_PIPE:
            target->AbortPipe();
            status = STATUS_SUCCESS;
            break;

        case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
            status = STATUS_SUCCESS;
            break;

        default:
            status = STATUS_NOT_IMPLEMENTED;
            break;
        }
        break;
    }

    case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
    {
        // report port as enabled and connected
        auto irp = WdfRequestWdmGetIrp(Request);
        auto portStatus = static_cast<PULONG>(irp->AssociatedIrp.SystemBuffer);
        if (portStatus)
            *portStatus = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_INTERNAL_USB_RESET_PORT:
        status = STATUS_SUCCESS;
        break;

    case IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION:
        status = STATUS_SUCCESS;
        break;

    default:
        break;
    }

    WdfRequestComplete(Request, status);
}

// usb bus interface implementations
BOOLEAN USB_BUSIFFN EmulationTarget::UsbIsDeviceHighSpeed(PVOID BusContext)
{
    UNREFERENCED_PARAMETER(BusContext);
    return TRUE;
}

NTSTATUS USB_BUSIFFN EmulationTarget::UsbQueryBusInformation(
    PVOID BusContext, ULONG Level, PVOID Buffer, PULONG BufferLength, PULONG ActualLength)
{
    UNREFERENCED_PARAMETER(BusContext);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(ActualLength);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS USB_BUSIFFN EmulationTarget::UsbSubmitIsoOutUrb(PVOID BusContext, PURB Urb)
{
    UNREFERENCED_PARAMETER(BusContext);
    UNREFERENCED_PARAMETER(Urb);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS USB_BUSIFFN EmulationTarget::UsbQueryBusTime(PVOID BusContext, PULONG CurrentFrame)
{
    UNREFERENCED_PARAMETER(BusContext);
    if (CurrentFrame) *CurrentFrame = 0;
    return STATUS_SUCCESS;
}

VOID USB_BUSIFFN EmulationTarget::UsbGetUSBDIVersion(
    PVOID BusContext, PUSBD_VERSION_INFORMATION VersionInfo, PULONG HcdCapabilities)
{
    UNREFERENCED_PARAMETER(BusContext);
    if (VersionInfo)
    {
        VersionInfo->USBDI_Version = 0x00000600;
        VersionInfo->Supported_USB_Version = 0x0200;
    }
    if (HcdCapabilities) *HcdCapabilities = 0;
}
