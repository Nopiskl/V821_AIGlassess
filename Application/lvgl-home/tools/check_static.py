#!/usr/bin/env python3
"""Check sources and dependency interfaces without linking or running the application."""
import argparse
import ast
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['aiglass_home.c', 'aiglass_game.c', 'aiglass_services.c',
           'aiglass_pairing.c', 'aiglass_http.c', 'aiglass_phone.c', 'aiglass_media.c',
           'fonts/aiglass_font_16.c', 'third_party/cJSON.c']
APP_ROOT = ROOT.parent / 'aiglass-apps'
MEDIA_ROOT = APP_ROOT / 'apps/src/media'
LOCAL_MEDIA_ROOT = ROOT.parent / 'mpp-media'


def check_text():
    count = 0
    for path in sorted(ROOT.rglob('*')):
        if not path.is_file() or path.suffix not in ('.c', '.h', '.py', '.md'):
            continue
        if 'third_party' in path.parts or path.name == 'aiglass_font_16.c':
            continue
        content = path.read_text(encoding='utf-8')
        if not content.endswith('\n'):
            raise ValueError(f'{path.relative_to(ROOT)}: missing final newline')
        for number, line in enumerate(content.splitlines(), 1):
            if line != line.rstrip() or '\t' in line:
                raise ValueError(f'{path.relative_to(ROOT)}:{number}: trailing whitespace or tab')
        if path.suffix == '.py':
            ast.parse(content, filename=str(path))
        if path.suffix == '.md':
            for target in re.findall(r'\]\(([^)]+)\)', content):
                if '://' not in target and not target.startswith('#'):
                    local = target.split('#', 1)[0]
                    if not (path.parent / local).exists():
                        raise ValueError(f'{path.relative_to(ROOT)}: missing link target {target}')
        count += 1
    print(f'PASS: UTF-8, whitespace, Python AST and local documentation links ({count} files)', flush=True)


def check_font():
    content = (ROOT / 'fonts/aiglass_font_16.c').read_text(encoding='utf-8')
    points = set()
    mappings = re.findall(r'\.range_start=(\d+).*?\.unicode_list=unicode_(\d+)', content)
    if not mappings:
        raise ValueError('Font cmap table is missing')
    for start, group in mappings:
        match = re.search(r'unicode_' + group + r'\[\]\s*=\s*\{([^}]+)\}', content)
        if not match:
            raise ValueError(f'Font unicode_{group} table is missing')
        points.update(int(start) + int(offset) for offset in re.findall(r'\d+', match[1]))
    for name in ('aiglass_home.c', 'aiglass_game.c'):
        literals = re.findall(r'"(?:[^"\\]|\\.)*"', (ROOT / name).read_text(encoding='utf-8'))
        missing = sorted({ch for text in literals for ch in text if ord(ch) > 127 and ord(ch) not in points})
        if missing:
            raise ValueError(f'{name}: font lacks {missing}')
    print(f'PASS: built-in UI text covered by {len(points)} font code points', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lvgl', required=True, type=Path, help='LVGL 8.3/8.4 source root containing lvgl.h')
    parser.add_argument('--curl-include', required=True, type=Path, help='Include directory containing curl/curl.h')
    parser.add_argument('--cc', default='gcc', help='C compiler executable; only -fsyntax-only is used')
    parser.add_argument('--aglink-include', type=Path, help='Real BSP UAPI root containing aglink/ag_proto.h')
    parser.add_argument('--vendor-syntax', action='store_true', help='Parse vendor C grammar with tree-sitter; no MPP type checking')
    args = parser.parse_args()
    try:
        for header in (args.lvgl / 'lvgl.h', args.curl_include / 'curl/curl.h'):
            if not header.is_file():
                raise ValueError(f'Missing dependency header: {header}')
        check_text()
        check_font()
        command = [args.cc, '-std=c11', '-fsyntax-only', '-Wall', '-Wextra', '-Werror',
                   '-Wformat=2', '-Wshadow', '-pthread', '-I', str(args.lvgl.resolve()),
                   '-I', str(args.curl_include.resolve()), '-I', str(LOCAL_MEDIA_ROOT),
                   '-DLV_CONF_SKIP=1', '-DLV_USE_USER_DATA=1']
        subprocess.run(command + [str(ROOT / name) for name in SOURCES], check=True)
        print(f'PASS: C11 syntax and warnings for {len(SOURCES)} C sources; no objects or executable generated')
        if args.aglink_include:
            if not (args.aglink_include / 'aglink/ag_proto.h').is_file():
                raise ValueError('Missing real BSP aglink/ag_proto.h')
            subprocess.run(command + ['-I', str(args.aglink_include.resolve()), str(MEDIA_ROOT / 'ag_media.c'),
                                      str(APP_ROOT / 'integration/protocol_contract.c')], check=True)
            print('PASS: controlled media API and protocol layout checked against the real AGLink UAPI')
        else:
            print('SKIP: AGLink media implementation type check (supply --aglink-include)')
        if args.vendor_syntax:
            try:
                from tree_sitter import Language, Parser
                import tree_sitter_c
            except ImportError as error:
                raise ValueError('--vendor-syntax requires tree-sitter and tree-sitter-c') from error
            parser_c = Parser(Language(tree_sitter_c.language()))
            for source in (APP_ROOT / 'apps/app/video/take_video.c', MEDIA_ROOT / 'ag_media.c',
                           APP_ROOT / 'apps/app/audio/take_audio.c', LOCAL_MEDIA_ROOT / 'vm_media.c', *sorted((APP_ROOT / 'apps/app').glob('*/main.c'))):
                tree = parser_c.parse(source.read_bytes())
                if tree.root_node.has_error:
                    raise ValueError(f'C grammar error in {source}')
            print('PASS: local MPP/controlled media/entry C grammar; full MPP type checking requires V821 BSP headers')
        print('Layout formulas require separate review; device and runtime behavior are not validated by this script.')
        return 0
    except (OSError, ValueError, SyntaxError, subprocess.CalledProcessError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
