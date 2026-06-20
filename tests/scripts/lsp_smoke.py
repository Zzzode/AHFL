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


def find_response(messages, response_id):
    for message in messages:
        if message.get("id") == response_id:
            return message
    raise AssertionError(f"missing response id {response_id}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: lsp_smoke.py <ahfl-lsp>")

    server = sys.argv[1]
    uri = "file:///smoke.ahfl"
    source = """struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

capability Echo(value: String) -> Request;

agent TestAgent {
    input: Request;
    context: Context;
    output: Request;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}

flow for TestAgent {
    state Init {
        let x = input.value;
        goto Done;
    }
}
"""

    payload = b"".join(
        [
            frame({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}),
            frame(
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/didOpen",
                    "params": {
                        "textDocument": {
                            "uri": uri,
                            "languageId": "ahfl",
                            "version": 1,
                            "text": source,
                        }
                    },
                }
            ),
            frame(
                {
                    "jsonrpc": "2.0",
                    "id": 2,
                    "method": "textDocument/hover",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 23, "character": 25},
                    },
                }
            ),
            frame(
                {
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "textDocument/completion",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 23, "character": 22},
                    },
                }
            ),
            frame(
                {
                    "jsonrpc": "2.0",
                    "id": 4,
                    "method": "textDocument/diagnostic",
                    "params": {
                        "textDocument": {"uri": uri},
                    },
                }
            ),
            frame({"jsonrpc": "2.0", "id": 5, "method": "shutdown", "params": {}}),
        ]
    )

    result = subprocess.run([server], input=payload, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise AssertionError(result.stderr.decode("utf-8", errors="replace"))

    messages = parse_frames(result.stdout)
    diagnostic_resp = find_response(messages, 4)
    diagnostic_result = diagnostic_resp.get("result", {})
    if diagnostic_result.get("kind") != "full":
        raise AssertionError("expected full diagnostic report")
    if "items" not in diagnostic_result:
        raise AssertionError("expected diagnostic items array")
    if "resultId" not in diagnostic_result:
        raise AssertionError("expected resultId in diagnostic report")

    hover = find_response(messages, 2)
    hover_text = hover.get("result", {}).get("contents", {}).get("value", "")
    if "String" not in hover_text:
        raise AssertionError(f"expected String hover, got {hover}")

    completion = find_response(messages, 3)
    labels = {item.get("label") for item in completion.get("result", [])}
    if "value" not in labels:
        raise AssertionError(f"expected member completion, got {completion}")


if __name__ == "__main__":
    main()
