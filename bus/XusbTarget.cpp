#include "XusbTarget.h"
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

XusbTarget::XusbTarget(ULONG Serial, LONG SessionId, USHORT VendorId, USHORT ProductId)
    : EmulationTarget(Serial, SessionId, VendorId, ProductId)
{
    _targetType = GamepadTypeXbox360;
    _usbConfigDescriptorSize = XUSB_DESCRIPTOR_SIZE;

    _pnpCapabilities.Removable = WdfTrue;
    _pnpCapabilities.SurpriseRemovalOK = WdfTrue;
    _pnpCapabilities.UniqueID = WdfTrue;

    _powerCapabilities.DeviceState[PowerSystemWorking] = PowerDeviceD0;
    _powerCapabilities.DeviceState[PowerSystemSleeping1] = PowerDeviceD2;
    _powerCapabilities.DeviceState[PowerSystemSleeping2] = PowerDeviceD2;
    _powerCapabilities.DeviceState[PowerSystemSleeping3] = PowerDeviceD2;
    _powerCapabilities.DeviceState[PowerSystemHibernate] = PowerDeviceD2;
    _powerCapabilities.DeviceState[PowerSystemShutdown] = PowerDeviceD3;
    _powerCapabilities.DeviceD1 = WdfTrue;
    _powerCapabilities.DeviceD2 = WdfTrue;
    _powerCapabilities.WakeFromD0 = WdfTrue;
    _powerCapabilities.WakeFromD1 = WdfTrue;
    _powerCapabilities.WakeFromD2 = WdfTrue;

    RtlZeroMemory(&_packet, sizeof(XUSB_INTERRUPT_IN_PACKET));
    _packet.Size = 0x14;
}

NTSTATUS XusbTarget::PdoPrepareDevice(PWDFDEVICE_INIT DeviceInit, PUNICODE_STRING DeviceId, PUNICODE_STRING DeviceDesc)
{
    NTSTATUS status;
    DECLARE_UNICODE_STRING_SIZE(buffer, MAX_HARDWARE_ID_LENGTH);

    status = RtlUnicodeStringInit(DeviceDesc, L"Virtual Xbox 360 Controller");
    if (!NT_SUCCESS(status)) return status;

    RtlUnicodeStringPrintf(&buffer, L"USB\\VID_%04X&PID_%04X", _vendorId, _productId);
    RtlUnicodeStringCopy(DeviceId, &buffer);

    status = WdfPdoInitAddHardwareID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;

    RtlUnicodeStringInit(&buffer, L"USB\\MS_COMP_XUSB10");
    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;

    RtlUnicodeStringInit(&buffer, L"USB\\Class_FF&SubClass_5D&Prot_01");
    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;

    RtlUnicodeStringInit(&buffer, L"USB\\Class_FF&SubClass_5D");
    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;

    RtlUnicodeStringInit(&buffer, L"USB\\Class_FF");
    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;

    return STATUS_SUCCESS;
}

NTSTATUS XusbTarget::PdoPrepareHardware()
{
    USB_BUS_INTERFACE_USBDI_V1 usbInterface;
    usbInterface.Size = sizeof(USB_BUS_INTERFACE_USBDI_V1);
    usbInterface.Version = USB_BUSIF_USBDI_VERSION_1;
    usbInterface.BusContext = static_cast<PVOID>(_pdoDevice);
    usbInterface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
    usbInterface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
    usbInterface.SubmitIsoOutUrb = UsbSubmitIsoOutUrb;
    usbInterface.GetUSBDIVersion = UsbGetUSBDIVersion;
    usbInterface.QueryBusTime = UsbQueryBusTime;
    usbInterface.QueryBusInformation = UsbQueryBusInformation;
    usbInterface.IsDeviceHighSpeed = UsbIsDeviceHighSpeed;

    WDF_QUERY_INTERFACE_CONFIG ifaceCfg;
    WDF_QUERY_INTERFACE_CONFIG_INIT(&ifaceCfg, reinterpret_cast<PINTERFACE>(&usbInterface),
        &USB_BUS_INTERFACE_USBDI_GUID, nullptr);

    return WdfDeviceAddQueryInterface(_pdoDevice, &ifaceCfg);
}

