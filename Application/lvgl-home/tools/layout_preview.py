#!/usr/bin/env python3
"""Generate the 640 x 480 layout schematic; never substitute it for a native frame."""
from html import escape
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'aiglass_home.c').read_text(encoding='utf-8')
CONSTANTS = {key: int(value) for key, value in re.findall(r'#define UI_(\w+) (\d+)', SOURCE)}
PARTS = []
GREEN = '#42ff80'
DIM = '#208747'


def text(x, y, value, size=16, color=GREEN, anchor='start'):
    PARTS.append(f'<text x="{x}" y="{y}" fill="{color}" font-size="{size}" text-anchor="{anchor}">{escape(value)}</text>')


def rect(x, y, width, height, caption=None):
    PARTS.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="7" fill="#000" stroke="{DIM}"/>')
    if caption:
        text(x + width / 2, y + height / 2 + 6, caption, anchor='middle')


def screen(index, title):
    x = 24 + (index % 2) * 664
    y = 88 + (index // 2) * 536
    text(x, y - 16, title, color='#b7c6bd')
    PARTS.append(f'<g transform="translate({x},{y})">')
    rect(0, 0, 640, 480)
    text(12, 26, '--:--')
    text(96, 26, title)
    text(628, 26, '联网  手机  --', anchor='end')
    PARTS.append(f'<path d="M8 38H632 M12 424H628" stroke="{DIM}"/>')
    text(80, 410, '准备就绪')
    text(12, 452, '手机消息：下午三点出发。')
    if index:
        rect(8, 392, 56, 24, '返回')
    PARTS.append('<g transform="translate(12,52)">')


def end_screen():
    PARTS.append('</g></g>')


def main():
    width = 640 - 2 * CONSTANTS['MARGIN']
    height = 480 - CONSTANTS['HEADER'] - CONSTANTS['FOOTER'] - 2 * CONSTANTS['MARGIN']
    gap = CONSTANTS['GAP']
    PARTS.extend([
        '<svg xmlns="http://www.w3.org/2000/svg" width="1352" height="1136" viewBox="0 0 1352 1136">',
        '<title>V821 640 × 480 横屏布局示意</title>',
        '<desc>源码尺寸结构示意，非 LVGL 运行截图。</desc>',
        '<rect width="1352" height="1136" fill="#0b100d"/>',
        '<g font-family="Noto Sans CJK SC, sans-serif">',
    ])
    text(24, 30, '640 × 480 横屏布局', 22)
    text(24, 54, '结构示意 · 示例文案 · 非 LVGL 运行截图', 14, '#b7c6bd')
    screen(0, '菜单')
    cell_w = (width - 3 * gap) // 4
    cell_h = (height - CONSTANTS['BUTTON'] - 2 * gap) // 2
    for i, caption in enumerate(('导航', '骑行', '手机互联', '录音机', '实时翻译', '相机', 'AI 助手', '设置')):
        rect(i % 4 * (cell_w + gap), i // 4 * (cell_h + gap), cell_w, cell_h, caption)
    rect(0, height - 36, width, 36, '全部应用')
    end_screen()

    screen(1, '设备配对')
    left = (width - gap) // 2
    rect(0, 0, left, height - 48)
    text(16, 38, '首次连接')
    text(16, 78, 'V821 · 设备标识')
    text(16, 136, '六位配对码', 22)
    text(16, 182, '在手机端输入配对码')
    for i, step in enumerate(('1. 查找设备', '2. 核对编号与配对码', '3. 配对成功后进入应用')):
        text(left + gap, 38 + i * 48, step)
    rect(0, height - 36, left, 36, '刷新配对码')
    rect(left + gap, height - 36, left, 36, '进入应用')
    end_screen()

    for index, phone in ((2, False), (3, True)):
        screen(index, '手机互联' if phone else 'AI 助手')
        rail_h = (height - 2 * gap) // 3
        for i, caption in enumerate(('实时翻译', 'AI 助手', '手机互联')):
            rect(0, i * (rail_h + gap), CONSTANTS['RAIL'], rail_h, caption)
        x = CONSTANTS['RAIL'] + gap
        w = width - x
        if phone:
            for i, value in enumerate(('手机已连接', '连接地址', '收 0 KB / 发 0 KB')):
                text(x, 20 + i * 26, value)
            rect(x, 88, w, height - 132, '手机消息正文 · 可滚动')
            rect(x, height - 32, (w - 6) // 2, 30, '文件')
            rect(x + (w + 6) // 2, height - 32, (w - 6) // 2, 30, '设备配对')
        else:
            text(x, 20, 'AI 助手')
            text(x, 52, '输入文字或使用语音')
            rect(x, 62, w, height - 138, '对话内容 · 可滚动')
            bw = (w - 12) // 3
            for i, caption in enumerate(('输入', '发送', '语音')):
                rect(x + i * (bw + 6), height - 68, bw, 30, caption)
            rect(x, height - 32, w, 30, '取消请求')
        end_screen()
    PARTS.append('</g></svg>')
    destination = ROOT / 'docs/layout.svg'
    destination.write_text('\n'.join(PARTS) + '\n', encoding='utf-8')
    print(destination)


if __name__ == '__main__':
    main()
