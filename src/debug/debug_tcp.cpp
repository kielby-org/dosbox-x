#include "config.h"
#include "debug_tcp.h"

#if C_DEBUG && C_MODEM

#include <string>
#include <cstring>
#include <cstdio>
#include "support.h"
#include "logging.h"
#include "setup.h"
#include "control.h"
#include "regs.h"
#include "cpu.h"
#include "keyboard.h"
#include "mouse.h"
#include "../hardware/serialport/misc_util.h"

/* Forward declarations — defined in debug.cpp */
extern bool ParseCommand(char* str);
extern bool IsDebuggerActive(void);
extern bool IsDebuggerRunwatch(void);
extern Bitu cycle_count;

/* Key name to KBD_KEYS mapping for SENDKEY command */
static const struct { const char* name; KBD_KEYS key; } key_names[] = {
    {"1",KBD_1},{"2",KBD_2},{"3",KBD_3},{"4",KBD_4},{"5",KBD_5},
    {"6",KBD_6},{"7",KBD_7},{"8",KBD_8},{"9",KBD_9},{"0",KBD_0},
    {"a",KBD_a},{"b",KBD_b},{"c",KBD_c},{"d",KBD_d},{"e",KBD_e},
    {"f",KBD_f},{"g",KBD_g},{"h",KBD_h},{"i",KBD_i},{"j",KBD_j},
    {"k",KBD_k},{"l",KBD_l},{"m",KBD_m},{"n",KBD_n},{"o",KBD_o},
    {"p",KBD_p},{"q",KBD_q},{"r",KBD_r},{"s",KBD_s},{"t",KBD_t},
    {"u",KBD_u},{"v",KBD_v},{"w",KBD_w},{"x",KBD_x},{"y",KBD_y},{"z",KBD_z},
    {"f1",KBD_f1},{"f2",KBD_f2},{"f3",KBD_f3},{"f4",KBD_f4},{"f5",KBD_f5},
    {"f6",KBD_f6},{"f7",KBD_f7},{"f8",KBD_f8},{"f9",KBD_f9},{"f10",KBD_f10},
    {"f11",KBD_f11},{"f12",KBD_f12},
    {"esc",KBD_esc},{"tab",KBD_tab},{"backspace",KBD_backspace},
    {"enter",KBD_enter},{"space",KBD_space},
    {"leftalt",KBD_leftalt},{"rightalt",KBD_rightalt},
    {"leftctrl",KBD_leftctrl},{"rightctrl",KBD_rightctrl},
    {"leftshift",KBD_leftshift},{"rightshift",KBD_rightshift},
    {"capslock",KBD_capslock},{"scrolllock",KBD_scrolllock},{"numlock",KBD_numlock},
    {"grave",KBD_grave},{"minus",KBD_minus},{"equals",KBD_equals},
    {"backslash",KBD_backslash},{"leftbracket",KBD_leftbracket},
    {"rightbracket",KBD_rightbracket},{"semicolon",KBD_semicolon},
    {"quote",KBD_quote},{"period",KBD_period},{"comma",KBD_comma},{"slash",KBD_slash},
    {"printscreen",KBD_printscreen},{"pause",KBD_pause},
    {"insert",KBD_insert},{"home",KBD_home},{"pageup",KBD_pageup},
    {"delete",KBD_delete},{"end",KBD_end},{"pagedown",KBD_pagedown},
    {"left",KBD_left},{"up",KBD_up},{"down",KBD_down},{"right",KBD_right},
    {"kp0",KBD_kp0},{"kp1",KBD_kp1},{"kp2",KBD_kp2},{"kp3",KBD_kp3},
    {"kp4",KBD_kp4},{"kp5",KBD_kp5},{"kp6",KBD_kp6},{"kp7",KBD_kp7},
    {"kp8",KBD_kp8},{"kp9",KBD_kp9},
    {"kpdivide",KBD_kpdivide},{"kpmultiply",KBD_kpmultiply},
    {"kpminus",KBD_kpminus},{"kpplus",KBD_kpplus},
    {"kpenter",KBD_kpenter},{"kpperiod",KBD_kpperiod},
    {"lwindows",KBD_lwindows},{"rwindows",KBD_rwindows},
    {nullptr, KBD_NONE}
};

static KBD_KEYS lookup_key(const char* name) {
    for (int i = 0; key_names[i].name != nullptr; i++) {
        if (strcasecmp(name, key_names[i].name) == 0)
            return key_names[i].key;
    }
    return KBD_NONE;
}

/* TCP state */
static NETServerSocket* tcp_server = nullptr;
static NETClientSocket* tcp_client = nullptr;

/* Line buffer for accumulating incoming bytes until newline */
static char line_buffer[4096];
static size_t line_pos = 0;