NTSTATUS XusbTarget::PdoInitContext()
{
    WDF_OBJECT_ATTRIBUTES attribs;
    WDF_OBJECT_ATTRIBUTES_INIT(&attribs);
    attribs.ParentObject = _pdoDevice;

    PUCHAR blobBuffer;
    NTSTATUS status = WdfMemoryCreate(&attribs, NonPagedPoolNx, GAMEPAD_POOL_TAG,
        XUSB_BLOB_STORAGE_SIZE, &_interruptBlobStorage, reinterpret_cast<PVOID*>(&blobBuffer));
    if (!NT_SUCCESS(status)) return status;

    // init blob data (6 stages + capabilities + xenon magic)
    UCHAR blobData[] = {
        0x01, 0x03, 0x0E,       // stage 0
        0x02, 0x03, 0x00,       // stage 1
        0x03, 0x03, 0x03,       // stage 2
        0x08, 0x03, 0x00,       // stage 3
        // stage 4 (20 bytes - default interrupt in packet)
        0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0xe4, 0xf2,
        0xb3, 0xf8, 0x49, 0xf3, 0xb0, 0xfc, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x03, 0x03,       // stage 5
        0x05, 0x03, 0x00,       // capabilities
        0x31, 0x3F, 0xCF, 0xDC  // xenon magic
    };
    RtlCopyMemory(blobBuffer, blobData, sizeof(blobData));

    // holding queue for control pipe requests
    WDF_IO_QUEUE_CONFIG holdingConfig;
    WDF_IO_QUEUE_CONFIG_INIT(&holdingConfig, WdfIoQueueDispatchManual);
    status = WdfIoQueueCreate(_pdoDevice, &holdingConfig, WDF_NO_OBJECT_ATTRIBUTES, &_holdingUsbInRequests);

    return status;
}

VOID XusbTarget::GetConfigurationDescriptor(PUCHAR Buffer, ULONG Length)
{
    UCHAR descriptorData[XUSB_DESCRIPTOR_SIZE] = {
        0x09, 0x02, 0x99, 0x00, 0x04, 0x01, 0x00, 0xA0, 0xFA,
        // interface 0
        0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
        0x11, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, 0x01, 0x08, 0x00, 0x00,
        0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x04,
        0x07, 0x05, 0x01, 0x03, 0x20, 0x00, 0x08,
        // interface 1
        0x09, 0x04, 0x01, 0x00, 0x04, 0xFF, 0x5D, 0x03, 0x00,
        0x1B, 0x21, 0x00, 0x01, 0x01, 0x01, 0x82, 0x40, 0x01,
        0x02, 0x20, 0x16, 0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x07, 0x05, 0x82, 0x03, 0x20, 0x00, 0x02,
        0x07, 0x05, 0x02, 0x03, 0x20, 0x00, 0x04,
        0x07, 0x05, 0x83, 0x03, 0x20, 0x00, 0x40,
        0x07, 0x05, 0x03, 0x03, 0x20, 0x00, 0x10,
        // interface 2
        0x09, 0x04, 0x02, 0x00, 0x01, 0xFF, 0x5D, 0x02, 0x00,
        0x09, 0x21, 0x00, 0x01, 0x01, 0x22, 0x84, 0x07, 0x00,
        0x07, 0x05, 0x84, 0x03, 0x20, 0x00, 0x10,
        // interface 3
        0x09, 0x04, 0x03, 0x00, 0x00, 0xFF, 0xFD, 0x13, 0x04,
        0x06, 0x41, 0x00, 0x01, 0x01, 0x03
    };

    RtlCopyMemory(Buffer, descriptorData, min(Length, (ULONG)XUSB_DESCRIPTOR_SIZE));
}

