#pragma once

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID( \
        GamepadFilterTraceGuid, (5d9c6b2e, 4a3f, 4c8d, 8e, 1f, 7a, 9b, 2c, 0d, 5e, 3f), \
        WPP_DEFINE_BIT(TRACE_FILTER)     \
        WPP_DEFINE_BIT(TRACE_QUEUE)      \
    )
