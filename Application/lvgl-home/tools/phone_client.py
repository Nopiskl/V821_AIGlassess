#!/usr/bin/env python3
"""Reference client for the newline JSON bridge. Uses only the Python standard library."""
import argparse
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
import time

LINE_MAX = 16384
CHUNK = 4096
TRANSFER_MAX = 32 * 1024 * 1024


class Bridge:
    def __init__(self, args):
        self.token = os.environ.get('AIGLASS_PHONE_TOKEN', '')
        self.credential = os.environ.get('AIGLASS_PHONE_CREDENTIAL', '')
        self.sequence = 0
        if args.host:
            if not args.port or len(self.token.encode()) < 16:
                raise ValueError('TCP requires --port and AIGLASS_PHONE_TOKEN (at least 16 bytes)')
            self.socket = socket.create_connection((args.host, args.port), timeout=10)
        else:
            self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.socket.settimeout(10)
            try:
                self.socket.connect(args.socket)
            except OSError:
                self.socket.close()
                raise
        self.reader = self.socket.makefile('rb')

    def close(self):
        self.reader.close()
        self.socket.close()

    def request(self, op, **fields):
        self.sequence += 1
        payload = dict(fields, op=op, id=self.sequence)
        if self.credential and op not in ('hello', 'pair'):
            payload['credential'] = self.credential
        if self.token:
            payload['token'] = self.token
        line = json.dumps(payload, ensure_ascii=False, separators=(',', ':')).encode('utf-8') + b'\n'
        if len(line) >= LINE_MAX:
            raise ValueError('Request exceeds the protocol line limit')
        self.socket.sendall(line)
        line = self.reader.readline(LINE_MAX + 1)
        if len(line) > LINE_MAX or not line.endswith(b'\n'):
            raise ValueError('Incomplete or oversized response')
        reply = json.loads(line)
        if not isinstance(reply, dict) or reply.get('id') != self.sequence:
            raise ValueError('Response id mismatch')
        if reply.get('ok') is not True:
            raise ValueError(f"Bridge error {reply.get('code')}: {reply.get('error')}")
        return reply


def put_file(bridge, source, name):
    if not name or name.startswith('.') or len(name.encode()) > 120 or any(c in name for c in '/\\\r\n'):
        raise ValueError('Invalid remote filename')
    if not source.is_file() or source.stat().st_size > TRANSFER_MAX:
        raise ValueError('Source must be a regular file of at most 32 MiB')
    offset = 0
    with source.open('rb') as stream:
        data = stream.read(CHUNK)
        while True:
            following = stream.read(CHUNK)
            if offset + len(data) > TRANSFER_MAX:
                raise ValueError('Source grew beyond 32 MiB during upload')
            reply = bridge.request('put', name=name, offset=offset, data_hex=data.hex(), final=not following)
            offset += len(data)
            if reply.get('offset') != offset:
                raise ValueError('Upload offset mismatch')
            if not following:
                return reply
            data = following


def get_file(bridge, remote, destination):
    if destination.exists():
        raise FileExistsError(f'Destination already exists: {destination}')
    fd, temporary = tempfile.mkstemp(prefix='.aiglass-', dir=destination.parent)
    offset = 0
    try:
        with os.fdopen(fd, 'wb') as stream:
            while True:
                reply = bridge.request('get', path=remote, offset=offset)
                data = bytes.fromhex(reply['data_hex'])
                if len(data) > CHUNK or offset + len(data) > TRANSFER_MAX:
                    raise ValueError('Download exceeds protocol limits')
                offset += len(data)
                if reply.get('offset') != offset or (not data and reply.get('final') is not True):
                    raise ValueError('Download made no progress or has an invalid offset')
                stream.write(data)
                if reply.get('final') is True:
                    stream.flush()
                    os.fsync(stream.fileno())
                    break
        os.link(temporary, destination)  # Publish without overwriting an existing file.
        return {'ok': True, 'path': str(destination), 'bytes': offset}
    finally:
        os.unlink(temporary)


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument('--socket', default=os.environ.get('AIGLASS_SOCKET') or './aiglass-data/bridge.sock')
    transport.add_argument('--host', help='IPv4 address; token is read from AIGLASS_PHONE_TOKEN')
    parser.add_argument('--port', type=int)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('hello', help='Discover device identity without a binding')
    pairing = commands.add_parser('pair', help='Pair using the code shown on the LVGL display')
    pairing.add_argument('code')
    commands.add_parser('unpair')
    commands.add_parser('ping')
    commands.add_parser('watch', help='Send a heartbeat every five seconds until interrupted')
    notify = commands.add_parser('notify')
    notify.add_argument('text')
    draft = commands.add_parser('input')
    draft.add_argument('target', choices=('chat', 'translate'))
    draft.add_argument('text')
    navigation = commands.add_parser('navigation')
    navigation.add_argument('text')
    navigation.add_argument('--speed', type=float, default=0)
    navigation.add_argument('--distance', type=float, default=0)
    listing = commands.add_parser('list')
    listing.add_argument('folder', choices=('inbox', 'media', 'notes'))
    upload = commands.add_parser('put')
    upload.add_argument('source', type=Path)
    upload.add_argument('--name')
    download = commands.add_parser('get')
    download.add_argument('remote', help='folder/filename')
    download.add_argument('destination', type=Path)
    return parser.parse_args()


def main():
    args = arguments()
    bridge = None
    try:
        bridge = Bridge(args)
        if args.command == 'watch':
            while True:
                print(json.dumps(bridge.request('ping'), ensure_ascii=False), flush=True)
                time.sleep(5)
        elif args.command == 'pair':
            if len(args.code) != 6 or not args.code.isascii() or not args.code.isdigit():
                raise ValueError('Pairing code must contain six ASCII digits')
            reply = bridge.request('pair', code=args.code)
        elif args.command == 'put':
            reply = put_file(bridge, args.source, args.name or args.source.name)
        elif args.command == 'get':
            reply = get_file(bridge, args.remote, args.destination)
        elif args.command == 'navigation':
            reply = bridge.request('navigation', text=args.text, speed_kmh=args.speed, distance_km=args.distance)
        elif args.command == 'input':
            reply = bridge.request('input', target=args.target, text=args.text)
        elif args.command == 'notify':
            reply = bridge.request('notify', text=args.text)
        elif args.command == 'list':
            reply = bridge.request('list', folder=args.folder)
        else:
            reply = bridge.request(args.command)
        print(json.dumps(reply, ensure_ascii=False, indent=2))
        return 0
    except KeyboardInterrupt:
        return 130
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if bridge:
            bridge.close()


if __name__ == '__main__':
    sys.exit(main())