NTSTATUS XusbTarget::UsbGetDeviceDescriptor(PUSB_DEVICE_DESCRIPTOR Descriptor)
{
    Descriptor->bLength = 0x12;
    Descriptor->bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
    Descriptor->bcdUSB = 0x0200;
    Descriptor->bDeviceClass = 0xFF;
    Descriptor->bDeviceSubClass = 0xFF;
    Descriptor->bDeviceProtocol = 0xFF;
    Descriptor->bMaxPacketSize0 = 0x08;
    Descriptor->idVendor = _vendorId;
    Descriptor->idProduct = _productId;
    Descriptor->bcdDevice = 0x0114;
    Descriptor->iManufacturer = 0x01;
    Descriptor->iProduct = 0x02;
    Descriptor->iSerialNumber = 0x03;
    Descriptor->bNumConfigurations = 0x01;
    return STATUS_SUCCESS;
}

NTSTATUS XusbTarget::UsbSelectConfiguration(PURB Urb)
{
    if (Urb->UrbHeader.Length < XUSB_CONFIGURATION_SIZE)
        return STATUS_INVALID_PARAMETER;

    auto pInfo = &Urb->UrbSelectConfiguration.Interface;

    // interface 0 - main data
    pInfo->Class = 0xFF;
    pInfo->SubClass = 0x5D;
    pInfo->Protocol = 0x01;
    pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0000);
    pInfo->Pipes[0].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[0].MaximumPacketSize = 0x20;
    pInfo->Pipes[0].EndpointAddress = 0x81;
    pInfo->Pipes[0].Interval = 0x04;
    pInfo->Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0081);
    pInfo->Pipes[1].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[1].MaximumPacketSize = 0x20;
    pInfo->Pipes[1].EndpointAddress = 0x01;
    pInfo->Pipes[1].Interval = 0x08;
    pInfo->Pipes[1].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[1].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0001);

    // interface 1
    pInfo = reinterpret_cast<PUSBD_INTERFACE_INFORMATION>(reinterpret_cast<PCHAR>(pInfo) + pInfo->Length);
    pInfo->Class = 0xFF;
    pInfo->SubClass = 0x5D;
    pInfo->Protocol = 0x03;
    pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0000);
    pInfo->Pipes[0].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[0].MaximumPacketSize = 0x20;
    pInfo->Pipes[0].EndpointAddress = 0x82;
    pInfo->Pipes[0].Interval = 0x04;
    pInfo->Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0082);
    pInfo->Pipes[1].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[1].MaximumPacketSize = 0x20;
    pInfo->Pipes[1].EndpointAddress = 0x02;
    pInfo->Pipes[1].Interval = 0x08;
    pInfo->Pipes[1].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[1].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0002);
    pInfo->Pipes[2].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[2].MaximumPacketSize = 0x20;
    pInfo->Pipes[2].EndpointAddress = 0x83;
    pInfo->Pipes[2].Interval = 0x08;
    pInfo->Pipes[2].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[2].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0083);
    pInfo->Pipes[3].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[3].MaximumPacketSize = 0x20;
    pInfo->Pipes[3].EndpointAddress = 0x03;
    pInfo->Pipes[3].Interval = 0x08;
    pInfo->Pipes[3].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[3].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0003);

    // interface 2
    pInfo = reinterpret_cast<PUSBD_INTERFACE_INFORMATION>(reinterpret_cast<PCHAR>(pInfo) + pInfo->Length);
    pInfo->Class = 0xFF;
    pInfo->SubClass = 0x5D;
    pInfo->Protocol = 0x02;
    pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0000);
    pInfo->Pipes[0].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[0].MaximumPacketSize = 0x20;
    pInfo->Pipes[0].EndpointAddress = 0x84;
    pInfo->Pipes[0].Interval = 0x04;
    pInfo->Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0084);

    // interface 3
    pInfo = reinterpret_cast<PUSBD_INTERFACE_INFORMATION>(reinterpret_cast<PCHAR>(pInfo) + pInfo->Length);
    pInfo->Class = 0xFF;
    pInfo->SubClass = 0xFD;
    pInfo->Protocol = 0x13;
    pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0000);

    return STATUS_SUCCESS;
}

