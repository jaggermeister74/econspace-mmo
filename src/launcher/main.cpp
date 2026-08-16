// econlauncher -- a small connection screen for EconSpace.
//
// The game itself deliberately has no offline mode and expects
// "econspace connect <host> <port>". This launcher supplies those arguments from a
// normal window and can start the bundled local server first.

// Pull in only the process-related WinAPI declarations.  These macros must precede
// winsock2.h too: it includes windows.h internally.
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// NOUSER prevents raylib/WinAPI name collisions; the one user32 call the launcher needs
// is declared explicitly instead of including all of winuser.h.
extern "C" __declspec(dllimport) BOOL WINAPI GetCursorPos(POINT* lpPoint);

#include "ui/UiTheme.h"

#include <filesystem>
#include <string>
#include <vector>

namespace
{

constexpr int WINDOW_W = 560;
constexpr int WINDOW_H = 390;
constexpr int TITLE_BAR_H = 38;

enum class Field
{
    Host,
    Port
};

std::wstring Quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

bool Spawn(const std::filesystem::path& executable, const std::vector<std::wstring>& args,
           bool hidden, std::string& error)
{
    if (!std::filesystem::exists(executable))
    {
        error = "Missing executable: " + executable.string();
        return false;
    }

    std::wstring command = Quote(executable.wstring());
    for (const std::wstring& arg : args)
        command += L" " + Quote(arg);

    STARTUPINFOW        startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);

    const DWORD flags = hidden ? CREATE_NO_WINDOW : 0;
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, flags, nullptr,
                        executable.parent_path().c_str(), &startup, &process))
    {
        error = "Could not start " + executable.filename().string() +
                " (Windows error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

std::filesystem::path SiblingExecutable(const char* folder, const char* executable)
{
    std::filesystem::path here(GetApplicationDirectory());
    here = here.lexically_normal();
    if (here.filename().empty())  // GetApplicationDirectory may retain its trailing separator.
        here = here.parent_path();

    const std::filesystem::path sibling = here.parent_path() / folder / executable;
    if (std::filesystem::exists(sibling))
        return sibling;

    // A packaged build may keep every executable in one directory.
    return here / executable;
}

void DrawInput(Rectangle bounds, const char* label, const std::string& value, bool focused)
{
    Ui::Text(label, (int)bounds.x, (int)bounds.y - 24, 16, Ui::TEXT);
    DrawRectangleRec(bounds, Fade(BLACK, 0.35f));
    DrawRectangleLinesEx(bounds, focused ? 2.0f : 1.0f, focused ? WHITE : Ui::PANEL_BORDER);
    Ui::Text(value.c_str(), (int)bounds.x + 12, (int)bounds.y + 12, 18, WHITE);

    if (focused && ((int)(GetTime() * 2.0) % 2 == 0))
    {
        const int cursorX = (int)bounds.x + 12 + Ui::TextWidth(value.c_str(), 18);
        DrawRectangle(cursorX, (int)bounds.y + 11, 2, 22, WHITE);
    }
}

bool DrawButton(Rectangle bounds, const char* label)
{
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    DrawRectangleRec(bounds, hovered ? Fade(Ui::TITLE_BG, 0.95f) : Fade(Ui::PANEL_BG, 0.95f));
    DrawRectangleLinesEx(bounds, hovered ? 2.0f : 1.0f, hovered ? WHITE : Ui::PANEL_BORDER);
    const int textSize = 18;
    Ui::Text(label, (int)(bounds.x + (bounds.width - Ui::TextWidth(label, textSize)) / 2.0f),
             (int)(bounds.y + (bounds.height - textSize) / 2.0f), textSize, WHITE);
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool IsHostCharacter(int codepoint)
{
    return (codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= '0' && codepoint <= '9') || codepoint == '.' || codepoint == '-' ||
           codepoint == ':';
}

bool ParsePort(const std::string& text, unsigned short& out)
{
    if (text.empty())
        return false;

    unsigned int value = 0;
    for (char ch : text)
    {
        if (ch < '0' || ch > '9')
            return false;
        value = value * 10 + (unsigned int)(ch - '0');
        if (value > 65535)
            return false;
    }
    if (value == 0)
        return false;

    out = (unsigned short)value;
    return true;
}

// A completed TCP connection is the useful definition of "server already running": it
// avoids spawning a second authoritative simulation just because the launcher was opened
// twice. Winsock must have been initialized by main().
bool LocalServerReady(unsigned short port)
{
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET)
        return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    const bool connected = connect(socketHandle, (sockaddr*)&address, sizeof(address)) == 0;
    closesocket(socketHandle);
    return connected;
}

}  // namespace

