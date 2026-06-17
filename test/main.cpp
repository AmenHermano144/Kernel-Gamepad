#include <stdio.h>
#include <conio.h>
#include <GamepadCore/GamepadCoreClient.h>

void printUsage()
{
    printf("GamepadCore Test App\n");
    printf("====================\n\n");
    printf("Commands:\n");
    printf("  1 - Plug in virtual Xbox 360 controller\n");
    printf("  2 - Plug in virtual DualShock 4 controller\n");
    printf("  3 - Submit Xbox 360 test input (press A + move left stick)\n");
    printf("  4 - Submit DS4 test input (press cross + move left stick)\n");
    printf("  5 - Unplug all virtual controllers\n");
    printf("  6 - Mute physical controller (if filter installed)\n");
    printf("  7 - Unmute physical controller\n");
    printf("  8 - Read physical input (one shot)\n");
    printf("  q - Quit\n\n");
}

int main()
{
    HANDLE bus = GamepadCore::OpenBus();
    if (bus == INVALID_HANDLE_VALUE)
    {
        printf("[!] Failed to open GamepadBus. Is the driver installed?\n");
        printf("    Install: devcon install GamepadBus.inf Root\\GamepadBus\n");
        return 1;
    }

    if (!GamepadCore::CheckVersion(bus))
    {
        printf("[!] Driver version mismatch\n");
        CloseHandle(bus);
        return 1;
    }

    printf("[+] Connected to GamepadBus driver\n");

    HANDLE filter = GamepadCore::OpenFilter();
    if (filter != INVALID_HANDLE_VALUE)
        printf("[+] Connected to GamepadFilter driver\n");
    else
        printf("[*] GamepadFilter not available (physical interception disabled)\n");

    printUsage();

    ULONG nextSerial = 1;
    bool running = true;

    while (running)
    {
        printf("> ");
        int ch = _getch();
        printf("%c\n", ch);

        switch (ch)
        {
        case '1':
        {
            ULONG serial = nextSerial++;
            if (GamepadCore::PlugIn(bus, serial, GamepadTypeXbox360))
            {
                printf("[+] Xbox 360 controller plugged in (serial=%u)\n", serial);
                if (GamepadCore::WaitDeviceReady(bus, serial))
                    printf("[+] Device ready\n");
            }
            else
                printf("[!] Failed to plug in: %u\n", GetLastError());
            break;
        }
        case '2':
        {
            ULONG serial = nextSerial++;
            if (GamepadCore::PlugIn(bus, serial, GamepadTypeDS4))
            {
                printf("[+] DualShock 4 controller plugged in (serial=%u)\n", serial);
                if (GamepadCore::WaitDeviceReady(bus, serial))
                    printf("[+] Device ready\n");
            }
            else
                printf("[!] Failed to plug in: %u\n", GetLastError());
            break;
        }
        case '3':
        {
            XUSB_GAMEPAD_REPORT report = {};
            report.Buttons = XUSB_GAMEPAD_A;
            report.ThumbLX = 16384;
            report.ThumbLY = 16384;
            if (GamepadCore::SubmitXusbReport(bus, 1, report))
                printf("[+] Xbox 360 report submitted\n");
            else
                printf("[!] Submit failed: %u\n", GetLastError());
            break;
        }
        case '4':
        {
            DS4_GAMEPAD_REPORT report = {};
            report.Buttons = DS4_BUTTON_CROSS;
            report.ThumbLX = 192;
            report.ThumbLY = 64;
            report.ThumbRX = 128;
            report.ThumbRY = 128;
            if (GamepadCore::SubmitDs4Report(bus, 2, report))
                printf("[+] DS4 report submitted\n");
            else
                printf("[!] Submit failed: %u\n", GetLastError());
            break;
        }
        case '5':
        {
            if (GamepadCore::Unplug(bus, 0))
                printf("[+] All controllers unplugged\n");
            else
                printf("[!] Unplug failed: %u\n", GetLastError());
            nextSerial = 1;
            break;
        }
        case '6':
        {
            if (filter != INVALID_HANDLE_VALUE)
            {
                if (GamepadCore::SetMute(filter, true))
                    printf("[+] Physical controller muted\n");
                else
                    printf("[!] Mute failed: %u\n", GetLastError());
            }
            else
                printf("[!] No filter driver\n");
            break;
        }
        case '7':
        {
            if (filter != INVALID_HANDLE_VALUE)
            {
                if (GamepadCore::SetMute(filter, false))
                    printf("[+] Physical controller unmuted\n");
                else
                    printf("[!] Unmute failed: %u\n", GetLastError());
            }
            else
                printf("[!] No filter driver\n");
            break;
        }
        case '8':
        {
            if (filter != INVALID_HANDLE_VALUE)
            {
                OVERLAPPED ov = {};
                ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
                FILTER_INPUT_REPORT report = {};

                printf("[*] Waiting for physical input (5 sec timeout)...\n");
                GamepadCore::ReadInputAsync(filter, &report, &ov);

                if (WaitForSingleObject(ov.hEvent, 5000) == WAIT_OBJECT_0)
                {
                    DWORD transferred;
                    if (GetOverlappedResult(filter, &ov, &transferred, FALSE))
                    {
                        printf("[+] Got %u bytes from VID_%04X PID_%04X: ",
                            report.ReportLength, report.VendorId, report.ProductId);
                        for (ULONG i = 0; i < min(report.ReportLength, 20u); i++)
                            printf("%02X ", report.ReportData[i]);
                        printf("\n");
                    }
                }
                else
                    printf("[*] Timeout\n");

                CloseHandle(ov.hEvent);
            }
            else
                printf("[!] No filter driver\n");
            break;
        }
        case 'q':
        case 'Q':
            running = false;
            break;
        }
    }

    GamepadCore::Unplug(bus, 0);

    if (filter != INVALID_HANDLE_VALUE) CloseHandle(filter);
    CloseHandle(bus);

    printf("[+] Done\n");
    return 0;
}