/* Response capture state */
static bool capturing_response = false;
static std::string response_buffer;

/* Send a string to the connected TCP client */
static bool tcp_send_string(const char* str) {
    if (!tcp_client) return false;
    size_t len = strlen(str);
    return tcp_client->SendArray(reinterpret_cast<const uint8_t*>(str), len);
}

/* Process a complete command line received from TCP */
static void tcp_process_command(char* cmd) {
    /* Trim trailing \r if present (telnet sends \r\n) */
    size_t len = strlen(cmd);
    while (len > 0 && (cmd[len-1] == '\r' || cmd[len-1] == '\n'))
        cmd[--len] = '\0';

    if (len == 0) {
        tcp_send_string("---END---\n");
        return;
    }

    /* Built-in PING for connection testing — bypasses debugger state */
    if (strcasecmp(cmd, "PING") == 0) {
        tcp_send_string("PONG\n---END---\n");
        return;
    }

    /* Built-in STATUS — always available, reports debugger state */
    if (strcasecmp(cmd, "STATUS") == 0) {
        char buf[256];
        bool active = IsDebuggerActive();
        const char* mode = "off";
        if (active && IsDebuggerRunwatch()) mode = "running";
        else if (active) mode = "paused";
        snprintf(buf, sizeof(buf),
            "debugger=%s\n"
            "mode=%s\n"
            "cs=%04X\n"
            "ip=%08X\n"
            "cycles=%u\n"
            "---END---\n",
            active ? "active" : "inactive",
            mode,
            (unsigned)SegValue(cs),
            (unsigned)reg_eip,
            (unsigned)cycle_count);
        tcp_send_string(buf);
        return;
    }

    /* Input injection commands — work regardless of debugger state */

    /* SENDKEY <keyname> [DOWN|UP] — inject keyboard event */
    if (strncasecmp(cmd, "SENDKEY ", 8) == 0) {
        char* args = cmd + 8;
        while (*args == ' ') args++;

        /* Split into key name and optional direction */
        char keyname[64] = {0};
        char direction[16] = {0};
        sscanf(args, "%63s %15s", keyname, direction);

        if (keyname[0] == '\0') {
            tcp_send_string("ERROR: Usage: SENDKEY <keyname> [DOWN|UP]\n---END---\n");
            return;
        }

        KBD_KEYS key = lookup_key(keyname);
        if (key == KBD_NONE) {
            char buf[128];
            snprintf(buf, sizeof(buf), "ERROR: Unknown key '%s'\n---END---\n", keyname);
            tcp_send_string(buf);
            return;
        }

        if (strcasecmp(direction, "DOWN") == 0) {
            KEYBOARD_AddKey(key, true);
        } else if (strcasecmp(direction, "UP") == 0) {
            KEYBOARD_AddKey(key, false);
        } else {
            /* Default: press and release */
            KEYBOARD_AddKey(key, true);
            KEYBOARD_AddKey(key, false);
        }
        tcp_send_string("OK\n---END---\n");
        return;
    }

    /* SENDCLICK <button> — click (press+release) a mouse button */
    if (strncasecmp(cmd, "SENDCLICK ", 10) == 0) {
        char* args = cmd + 10;
        while (*args == ' ') args++;

        uint8_t button;
        if (strcasecmp(args, "left") == 0 || strcmp(args, "0") == 0)
            button = 0;
        else if (strcasecmp(args, "right") == 0 || strcmp(args, "1") == 0)
            button = 1;
        else if (strcasecmp(args, "middle") == 0 || strcmp(args, "2") == 0)
            button = 2;
        else {
            tcp_send_string("ERROR: Usage: SENDCLICK <0|1|2|left|right|middle>\n---END---\n");
            return;
        }

        Mouse_ButtonPressed(button);
        Mouse_ButtonReleased(button);
        tcp_send_string("OK\n---END---\n");
        return;
    }

    /* SENDMOUSE <xrel> <yrel> — relative mouse movement */
    if (strncasecmp(cmd, "SENDMOUSE ", 10) == 0) {
        char* args = cmd + 10;
        float xrel = 0, yrel = 0;
        if (sscanf(args, "%f %f", &xrel, &yrel) != 2) {
            tcp_send_string("ERROR: Usage: SENDMOUSE <xrel> <yrel>\n---END---\n");
            return;
        }
        Mouse_CursorMoved(xrel, yrel, 0, 0, true);
        tcp_send_string("OK\n---END---\n");
        return;
    }

    /* Check if debugger is active */
    if (!IsDebuggerActive()) {
        tcp_send_string("ERROR: Debugger not active (press Alt+Pause or use -break-start)\n---END---\n");
        return;
    }

    /* Built-in REGS — text register dump (DrawRegisters writes to ncurses, not DEBUG_ShowMsg) */
    if (strcasecmp(cmd, "REGS") == 0) {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "EAX=%08X EBX=%08X ECX=%08X EDX=%08X\n"
            "ESI=%08X EDI=%08X EBP=%08X ESP=%08X\n"
            "EIP=%08X CS=%04X DS=%04X ES=%04X FS=%04X GS=%04X SS=%04X\n"
            "CF=%u ZF=%u SF=%u OF=%u AF=%u PF=%u DF=%u IF=%u TF=%u\n"
            "---END---\n",
            reg_eax, reg_ebx, reg_ecx, reg_edx,
            reg_esi, reg_edi, reg_ebp, reg_esp,
            reg_eip,
            (unsigned)SegValue(cs), (unsigned)SegValue(ds), (unsigned)SegValue(es),
            (unsigned)SegValue(fs), (unsigned)SegValue(gs), (unsigned)SegValue(ss),
            GETFLAG(CF)?1:0, GETFLAG(ZF)?1:0, GETFLAG(SF)?1:0, GETFLAG(OF)?1:0,
            GETFLAG(AF)?1:0, GETFLAG(PF)?1:0, GETFLAG(DF)?1:0, GETFLAG(IF)?1:0,
            GETFLAG(TF)?1:0);
        tcp_send_string(buf);
        return;
    }

    /* Start capturing DEBUG_ShowMsg output */
    capturing_response = true;
    response_buffer.clear();

    /* Route through the existing debugger command parser */
    if (!ParseCommand(cmd)) {
        response_buffer += "ERROR: Unknown command\n";
    }

    /* Stop capturing and send response */
    capturing_response = false;
    response_buffer += "---END---\n";
    tcp_send_string(response_buffer.c_str());
    response_buffer.clear();
}

