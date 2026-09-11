#!/usr/bin/env python3
"""Exercise the real Windows dashboard message loop and inspect GPU capture metadata."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from verify_autopsy import png_rgb


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('requires a Windows desktop')
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    user = C.WinDLL('user32', use_last_error=True)
    callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    user.EnumWindows.argtypes = [callback_type, W.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
    user.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
    user.SendMessageTimeoutW.restype = C.c_ssize_t
    user.MoveWindow.argtypes = [W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.BOOL]
    user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    user.IsWindow.argtypes = [W.HWND]
    checks, proc = [], None

    def wait(predicate, timeout=10):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            value = predicate()
            if value:
                return value
            if proc and proc.poll() is not None:
                raise RuntimeError(f'dashboard exited early: {proc.returncode}')
            time.sleep(.02)
        raise TimeoutError('dashboard did not respond')

    try:
        with tempfile.TemporaryDirectory(prefix='matcha windows ') as tmp:
            rom = Path(tmp) / 'input fixture.gb'
            data = bytearray(32768)
            data[0x100:0x103] = bytes([0xC3, 0x50, 1])
            data[0x150:0x153] = bytes([0, 0x18, 0xFD])
            rom.write_bytes(data)
            capture = output / 'controls.png'
            metadata = Path(str(capture) + '.json')
            proc = subprocess.Popen([str(binary), str(rom), '--frames', '2', '--inspector', '--paused', '--capture', str(capture)])
            found = []

            @callback_type
            def enum(hwnd, _):
                pid = W.DWORD()
                user.GetWindowThreadProcessId(hwnd, C.byref(pid))
                name = C.create_unicode_buffer(128)
                user.GetClassNameW(hwnd, name, len(name))
                if pid.value == proc.pid and name.value == 'MatchaboyAutopsy':
                    found.append(hwnd)
                return True

            def find():
                user.EnumWindows(enum, 0)
                return found[0] if found else None

            hwnd = wait(find)
            wait(metadata.exists)

            def send(message, wp=0, lp=0):
                result = C.c_size_t()
                if not user.SendMessageTimeoutW(hwnd, message, wp, lp, 2, 5000, C.byref(result)):
                    raise RuntimeError(f'window message {message:x} failed')

            def key(value, down=True, repeat=False):
                send(0x100 if down else 0x101, value, (1 << 30) if repeat else 0)

            def snapshot():
                old = metadata.stat().st_mtime_ns
                key(0x7B)
                wait(lambda: metadata.stat().st_mtime_ns != old)
                state = json.loads(metadata.read_text())
                assert state['gpu_readback']
                return state

            start = snapshot()
            assert start['inspector_view']
            user.GetMenu.argtypes = [W.HWND]; user.GetMenu.restype = W.HMENU
            user.GetMenuStringW.argtypes = [W.HMENU, W.UINT, W.LPWSTR, C.c_int, W.UINT]
            names = []
            for index in range(4):
                label = C.create_unicode_buffer(100)
                user.GetMenuStringW(user.GetMenu(hwnd), index, label, len(label), 0x400)
                names.append(label.value.replace('&', ''))
            assert names == ['File', 'Emulation', 'Audio/Video', 'Tools'], names
            for tab in range(4):
                send(0x111, 1020 + tab)
                state = snapshot()
                assert state['inspector_tab'] == tab and state['cycles'] == start['cycles']
            checks.append('native menus expose Emulation, Audio/Video, Tools and working Inspector panel commands')
            title = C.create_unicode_buffer(512)
            user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
            user.GetWindowTextW(hwnd, title, len(title))
            assert title.value.startswith('Matchaboy - ')
            for tab in range(4):
                key(ord('1') + tab)
                state = snapshot()
                assert state['inspector_tab'] == tab and state['cycles'] == start['cycles']
                shutil.copy2(capture, output / f'inspector-{tab}.png')
            client = W.RECT()
            user.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
            user.GetClientRect(hwnd, C.byref(client))
            scale = min(client.right / 1280, client.bottom / 920)
            for tab in range(4):
                x = int((client.right - 1280 * scale) / 2 + (100 + tab * 230) * scale)
                y = int((client.bottom - 920 * scale) / 2 + 116 * scale)
                send(0x201, 1, x | (y << 16)); send(0x202, 0, x | (y << 16))
                state = snapshot()
                assert state['inspector_tab'] == tab and state['cycles'] == start['cycles']
            key(ord('1'))
            checks.append('Matchaboy title; four Inspector tabs switch by keyboard and click without advancing emulation')
            key(0x09)
            player = snapshot()
            assert not player['inspector_view'] and player['cycles'] == start['cycles']
            def lcd_pixel():
                width, height, pixels = png_rgb(capture)
                x, y = int(400*width/1280), int(400*height/920)
                return tuple(pixels[(y*width+x)*3:(y*width+x)*3+3])
            gray = lcd_pixel()
            assert gray[0] == gray[1] == gray[2]
            send(0x111, 1004)
            assert snapshot()['cycles'] == player['cycles']
            tinted = lcd_pixel()
            assert len(set(tinted)) > 1
            send(0x111, 1003)
            assert snapshot()['cycles'] == player['cycles'] and lcd_pixel() == gray
            checks.append('default grayscale and optional green change display without advancing game')
            key(ord('S'))
            assert snapshot()['cycles'] == start['cycles']
            key(0x09)
            assert snapshot()['inspector_view']
            checks.append('player/Inspector toggle preserves state; player ignores debug step keys')
            time.sleep(.1)
            assert snapshot()['cycles'] == start['cycles']
            checks.append('paused state does not advance')
            key(ord('S')); step = snapshot()
            assert step['instructions'] == start['instructions'] + 1 and step['cycles'] > start['cycles']
            key(ord('F')); frame = snapshot()
            assert frame['frames'] == step['frames'] + 1 and frame['paused']
            key(ord('D')); dot = snapshot()
            assert dot['cycles'] == frame['cycles'] + 1 and dot['instructions'] == frame['instructions']
            checks.append('instruction, frame and peripheral-dot stepping')
            for bit, code in enumerate([0x27, 0x25, 0x26, 0x28, ord('Z'), ord('X'), 0x10, 0x0D]):
                key(code)
                assert snapshot()['buttons'] == 1 << bit
                key(code, False)
                assert snapshot()['buttons'] == 0
            key(ord('Z')); send(0x8)
            assert snapshot()['buttons'] == 0
            checks.append('all eight joypad buttons, release and focus loss')
            send(0x20A, 120 << 16)
            assert snapshot()['trace_scroll'] == 3
            checks.append('instruction trace scrolling')
            key(0x20); key(0x20, repeat=True)
            time.sleep(.15)
            running = snapshot()
            assert not running['paused'] and running['cycles'] > dot['cycles']
            key(0x20)
            assert snapshot()['paused']
            checks.append('resume, pause and held-space repeat protection')
            assert user.MoveWindow(hwnd, 30, 30, 900, 700, True)
            resized = snapshot()
            assert resized['width'] < 1280 and abs(resized['width']/resized['height'] - 1280/920) < .005
            checks.append('resizing preserves dashboard aspect ratio')
            def chooser():
                dialogs = []
                @callback_type
                def collect(window, _):
                    pid = W.DWORD(); name = C.create_unicode_buffer(128)
                    user.GetWindowThreadProcessId(window, C.byref(pid))
                    user.GetClassNameW(window, name, len(name))
                    if pid.value == proc.pid and name.value == '#32770':
                        dialogs.append(window)
                    return True
                user.EnumWindows(collect, 0)
                return dialogs[0] if len(dialogs) == 1 else None
            before_open = snapshot()
            user.PostMessageW(hwnd, 0x111, 1001, 0)
            dialog = wait(chooser)
            user.PostMessageW(dialog, 0x111, 2, 0)
            wait(lambda: not user.IsWindow(dialog))
            cancelled = snapshot()
            assert cancelled['cycles'] == before_open['cycles'] and cancelled['paused']
            checks.append('cancel Open game preserves current machine and pause state')
            # File selection is also verified interactively through the native chooser.
            send(0x10)
            assert proc.wait(timeout=5) == 0
            proc = None
            checks.append('clean native window shutdown')
            standalone = Path(tmp) / 'standalone \u6d4b'
            standalone.mkdir()
            app = standalone / 'autopsy.exe'
            shutil.copy2(binary, app)
            independent_rom = standalone / 'demo \u6d4b.gb'
            shutil.copy2(rom, independent_rom)
            command = [str(app), str(independent_rom), '--headless', '--frames', '2']
            image = standalone / 'capture \u6d4b.png'
            subprocess.run([*command, '--capture', str(image)], cwd=standalone, check=True, timeout=10)
            assert image.read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
            checks.append('standalone executable and Unicode ROM/capture paths')
            # STOP halts emulated clocks, not the host loop or its ability to exit.
            data[0x150:0x154] = bytes([0x10, 0, 0x18, 0xFE])
            independent_rom.write_bytes(data)
            subprocess.run([*command, '--capture', str(image)], cwd=standalone, check=True, timeout=10)
            checks.append('STOP-gated ROM does not hang frame advance')
            invalid = subprocess.run([*command, '--capture', str(standalone / 'missing' / 'bad.png')],
                                     cwd=standalone, timeout=10)
            assert invalid.returncode != 0
            checks.append('unwritable capture fails explicitly')
        report = {'passed': True, 'checks': checks}
    except Exception as error:
        report = {'passed': False, 'checks': checks, 'error': str(error)}
    finally:
        if proc and proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