int main()
{
    SetConfigFlags(FLAG_WINDOW_UNDECORATED);
    InitWindow(WINDOW_W, WINDOW_H, "EconSpace Launcher");
    SetExitKey(KEY_NULL);  // Escape never terminates a project window implicitly.
    SetTargetFPS(60);
    Ui::LoadAssets();

    WSADATA winsock{};
    const bool winsockReady = WSAStartup(MAKEWORD(2, 2), &winsock) == 0;

    std::string host = "127.0.0.1";
    std::string port = "50800";
    std::string status = "Enter a server address, or start a local server.";
    Color       statusColor = Ui::TEXT_DIM;
    Field       field = Field::Host;
    bool        launchLocalPending = false;
    unsigned short pendingPort = 0;
    double      localServerDeadline = 0.0;
    double      nextLocalServerCheck = 0.0;

    const std::filesystem::path gameExe = SiblingExecutable("game", "econspace.exe");
    const std::filesystem::path serverExe = SiblingExecutable("server", "econserver.exe");

    auto launchGame = [&]() -> bool
    {
        std::string error;
        if (!Spawn(gameExe, { L"connect", std::wstring(host.begin(), host.end()),
                              std::wstring(port.begin(), port.end()) },
                   false, error))
        {
            status = error;
            statusColor = RED;
            return false;
        }
        return true;
    };

    bool exitRequested = false;
    bool draggingTitleBar = false;
    Vector2 dragOffset{};  // cursor position inside the window when a title-bar drag began
    while (!WindowShouldClose() && !exitRequested)
    {
        const int screenW = GetScreenWidth();
        const int screenH = GetScreenHeight();
        const float fieldW = (float)screenW - 120.0f;
        const Rectangle hostBox{ 60.0f, 128.0f, fieldW, 48.0f };
        const Rectangle portBox{ 60.0f, 212.0f, fieldW, 48.0f };
        const Rectangle closeButton{ (float)(screenW - TITLE_BAR_H), 0.0f,
                                     (float)TITLE_BAR_H, (float)TITLE_BAR_H };
        const Rectangle dragArea{ 0.0f, 0.0f, (float)(screenW - TITLE_BAR_H),
                                  (float)TITLE_BAR_H };

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), closeButton))
            exitRequested = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), dragArea))
        {
            POINT cursor{};
            GetCursorPos(&cursor);  // screen coordinates: unaffected by moving the window itself
            const Vector2 position = GetWindowPosition();
            dragOffset = { (float)cursor.x - position.x, (float)cursor.y - position.y };
            draggingTitleBar = true;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            draggingTitleBar = false;
        if (draggingTitleBar)
        {
            POINT cursor{};
            GetCursorPos(&cursor);
            SetWindowPosition((int)((float)cursor.x - dragOffset.x),
                              (int)((float)cursor.y - dragOffset.y));
        }

        if (IsKeyPressed(KEY_TAB))
            field = field == Field::Host ? Field::Port : Field::Host;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(GetMousePosition(), hostBox))
                field = Field::Host;
            else if (CheckCollisionPointRec(GetMousePosition(), portBox))
                field = Field::Port;
        }
        if (IsKeyPressed(KEY_BACKSPACE))
        {
            std::string& value = field == Field::Host ? host : port;
            if (!value.empty())
                value.pop_back();
        }
        for (int codepoint = GetCharPressed(); codepoint != 0; codepoint = GetCharPressed())
        {
            std::string& value = field == Field::Host ? host : port;
            const bool allowed = field == Field::Host ? IsHostCharacter(codepoint)
                                                       : (codepoint >= '0' && codepoint <= '9');
            if (allowed && value.size() < 63)
                value.push_back((char)codepoint);
        }

        if (launchLocalPending && GetTime() >= nextLocalServerCheck)
        {
            nextLocalServerCheck = GetTime() + 0.2;
            if (LocalServerReady(pendingPort))
            {
                launchLocalPending = false;
                if (launchGame())
                    exitRequested = true;
            }
            else if (GetTime() >= localServerDeadline)
            {
                launchLocalPending = false;
                status = "The local server did not start in time.";
                statusColor = RED;
            }
        }

        const Rectangle connectButton{ 60.0f, (float)screenH - 82.0f, fieldW / 2.0f - 8.0f, 46.0f };
        const Rectangle localButton{ 68.0f + fieldW / 2.0f, (float)screenH - 82.0f,
                                     fieldW / 2.0f - 8.0f, 46.0f };

        BeginDrawing();
        ClearBackground(Color{ 8, 9, 14, 255 });
        DrawRectangle(0, 0, screenW, TITLE_BAR_H, Ui::TITLE_BG);
        Ui::Text("EconSpace Launcher", 14, 10, 16, Ui::TEXT);

        const bool closeHovered = CheckCollisionPointRec(GetMousePosition(), closeButton);
        if (closeHovered)
            DrawRectangleRec(closeButton, Color{ 170, 55, 62, 255 });
        DrawLineEx({ closeButton.x + 12.0f, closeButton.y + 12.0f },
                   { closeButton.x + closeButton.width - 12.0f,
                     closeButton.y + closeButton.height - 12.0f },
                   1.5f, WHITE);
        DrawLineEx({ closeButton.x + closeButton.width - 12.0f, closeButton.y + 12.0f },
                   { closeButton.x + 12.0f, closeButton.y + closeButton.height - 12.0f },
                   1.5f, WHITE);

        Ui::Text("ECONSPACE", 60, 44, 30, WHITE);
        Ui::Text("Connect to an authoritative server", 60, 82, 17, Ui::TEXT_DIM);
        DrawInput(hostBox, "Server address", host, field == Field::Host);
        DrawInput(portBox, "Port", port, field == Field::Port);

        if (DrawButton(connectButton, "Connect") || IsKeyPressed(KEY_ENTER))
        {
            if (host.empty() || port.empty())
            {
                status = "Server address and port are required.";
                statusColor = RED;
            }
            else if (launchGame())
            {
                exitRequested = true;
            }
        }

        const bool localServerRequested = DrawButton(localButton, "Start local server");
        if (localServerRequested)
        {
            std::string error;
            unsigned short portNumber = 0;
            if (launchLocalPending)
            {
                status = "Waiting for the local server to start...";
                statusColor = Ui::ACCENT;
            }
            else if (!ParsePort(port, portNumber))
            {
                status = "Enter a port from 1 to 65535.";
                statusColor = RED;
            }
            else if (!winsockReady)
            {
                status = "Windows socket startup failed.";
                statusColor = RED;
            }
            else if (LocalServerReady(portNumber))
            {
                host = "127.0.0.1";
                status = "A local server is already running. Connecting...";
                statusColor = Ui::ACCENT;
                if (launchGame())
                    exitRequested = true;
            }
            else if (Spawn(serverExe, { L"host", std::wstring(port.begin(), port.end()) }, true, error))
            {
                host = "127.0.0.1";
                status = "Starting local server...";
                statusColor = Ui::ACCENT;
                launchLocalPending = true;
                pendingPort = portNumber;
                nextLocalServerCheck = GetTime() + 0.1;
                localServerDeadline = GetTime() + 8.0;
            }
            else
            {
                status = error;
                statusColor = RED;
            }
        }

        Ui::Text(status.c_str(), 60, screenH - 118, 15, statusColor);
        EndDrawing();
    }

    Ui::UnloadAssets();
    CloseWindow();
    if (winsockReady)
        WSACleanup();
    return 0;
}
