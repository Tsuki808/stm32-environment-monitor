from __future__ import annotations

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import urllib.error
import urllib.request
import webbrowser


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8000
DEEPSEEK_API_URL = os.environ.get("DEEPSEEK_API_URL", "https://api.deepseek.com/chat/completions").strip()
DEEPSEEK_MODEL = os.environ.get("DEEPSEEK_MODEL", "deepseek-chat").strip()
DEEPSEEK_API_KEY = os.environ.get("DEEPSEEK_API_KEY", "").strip()
SERVER_BUILD = "lite-20260531"


def deepseek_api_key() -> str:
    return DEEPSEEK_API_KEY.strip()


class GroundStationHandler(SimpleHTTPRequestHandler):
    server_version = "GroundStation/20260531"

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if self.path == "/api/deepseek/status":
            self._send_json({
                "ok": True,
                "server_build": SERVER_BUILD,
                "model": DEEPSEEK_MODEL,
                "api_url": DEEPSEEK_API_URL,
                "key_configured": bool(deepseek_api_key()),
                "target": "STM32F103 LITE USART1 115200 8N1",
            })
            return
        if self.path == "/":
            self.path = "/index.html"
        super().do_GET()

    def do_POST(self):
        if self.path != "/api/deepseek":
            self.send_error(404, "Unknown API endpoint")
            return

        try:
            payload = self._read_json_body()
        except ValueError as exc:
            self._send_json({"error": {"message": str(exc)}}, status=400)
            return

        api_key = deepseek_api_key()
        if not api_key:
            self._send_deepseek_fallback("DEEPSEEK_API_KEY is not set")
            return

        payload["model"] = DEEPSEEK_MODEL
        request = urllib.request.Request(
            DEEPSEEK_API_URL,
            data=json.dumps(payload).encode("utf-8"),
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
            method="POST",
        )

        try:
            with urllib.request.urlopen(request, timeout=45) as response:
                body = response.read()
                self.send_response(response.status)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace") or str(exc.reason)
            self._send_deepseek_fallback(f"HTTP {exc.code}: {detail[:240]}")
        except urllib.error.URLError as exc:
            self._send_deepseek_fallback(f"Connection failed: {exc.reason}")

    def _read_json_body(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 1_000_000:
            raise ValueError("Invalid JSON body length")
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise ValueError("Invalid JSON payload") from exc

    def _send_json(self, payload, status=200):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_deepseek_fallback(self, reason):
        self._send_json({
            "local": True,
            "model": DEEPSEEK_MODEL,
            "api_url": DEEPSEEK_API_URL,
            "server_build": SERVER_BUILD,
            "choices": [{
                "message": {
                    "role": "assistant",
                    "content": (
                        "DeepSeek 在线代理当前不可用，地面站已保留本地规则分析。\n\n"
                        f"原因：{reason}\n"
                        "适配目标：STM32F103 LITE，USART1 115200 8N1。"
                    ),
                }
            }],
        })


def port_available(host: str, port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.3)
        return sock.connect_ex((host, port)) != 0


def parse_args():
    parser = argparse.ArgumentParser(description="STM32F103 ground station web server.")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"Bind host, default {DEFAULT_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"Bind port, default {DEFAULT_PORT}")
    parser.add_argument("--no-browser", action="store_true", help="Do not open a browser automatically")
    return parser.parse_args()


def main():
    args = parse_args()
    root = Path(__file__).resolve().parent

    if not port_available(args.host, args.port):
        raise SystemExit(f"Port {args.host}:{args.port} is already in use. Choose another port with --port.")

    handler = partial(GroundStationHandler, directory=str(root))
    server = ThreadingHTTPServer((args.host, args.port), handler)
    url = f"http://{args.host}:{args.port}/index.html"
    print(f"Serving ground station at {url}")
    print(f"Target: STM32F103 LITE, USART1 {115200} 8N1")
    print(f"DeepSeek proxy: build={SERVER_BUILD}, model={DEEPSEEK_MODEL}, key_configured={bool(DEEPSEEK_API_KEY)}")
    if not args.no_browser:
        webbrowser.open(url)
    server.serve_forever()


if __name__ == "__main__":
    main()
