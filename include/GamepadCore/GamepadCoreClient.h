#pragma once

// usermode only - header-only client library for GamepadCore drivers

#ifndef _KERNEL_MODE

#include <Windows.h>
#include <SetupAPI.h>
#include <initguid.h>
#include <cfgmgr32.h>
#include <string>
#include <vector>

#include "Common.h"
#include "Ioctl.h"

#pragma comment(lib, "setupapi.lib")

namespace GamepadCore {

inline HANDLE OpenDeviceInterface(const GUID& interfaceGuid, int index = 0)
{
    HDEVINFO devInfo = SetupDiGetClassDevs(&interfaceGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    SP_DEVICE_INTERFACE_DATA ifaceData = {};
    ifaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiEnumDeviceInterfaces(devInfo, nullptr, &interfaceGuid, index, &ifaceData))
    {
        SetupDiDestroyDeviceInfoList(devInfo);
        return INVALID_HANDLE_VALUE;
    }

    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetail(devInfo, &ifaceData, nullptr, 0, &requiredSize, nullptr);

    auto detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(malloc(requiredSize));
    if (!detailData)
    {
        SetupDiDestroyDeviceInfoList(devInfo);
        return INVALID_HANDLE_VALUE;
    }
    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    HANDLE handle = INVALID_HANDLE_VALUE;
    if (SetupDiGetDeviceInterfaceDetail(devInfo, &ifaceData, detailData, requiredSize, nullptr, nullptr))
    {
        handle = CreateFile(detailData->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            nullptr);
    }

    free(detailData);
    SetupDiDestroyDeviceInfoList(devInfo);
    return handle;
}

inline HANDLE OpenBus(int index = 0)
{
    return OpenDeviceInterface(GUID_DEVINTERFACE_GAMEPADBUS, index);
}

inline HANDLE OpenFilter(int index = 0)
{
    return OpenDeviceInterface(GUID_DEVINTERFACE_GAMEPADFILTER, index);
}

inline bool CheckVersion(HANDLE bus)
{
    GAMEPAD_CHECK_VERSION ver = {};
    ver.Size = sizeof(ver);
    ver.Version = GAMEPADCORE_VERSION;
    DWORD ret;
    return DeviceIoControl(bus, IOCTL_GAMEPAD_CHECK_VERSION, &ver, sizeof(ver), nullptr, 0, &ret, nullptr);
}

inline bool PlugIn(HANDLE bus, ULONG serial, GAMEPAD_TARGET_TYPE type,
    USHORT vendorId = 0, USHORT productId = 0)
{
    GAMEPAD_PLUGIN_TARGET req = {};
    req.Size = sizeof(req);
    req.SerialNo = serial;
    req.TargetType = type;
    req.VendorId = vendorId;
    req.ProductId = productId;
    DWORD ret;
    return DeviceIoControl(bus, IOCTL_GAMEPAD_PLUGIN_TARGET, &req, sizeof(req), nullptr, 0, &ret, nullptr);
}

inline bool Unplug(HANDLE bus, ULONG serial = 0)
{
    GAMEPAD_UNPLUG_TARGET req = {};
    req.Size = sizeof(req);
    req.SerialNo = serial;
    DWORD ret;
    return DeviceIoControl(bus, IOCTL_GAMEPAD_UNPLUG_TARGET, &req, sizeof(req), nullptr, 0, &ret, nullptr);
}

inline bool WaitDeviceReady(HANDLE bus, ULONG serial)
{
    GAMEPAD_WAIT_DEVICE_READY req = {};
    req.Size = sizeof(req);
    req.SerialNo = serial;
    DWORD ret;
    return DeviceIoControl(bus, IOCTL_GAMEPAD_WAIT_DEVICE_READY, &req, sizeof(req), nullptr, 0, &ret, nullptr);
}

inline bool SubmitXusbReport(HANDLE bus, ULONG serial, const XUSB_GAMEPAD_REPORT& report)
{
    GAMEPAD_SUBMIT_REPORT req = {};
    req.Size = sizeof(req);
    req.SerialNo = serial;
    req.TargetType = GamepadTypeXbox360;
    req.Report.Xusb = report;
    DWORD ret;
    return DeviceIoControl(bus, IOCTL_GAMEPAD_SUBMIT_REPORT, &req, sizeof(req), nullptr, 0, &ret, nullptr);
}

inline bool SubmitDs4Report(HANDLE bus, ULONG serial, const DS4_GAMEPAD_REPORT& report)
{
    GAMEPAD_SUBMIT_REPORT req = {};
    req.Size = sizeof(req);
    req.SerialNo = serial;
    req.TargetType = GamepadTypeDS4;
    req.Report.Ds4 = report;
    DWORD ret;
    return DeviceIoControl(bus, IOCTL_GAMEPAD_SUBMIT_REPORT, &req, sizeof(req), nullptr, 0, &ret, nullptr);
}

inline bool RequestNotification(HANDLE bus, ULONG serial, GAMEPAD_TARGET_TYPE type,
    GAMEPAD_NOTIFICATION* outNotify, LPOVERLAPPED overlapped = nullptr)
{
    GAMEPAD_REQUEST_NOTIFICATION req = {};
    req.Size = sizeof(req);
    req.SerialNo = serial;
    req.TargetType = type;
    DWORD ret;
    return DeviceIoControl(bus, IOCTL_GAMEPAD_REQUEST_NOTIFICATION,
        &req, sizeof(req), outNotify, sizeof(GAMEPAD_NOTIFICATION), &ret, overlapped);
}

inline bool GetUserIndex(HANDLE bus, ULONG serial, ULONG* outIndex)
{
    GAMEPAD_GET_USER_INDEX req = {};
    req.Size = sizeof(req);
    req.SerialNo = serial;
    GAMEPAD_GET_USER_INDEX resp = {};
    DWORD ret;
    if (DeviceIoControl(bus, IOCTL_GAMEPAD_GET_USER_INDEX, &req, sizeof(req), &resp, sizeof(resp), &ret, nullptr))
    {
        *outIndex = resp.UserIndex;
        return true;
    }
    return false;
}

// filter operations
inline bool SetMute(HANDLE filter, bool mute)
{
    FILTER_SET_MUTE req = {};
    req.Size = sizeof(req);
    req.Mute = mute ? TRUE : FALSE;
    DWORD ret;
    return DeviceIoControl(filter, IOCTL_FILTER_SET_MUTE, &req, sizeof(req), nullptr, 0, &ret, nullptr);
}

inline bool GetMute(HANDLE filter, bool* outMuted)
{
    FILTER_GET_MUTE resp = {};
    DWORD ret;
    if (DeviceIoControl(filter, IOCTL_FILTER_GET_MUTE, nullptr, 0, &resp, sizeof(resp), &ret, nullptr))
    {
        *outMuted = resp.IsMuted ? true : false;
        return true;
    }
    return false;
}

inline bool ReadInputAsync(HANDLE filter, FILTER_INPUT_REPORT* outReport, LPOVERLAPPED overlapped)
{
    return DeviceIoControl(filter, IOCTL_FILTER_READ_INPUT,
        nullptr, 0, outReport, sizeof(FILTER_INPUT_REPORT), nullptr, overlapped);
}

inline bool GetDeviceInfo(HANDLE filter, FILTER_DEVICE_INFO* outInfo)
{
    DWORD ret;
    return DeviceIoControl(filter, IOCTL_FILTER_GET_DEVICE_INFO,
        nullptr, 0, outInfo, sizeof(FILTER_DEVICE_INFO), &ret, nullptr);
}

} // namespace GamepadCore

#endif // !_KERNEL_MODE
