#!/usr/bin/env python3
"""Workspace-local build/run helper. No system installs and no device access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tarfile
import time
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[2]
DEBUG = ROOT / '.host-debug'
SOURCE = Path(__file__).resolve().parent


def environment():
    for name in ('tmp', 'cache', 'logs', 'reports', 'runtime', 'deps', 'tools'):
        (DEBUG / name).mkdir(parents=True, exist_ok=True)
    result = os.environ.copy()
    result.update(TMPDIR=str(DEBUG / 'tmp'), XDG_CACHE_HOME=str(DEBUG / 'cache'),
                  npm_config_cache=str(DEBUG / 'cache/npm'), PYTHONDONTWRITEBYTECODE='1',
                  PLAYWRIGHT_BROWSERS_PATH=str(DEBUG / 'tools'))
    result['PATH'] = str(DEBUG / 'tools/node/bin') + os.pathsep + result['PATH']
    return result


def prepare():
    """Download pinned source archives into this workspace only."""
    specs = json.loads((SOURCE / 'dependencies.json').read_text())
    for item in specs:
        destination = DEBUG / item['destination']
        stamp = destination / '.archive-sha256'
        if (destination / item['marker']).is_file() and stamp.is_file() and stamp.read_text().strip() == item['sha256']:
            continue
        archive = DEBUG / 'cache' / item['archive']
        if not archive.is_file():
            print('Downloading', item['url'], flush=True)
            with urlopen(item['url'], timeout=60) as response, archive.open('wb') as output:
                shutil.copyfileobj(response, output)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError(f'Archive checksum mismatch: {archive}')
        staging = DEBUG / 'deps' / (item['archive'] + '.extract')
        staging.mkdir(exist_ok=True)
        with tarfile.open(archive) as bundle:
            # Only regular files/directories are needed; ignore archive symlinks.
            for member in bundle.getmembers():
                path = (staging / member.name).resolve()
                if not path.is_relative_to(staging.resolve()):
                    raise RuntimeError('Invalid archive path')
                if member.isfile() or member.isdir():
                    bundle.extract(member, staging)
        content = staging / item['root']
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(content, destination, dirs_exist_ok=True)
        shutil.rmtree(staging)
        stamp.write_text(item['sha256'] + '\n')
    print('Dependencies ready:', DEBUG / 'deps')


def command(arguments, logfile, cwd=ROOT):
    with (DEBUG / 'logs' / logfile).open('w') as output:
        result = subprocess.run(arguments, cwd=cwd, env=environment(), stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        print((DEBUG / 'logs' / logfile).read_text()[-10000:])
        raise SystemExit(result.returncode)


def build(sanitize=False):
    prepare()
    directory = DEBUG / ('build-asan' if sanitize else 'build')
    command(['cmake', '-S', str(SOURCE), '-B', str(directory), '-DCMAKE_BUILD_TYPE=Debug', f'-DAG_SANITIZE={"ON" if sanitize else "OFF"}'], 'configure.log')
    command(['cmake', '--build', str(directory), '-j', str(min(4, os.cpu_count() or 1))], 'build.log')
    print('LVGL build:', directory / 'aiglass-host')
    npm = shutil.which('npm', path=environment()['PATH'])
    if not npm:
        raise SystemExit('Node.js >=20 is required for H5; install it under .host-debug/tools/node or use an existing PATH.')
    if not (ROOT / 'Application/mobile-app/node_modules/.package-lock.json').exists():
        command([npm, '--prefix', 'Application/mobile-app', 'ci'], 'npm-install.log')
    command([npm, '--prefix', 'Application/mobile-app', 'run', 'build:h5'], 'mobile-build.log')
    print('H5 build:', ROOT / 'Application/mobile-app/dist/build/h5')


def run(args):
    runtime = (DEBUG / args.runtime).resolve()
    if not runtime.is_relative_to(DEBUG):
        raise ValueError('运行目录必须位于 .host-debug 内')
    runtime.mkdir(parents=True, exist_ok=True)
    children = []
    stopping = False
    def stop(_signum=None, _frame=None):
        nonlocal stopping
        stopping = True
        for process in reversed(children):
            if process.poll() is None:
                process.terminate()
        for process in children:
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        with (DEBUG / 'logs/lvgl.log').open('a') as lvlog, (DEBUG / 'logs/bridge.log').open('a') as bridge_log:
            children.append(subprocess.Popen([str(DEBUG / 'build/aiglass-host'), str(args.width), str(args.height)], cwd=runtime, env=environment(), stdout=lvlog, stderr=subprocess.STDOUT))
            for _ in range(100):
                if children[0].poll() is not None:
                    raise RuntimeError('LVGL failed to start; see .host-debug/logs/lvgl.log')
                if (runtime / 'data/bridge.sock').exists() and (runtime / 'ui.sock').exists():
                    break
                if stopping:
                    return
                time.sleep(.05)
            else:
                raise RuntimeError('LVGL startup timed out; see .host-debug/logs/lvgl.log')
            if stopping:
                return
            children.append(subprocess.Popen([sys.executable, str(SOURCE / 'bridge.py'), '--port', str(args.port), '--runtime', str(runtime)], cwd=ROOT, env=environment(), stdout=bridge_log, stderr=subprocess.STDOUT))
            print(f'Forward port {args.port}: http://localhost:{args.port}/debug/', flush=True)
            while all(process.poll() is None for process in children):
                time.sleep(.2)
            if not stopping:
                raise RuntimeError('A host process exited; see .host-debug/logs/lvgl.log and bridge.log')
    finally:
        stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare', 'build', 'run'])
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--width', type=int, default=640)
    parser.add_argument('--height', type=int, default=480)
    parser.add_argument('--port', type=int, default=8787)
    parser.add_argument('--runtime', default='runtime')
    args = parser.parse_args()
    environment()
    if args.command == 'prepare': prepare()
    elif args.command == 'build': build(args.sanitize)
    else: run(args)

if __name__ == '__main__':
    main()
