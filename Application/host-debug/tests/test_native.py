"""Compile first, then run the real LVGL/service executable in a private workspace directory."""
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[3]
DEBUG = ROOT / '.host-debug'
spec = importlib.util.spec_from_file_location('host_bridge', ROOT / 'Application/host-debug/bridge.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class HTTPFixture(BaseHTTPRequestHandler):
    """Loopback test response only; not an AI implementation or a production server."""
    requests = []
    payload = bytes(range(256)) * 20

    def respond(self, content, content_type):
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(content)))
        self.end_headers()
        self.wfile.write(content)

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append((self.path, self.headers.get('Authorization'), body))
        text = '[HTTP 测试回执] ' + body['messages'][-1]['content']
        self.respond(json.dumps({'text': text}, ensure_ascii=False).encode(), 'application/json')

    def do_GET(self):
        self.requests.append((self.path, self.headers.get('Authorization'), None))
        if self.path == '/page':
            self.respond('<h1>本机网页读取验证</h1><p>正文 &amp; 内容</p><script>hidden_fixture_script</script>'.encode(), 'text/html; charset=utf-8')
        else:
            self.respond(self.payload, 'application/octet-stream')

    def log_message(self, *_args):
        pass


class NativeIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = DEBUG / f'test-{time.time_ns()}'
        cls.runtime.mkdir(mode=0o700)
        cls.log = (cls.runtime / 'native.log').open('w')
        binary = Path(os.environ.get('AIGLASS_TEST_BINARY', DEBUG / 'build/aiglass-host'))
        env = os.environ.copy()
        env.update(TMPDIR=str(DEBUG / 'tmp'), ASAN_OPTIONS='detect_leaks=1:halt_on_error=1')
        cls.http = ThreadingHTTPServer(('127.0.0.1', 0), HTTPFixture)
        cls.http_thread = threading.Thread(target=cls.http.serve_forever, daemon=True)
        cls.http_thread.start()
        cls.base_url = f'http://127.0.0.1:{cls.http.server_port}'
        env.update(AIGLASS_CHAT_URL=cls.base_url + '/chat', AIGLASS_API_KEY='local-test-token', NO_PROXY='127.0.0.1', no_proxy='127.0.0.1')
        cls.width, cls.height = map(int, os.environ.get('AIGLASS_TEST_SIZE', '640x480').split('x'))
        cls.binary, cls.env = binary, env
        cls.credential = None
        cls.process = subprocess.Popen([str(binary), str(cls.width), str(cls.height)], cwd=cls.runtime, env=env, stdout=cls.log, stderr=subprocess.STDOUT)
        for _ in range(100):
            if cls.process.poll() is not None:
                raise RuntimeError((cls.runtime / 'native.log').read_text())
            if (cls.runtime / 'frame.ppm').exists(): break
            time.sleep(.05)
        else: raise RuntimeError('LVGL startup timeout')

    @classmethod
    def tearDownClass(cls):
        cls.process.send_signal(signal.SIGTERM)
        code = cls.process.wait(timeout=15)
        cls.log.close()
        cls.http.shutdown(); cls.http.server_close(); cls.http_thread.join()
        log = (cls.runtime / 'native.log').read_text()
        if code or 'ERROR: AddressSanitizer' in log or 'runtime error:' in log:
            raise AssertionError(f'Native shutdown failed ({code}): {log}')
        print('Native runtime artifacts:', cls.runtime)

    def rpc(self, op, **data):
        if self.credential and 'credential' not in data:
            data['credential'] = self.credential
        return bridge.unix_request(self.runtime / 'data/bridge.sock', dict(op=op, **data))

    def ui(self, op='inspect', **data):
        return bridge.unix_request(self.runtime / 'ui.sock', dict(op=op, **data))

    def labels_contain(self, text):
        for _ in range(60):
            if text in '\n'.join(self.ui()['labels']): return
            time.sleep(.05)
        self.fail(f'LVGL never displayed {text!r}: {self.ui()}')

    def test_00_first_connection_requires_pairing(self):
        self.assertEqual(self.ui()['page'], 15)
        (DEBUG / 'reports/lvgl-first-pairing.png').write_bytes(bridge.frame_png(self.runtime / 'frame.ppm'))
        hello = self.rpc('hello')
        self.assertEqual(hello['version'], 2)
        self.assertFalse(hello['paired'])
        self.assertNotIn('pairing_code', hello)
        self.assertEqual(self.rpc('notify', text='unauthorized')['code'], -13)
        code = next(label for label in self.ui()['labels'] if len(label) == 6 and label.isdigit())
        wrong = '000000' if code != '000000' else '111111'
        self.assertEqual(self.rpc('pair', code=wrong)['code'], -13)
        paired = self.rpc('pair', code=code)
        self.assertTrue(paired['ok'], paired)
        type(self).credential = paired['credential']
        self.assertEqual(len(self.credential), 64)
        self.labels_contain('全部应用')
        self.assertEqual(self.ui()['page'], 0)
        self.assertEqual((self.runtime / 'data/.phone-binding.json').stat().st_mode & 0o777, 0o600)

    def test_01_capabilities_and_real_frame(self):
        result = self.rpc('hello')
        self.assertTrue(result['ok'])
        self.assertEqual(result['runtime'], 'linux-host')
        self.assertFalse(result['capabilities']['media_control'])
        self.assertEqual(result['battery'], -1)
        png = bridge.frame_png(self.runtime / 'frame.ppm')
        self.assertTrue(png.startswith(b'\x89PNG'))
        self.assertGreater(len(png), 1000)
        self.assertEqual(self.ui()['height'], self.height)

    def test_02_upload_download_rename_delete(self):
        payload = bytes(range(256)) * 37 + '实际文件内容'.encode()
        offset = 0
        while offset < len(payload):
            part = payload[offset:offset + 4096]
            result = self.rpc('put', name='binary.dat', offset=offset, data_hex=part.hex(), final=offset + len(part) == len(payload))
            self.assertTrue(result['ok'], result)
            offset = result['offset']
            if offset < len(payload): self.assertFalse((self.runtime / 'data/inbox/binary.dat').exists())
        self.assertEqual((self.runtime / 'data/inbox/binary.dat').read_bytes(), payload)
        self.assertTrue(self.rpc('rename', path='inbox/binary.dat', name='renamed.dat')['ok'])
        result = bytearray(); offset = 0
        while True:
            reply = self.rpc('get', path='inbox/renamed.dat', offset=offset)
            result.extend(bytes.fromhex(reply['data_hex'])); offset = reply['offset']
            if reply['final']: break
        self.assertEqual(result, payload)
        self.assertTrue(self.rpc('delete', path='inbox/renamed.dat')['ok'])
        self.assertFalse((self.runtime / 'data/inbox/renamed.dat').exists())

    def test_03_partial_cancel_and_validation(self):
        self.assertTrue(self.rpc('put', name='partial.txt', offset=0, data_hex='61', final=False)['ok'])
        self.assertFalse(self.rpc('put', name='partial.txt', offset=9, data_hex='62', final=True)['ok'])
        self.assertTrue(self.rpc('abort_put', name='partial.txt')['ok'])
        self.assertFalse((self.runtime / 'data/inbox/.incoming-partial.txt.part').exists())
        for name in ['../escape', '.hidden', 'x/y', 'a' * 121]:
            self.assertFalse(self.rpc('put', name=name, offset=0, data_hex='', final=True)['ok'])
        (self.runtime / 'data/inbox/link.txt').symlink_to(self.runtime / 'native.log')
        self.assertFalse(self.rpc('get', path='inbox/link.txt', offset=0)['ok'])
        self.assertFalse(self.rpc('rename', path='inbox/link.txt', name='bad.txt')['ok'])
        self.assertFalse(self.rpc('get', path='../native.log', offset=0)['ok'])

    def test_04_messages_reach_live_lvgl(self):
        self.ui('open', page=3)
        self.assertTrue(self.rpc('notify', text='手机通知已到达真实 LVGL')['ok'])
        self.labels_contain('手机通知已到达真实 LVGL')
        self.ui('open', page=0)
        self.assertEqual(self.ui()['last_message'], '手机通知已到达真实 LVGL')
        self.labels_contain('手机通知已到达真实 LVGL')
        self.assertTrue(self.rpc('input', target='chat', text='联合调试对话草稿')['ok'])
        self.labels_contain('联合调试对话草稿')
        self.assertTrue(self.rpc('input', target='translate', text='翻译草稿检查')['ok'])
        self.labels_contain('翻译草稿检查')
        self.assertFalse(self.rpc('input', target='wrong', text='invalid')['ok'])
        self.assertFalse(self.rpc('notify', text='中' * 683)['ok'])

    def test_05_navigation_and_notes(self):
        self.assertTrue(self.rpc('navigation', text='前方 200 米右转', speed_kmh=12.5, distance_km=3.2)['ok'])
        self.ui('open', page=13); self.labels_contain('前方 200 米右转')
        self.ui('open', page=14); self.labels_contain('12.5 km/h')
        self.assertFalse(self.rpc('navigation', text='x' * 256, speed_kmh=0, distance_km=0)['ok'])
        text = '手机写入的笔记\n文件实际保存在主机。'
        self.assertTrue(self.rpc('note', name='phone-note.txt', text=text)['ok'])
        self.assertEqual((self.runtime / 'data/notes/phone-note.txt').read_text(), text)
        self.assertFalse(self.rpc('note', name='phone-note.txt', text='must not overwrite')['ok'])
        self.assertEqual((self.runtime / 'data/notes/phone-note.txt').read_text(), text)

    def test_06_native_editor_and_unavailable_hardware(self):
        self.ui('open', page=7); self.labels_contain('准备就绪')
        self.assertTrue(self.ui('click', text='编辑')['ok'])
        self.assertTrue(self.ui('edit', text='LVGL 主机编辑的真实笔记')['ok'])
        self.assertTrue(self.ui('click', text='完成')['ok'])
        time.sleep(.15)
        self.assertTrue(self.ui('click', text='保存')['ok'])
        for _ in range(40):
            file = self.runtime / 'data/notes/note.txt'
            if file.exists() and '真实笔记' in file.read_text(): break
            time.sleep(.05)
        self.assertEqual(file.read_text(), 'LVGL 主机编辑的真实笔记')
        self.ui('open', page=5); self.labels_contain('相机与麦克风不可用')
        self.assertFalse(self.ui('click', text='拍照')['ok'])
        self.assertFalse(list((self.runtime / 'data/media').iterdir()))

    def test_07_page_lifecycles_and_frames(self):
        # Exercise destruction and reconstruction, including focused controls.
        for _ in range(2):
            for page in range(16):
                self.assertTrue(self.ui('open', page=page)['ok'])
                self.assertTrue(self.ui('key', key='Tab')['ok'])
                time.sleep(.18)
                self.assertIsNone(self.process.poll())
                if _ == 0:
                    (DEBUG / f'reports/lvgl-page-{page:02d}.png').write_bytes(bridge.frame_png(self.runtime / 'frame.ppm'))
        self.ui('open', page=0)
        time.sleep(.2)
        (DEBUG / f'reports/lvgl-{self.width}x{self.height}-menu.png').write_bytes(bridge.frame_png(self.runtime / 'frame.ppm'))

    def test_08_http_text_translation_browser_and_download(self):
        for target, text in [('chat', '对话 HTTP 契约检查'), ('translate', '翻译 HTTP 契约检查')]:
            self.assertTrue(self.rpc('input', target=target, text=text)['ok'])
            self.labels_contain(text)
            self.assertTrue(self.ui('click', text='发送')['ok'])
            self.labels_contain('[HTTP 测试回执] ' + text)
        calls = [r for r in HTTPFixture.requests if r[0] == '/chat']
        self.assertEqual(len(calls), 2)
        self.assertTrue(all(r[1] == 'Bearer local-test-token' for r in calls))
        self.assertNotEqual(calls[0][2]['messages'][0]['content'], calls[1][2]['messages'][0]['content'])
        self.ui('open', page=8)
        self.labels_contain('输入网页地址')
        self.assertTrue(self.ui('click', text='输入网页地址')['ok'])
        self.assertTrue(self.ui('edit', text=self.base_url + '/page')['ok'])
        self.assertTrue(self.ui('click', text='完成')['ok'])
        time.sleep(.15)
        self.assertTrue(self.ui('click', text='打开')['ok'])
        self.labels_contain('本机网页读取验证')
        self.assertNotIn('hidden_fixture_script', '\n'.join(self.ui()['labels']))
        self.ui('open', page=10)
        self.labels_contain('download.bin')
        self.assertTrue(self.ui('click', text=self.base_url + '/page')['ok'])
        self.assertTrue(self.ui('edit', text=self.base_url + '/binary')['ok'])
        self.assertTrue(self.ui('click', text='完成')['ok'])
        time.sleep(.15)
        self.assertTrue(self.ui('click', text='下载')['ok'])
        for _ in range(60):
            matches = [f for f in (self.runtime / 'data/inbox').iterdir() if not f.name.startswith('.') and not f.is_symlink() and f.read_bytes() == HTTPFixture.payload]
            if matches: break
            time.sleep(.05)
        self.assertEqual(len(matches), 1)
        self.assertTrue(all(r[1] is None for r in HTTPFixture.requests if r[0] != '/chat'))

    def test_09_binding_persists_and_revokes(self):
        hello = self.rpc('hello')
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=15), 0)
        type(self).process = subprocess.Popen([str(self.binary), str(self.width), str(self.height)], cwd=self.runtime, env=self.env, stdout=self.log, stderr=subprocess.STDOUT)
        for _ in range(100):
            try:
                if self.rpc('status')['ok']: break
            except OSError:
                pass
            time.sleep(.05)
        self.assertEqual(self.rpc('status')['device_id'], hello['device_id'])
        self.assertTrue(self.rpc('status')['paired'])
        self.ui('open', page=15)
        self.labels_contain('解除配对')
        self.assertTrue(self.ui('click', text='解除配对')['ok'])
        self.assertTrue(self.rpc('status')['ok'])
        self.labels_contain('确认解除')
        self.assertTrue(self.ui('click', text='确认解除')['ok'])
        self.labels_contain('刷新配对码')
        self.assertEqual(self.rpc('status')['code'], -13)
        self.assertFalse(self.rpc('hello')['paired'])
        self.assertEqual(self.rpc('hello')['device_id'], hello['device_id'])
        self.assertEqual(self.ui()['last_message'], '')
        code = next(label for label in self.ui()['labels'] if len(label) == 6 and label.isdigit())
        old = self.credential
        type(self).credential = self.rpc('pair', code=code)['credential']
        self.assertNotEqual(old, self.credential)
        self.assertEqual(self.rpc('status', credential=old)['code'], -13)
        self.assertTrue(self.rpc('status')['ok'])

if __name__ == '__main__':
    unittest.main(verbosity=2)