NTSTATUS XusbTarget::UsbSelectInterface(PURB Urb)
{
    auto pInfo = &Urb->UrbSelectInterface.Interface;

    if (pInfo->InterfaceNumber == 1)
    {
        pInfo->Class = 0xFF;
        pInfo->SubClass = 0x5D;
        pInfo->Protocol = 0x03;
        pInfo->NumberOfPipes = 0x04;
        pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0000);

        pInfo->Pipes[0].MaximumTransferSize = 0x00400000;
        pInfo->Pipes[0].MaximumPacketSize = 0x20;
        pInfo->Pipes[0].EndpointAddress = 0x82;
        pInfo->Pipes[0].Interval = 0x04;
        pInfo->Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
        pInfo->Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0082);
        pInfo->Pipes[0].PipeFlags = 0x00;

        pInfo->Pipes[1].MaximumTransferSize = 0x00400000;
        pInfo->Pipes[1].MaximumPacketSize = 0x20;
        pInfo->Pipes[1].EndpointAddress = 0x02;
        pInfo->Pipes[1].Interval = 0x08;
        pInfo->Pipes[1].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
        pInfo->Pipes[1].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0002);
        pInfo->Pipes[1].PipeFlags = 0x00;

        pInfo->Pipes[2].MaximumTransferSize = 0x00400000;
        pInfo->Pipes[2].MaximumPacketSize = 0x20;
        pInfo->Pipes[2].EndpointAddress = 0x83;
        pInfo->Pipes[2].Interval = 0x08;
        pInfo->Pipes[2].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
        pInfo->Pipes[2].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0083);
        pInfo->Pipes[2].PipeFlags = 0x00;

        pInfo->Pipes[3].MaximumTransferSize = 0x00400000;
        pInfo->Pipes[3].MaximumPacketSize = 0x20;
        pInfo->Pipes[3].EndpointAddress = 0x03;
        pInfo->Pipes[3].Interval = 0x08;
        pInfo->Pipes[3].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
        pInfo->Pipes[3].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0003);
        pInfo->Pipes[3].PipeFlags = 0x00;

        return STATUS_SUCCESS;
    }

    if (pInfo->InterfaceNumber == 2)
    {
        pInfo->Class = 0xFF;
        pInfo->SubClass = 0x5D;
        pInfo->Protocol = 0x02;
        pInfo->NumberOfPipes = 0x01;
        pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0000);

        pInfo->Pipes[0].MaximumTransferSize = 0x00400000;
        pInfo->Pipes[0].MaximumPacketSize = 0x20;
        pInfo->Pipes[0].EndpointAddress = 0x84;
        pInfo->Pipes[0].Interval = 0x04;
        pInfo->Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
        pInfo->Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0084);
        pInfo->Pipes[0].PipeFlags = 0x00;

        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_PARAMETER;
}

