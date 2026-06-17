#pragma once

#include "EmulationTarget.h"

class XusbTarget : public EmulationTarget
{
public:
    XusbTarget(ULONG Serial, LONG SessionId, USHORT VendorId = 0x045E, USHORT ProductId = 0x028E);

    NTSTATUS PdoPrepareDevice(PWDFDEVICE_INIT DeviceInit, PUNICODE_STRING DeviceId, PUNICODE_STRING DeviceDesc) override;
    NTSTATUS PdoPrepareHardware() override;
    NTSTATUS PdoInitContext() override;
    VOID GetConfigurationDescriptor(PUCHAR Buffer, ULONG Length) override;
    NTSTATUS UsbGetDeviceDescriptor(PUSB_DEVICE_DESCRIPTOR Descriptor) override;
    NTSTATUS UsbSelectConfiguration(PURB Urb) override;
    NTSTATUS UsbSelectInterface(PURB Urb) override;
    NTSTATUS UsbGetStringDescriptor(PURB Urb) override;
    NTSTATUS UsbBulkOrInterruptTransfer(_URB_BULK_OR_INTERRUPT_TRANSFER* Transfer, WDFREQUEST Request) override;
    NTSTATUS UsbControlTransfer(PURB Urb) override;
    NTSTATUS UsbClassInterface(PURB Urb) override;
    NTSTATUS UsbGetDescriptorFromInterface(PURB Urb) override;
    void AbortPipe() override;
    NTSTATUS SubmitReportImpl(PVOID NewReport) override;
    NTSTATUS GetUserIndex(PULONG UserIndex) const override;

private:
    typedef struct _XUSB_INTERRUPT_IN_PACKET {
        UCHAR Id;
        UCHAR Size;
        XUSB_GAMEPAD_REPORT Report;
    } XUSB_INTERRUPT_IN_PACKET, *PXUSB_INTERRUPT_IN_PACKET;

    static constexpr bool IsDataPipe(_URB_BULK_OR_INTERRUPT_TRANSFER* t) {
        return (t->PipeHandle == reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0081));
    }
    static constexpr bool IsControlPipe(_URB_BULK_OR_INTERRUPT_TRANSFER* t) {
        return (t->PipeHandle == reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0083));
    }

    static const int XUSB_DESCRIPTOR_SIZE = 0x0099;
    static const int XUSB_RUMBLE_SIZE = 0x08;
    static const int XUSB_LEDSET_SIZE = 0x03;
    static const int XUSB_INIT_STAGE_SIZE = 0x03;
    static const int XUSB_BLOB_STORAGE_SIZE = 0x2A;

    static const int BLOB_00 = 0x00;
    static const int BLOB_01 = 0x03;
    static const int BLOB_02 = 0x06;
    static const int BLOB_03 = 0x09;
    static const int BLOB_04 = 0x0C;
    static const int BLOB_05 = 0x20;
    static const int BLOB_06 = 0x23;
    static const int BLOB_07 = 0x26;

#if defined(_X86_)
    static const int XUSB_CONFIGURATION_SIZE = 0x00E4;
#else
    static const int XUSB_CONFIGURATION_SIZE = 0x0130;
#endif

    UCHAR _rumble[XUSB_RUMBLE_SIZE]{};
    CHAR  _ledNumber = -1;
    XUSB_INTERRUPT_IN_PACKET _packet{};
    WDFQUEUE _holdingUsbInRequests{};
    BOOLEAN _reportedCapabilities = FALSE;
    ULONG _interruptInitStage = 0;
    WDFMEMORY _interruptBlobStorage{};
};
