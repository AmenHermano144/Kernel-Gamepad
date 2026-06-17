#pragma once

#include "Driver.h"

class EmulationTarget
{
public:
    EmulationTarget(ULONG Serial, LONG SessionId, USHORT VendorId, USHORT ProductId);
    virtual ~EmulationTarget() = default;

    // pdo lifecycle
    NTSTATUS CreatePdoDevice(_In_ WDFDEVICE ParentDevice, _In_ PWDFDEVICE_INIT DeviceInit);
    NTSTATUS Prepare(WDFDEVICE ParentDevice);
    NTSTATUS EnqueueWaitDeviceReady(WDFREQUEST Request);
    NTSTATUS SubmitReport(PVOID NewReport);
    NTSTATUS EnqueueNotification(WDFREQUEST Request) const;

    // accessors
    GAMEPAD_TARGET_TYPE GetType() const { return _targetType; }
    ULONG GetSerial() const { return _serialNo; }
    LONG GetSessionId() const { return _sessionId; }
    bool IsOwnerProcess() const;

    // static pdo lookup
    static bool FindByTypeAndSerial(
        WDFDEVICE ParentDevice,
        GAMEPAD_TARGET_TYPE Type,
        ULONG SerialNo,
        EmulationTarget** OutTarget);

    // usb descriptor handlers (pure virtual)
    virtual NTSTATUS UsbGetDeviceDescriptor(PUSB_DEVICE_DESCRIPTOR Descriptor) = 0;
    virtual VOID GetConfigurationDescriptor(PUCHAR Buffer, ULONG Length) = 0;
    virtual NTSTATUS UsbSelectConfiguration(PURB Urb) = 0;
    virtual NTSTATUS UsbSelectInterface(PURB Urb) = 0;
    virtual NTSTATUS UsbGetStringDescriptor(PURB Urb) = 0;
    virtual NTSTATUS UsbBulkOrInterruptTransfer(_URB_BULK_OR_INTERRUPT_TRANSFER* Transfer, WDFREQUEST Request) = 0;
    virtual NTSTATUS UsbControlTransfer(PURB Urb) = 0;
    virtual NTSTATUS UsbClassInterface(PURB Urb) = 0;
    virtual NTSTATUS UsbGetDescriptorFromInterface(PURB Urb) = 0;
    virtual void AbortPipe() = 0;

    virtual NTSTATUS GetUserIndex(PULONG UserIndex) const { UNREFERENCED_PARAMETER(UserIndex); return STATUS_NOT_SUPPORTED; }

protected:
    virtual NTSTATUS PdoPrepareDevice(PWDFDEVICE_INIT DeviceInit, PUNICODE_STRING DeviceId, PUNICODE_STRING DeviceDesc) = 0;
    virtual NTSTATUS PdoPrepareHardware() = 0;
    virtual NTSTATUS PdoInitContext() = 0;
    virtual NTSTATUS SubmitReportImpl(PVOID NewReport) = 0;

    // usb bus interface callbacks
    static BOOLEAN USB_BUSIFFN UsbIsDeviceHighSpeed(PVOID BusContext);
    static NTSTATUS USB_BUSIFFN UsbQueryBusInformation(PVOID BusContext, ULONG Level, PVOID Buffer, PULONG BufferLength, PULONG ActualLength);
    static NTSTATUS USB_BUSIFFN UsbSubmitIsoOutUrb(PVOID BusContext, PURB Urb);
    static NTSTATUS USB_BUSIFFN UsbQueryBusTime(PVOID BusContext, PULONG CurrentFrame);
    static VOID USB_BUSIFFN UsbGetUSBDIVersion(PVOID BusContext, PUSBD_VERSION_INFORMATION VersionInfo, PULONG HcdCapabilities);

    // wdf callbacks
    static EVT_WDF_DEVICE_PREPARE_HARDWARE EvtDevicePrepareHardware;
    static EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL EvtIoInternalDeviceControl;
    static EVT_WDF_DEVICE_CONTEXT_CLEANUP EvtDeviceContextCleanup;

    static constexpr ULONG MAX_HARDWARE_ID_LENGTH = 0xFF;

    WDFDEVICE   _pdoDevice{};
    WDFQUEUE    _pendingUsbInRequests{};
    WDFQUEUE    _pendingNotificationRequests{};
    WDFQUEUE    _waitDeviceReadyRequests{};

    ULONG       _serialNo;
    DWORD       _ownerProcessId{};
    LONG        _sessionId;
    GAMEPAD_TARGET_TYPE _targetType;
    USHORT      _vendorId;
    USHORT      _productId;
    ULONG       _usbConfigDescriptorSize{};

    KEVENT      _pdoBootNotificationEvent;

    WDF_DEVICE_PNP_CAPABILITIES   _pnpCapabilities;
    WDF_DEVICE_POWER_CAPABILITIES _powerCapabilities;
};
