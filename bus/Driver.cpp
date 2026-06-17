#include "Driver.h"
#include "EmulationTarget.h"

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
    WDF_DRIVER_CONFIG_INIT(&config, Bus_EvtDeviceAdd);

    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

extern "C"
NTSTATUS Bus_EvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    NTSTATUS status;

    // set device type
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);

    // child list config
    WDF_CHILD_LIST_CONFIG childListConfig;
    WDF_CHILD_LIST_CONFIG_INIT(
        &childListConfig,
        sizeof(PDO_IDENTIFICATION_DESCRIPTION),
        Bus_EvtDeviceListCreatePdo
    );
    childListConfig.EvtChildListIdentificationDescriptionCompare = Bus_EvtChildListCompare;
    WdfFdoInitSetDefaultChildListConfig(DeviceInit, &childListConfig, WDF_NO_OBJECT_ATTRIBUTES);

    // file object config for session tracking
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, Bus_DeviceFileCreate, Bus_FileClose, WDF_NO_EVENT_CALLBACK);

    WDF_OBJECT_ATTRIBUTES fileAttribs;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fileAttribs, FDO_FILE_DATA);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &fileAttribs);

    // create fdo
    WDF_OBJECT_ATTRIBUTES fdoAttribs;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttribs, FDO_DEVICE_DATA);

    WDFDEVICE device;
    status = WdfDeviceCreate(&DeviceInit, &fdoAttribs, &device);
    if (!NT_SUCCESS(status))
        return status;

    auto fdoData = FdoGetData(device);
    fdoData->InterfaceReferenceCounter = 0;
    fdoData->NextSessionId = 1;

    // device interface for usermode
    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_GAMEPADBUS, nullptr);
    if (!NT_SUCCESS(status))
        return status;

    // report as usb bus
    PNP_BUS_INFORMATION busInfo;
    busInfo.BusTypeGuid = GUID_BUS_TYPE_USB;
    busInfo.LegacyBusType = PNPBus;
    busInfo.BusNumber = 0;
    WdfDeviceSetBusInformationForChildren(device, &busInfo);

    // default io queue for ioctls
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = Bus_EvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
        return status;

    return STATUS_SUCCESS;
}

extern "C"
VOID Bus_DeviceFileCreate(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject)
{
    auto fdoData = FdoGetData(Device);
    auto fileData = FileObjectGetData(FileObject);

    fileData->SessionId = InterlockedIncrement(&fdoData->NextSessionId);
    InterlockedIncrement(&fdoData->InterfaceReferenceCounter);

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

extern "C"
VOID Bus_FileClose(
    _In_ WDFFILEOBJECT FileObject)
{
    auto fileData = FileObjectGetData(FileObject);
    auto device = WdfFileObjectGetDevice(FileObject);
    auto fdoData = FdoGetData(device);

    InterlockedDecrement(&fdoData->InterfaceReferenceCounter);

    // unplug all devices owned by this session
    Bus_UnPlugDevice(device, 0, fileData->SessionId);
}

extern "C"
BOOLEAN Bus_EvtChildListCompare(
    _In_ WDFCHILDLIST DeviceList,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER FirstIdentificationDescription,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER SecondIdentificationDescription)
{
    UNREFERENCED_PARAMETER(DeviceList);

    auto first = CONTAINING_RECORD(FirstIdentificationDescription, PDO_IDENTIFICATION_DESCRIPTION, Header);
    auto second = CONTAINING_RECORD(SecondIdentificationDescription, PDO_IDENTIFICATION_DESCRIPTION, Header);

    return (first->SerialNo == second->SerialNo) ? TRUE : FALSE;
}