NTSTATUS XusbTarget::UsbGetStringDescriptor(PURB Urb)
{
    UNREFERENCED_PARAMETER(Urb);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS XusbTarget::UsbBulkOrInterruptTransfer(_URB_BULK_OR_INTERRUPT_TRANSFER* pTransfer, WDFREQUEST Request)
{
    NTSTATUS status = STATUS_SUCCESS;

    // data from device to host (interrupt IN)
    if (pTransfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
    {
        auto blobBuffer = static_cast<PUCHAR>(WdfMemoryGetBuffer(_interruptBlobStorage, nullptr));

        if (IsDataPipe(pTransfer))
        {
            switch (_interruptInitStage)
            {
            case 0:
                pTransfer->TransferBufferLength = XUSB_INIT_STAGE_SIZE;
                _interruptInitStage++;
                RtlCopyMemory(pTransfer->TransferBuffer, &blobBuffer[BLOB_00], XUSB_INIT_STAGE_SIZE);
                return STATUS_SUCCESS;
            case 1:
                pTransfer->TransferBufferLength = XUSB_INIT_STAGE_SIZE;
                _interruptInitStage++;
                RtlCopyMemory(pTransfer->TransferBuffer, &blobBuffer[BLOB_01], XUSB_INIT_STAGE_SIZE);
                return STATUS_SUCCESS;
            case 2:
                pTransfer->TransferBufferLength = XUSB_INIT_STAGE_SIZE;
                _interruptInitStage++;
                RtlCopyMemory(pTransfer->TransferBuffer, &blobBuffer[BLOB_02], XUSB_INIT_STAGE_SIZE);
                return STATUS_SUCCESS;
            case 3:
                pTransfer->TransferBufferLength = XUSB_INIT_STAGE_SIZE;
                _interruptInitStage++;
                RtlCopyMemory(pTransfer->TransferBuffer, &blobBuffer[BLOB_03], XUSB_INIT_STAGE_SIZE);
                return STATUS_SUCCESS;
            case 4:
                pTransfer->TransferBufferLength = sizeof(XUSB_INTERRUPT_IN_PACKET);
                _interruptInitStage++;
                RtlCopyMemory(pTransfer->TransferBuffer, &blobBuffer[BLOB_04], sizeof(XUSB_INTERRUPT_IN_PACKET));
                return STATUS_SUCCESS;
            case 5:
                pTransfer->TransferBufferLength = XUSB_INIT_STAGE_SIZE;
                _interruptInitStage++;
                RtlCopyMemory(pTransfer->TransferBuffer, &blobBuffer[BLOB_05], XUSB_INIT_STAGE_SIZE);
                return STATUS_SUCCESS;
            default:
                // queue for pending report submission
                status = WdfRequestForwardToIoQueue(Request, _pendingUsbInRequests);
                return NT_SUCCESS(status) ? STATUS_PENDING : status;
            }
        }

        if (IsControlPipe(pTransfer))
        {
            if (!_reportedCapabilities && pTransfer->TransferBufferLength >= XUSB_INIT_STAGE_SIZE)
            {
                RtlCopyMemory(pTransfer->TransferBuffer, &blobBuffer[BLOB_06], XUSB_INIT_STAGE_SIZE);
                _reportedCapabilities = TRUE;
                return STATUS_SUCCESS;
            }

            status = WdfRequestForwardToIoQueue(Request, _holdingUsbInRequests);
            return NT_SUCCESS(status) ? STATUS_PENDING : status;
        }
    }

    // data from host to device (interrupt OUT) - LED and rumble
    if (pTransfer->TransferBufferLength == XUSB_LEDSET_SIZE)
    {
        auto buffer = static_cast<PUCHAR>(pTransfer->TransferBuffer);
        if (buffer[0] == 0x01 && buffer[1] == 0x03 && buffer[2] >= 0x02)
        {
            _ledNumber = static_cast<CHAR>(buffer[2] - 0x02);
            KeSetEvent(&_pdoBootNotificationEvent, 0, FALSE);

            // complete any pending wait-device-ready requests
            WDFREQUEST waitReq;
            while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(_waitDeviceReadyRequests, &waitReq)))
                WdfRequestComplete(waitReq, STATUS_SUCCESS);
        }
    }

    if (pTransfer->TransferBufferLength == XUSB_RUMBLE_SIZE)
    {
        RtlCopyMemory(_rumble, pTransfer->TransferBuffer, XUSB_RUMBLE_SIZE);

        // try to complete pending notification
        WDFREQUEST notifyReq;
        if (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(_pendingNotificationRequests, &notifyReq)))
        {
            PGAMEPAD_NOTIFICATION notify = nullptr;
            status = WdfRequestRetrieveOutputBuffer(notifyReq, sizeof(GAMEPAD_NOTIFICATION),
                reinterpret_cast<PVOID*>(&notify), nullptr);
            if (NT_SUCCESS(status))
            {
                notify->Size = sizeof(GAMEPAD_NOTIFICATION);
                notify->SerialNo = _serialNo;
                notify->TargetType = GamepadTypeXbox360;
                notify->Output.Xusb.LargeMotor = _rumble[3];
                notify->Output.Xusb.SmallMotor = _rumble[4];
                notify->Output.Xusb.LedNumber = static_cast<UCHAR>(_ledNumber);
                WdfRequestCompleteWithInformation(notifyReq, STATUS_SUCCESS, sizeof(GAMEPAD_NOTIFICATION));
            }
            else
            {
                WdfRequestComplete(notifyReq, status);
            }
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS XusbTarget::UsbControlTransfer(PURB Urb)
{
    auto blobBuffer = static_cast<PUCHAR>(WdfMemoryGetBuffer(_interruptBlobStorage, nullptr));

    switch (Urb->UrbControlTransfer.SetupPacket[6])
    {
    case 0x04:
        RtlCopyMemory(Urb->UrbControlTransfer.TransferBuffer, &blobBuffer[BLOB_07], 0x04);
        return STATUS_SUCCESS;
    case 0x14:
    case 0x08:
        Urb->UrbControlTransfer.Hdr.Status = USBD_STATUS_STALL_PID;
        return STATUS_UNSUCCESSFUL;
    default:
        return STATUS_SUCCESS;
    }
}

NTSTATUS XusbTarget::UsbClassInterface(PURB Urb)
{
    UNREFERENCED_PARAMETER(Urb);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS XusbTarget::UsbGetDescriptorFromInterface(PURB Urb)
{
    UNREFERENCED_PARAMETER(Urb);
    return STATUS_NOT_IMPLEMENTED;
}

void XusbTarget::AbortPipe() {}

NTSTATUS XusbTarget::SubmitReportImpl(PVOID NewReport)
{
    auto submitReport = static_cast<PGAMEPAD_SUBMIT_REPORT>(NewReport);

    // skip if report hasn't changed
    if (RtlCompareMemory(&_packet.Report, &submitReport->Report.Xusb, sizeof(XUSB_GAMEPAD_REPORT))
        == sizeof(XUSB_GAMEPAD_REPORT))
        return STATUS_SUCCESS;

    WDFREQUEST usbRequest;
    NTSTATUS status = WdfIoQueueRetrieveNextRequest(_pendingUsbInRequests, &usbRequest);
    if (!NT_SUCCESS(status))
        return status;

    auto irp = WdfRequestWdmGetIrp(usbRequest);
    auto urb = static_cast<PURB>(URB_FROM_IRP(irp));
    auto buffer = static_cast<PUCHAR>(urb->UrbBulkOrInterruptTransfer.TransferBuffer);

    urb->UrbBulkOrInterruptTransfer.TransferBufferLength = sizeof(XUSB_INTERRUPT_IN_PACKET);

    RtlCopyMemory(&_packet.Report, &submitReport->Report.Xusb, sizeof(XUSB_GAMEPAD_REPORT));
    RtlCopyMemory(buffer, &_packet, sizeof(XUSB_INTERRUPT_IN_PACKET));

    WdfRequestComplete(usbRequest, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS XusbTarget::GetUserIndex(PULONG UserIndex) const
{
    if (!IsOwnerProcess())
        return STATUS_ACCESS_DENIED;
    if (!UserIndex)
        return STATUS_INVALID_PARAMETER;
    if (_ledNumber >= 0)
    {
        *UserIndex = static_cast<ULONG>(_ledNumber);
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_DEVICE_OBJECT_PARAMETER;
}
