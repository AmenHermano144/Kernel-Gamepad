#include "Driver.h"
#include "EmulationTarget.h"

extern "C"
VOID Bus_EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    size_t bytesReturned = 0;
    auto device = WdfIoQueueGetDevice(Queue);

    switch (IoControlCode)
    {
    // ----------------------------------------------------------------
    case IOCTL_GAMEPAD_CHECK_VERSION:
    {
        PGAMEPAD_CHECK_VERSION pVersion = nullptr;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(GAMEPAD_CHECK_VERSION),
            reinterpret_cast<PVOID*>(&pVersion), nullptr);
        if (!NT_SUCCESS(status)) break;

        status = (pVersion->Version == GAMEPADCORE_VERSION) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
        break;
    }

    // ----------------------------------------------------------------
    case IOCTL_GAMEPAD_PLUGIN_TARGET:
    {
        PGAMEPAD_PLUGIN_TARGET pPlugIn = nullptr;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(GAMEPAD_PLUGIN_TARGET),
            reinterpret_cast<PVOID*>(&pPlugIn), nullptr);
        if (!NT_SUCCESS(status)) break;

        auto fileData = FileObjectGetData(WdfRequestGetFileObject(Request));
        status = Bus_PlugInDevice(device, pPlugIn, fileData->SessionId);
        break;
    }

    // ----------------------------------------------------------------
    case IOCTL_GAMEPAD_UNPLUG_TARGET:
    {
        PGAMEPAD_UNPLUG_TARGET pUnplug = nullptr;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(GAMEPAD_UNPLUG_TARGET),
            reinterpret_cast<PVOID*>(&pUnplug), nullptr);
        if (!NT_SUCCESS(status)) break;

        auto fileData = FileObjectGetData(WdfRequestGetFileObject(Request));
        status = Bus_UnPlugDevice(device, pUnplug->SerialNo, fileData->SessionId);
        break;
    }

    // ----------------------------------------------------------------
    case IOCTL_GAMEPAD_WAIT_DEVICE_READY:
    {
        PGAMEPAD_WAIT_DEVICE_READY pWait = nullptr;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(GAMEPAD_WAIT_DEVICE_READY),
            reinterpret_cast<PVOID*>(&pWait), nullptr);
        if (!NT_SUCCESS(status)) break;

        // find the target by serial (search all types)
        EmulationTarget* target = nullptr;
        bool found = EmulationTarget::FindByTypeAndSerial(device, GamepadTypeXbox360, pWait->SerialNo, &target);
        if (!found)
            found = EmulationTarget::FindByTypeAndSerial(device, GamepadTypeDS4, pWait->SerialNo, &target);

        if (!found || !target)
        {
            status = STATUS_DEVICE_DOES_NOT_EXIST;
            break;
        }

        status = target->EnqueueWaitDeviceReady(Request);
        if (NT_SUCCESS(status))
            return; // pending - don't complete
        break;
    }

    // ----------------------------------------------------------------
    case IOCTL_GAMEPAD_SUBMIT_REPORT:
    {
        PGAMEPAD_SUBMIT_REPORT pSubmit = nullptr;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(GAMEPAD_SUBMIT_REPORT),
            reinterpret_cast<PVOID*>(&pSubmit), nullptr);
        if (!NT_SUCCESS(status)) break;

        EmulationTarget* target = nullptr;
        if (!EmulationTarget::FindByTypeAndSerial(device, pSubmit->TargetType, pSubmit->SerialNo, &target))
        {
            status = STATUS_DEVICE_DOES_NOT_EXIST;
            break;
        }

        status = target->SubmitReport(pSubmit);
        break;
    }

    // ----------------------------------------------------------------
    case IOCTL_GAMEPAD_REQUEST_NOTIFICATION:
    {
        PGAMEPAD_REQUEST_NOTIFICATION pNotify = nullptr;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(GAMEPAD_REQUEST_NOTIFICATION),
            reinterpret_cast<PVOID*>(&pNotify), nullptr);
        if (!NT_SUCCESS(status)) break;

        EmulationTarget* target = nullptr;
        if (!EmulationTarget::FindByTypeAndSerial(device, pNotify->TargetType, pNotify->SerialNo, &target))
        {
            status = STATUS_DEVICE_DOES_NOT_EXIST;
            break;
        }

        status = target->EnqueueNotification(Request);
        if (NT_SUCCESS(status))
            return; // pending - don't complete
        break;
    }

    // ----------------------------------------------------------------
    case IOCTL_GAMEPAD_GET_USER_INDEX:
    {
        PGAMEPAD_GET_USER_INDEX pIndex = nullptr;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(GAMEPAD_GET_USER_INDEX),
            reinterpret_cast<PVOID*>(&pIndex), nullptr);
        if (!NT_SUCCESS(status)) break;

        PGAMEPAD_GET_USER_INDEX pOutput = nullptr;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(GAMEPAD_GET_USER_INDEX),
            reinterpret_cast<PVOID*>(&pOutput), nullptr);
        if (!NT_SUCCESS(status)) break;

        EmulationTarget* target = nullptr;
        if (!EmulationTarget::FindByTypeAndSerial(device, GamepadTypeXbox360, pIndex->SerialNo, &target))
        {
            status = STATUS_DEVICE_DOES_NOT_EXIST;
            break;
        }

        status = target->GetUserIndex(&pOutput->UserIndex);
        if (NT_SUCCESS(status))
        {
            pOutput->Size = sizeof(GAMEPAD_GET_USER_INDEX);
            pOutput->SerialNo = pIndex->SerialNo;
            bytesReturned = sizeof(GAMEPAD_GET_USER_INDEX);
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
