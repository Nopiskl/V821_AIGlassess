#!/usr/bin/env python3
"""Run native, H5 and transport regression tests entirely inside the workspace."""
import argparse
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import time
from urllib.request import urlopen

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('host_tools', SOURCE / 'host.py')
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8788)
    parser.add_argument('--suite', choices=['all', 'native', 'browser'], default='all')
    args = parser.parse_args()
    env = host.environment()
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    node = shutil.which('node', path=env['PATH'])
    if not node:
        parser.error('Node.js is required; see ../README.md')
    def check(command, report, **extra):
        destination = host.DEBUG / 'reports' / report
        print('Running:', report, flush=True)
        with destination.open('w') as output:
            result = subprocess.run(command, cwd=host.ROOT, env={**env, **extra}, stdout=output, stderr=subprocess.STDOUT)
        print(destination.read_text()[-4500:], flush=True)
        if result.returncode:
            raise SystemExit(result.returncode)
    if args.suite != 'browser':
        check([sys.executable, str(SOURCE / 'tests/test_native.py')], 'native-tests.log')
    if args.suite == 'native':
        return
    runtime = host.DEBUG / f'browser-{time.time_ns()}'
    url = f'http://127.0.0.1:{args.port}'
    with (host.DEBUG / 'logs/test-supervisor.log').open('w') as output:
        supervisor = subprocess.Popen([sys.executable, str(SOURCE / 'host.py'), 'run', '--port', str(args.port), '--runtime', runtime.name], cwd=host.ROOT, env=env, stdout=output, stderr=subprocess.STDOUT)
        try:
            for _ in range(100):
                if supervisor.poll() is not None:
                    raise RuntimeError('Test services failed; see .host-debug/logs/test-supervisor.log')
                try:
                    # Do not accept an older service occupying this port.
                    if (runtime / 'frame.ppm').exists():
                        with urlopen(url + '/api/host/health', timeout=.5) as response:
                            if response.status == 200:
                                time.sleep(.3)
                                if supervisor.poll() is None:
                                    break
                except OSError:
                    pass
                time.sleep(.1)
            else:
                raise RuntimeError('Test service startup timeout')
            cli = 'Application/mobile-app/node_modules/@playwright/test/cli.js'
            check([node, cli, 'test', '--config', str(SOURCE / 'tests/playwright.config.cjs')], 'browser-tests.log', HOST_TEST_RUNTIME=str(runtime), HOST_TEST_URL=url)
        finally:
            supervisor.terminate()
            try:
                supervisor.wait(timeout=20)
            except subprocess.TimeoutExpired:
                supervisor.kill(); supervisor.wait()
    print(f'Host {args.suite} tests passed. Reports:', host.DEBUG / 'reports')

if __name__ == '__main__':
    main()
