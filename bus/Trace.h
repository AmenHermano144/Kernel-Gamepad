#pragma once

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID( \
        GamepadBusTraceGuid, (3c8c5a1e, 2b4f, 4a3d, 9e, 7c, 5f, 6a, 8b, 1d, 3e, 0f), \
        WPP_DEFINE_BIT(TRACE_DRIVER)     \
        WPP_DEFINE_BIT(TRACE_BUSENUM)    \
        WPP_DEFINE_BIT(TRACE_QUEUE)      \
        WPP_DEFINE_BIT(TRACE_USBPDO)     \
        WPP_DEFINE_BIT(TRACE_XUSB)       \
        WPP_DEFINE_BIT(TRACE_DS4)        \
    )
