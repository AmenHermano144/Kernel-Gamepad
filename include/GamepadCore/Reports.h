#pragma once

#include <pshpack1.h>

// xbox 360 button masks
#define XUSB_GAMEPAD_DPAD_UP        0x0001
#define XUSB_GAMEPAD_DPAD_DOWN      0x0002
#define XUSB_GAMEPAD_DPAD_LEFT      0x0004
#define XUSB_GAMEPAD_DPAD_RIGHT     0x0008
#define XUSB_GAMEPAD_START          0x0010
#define XUSB_GAMEPAD_BACK           0x0020
#define XUSB_GAMEPAD_LEFT_THUMB     0x0040
#define XUSB_GAMEPAD_RIGHT_THUMB    0x0080
#define XUSB_GAMEPAD_LEFT_SHOULDER  0x0100
#define XUSB_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XUSB_GAMEPAD_GUIDE          0x0400
#define XUSB_GAMEPAD_A              0x1000
#define XUSB_GAMEPAD_B              0x2000
#define XUSB_GAMEPAD_X              0x4000
#define XUSB_GAMEPAD_Y              0x8000

typedef struct _XUSB_GAMEPAD_REPORT {
    USHORT Buttons;
    UCHAR  LeftTrigger;
    UCHAR  RightTrigger;
    SHORT  ThumbLX;
    SHORT  ThumbLY;
    SHORT  ThumbRX;
    SHORT  ThumbRY;
} XUSB_GAMEPAD_REPORT, *PXUSB_GAMEPAD_REPORT;

// ds4 button masks (byte 5 of report)
#define DS4_BUTTON_SQUARE       0x0010
#define DS4_BUTTON_CROSS        0x0020
#define DS4_BUTTON_CIRCLE       0x0040
#define DS4_BUTTON_TRIANGLE     0x0080
#define DS4_BUTTON_L1           0x0100
#define DS4_BUTTON_R1           0x0200
#define DS4_BUTTON_L2           0x0400
#define DS4_BUTTON_R2           0x0800
#define DS4_BUTTON_SHARE        0x1000
#define DS4_BUTTON_OPTIONS      0x2000
#define DS4_BUTTON_L3           0x4000
#define DS4_BUTTON_R3           0x8000

// ds4 special button masks (byte 7 of report)
#define DS4_SPECIAL_PS          0x01
#define DS4_SPECIAL_TOUCHPAD    0x02

typedef struct _DS4_GAMEPAD_REPORT {
    UCHAR  ThumbLX;
    UCHAR  ThumbLY;
    UCHAR  ThumbRX;
    UCHAR  ThumbRY;
    USHORT Buttons;
    UCHAR  Special;
    UCHAR  LeftTrigger;
    UCHAR  RightTrigger;
} DS4_GAMEPAD_REPORT, *PDS4_GAMEPAD_REPORT;

// xbox 360 output (rumble feedback from game)
typedef struct _XUSB_OUTPUT_REPORT {
    UCHAR LargeMotor;
    UCHAR SmallMotor;
    UCHAR LedNumber;
} XUSB_OUTPUT_REPORT, *PXUSB_OUTPUT_REPORT;

// ds4 output (rumble + lightbar from game)
typedef struct _DS4_OUTPUT_DATA {
    UCHAR SmallMotor;
    UCHAR LargeMotor;
    UCHAR LightbarRed;
    UCHAR LightbarGreen;
    UCHAR LightbarBlue;
} DS4_OUTPUT_DATA, *PDS4_OUTPUT_DATA;

#include <poppack.h>
