#include "Ds4Target.h"
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

extern "C" NTSYSAPI ULONG NTAPI RtlRandomEx(_Inout_ PULONG Seed);

Ds4Target::Ds4Target(ULONG Serial, LONG SessionId, USHORT VendorId, USHORT ProductId)
    : EmulationTarget(Serial, SessionId, VendorId, ProductId)
{
    _targetType = GamepadTypeDS4;
    _usbConfigDescriptorSize = DS4_DESCRIPTOR_SIZE;

    _pnpCapabilities.SurpriseRemovalOK = WdfTrue;
    _powerCapabilities.DeviceState[PowerSystemWorking] = PowerDeviceD0;
    _powerCapabilities.WakeFromD0 = WdfTrue;
}

NTSTATUS Ds4Target::PdoPrepareDevice(PWDFDEVICE_INIT DeviceInit, PUNICODE_STRING DeviceId, PUNICODE_STRING DeviceDesc)
{
    NTSTATUS status;
    DECLARE_UNICODE_STRING_SIZE(buffer, MAX_HARDWARE_ID_LENGTH);

    status = RtlUnicodeStringInit(DeviceDesc, L"Virtual DualShock 4 Controller");
    if (!NT_SUCCESS(status)) return status;

    RtlUnicodeStringPrintf(&buffer, L"USB\\VID_%04X&PID_%04X&REV_0100", _vendorId, _productId);
    status = WdfPdoInitAddHardwareID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;
    RtlUnicodeStringCopy(DeviceId, &buffer);

    RtlUnicodeStringPrintf(&buffer, L"USB\\VID_%04X&PID_%04X", _vendorId, _productId);
    status = WdfPdoInitAddHardwareID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;

    RtlUnicodeStringInit(&buffer, L"USB\\Class_03&SubClass_00&Prot_00");
    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;

    RtlUnicodeStringInit(&buffer, L"USB\\Class_03&SubClass_00");
    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;

    RtlUnicodeStringInit(&buffer, L"USB\\Class_03");
    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status)) return status;

    return STATUS_SUCCESS;
}

NTSTATUS Ds4Target::PdoPrepareHardware()
{
    // usb bus interface
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

    NTSTATUS status = WdfDeviceAddQueryInterface(_pdoDevice, &ifaceCfg);
    if (!NT_SUCCESS(status)) return status;

    // default hid report
    UCHAR defaultReport[DS4_REPORT_SIZE] = {
        0x01, 0x82, 0x7F, 0x7E, 0x80, 0x08, 0x00, 0x58,
        0x00, 0x00, 0xFD, 0x63, 0x06, 0x03, 0x00, 0xFE,
        0xFF, 0xFC, 0xFF, 0x79, 0xFD, 0x1B, 0x14, 0xD1,
        0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1B, 0x00,
        0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80,
        0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00
    };
    RtlCopyMemory(_report, defaultReport, DS4_REPORT_SIZE);

    WdfTimerStart(_pendingUsbInRequestsTimer, WDF_REL_TIMEOUT_IN_MS(DS4_QUEUE_FLUSH_PERIOD));

    return STATUS_SUCCESS;
}

NTSTATUS Ds4Target::PdoInitContext()
{
    // timer for periodic report flushing
    WDF_TIMER_CONFIG timerConfig;
    WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, PendingUsbRequestsTimerFunc, DS4_QUEUE_FLUSH_PERIOD);

    WDF_OBJECT_ATTRIBUTES timerAttribs;
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttribs);
    timerAttribs.ParentObject = _pdoDevice;

    NTSTATUS status = WdfTimerCreate(&timerConfig, &timerAttribs, &_pendingUsbInRequestsTimer);
    if (!NT_SUCCESS(status)) return status;

    // generate random mac address
    GenerateRandomMacAddress(&_targetMacAddress);
    GenerateRandomMacAddress(&_hostMacAddress);

    return STATUS_SUCCESS;
}

