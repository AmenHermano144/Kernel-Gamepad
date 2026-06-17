# GamepadCore

emulación de gamepads virtuales (Xbox 360 / DualShock 4) e interceptación del input físico desde modo usuario, vía dos drivers KMDF puros.

## objetivo

windows ve los dispositivos virtuales como controladores USB reales (XUSB / HID), mientras los gamepads físicos quedan silenciados a nivel de URB. desde una app en usermode se lee el input físico, opcionalmente se transforma, y se reinyecta a través del gamepad virtual. todo se controla por IOCTLs — no hay diálogo kernel-a-kernel.

usos principales:
- bridge físico → virtual con transformación arbitraria en usermode
- testing y fuzzing de stacks XUSB / HID
- input replay / scripting para investigación de anti-cheat

## arquitectura

```
   ┌────────────────────────────────────────────────────────────┐
   │              app usermode (GamepadCoreClient.h)            │
   │  plug/unplug, lectura de input físico, submit de reportes  │
   └───────────────┬──────────────────────────┬─────────────────┘
                   │ IOCTL                    │ IOCTL
                   ▼                          ▼
        ┌────────────────────┐      ┌────────────────────┐
        │  GamepadBus.sys    │      │ GamepadFilter.sys  │
        │  (bus driver)      │      │  (lower filter)    │
        │                    │      │                    │
        │  crea PDOs XUSB    │      │  intercepta URBs   │
        │  y DS4 virtuales   │      │  del gamepad físico│
        └─────────┬──────────┘      └─────────┬──────────┘
                  │                           │
                  ▼                           ▼
            windows usbhub /              dispositivo USB
            xusb / hidclass               físico real
```

dos drivers, roles separados:

- **`GamepadBus.sys`** — bus driver raíz (`Root\GamepadBus`). enumera PDOs virtuales que windows trata como controladores USB legítimos. expone los IOCTLs de control y submit.
- **`GamepadFilter.sys`** — lower filter HID. se engancha a un VID/PID físico concreto (xbox 360 wired/wireless, DS4 v1/v2) y permite mutear o leer su input.

### bus driver (`bus/`)

clases:
- `EmulationTarget` — base. maneja creación del PDO, dispatch de URBs (`EvtIoInternalDeviceControl`) y la USB bus interface
- `XusbTarget` — xbox 360. clase USB vendor-specific, handshake de inicialización en 6 fases, paquetes interrupt de 22 bytes, pipe handles `0xFFFF0081` / `0xFFFF0083`
- `Ds4Target` — dualshock 4. clase HID, reportes de 64 bytes, descriptor HID de 467 bytes, flush por timer de 5 ms, MAC address aleatoria

IOCTLs (definidos en `include/GamepadCore/Ioctl.h`):
- `IOCTL_GAMEPAD_CHECK_VERSION`
- `IOCTL_GAMEPAD_PLUGIN_TARGET` / `IOCTL_GAMEPAD_UNPLUG_TARGET`
- `IOCTL_GAMEPAD_WAIT_DEVICE_READY`
- `IOCTL_GAMEPAD_SUBMIT_REPORT`
- `IOCTL_GAMEPAD_REQUEST_NOTIFICATION`
- `IOCTL_GAMEPAD_GET_USER_INDEX`

### filter driver (`filter/`)

se carga como lower filter sobre VID/PIDs declarados en `GamepadFilter.inf`:
- xbox 360 wired (`045E:028E`), wireless receiver (`045E:0719`)
- DS4 v1 (`054C:05C4`), DS4 v2 (`054C:09CC`)

estrategia de muting: en vez de fallar la URB, **cero­ea el buffer en el completion** — el function driver ve input neutro válido y no entra en estado de error.

IOCTLs:
- `IOCTL_FILTER_GET_DEVICE_INFO`
- `IOCTL_FILTER_SET_MUTE` / `IOCTL_FILTER_GET_MUTE`
- `IOCTL_FILTER_READ_INPUT` — inverted call asíncrona

### headers compartidos (`include/GamepadCore/`)

mismos headers para kernel y usermode:
- `Common.h` — versionado, tipos comunes
- `Reports.h` — layouts de los reportes XUSB / DS4
- `Ioctl.h` — códigos IOCTL y structs de entrada/salida
- `GamepadCoreClient.h` — wrapper C++ header-only para usermode

### app de prueba (`test/`)

`TestApp.vcxproj` consume `GamepadCoreClient.h` y ejercita el flujo plug → submit → unplug. usar como referencia mínima de integración.

## build

- visual studio 2022 + WDK
- x64 únicamente, C++17
- KMDF puro (sin DMF, a diferencia de ViGEmBus que sirvió como referencia)
- abrir `GamepadCore.sln`, build solution

## instalación

bus driver (una sola vez):
```
devcon install bus\GamepadBus.inf Root\GamepadBus
```

filter driver (por VID/PID físico que se quiera interceptar):
```
devcon install filter\GamepadFilter.inf "USB\VID_045E&PID_028E"
```

ambos `.sys` deben estar firmados o el sistema en test-signing.

## layout del repo

```
GamepadCore/
├── bus/         GamepadBus.sys — driver bus de gamepads virtuales
├── filter/      GamepadFilter.sys — lower filter para físicos
├── include/     headers compartidos kernel/usermode
├── test/        app de prueba en usermode
└── GamepadCore.sln
```

## notas

- la comunicación bus ↔ filter pasa siempre por usermode. da más libertad de transformación y debugging que un IPC kernel
- los descriptores USB y el handshake de inicio se portaron de ViGEmBus (`Z:\!NIMRODCORE\DRIVERS\SOURCES\Controller\ViGEmBus-master`), pero simplificados al quitar DMF
