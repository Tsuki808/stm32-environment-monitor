from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import urllib.error
import urllib.request
import webbrowser


HOST = "127.0.0.1"
PORT = 8000
DEEPSEEK_API_URL = "https://api.deepseek.com/v1/chat/completions"


class GroundStationHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_POST(self):
        if self.path != "/api/deepseek":
            self.send_error(404, "Unknown API endpoint")
            return
        try:
            payload = self._read_json_body()
        except ValueError as exc:
            self._send_json({"error": {"message": str(exc)}}, status=400)
            return

        api_key = os.environ.get("DEEPSEEK_API_KEY", "").strip()
        if not api_key:
            self._send_json({
                "local": True,
                "choices": [{
                    "message": {
                        "role": "assistant",
                        "content": "本地 AI 面板已就绪，但当前服务器没有配置 DEEPSEEK_API_KEY。请设置环境变量后重新运行 python src/visualizer.py，即可把本面板连接到 DeepSeek。"
                    }
                }]
            })
            return

        url = os.environ.get("DEEPSEEK_API_URL", DEEPSEEK_API_URL).strip()
        request = urllib.request.Request(
            url,
            data=json.dumps(payload).encode("utf-8"),
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json"
            },
            method="POST"
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
            body = exc.read() or json.dumps({"error": {"message": exc.reason}}).encode("utf-8")
            self.send_response(exc.code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except urllib.error.URLError as exc:
            self._send_json({"error": {"message": f"DeepSeek connection failed: {exc.reason}"}}, status=502)

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


def main():
    root = Path(__file__).resolve().parent
    handler = lambda *args, **kwargs: GroundStationHandler(*args, directory=str(root), **kwargs)
    server = ThreadingHTTPServer((HOST, PORT), handler)
    url = f"http://{HOST}:{PORT}/index.html"
    print(f"Serving STM32 ground station at {url}")
    webbrowser.open(url)
    server.serve_forever()


if __name__ == "__main__":
    main()
