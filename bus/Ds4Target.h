#pragma once

#include "EmulationTarget.h"

typedef struct _MAC_ADDRESS {
    UCHAR Vendor0;
    UCHAR Vendor1;
    UCHAR Vendor2;
    UCHAR Nic0;
    UCHAR Nic1;
    UCHAR Nic2;
} MAC_ADDRESS, *PMAC_ADDRESS;

class Ds4Target : public EmulationTarget
{
public:
    Ds4Target(ULONG Serial, LONG SessionId, USHORT VendorId = 0x054C, USHORT ProductId = 0x05C4);

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

private:
    static EVT_WDF_TIMER PendingUsbRequestsTimerFunc;
    static void ReverseByteArray(PUCHAR Array, int Length);
    static void GenerateRandomMacAddress(PMAC_ADDRESS Address);

    static const int DS4_DESCRIPTOR_SIZE = 0x0029;
    static const int DS4_REPORT_SIZE = 0x40;
    static const int DS4_QUEUE_FLUSH_PERIOD = 5;
    static const int DS4_OUTPUT_BUFFER_OFFSET = 0x04;
    static const int DS4_OUTPUT_BUFFER_LENGTH = 0x05;
    static const int DS4_MANUFACTURER_NAME_LENGTH = 0x38;
    static const int DS4_PRODUCT_NAME_LENGTH = 0x28;

#if defined(_X86_)
    static const int DS4_CONFIGURATION_SIZE = 0x0050;
#else
    static const int DS4_CONFIGURATION_SIZE = 0x0070;
#endif

    UCHAR _report[DS4_REPORT_SIZE]{};
    DS4_OUTPUT_DATA _outputReport{};
    WDFTIMER _pendingUsbInRequestsTimer{};
    MAC_ADDRESS _targetMacAddress{};
    MAC_ADDRESS _hostMacAddress{};
};