VOID Ds4Target::GetConfigurationDescriptor(PUCHAR Buffer, ULONG Length)
{
    UCHAR descriptorData[DS4_DESCRIPTOR_SIZE] = {
        0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0xC0, 0xFA,
        0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
        0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0xD3, 0x01,
        0x07, 0x05, 0x84, 0x03, 0x40, 0x00, 0x05,
        0x07, 0x05, 0x03, 0x03, 0x40, 0x00, 0x05
    };
    RtlCopyMemory(Buffer, descriptorData, min(Length, (ULONG)DS4_DESCRIPTOR_SIZE));
}

NTSTATUS Ds4Target::UsbGetDeviceDescriptor(PUSB_DEVICE_DESCRIPTOR Descriptor)
{
    Descriptor->bLength = 0x12;
    Descriptor->bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
    Descriptor->bcdUSB = 0x0200;
    Descriptor->bDeviceClass = 0x00;
    Descriptor->bDeviceSubClass = 0x00;
    Descriptor->bDeviceProtocol = 0x00;
    Descriptor->bMaxPacketSize0 = 0x40;
    Descriptor->idVendor = _vendorId;
    Descriptor->idProduct = _productId;
    Descriptor->bcdDevice = 0x0100;
    Descriptor->iManufacturer = 0x01;
    Descriptor->iProduct = 0x02;
    Descriptor->iSerialNumber = 0x00;
    Descriptor->bNumConfigurations = 0x01;
    return STATUS_SUCCESS;
}

NTSTATUS Ds4Target::UsbSelectConfiguration(PURB Urb)
{
    if (Urb->UrbHeader.Length < DS4_CONFIGURATION_SIZE)
        return STATUS_INVALID_PARAMETER;

    auto pInfo = &Urb->UrbSelectConfiguration.Interface;
    pInfo->Class = 0x03;
    pInfo->SubClass = 0x00;
    pInfo->Protocol = 0x00;
    pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0000);

    pInfo->Pipes[0].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[0].MaximumPacketSize = 0x40;
    pInfo->Pipes[0].EndpointAddress = 0x84;
    pInfo->Pipes[0].Interval = 0x05;
    pInfo->Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0084);

    pInfo->Pipes[1].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[1].MaximumPacketSize = 0x40;
    pInfo->Pipes[1].EndpointAddress = 0x03;
    pInfo->Pipes[1].Interval = 0x05;
    pInfo->Pipes[1].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[1].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0003);

    return STATUS_SUCCESS;
}

