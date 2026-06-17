#!/usr/bin/env python3
import json
import subprocess
import sys


def frame(payload):
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    return b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n" + body


def parse_frames(data):
    messages = []
    cursor = 0
    while cursor < len(data):
        header_end = data.find(b"\r\n\r\n", cursor)
        if header_end < 0:
            break
        header = data[cursor:header_end].decode("ascii")
        length = None
        for line in header.split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
        if length is None:
            raise AssertionError("missing Content-Length")
        body_start = header_end + 4
        body_end = body_start + length
        messages.append(json.loads(data[body_start:body_end].decode("utf-8")))
        cursor = body_end
    return messages


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: dap_smoke.py <ahfl-dap>")

    server = sys.argv[1]
    payload = b"".join(
        [
            frame({"seq": 1, "type": "request", "command": "initialize"}),
            frame({"seq": 2, "type": "request", "command": "disconnect"}),
        ]
    )

    result = subprocess.run([server], input=payload, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise AssertionError(result.stderr.decode("utf-8", errors="replace"))

    stderr = result.stderr.decode("utf-8", errors="replace")
    if stderr:
        raise AssertionError(f"unexpected stderr: {stderr!r}")

    messages = parse_frames(result.stdout)
    commands = [message.get("command") for message in messages]
    if commands != ["initialize", "disconnect"]:
        raise AssertionError(f"unexpected DAP response commands: {commands!r}")

    initialize = messages[0]
    body = initialize.get("body", {})
    if body.get("supportsConfigurationDoneRequest") is not True:
        raise AssertionError(f"missing initialize capabilities: {initialize!r}")


if __name__ == "__main__":
    main()
