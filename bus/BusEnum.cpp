#include "Driver.h"
#include "EmulationTarget.h"
#include "XusbTarget.h"
#include "Ds4Target.h"

NTSTATUS Bus_PlugInDevice(
    _In_ WDFDEVICE Device,
    _In_ PGAMEPAD_PLUGIN_TARGET PlugIn,
    _In_ LONG SessionId)
{
    if (PlugIn->SerialNo == 0)
        return STATUS_INVALID_PARAMETER;

    // create target object
    EmulationTarget* target = nullptr;

    switch (PlugIn->TargetType)
    {
    case GamepadTypeXbox360:
        target = new XusbTarget(
            PlugIn->SerialNo, SessionId,
            PlugIn->VendorId ? PlugIn->VendorId : 0x045E,
            PlugIn->ProductId ? PlugIn->ProductId : 0x028E);
        break;

    case GamepadTypeDS4:
        target = new Ds4Target(
            PlugIn->SerialNo, SessionId,
            PlugIn->VendorId ? PlugIn->VendorId : 0x054C,
            PlugIn->ProductId ? PlugIn->ProductId : 0x05C4);
        break;

    default:
        return STATUS_INVALID_PARAMETER;
    }

    if (!target)
        return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS status = target->Prepare(Device);
    if (!NT_SUCCESS(status))
    {
        delete target;
        return status;
    }

    // add to child list
    PDO_IDENTIFICATION_DESCRIPTION description;
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(PDO_IDENTIFICATION_DESCRIPTION));
    description.SerialNo = PlugIn->SerialNo;
    description.SessionId = SessionId;
    description.Target = target;

    status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
        WdfFdoGetDefaultChildList(Device),
        &description.Header,
        nullptr);

    if (!NT_SUCCESS(status))
    {
        delete target;
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS Bus_UnPlugDevice(
    _In_ WDFDEVICE Device,
    _In_ ULONG SerialNo,
    _In_ LONG SessionId)
{
    WDFCHILDLIST childList = WdfFdoGetDefaultChildList(Device);

    WDF_CHILD_LIST_ITERATOR iterator;
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator, WdfRetrievePresentChildren);

    WdfChildListBeginIteration(childList, &iterator);

    WDF_CHILD_RETRIEVE_INFO childInfo;
    PDO_IDENTIFICATION_DESCRIPTION description;

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(PDO_IDENTIFICATION_DESCRIPTION));
    WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &description.Header);

    NTSTATUS iterStatus;
    while (NT_SUCCESS(iterStatus = WdfChildListRetrieveNextDevice(childList, &iterator, nullptr, &childInfo)))
    {
        if (childInfo.Status != WdfChildListRetrieveDeviceSuccess)
        {
            WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(PDO_IDENTIFICATION_DESCRIPTION));
            WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &description.Header);
            continue;
        }

        // match session, and optionally serial (0 = all)
        if (description.SessionId == SessionId &&
            (SerialNo == 0 || description.SerialNo == SerialNo))
        {
            WdfChildListUpdateChildDescriptionAsMissing(childList, &description.Header);
        }

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(PDO_IDENTIFICATION_DESCRIPTION));
        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &description.Header);
    }

    WdfChildListEndIteration(childList, &iterator);

    return STATUS_SUCCESS;
}

// called by wdf when a new pdo needs to be created
extern "C"
NTSTATUS Bus_EvtDeviceListCreatePdo(
    _In_ WDFCHILDLIST DeviceList,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    _In_ PWDFDEVICE_INIT ChildInit)
{
    auto description = CONTAINING_RECORD(IdentificationDescription, PDO_IDENTIFICATION_DESCRIPTION, Header);

    if (!description->Target)
        return STATUS_INVALID_PARAMETER;

    return description->Target->CreatePdoDevice(WdfChildListGetDevice(DeviceList), ChildInit);
}
