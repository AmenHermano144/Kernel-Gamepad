# GamepadCore

virtual gamepad emulation (Xbox 360 / DualShock 4) and physical input interception from usermode, via two pure KMDF drivers.

## goal

windows sees the virtual devices as real USB controllers (XUSB / HID), while physical gamepads are muted at the URB level. a usermode app reads the physical input, optionally transforms it, and reinjects it through the virtual gamepad. everything is controlled by IOCTLs — there is no kernel-to-kernel dialogue.

main uses:
- physical → virtual bridge with arbitrary transformation in usermode
- testing and fuzzing of XUSB / HID stacks
- input replay / scripting 

## architecture

```
   ┌────────────────────────────────────────────────────────────┐
   │              usermode app (GamepadCoreClient.h)            │
   │  plug/unplug, physical input read, report submit           │
   └───────────────┬──────────────────────────┬─────────────────┘
                   │ IOCTL                    │ IOCTL
                   ▼                          ▼
        ┌────────────────────┐      ┌────────────────────┐
        │  GamepadBus.sys    │      │ GamepadFilter.sys  │
        │  (bus driver)      │      │  (lower filter)    │
        │                    │      │                    │
        │  creates virtual   │      │  intercepts URBs   │
        │  XUSB and DS4 PDOs │      │  from physical pad │
        └─────────┬──────────┘      └─────────┬──────────┘
                  │                           │
                  ▼                           ▼
            windows usbhub /              real physical
            xusb / hidclass               USB device
```

two drivers, separate roles:

- **`GamepadBus.sys`** — root bus driver (`Root\GamepadBus`). enumerates virtual PDOs that windows treats as legitimate USB controllers. exposes the control and submit IOCTLs.
- **`GamepadFilter.sys`** — HID lower filter. hooks onto a specific physical VID/PID (xbox 360 wired/wireless, DS4 v1/v2) and allows muting or reading its input.

### bus driver (`bus/`)

classes:
- `EmulationTarget` — base. handles PDO creation, URB dispatch (`EvtIoInternalDeviceControl`) and the USB bus interface
- `XusbTarget` — xbox 360. vendor-specific USB class, 6-phase initialization handshake, 22-byte interrupt packets, pipe handles `0xFFFF0081` / `0xFFFF0083`
- `Ds4Target` — dualshock 4. HID class, 64-byte reports, 467-byte HID descriptor, 5 ms timer-driven flush, randomized MAC address

IOCTLs (defined in `include/GamepadCore/Ioctl.h`):
- `IOCTL_GAMEPAD_CHECK_VERSION`
- `IOCTL_GAMEPAD_PLUGIN_TARGET` / `IOCTL_GAMEPAD_UNPLUG_TARGET`
- `IOCTL_GAMEPAD_WAIT_DEVICE_READY`
- `IOCTL_GAMEPAD_SUBMIT_REPORT`
- `IOCTL_GAMEPAD_REQUEST_NOTIFICATION`
- `IOCTL_GAMEPAD_GET_USER_INDEX`

### filter driver (`filter/`)

loaded as a lower filter over VID/PIDs declared in `GamepadFilter.inf`:
- xbox 360 wired (`045E:028E`), wireless receiver (`045E:0719`)
- DS4 v1 (`054C:05C4`), DS4 v2 (`054C:09CC`)

muting strategy: instead of failing the URB, **the buffer is zeroed in the completion** — the function driver sees valid neutral input and doesn't enter an error state.

IOCTLs:
- `IOCTL_FILTER_GET_DEVICE_INFO`
- `IOCTL_FILTER_SET_MUTE` / `IOCTL_FILTER_GET_MUTE`
- `IOCTL_FILTER_READ_INPUT` — asynchronous inverted call

### shared headers (`include/GamepadCore/`)

same headers for kernel and usermode:
- `Common.h` — versioning, common types
- `Reports.h` — XUSB / DS4 report layouts
- `Ioctl.h` — IOCTL codes and input/output structs
- `GamepadCoreClient.h` — header-only C++ wrapper for usermode

### test app (`test/`)

`TestApp.vcxproj` consumes `GamepadCoreClient.h` and exercises the plug → submit → unplug flow. use it as a minimal integration reference.

## build

- visual studio 2022 + WDK
- x64 only, C++17
- pure KMDF (no DMF, unlike ViGEmBus which served as reference)
- open `GamepadCore.sln`, build solution

## installation

bus driver (one time only):
```
devcon install bus\GamepadBus.inf Root\GamepadBus
```

filter driver (per physical VID/PID you want to intercept):
```
devcon install filter\GamepadFilter.inf "USB\VID_045E&PID_028E"
```

both `.sys` files must be signed or the system must be in test-signing mode.

## repo layout

```
GamepadCore/
├── bus/         GamepadBus.sys — virtual gamepad bus driver
├── filter/      GamepadFilter.sys — lower filter for physical pads
├── include/     shared kernel/usermode headers
├── test/        usermode test app
└── GamepadCore.sln
```

## notes

- bus ↔ filter communication always goes through usermode. it gives more freedom for transformation and debugging than a kernel IPC
- the USB descriptors and the init handshake were ported from ViGEmBus, but simplified by removing DMF
"# Kernel-Gamepad" 
