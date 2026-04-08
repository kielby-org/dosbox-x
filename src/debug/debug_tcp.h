#ifndef DOSBOX_DEBUG_TCP_H
#define DOSBOX_DEBUG_TCP_H

/* TCP Debug Interface
 *
 * Provides a TCP-based remote control interface for the DOSBox-X debugger.
 * External tools (MCP servers, telnet, scripts) connect and send debugger
 * commands as newline-terminated text. Responses are terminated by "---END---\n".
 *
 * Protocol:
 *   Client sends: "COMMAND args\n"
 *   Server responds: "response lines...\n---END---\n"
 *
 * Single client at a time. New connections replace existing ones.
 * Port 0 (default) disables the listener.
 */

void DEBUG_TCP_Init(void);
void DEBUG_TCP_Shutdown(void);
void DEBUG_TCP_Poll(void);

/* Response capture — called from DEBUG_ShowMsg to intercept output
 * during TCP command processing */
void DEBUG_TCP_CaptureMsg(const char* msg);
bool DEBUG_TCP_IsCapturing(void);

#endif
