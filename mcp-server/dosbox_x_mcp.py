"""MCP server for DOSBox-X debugger control.

Connects to DOSBox-X's TCP debug interface and exposes tools for:
- Connection testing (ping)
- Debugger state inspection (status, regs)
- Input injection (sendkey, sendclick, sendmouse)
- Raw debugger commands (command)

Configure DOSBox-X with: -set "log tcp_debug_port=12345"
Set DOSBOX_HOST / DOSBOX_PORT env vars to override defaults.
"""

import asyncio
import os
from mcp.server.fastmcp import FastMCP

mcp = FastMCP("dosbox-x")

DOSBOX_HOST = os.environ.get("DOSBOX_HOST", "localhost")
DOSBOX_PORT = int(os.environ.get("DOSBOX_PORT", "12345"))
TIMEOUT = 5.0


class DOSBoxClient:
    """Async TCP client for DOSBox-X debug interface."""

    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None

    async def connect(self) -> None:
        self.reader, self.writer = await asyncio.wait_for(
            asyncio.open_connection(self.host, self.port),
            timeout=TIMEOUT,
        )

    async def disconnect(self) -> None:
        if self.writer:
            try:
                self.writer.close()
                await self.writer.wait_closed()
            except Exception:
                pass
        self.reader = None
        self.writer = None

    async def send_command(self, cmd: str) -> str:
        """Send a command and read response until ---END--- delimiter."""
        # Connect if needed
        if not self.writer or self.writer.is_closing():
            await self.connect()

        try:
            return await self._send_and_read(cmd)
        except (ConnectionError, asyncio.TimeoutError, OSError):
            # Reconnect once and retry
            await self.disconnect()
            await self.connect()
            return await self._send_and_read(cmd)

    async def _send_and_read(self, cmd: str) -> str:
        assert self.writer is not None and self.reader is not None
        self.writer.write((cmd + "\n").encode())
        await self.writer.drain()

        response = ""
        while True:
            line = await asyncio.wait_for(
                self.reader.readline(), timeout=TIMEOUT
            )
            if not line:
                raise ConnectionError("Connection closed by DOSBox-X")
            decoded = line.decode("utf-8", errors="replace")
            if decoded.strip() == "---END---":
                break
            response += decoded
        return response.rstrip("\n")


dosbox = DOSBoxClient(DOSBOX_HOST, DOSBOX_PORT)


def _connection_error_msg() -> str:
    return (
        f"Cannot connect to DOSBox-X at {DOSBOX_HOST}:{DOSBOX_PORT}. "
        f"Make sure DOSBox-X is running with: "
        f'-set "log tcp_debug_port={DOSBOX_PORT}"'
    )


@mcp.tool()
async def ping() -> str:
    """Test connection to DOSBox-X. Returns PONG if connected."""
    try:
        return await dosbox.send_command("PING")
    except (ConnectionError, asyncio.TimeoutError, OSError):
        return _connection_error_msg()


@mcp.tool()
async def status() -> str:
    """Get DOSBox-X debugger status.

    Returns key=value pairs: debugger state (active/inactive),
    mode (paused/running/off), CS:IP, and cycle count.
    Works regardless of whether the debugger is active.
    """
    try:
        return await dosbox.send_command("STATUS")
    except (ConnectionError, asyncio.TimeoutError, OSError):
        return _connection_error_msg()


@mcp.tool()
async def regs() -> str:
    """Get CPU register dump.

    Returns EAX-ESP, EIP, segment registers (CS/DS/ES/FS/GS/SS),
    and flags (CF/ZF/SF/OF/AF/PF/DF/IF/TF).
    Requires the debugger to be active.
    """
    try:
        return await dosbox.send_command("REGS")
    except (ConnectionError, asyncio.TimeoutError, OSError):
        return _connection_error_msg()


@mcp.tool()
async def sendkey(key: str, direction: str = "") -> str:
    """Send a keyboard event to DOSBox-X.

    Args:
        key: Key name (a-z, 0-9, f1-f12, enter, space, esc, leftshift,
             leftctrl, leftalt, up, down, left, right, tab, backspace,
             home, end, pageup, pagedown, insert, delete, etc.)
        direction: Optional. "DOWN" for key press, "UP" for key release.
                   Empty string (default) sends press then release.

    Works regardless of whether the debugger is active.
    """
    cmd = f"SENDKEY {key}"
    if direction:
        cmd += f" {direction}"
    try:
        return await dosbox.send_command(cmd)
    except (ConnectionError, asyncio.TimeoutError, OSError):
        return _connection_error_msg()


@mcp.tool()
async def sendclick(button: str) -> str:
    """Click a mouse button in DOSBox-X (press and release).

    Args:
        button: "left" or "0", "right" or "1", "middle" or "2"

    Works regardless of whether the debugger is active.
    """
    try:
        return await dosbox.send_command(f"SENDCLICK {button}")
    except (ConnectionError, asyncio.TimeoutError, OSError):
        return _connection_error_msg()


@mcp.tool()
async def sendmouse(xrel: float, yrel: float) -> str:
    """Move the mouse by relative coordinates in DOSBox-X.

    Args:
        xrel: Horizontal movement in pixels (positive = right)
        yrel: Vertical movement in pixels (positive = down)

    Works regardless of whether the debugger is active.
    """
    try:
        return await dosbox.send_command(f"SENDMOUSE {xrel} {yrel}")
    except (ConnectionError, asyncio.TimeoutError, OSError):
        return _connection_error_msg()


@mcp.tool()
async def command(cmd: str) -> str:
    """Send a raw debugger command to DOSBox-X.

    Passes the command directly to DOSBox-X's ParseCommand() dispatcher.
    This gives access to the full debugger command set (105+ commands).

    Common commands:
        HELP - list all debugger commands
        BP seg:off - set breakpoint
        BPINT intNr - interrupt breakpoint
        BPLIST - list breakpoints
        BPDEL nr - delete breakpoint
        RUN - resume execution
        CPU - CPU info
        FPU - FPU info
        SR reg val - set register
        D seg:off - set data view
        C seg:off - set code view
        MEMDUMP seg ofs num - dump memory

    Requires the debugger to be active for most commands.
    """
    try:
        return await dosbox.send_command(cmd)
    except (ConnectionError, asyncio.TimeoutError, OSError):
        return _connection_error_msg()


if __name__ == "__main__":
    mcp.run(transport="stdio")