NTSTATUS Ds4Target::UsbSelectInterface(PURB Urb)
{
    UNREFERENCED_PARAMETER(Urb);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS Ds4Target::UsbGetStringDescriptor(PURB Urb)
{
    switch (Urb->UrbControlDescriptorRequest.Index)
    {
    case 0:
    {
        UCHAR langId[] = { 0x04, 0x03, 0x09, 0x04 };
        Urb->UrbControlDescriptorRequest.TransferBufferLength = sizeof(langId);
        RtlCopyMemory(Urb->UrbControlDescriptorRequest.TransferBuffer, langId, sizeof(langId));
        break;
    }
    case 1:
    {
        if (Urb->UrbControlDescriptorRequest.TransferBufferLength < DS4_MANUFACTURER_NAME_LENGTH)
        {
            auto pDesc = static_cast<PUSB_STRING_DESCRIPTOR>(Urb->UrbControlDescriptorRequest.TransferBuffer);
            pDesc->bLength = DS4_MANUFACTURER_NAME_LENGTH;
            break;
        }
        // "Sony Computer Entertainment"
        UCHAR manufacturerString[DS4_MANUFACTURER_NAME_LENGTH] = {
            0x38, 0x03, 0x53, 0x00, 0x6F, 0x00, 0x6E, 0x00,
            0x79, 0x00, 0x20, 0x00, 0x43, 0x00, 0x6F, 0x00,
            0x6D, 0x00, 0x70, 0x00, 0x75, 0x00, 0x74, 0x00,
            0x65, 0x00, 0x72, 0x00, 0x20, 0x00, 0x45, 0x00,
            0x6E, 0x00, 0x74, 0x00, 0x65, 0x00, 0x72, 0x00,
            0x74, 0x00, 0x61, 0x00, 0x69, 0x00, 0x6E, 0x00,
            0x6D, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x74, 0x00
        };
        Urb->UrbControlDescriptorRequest.TransferBufferLength = DS4_MANUFACTURER_NAME_LENGTH;
        RtlCopyMemory(Urb->UrbControlDescriptorRequest.TransferBuffer, manufacturerString, DS4_MANUFACTURER_NAME_LENGTH);
        break;
    }
    case 2:
    {
        if (Urb->UrbControlDescriptorRequest.TransferBufferLength < DS4_PRODUCT_NAME_LENGTH)
        {
            auto pDesc = static_cast<PUSB_STRING_DESCRIPTOR>(Urb->UrbControlDescriptorRequest.TransferBuffer);
            pDesc->bLength = DS4_PRODUCT_NAME_LENGTH;
            break;
        }
        // "Wireless Controller"
        UCHAR productString[DS4_PRODUCT_NAME_LENGTH] = {
            0x28, 0x03, 0x57, 0x00, 0x69, 0x00, 0x72, 0x00,
            0x65, 0x00, 0x6C, 0x00, 0x65, 0x00, 0x73, 0x00,
            0x73, 0x00, 0x20, 0x00, 0x43, 0x00, 0x6F, 0x00,
            0x6E, 0x00, 0x74, 0x00, 0x72, 0x00, 0x6F, 0x00,
            0x6C, 0x00, 0x6C, 0x00, 0x65, 0x00, 0x72, 0x00
        };
        Urb->UrbControlDescriptorRequest.TransferBufferLength = DS4_PRODUCT_NAME_LENGTH;
        RtlCopyMemory(Urb->UrbControlDescriptorRequest.TransferBuffer, productString, DS4_PRODUCT_NAME_LENGTH);
        break;
    }
    }
    return STATUS_SUCCESS;
}

NTSTATUS Ds4Target::UsbBulkOrInterruptTransfer(_URB_BULK_OR_INTERRUPT_TRANSFER* pTransfer, WDFREQUEST Request)
{
    NTSTATUS status = STATUS_SUCCESS;

    // interrupt IN - device to host
    if ((pTransfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
        && pTransfer->PipeHandle == reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0084))
    {
        status = WdfRequestForwardToIoQueue(Request, _pendingUsbInRequests);
        return NT_SUCCESS(status) ? STATUS_PENDING : status;
    }

    // interrupt OUT - host to device (output reports: rumble, led)
    if (pTransfer->TransferBufferLength >= DS4_OUTPUT_BUFFER_OFFSET + DS4_OUTPUT_BUFFER_LENGTH)
    {
        auto outBuf = static_cast<PUCHAR>(pTransfer->TransferBuffer) + DS4_OUTPUT_BUFFER_OFFSET;
        _outputReport.SmallMotor = outBuf[0];
        _outputReport.LargeMotor = outBuf[1];
        _outputReport.LightbarRed = outBuf[2];
        _outputReport.LightbarGreen = outBuf[3];
        _outputReport.LightbarBlue = outBuf[4];

        // notify usermode
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
                notify->TargetType = GamepadTypeDS4;
                notify->Output.Ds4 = _outputReport;
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

NTSTATUS Ds4Target::UsbControlTransfer(PURB Urb)
{
    switch (Urb->UrbControlTransfer.SetupPacket[6])
    {
    case 0x14:
    case 0x08:
        Urb->UrbControlTransfer.Hdr.Status = USBD_STATUS_STALL_PID;
        return STATUS_UNSUCCESSFUL;
    default:
        return STATUS_SUCCESS;
    }
}

NTSTATUS Ds4Target::UsbClassInterface(PURB Urb)
{
    auto pRequest = &Urb->UrbControlVendorClassRequest;
    UCHAR reportId = pRequest->Value & 0xFF;
    UCHAR reportType = (pRequest->Value >> 8) & 0xFF;

    if (pRequest->Request == 0x01) // GET_REPORT
    {
        if (reportType == 0x03) // FEATURE
        {
            switch (reportId)
            {
            case 0xA3:
            {
                UCHAR response[] = {
                    0xA3, 0x41, 0x75, 0x67, 0x20, 0x20, 0x33, 0x20,
                    0x32, 0x30, 0x31, 0x33, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x30, 0x37, 0x3A, 0x30, 0x31, 0x3A, 0x31,
                    0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x01, 0x00, 0x31, 0x03, 0x00, 0x00,
                    0x00, 0x49, 0x00, 0x05, 0x00, 0x00, 0x80, 0x03, 0x00
                };
                pRequest->TransferBufferLength = sizeof(response);
                RtlCopyMemory(pRequest->TransferBuffer, response, sizeof(response));
                break;
            }
            case 0x02:
            {
                UCHAR response[] = {
                    0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87,
                    0x22, 0x7B, 0xDD, 0xB2, 0x22, 0x47, 0xDD, 0xBD,
                    0x22, 0x43, 0xDD, 0x1C, 0x02, 0x1C, 0x02, 0x7F,
                    0x1E, 0x2E, 0xDF, 0x60, 0x1F, 0x4C, 0xE0, 0x3A,
                    0x1D, 0xC6, 0xDE, 0x08, 0x00
                };
                pRequest->TransferBufferLength = sizeof(response);
                RtlCopyMemory(pRequest->TransferBuffer, response, sizeof(response));
                break;
            }
            case 0x12:
            {
                UCHAR response[] = {
                    0x12, 0x8B, 0x09, 0x07, 0x6D, 0x66, 0x1C, 0x08,
                    0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                };
                RtlCopyMemory(response + 1, &_targetMacAddress, sizeof(MAC_ADDRESS));
                ReverseByteArray(response + 1, sizeof(MAC_ADDRESS));
                RtlCopyMemory(response + 10, &_hostMacAddress, sizeof(MAC_ADDRESS));
                ReverseByteArray(response + 10, sizeof(MAC_ADDRESS));
                pRequest->TransferBufferLength = sizeof(response);
                RtlCopyMemory(pRequest->TransferBuffer, response, sizeof(response));
                break;
            }
            }
        }
    }
    else if (pRequest->Request == 0x09) // SET_REPORT
    {
        if (reportType == 0x03)
        {
            switch (reportId)
            {
            case 0x13:
            {
                UCHAR response[] = {
                    0x13, 0xAC, 0x9E, 0x17, 0x94, 0x05, 0xB0, 0x56,
                    0xE8, 0x81, 0x38, 0x08, 0x06, 0x51, 0x41, 0xC0,
                    0x7F, 0x12, 0xAA, 0xD9, 0x66, 0x3C, 0xCE
                };
                pRequest->TransferBufferLength = sizeof(response);
                RtlCopyMemory(pRequest->TransferBuffer, response, sizeof(response));
                break;
            }
            case 0x14:
            {
                UCHAR response[] = {
                    0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                };
                pRequest->TransferBufferLength = sizeof(response);
                RtlCopyMemory(pRequest->TransferBuffer, response, sizeof(response));
                break;
            }
            }
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS Ds4Target::UsbGetDescriptorFromInterface(PURB Urb)
{
    // full 467-byte HID report descriptor
    UCHAR hidReportDescriptor[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,
        0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
        0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95,
        0x04, 0x81, 0x02, 0x09, 0x39, 0x15, 0x00, 0x25,
        0x07, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14,
        0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x15, 0x00,
        0x25, 0x01, 0x75, 0x01, 0x95, 0x0E, 0x81, 0x02,
        0x06, 0x00, 0xFF, 0x09, 0x20, 0x75, 0x06, 0x95,
        0x01, 0x15, 0x00, 0x25, 0x7F, 0x81, 0x02, 0x05,
        0x01, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26,
        0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
        0x06, 0x00, 0xFF, 0x09, 0x21, 0x95, 0x36, 0x81,
        0x02, 0x85, 0x05, 0x09, 0x22, 0x95, 0x1F, 0x91,
        0x02, 0x85, 0x04, 0x09, 0x23, 0x95, 0x24, 0xB1,
        0x02, 0x85, 0x02, 0x09, 0x24, 0x95, 0x24, 0xB1,
        0x02, 0x85, 0x08, 0x09, 0x25, 0x95, 0x03, 0xB1,
        0x02, 0x85, 0x10, 0x09, 0x26, 0x95, 0x04, 0xB1,
        0x02, 0x85, 0x11, 0x09, 0x27, 0x95, 0x02, 0xB1,
        0x02, 0x85, 0x12, 0x06, 0x02, 0xFF, 0x09, 0x21,
        0x95, 0x0F, 0xB1, 0x02, 0x85, 0x13, 0x09, 0x22,
        0x95, 0x16, 0xB1, 0x02, 0x85, 0x14, 0x06, 0x05,
        0xFF, 0x09, 0x20, 0x95, 0x10, 0xB1, 0x02, 0x85,
        0x15, 0x09, 0x21, 0x95, 0x2C, 0xB1, 0x02, 0x06,
        0x80, 0xFF, 0x85, 0x80, 0x09, 0x20, 0x95, 0x06,
        0xB1, 0x02, 0x85, 0x81, 0x09, 0x21, 0x95, 0x06,
        0xB1, 0x02, 0x85, 0x82, 0x09, 0x22, 0x95, 0x05,
        0xB1, 0x02, 0x85, 0x83, 0x09, 0x23, 0x95, 0x01,
        0xB1, 0x02, 0x85, 0x84, 0x09, 0x24, 0x95, 0x04,
        0xB1, 0x02, 0x85, 0x85, 0x09, 0x25, 0x95, 0x06,
        0xB1, 0x02, 0x85, 0x86, 0x09, 0x26, 0x95, 0x06,
        0xB1, 0x02, 0x85, 0x87, 0x09, 0x27, 0x95, 0x23,
        0xB1, 0x02, 0x85, 0x88, 0x09, 0x28, 0x95, 0x22,
        0xB1, 0x02, 0x85, 0x89, 0x09, 0x29, 0x95, 0x02,
        0xB1, 0x02, 0x85, 0x90, 0x09, 0x30, 0x95, 0x05,
        0xB1, 0x02, 0x85, 0x91, 0x09, 0x31, 0x95, 0x03,
        0xB1, 0x02, 0x85, 0x92, 0x09, 0x32, 0x95, 0x03,
        0xB1, 0x02, 0x85, 0x93, 0x09, 0x33, 0x95, 0x0C,
        0xB1, 0x02, 0x85, 0xA0, 0x09, 0x40, 0x95, 0x06,
        0xB1, 0x02, 0x85, 0xA1, 0x09, 0x41, 0x95, 0x01,
        0xB1, 0x02, 0x85, 0xA2, 0x09, 0x42, 0x95, 0x01,
        0xB1, 0x02, 0x85, 0xA3, 0x09, 0x43, 0x95, 0x30,
        0xB1, 0x02, 0x85, 0xA4, 0x09, 0x44, 0x95, 0x0D,
        0xB1, 0x02, 0x85, 0xA5, 0x09, 0x45, 0x95, 0x15,
        0xB1, 0x02, 0x85, 0xA6, 0x09, 0x46, 0x95, 0x15,
        0xB1, 0x02, 0x85, 0xF0, 0x09, 0x47, 0x95, 0x3F,
        0xB1, 0x02, 0x85, 0xF1, 0x09, 0x48, 0x95, 0x3F,
        0xB1, 0x02, 0x85, 0xF2, 0x09, 0x49, 0x95, 0x0F,
        0xB1, 0x02, 0x85, 0xA7, 0x09, 0x4A, 0x95, 0x01,
        0xB1, 0x02, 0x85, 0xA8, 0x09, 0x4B, 0x95, 0x01,
        0xB1, 0x02, 0x85, 0xA9, 0x09, 0x4C, 0x95, 0x08,
        0xB1, 0x02, 0x85, 0xAA, 0x09, 0x4E, 0x95, 0x01,
        0xB1, 0x02, 0x85, 0xAB, 0x09, 0x4F, 0x95, 0x39,
        0xB1, 0x02, 0x85, 0xAC, 0x09, 0x50, 0x95, 0x39,
        0xB1, 0x02, 0x85, 0xAD, 0x09, 0x51, 0x95, 0x0B,
        0xB1, 0x02, 0x85, 0xAE, 0x09, 0x52, 0x95, 0x01,
        0xB1, 0x02, 0x85, 0xAF, 0x09, 0x53, 0x95, 0x02,
        0xB1, 0x02, 0x85, 0xB0, 0x09, 0x54, 0x95, 0x3F,
        0xB1, 0x02, 0xC0
    };

    auto pRequest = &Urb->UrbControlDescriptorRequest;
    if (pRequest->TransferBufferLength >= sizeof(hidReportDescriptor))
    {
        RtlCopyMemory(pRequest->TransferBuffer, hidReportDescriptor, sizeof(hidReportDescriptor));
        KeSetEvent(&_pdoBootNotificationEvent, 0, FALSE);

        // complete wait-device-ready requests
        WDFREQUEST waitReq;
        while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(_waitDeviceReadyRequests, &waitReq)))
            WdfRequestComplete(waitReq, STATUS_SUCCESS);

        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_PARAMETER;
}

void Ds4Target::AbortPipe()
{
    WdfTimerStop(_pendingUsbInRequestsTimer, TRUE);
}

NTSTATUS Ds4Target::SubmitReportImpl(PVOID NewReport)
{
    auto submitReport = static_cast<PGAMEPAD_SUBMIT_REPORT>(NewReport);

    WDFREQUEST usbRequest;
    NTSTATUS status = WdfIoQueueRetrieveNextRequest(_pendingUsbInRequests, &usbRequest);
    if (!NT_SUCCESS(status))
        return status;

    auto irp = WdfRequestWdmGetIrp(usbRequest);
    auto urb = static_cast<PURB>(URB_FROM_IRP(irp));
    auto buffer = static_cast<PUCHAR>(urb->UrbBulkOrInterruptTransfer.TransferBuffer);

    urb->UrbBulkOrInterruptTransfer.TransferBufferLength = DS4_REPORT_SIZE;

    // copy report data starting at offset 1 (skip report id)
    RtlCopyMemory(&_report[1], &submitReport->Report.Ds4, sizeof(DS4_GAMEPAD_REPORT));

    if (buffer)
        RtlCopyMemory(buffer, _report, DS4_REPORT_SIZE);

    WdfRequestComplete(usbRequest, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

// timer: flush pending interrupt in requests with current report data
VOID Ds4Target::PendingUsbRequestsTimerFunc(WDFTIMER Timer)
{
    auto device = WdfTimerGetParentObject(Timer);
    auto target = static_cast<Ds4Target*>(PdoGetTarget(device)->Target);
    if (!target) return;

    WDFREQUEST usbRequest;
    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(target->_pendingUsbInRequests, &usbRequest)))
    {
        auto irp = WdfRequestWdmGetIrp(usbRequest);
        auto urb = static_cast<PURB>(URB_FROM_IRP(irp));
        auto buffer = static_cast<PUCHAR>(urb->UrbBulkOrInterruptTransfer.TransferBuffer);

        urb->UrbBulkOrInterruptTransfer.TransferBufferLength = DS4_REPORT_SIZE;

        if (buffer)
            RtlCopyMemory(buffer, target->_report, DS4_REPORT_SIZE);

        WdfRequestComplete(usbRequest, STATUS_SUCCESS);
    }
}

void Ds4Target::ReverseByteArray(PUCHAR Array, int Length)
{
    for (int i = 0; i < Length / 2; i++)
    {
        UCHAR tmp = Array[i];
        Array[i] = Array[Length - 1 - i];
        Array[Length - 1 - i] = tmp;
    }
}

void Ds4Target::GenerateRandomMacAddress(PMAC_ADDRESS Address)
{
    Address->Vendor0 = 0xC0;
    Address->Vendor1 = 0x13;
    Address->Vendor2 = 0x37;

    ULONG seed = KeQueryPerformanceCounter(nullptr).LowPart;
    Address->Nic0 = static_cast<UCHAR>(RtlRandomEx(&seed) % 0xFF);
    Address->Nic1 = static_cast<UCHAR>(RtlRandomEx(&seed) % 0xFF);
    Address->Nic2 = static_cast<UCHAR>(RtlRandomEx(&seed) % 0xFF);
}
