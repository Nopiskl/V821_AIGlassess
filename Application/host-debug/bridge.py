#!/usr/bin/env python3
"""Loopback-only H5 -> existing LVGL Unix socket bridge; no device drivers."""
import argparse
import json
import mimetypes
import socket
import struct
import threading
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit, unquote

WORKSPACE = Path(__file__).resolve().parents[2]
WEB = Path(__file__).with_name('web')
PHONE = WORKSPACE / 'Application/mobile-app/dist/build/h5'
RPC_OPS = {'hello', 'pair', 'unpair', 'ping', 'status', 'notify', 'input', 'navigation', 'list', 'put', 'get', 'note', 'rename', 'delete', 'abort_put'}
UI_OPS = {'inspect', 'open', 'key', 'tap', 'click', 'edit'}
UI_LOCK = threading.Lock()


def unix_request(path, body):
    payload = json.dumps(body, ensure_ascii=False, separators=(',', ':')).encode() + b'\n'
    if len(payload) >= 16384:
        raise ValueError('请求超过协议上限')
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(3)
        connection.connect(str(path))
        connection.sendall(payload)
        received = bytearray()
        while b'\n' not in received:
            block = connection.recv(16384)
            if not block:
                raise ConnectionError('LVGL 连接已关闭')
            received.extend(block)
            if len(received) > 131072:
                raise ValueError('响应超过上限')
    return json.loads(received.split(b'\n', 1)[0])


def frame_png(path):
    data = path.read_bytes()
    header, dimensions, maximum, rgb = data.split(b'\n', 3)
    width, height = map(int, dimensions.split())
    if header != b'P6' or maximum != b'255' or not (320 <= width <= 1024 and 320 <= height <= 1024) or len(rgb) != width * height * 3:
        raise ValueError('LVGL 帧数据无效')
    def chunk(kind, value):
        return struct.pack('!I', len(value)) + kind + value + struct.pack('!I', zlib.crc32(kind + value) & 0xffffffff)
    rows = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 3)) + chunk(b'IEND', b'')


class Bridge(BaseHTTPRequestHandler):
    server_version = 'AiglassHost/1.0'
    def respond(self, status, data, content_type='application/json; charset=utf-8'):
        if isinstance(data, dict):
            data = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def local_request(self):
        host = urlsplit('http://' + self.headers.get('Host', '')).hostname
        origin = self.headers.get('Origin')
        return host in {'localhost', '127.0.0.1', '::1'} and (not origin or urlsplit(origin).hostname == host)

    def do_GET(self):
        if not self.local_request():
            return self.respond(403, {'ok': False, 'error': '仅允许本地端口转发访问'})
        path = urlsplit(self.path).path
        try:
            if path == '/api/host/health':
                result = unix_request(self.server.runtime / 'data/bridge.sock', {'op': 'hello'})
                return self.respond(200, result)
            if path == '/api/host/frame.png':
                return self.respond(200, frame_png(self.server.runtime / 'frame.ppm'), 'image/png')
            if path == '/debug':
                self.send_response(302); self.send_header('Location', '/debug/'); self.end_headers(); return
            root, relative = (WEB, path[len('/debug/'):]) if path.startswith('/debug/') else (PHONE, path.lstrip('/'))
            target = (root / (unquote(relative) or 'index.html')).resolve()
            if not target.is_relative_to(root.resolve()) or not target.is_file():
                return self.respond(404, {'ok': False, 'error': '页面不存在；请先构建 H5'})
            return self.respond(200, target.read_bytes(), mimetypes.guess_type(target.name)[0] or 'application/octet-stream')
        except (OSError, ValueError, ConnectionError) as error:
            return self.respond(503, {'ok': False, 'error': '主机 LVGL 服务不可用', 'detail': str(error)})

    def do_POST(self):
        if not self.local_request() or self.headers.get('X-Aiglass-Host') != '1' or not self.headers.get('Content-Type', '').startswith('application/json'):
            return self.respond(403, {'ok': False, 'error': '无效的主机调试请求'})
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length < 16384:
                return self.respond(413, {'ok': False, 'error': '请求长度无效'})
            self.connection.settimeout(5)
            payload = self.rfile.read(length)
            if len(payload) != length:
                raise ValueError('请求不完整')
            body = json.loads(payload)
            path = urlsplit(self.path).path
            allowed = RPC_OPS if path == '/api/host/rpc' else UI_OPS if path == '/api/host/ui' else set()
            if not isinstance(body, dict) or body.get('op') not in allowed:
                return self.respond(400, {'ok': False, 'error': '不支持的操作'})
            if path.endswith('/ui'):
                with UI_LOCK:
                    result = unix_request(self.server.runtime / 'ui.sock', body)
            else:
                result = unix_request(self.server.runtime / 'data/bridge.sock', body)
            return self.respond(200, result)
        except (ValueError, TypeError, json.JSONDecodeError):
            return self.respond(400, {'ok': False, 'error': '无效的 JSON 请求'})
        except OSError as error:
            return self.respond(503, {'ok': False, 'error': '主机 LVGL 服务不可用', 'detail': str(error)})

    def log_message(self, fmt, *args):
        # Request bodies and file data are deliberately not logged.
        if '/frame.png' not in self.path:
            super().log_message(fmt, *args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8787)
    parser.add_argument('--runtime', type=Path, default=WORKSPACE / '.host-debug/runtime')
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    if not runtime.is_relative_to(WORKSPACE):
        parser.error('运行目录必须位于当前工作区')
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Bridge)
    server.runtime = runtime
    server.daemon_threads = True
    print(f'Phone: http://localhost:{args.port}/ ; LVGL: http://localhost:{args.port}/debug/', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == '__main__':
    main()