void DEBUG_TCP_Init(void) {
    Section_prop* sect = static_cast<Section_prop*>(control->GetSection("log"));
    if (!sect) return;

    uint16_t port = (uint16_t)sect->Get_int("tcp_debug_port");
    if (port == 0) return;

    tcp_server = NETServerSocket::NETServerFactory(SOCKET_TYPE_TCP, port);
    if (!tcp_server || !tcp_server->isopen) {
        LOG_MSG("DEBUG_TCP: Failed to listen on port %u", (unsigned)port);
        delete tcp_server;
        tcp_server = nullptr;
        return;
    }

    LOG_MSG("DEBUG_TCP: Listening on port %u", (unsigned)port);
}

void DEBUG_TCP_Shutdown(void) {
    if (tcp_client) {
        delete tcp_client;
        tcp_client = nullptr;
    }
    if (tcp_server) {
        delete tcp_server;
        tcp_server = nullptr;
    }
    line_pos = 0;
    capturing_response = false;
    response_buffer.clear();
}

void DEBUG_TCP_Poll(void) {
    if (!tcp_server) return;

    /* Accept new connections (replaces existing client) */
    NETClientSocket* pending = tcp_server->Accept();
    if (pending) {
        if (tcp_client) {
            LOG_MSG("DEBUG_TCP: New connection, dropping previous client");
            delete tcp_client;
        }
        tcp_client = pending;
        line_pos = 0;
        LOG_MSG("DEBUG_TCP: Client connected");
    }

    if (!tcp_client) return;

    /* Read available bytes non-blocking */
    uint8_t byte;
    for (;;) {
        SocketState state = tcp_client->GetcharNonBlock(byte);

        if (state == SocketState::Good) {
            if (byte == '\n') {
                /* Complete line received — process it */
                line_buffer[line_pos] = '\0';
                tcp_process_command(line_buffer);
                line_pos = 0;
            } else if (line_pos < sizeof(line_buffer) - 1) {
                line_buffer[line_pos++] = (char)byte;
            }
            /* else: line too long, drop excess bytes until newline */
        } else if (state == SocketState::Empty) {
            break; /* No more data available */
        } else {
            /* SocketState::Closed */
            LOG_MSG("DEBUG_TCP: Client disconnected");
            delete tcp_client;
            tcp_client = nullptr;
            line_pos = 0;
            break;
        }
    }
}

void DEBUG_TCP_CaptureMsg(const char* msg) {
    if (capturing_response) {
        response_buffer += msg;
        response_buffer += "\n";
    }
}

bool DEBUG_TCP_IsCapturing(void) {
    return capturing_response;
}

#else /* !C_DEBUG || !C_MODEM */

void DEBUG_TCP_Init(void) {}
void DEBUG_TCP_Shutdown(void) {}
void DEBUG_TCP_Poll(void) {}
void DEBUG_TCP_CaptureMsg(const char*) {}
bool DEBUG_TCP_IsCapturing(void) { return false; }

#endif /* C_DEBUG && C_MODEM */
