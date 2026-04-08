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
#include "../hardware/serialport/misc_util.h"

/* Forward declarations — defined in debug.cpp */
extern bool ParseCommand(char* str);
extern bool IsDebuggerActive(void);
extern bool IsDebuggerRunwatch(void);
extern Bitu cycle_count;

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
