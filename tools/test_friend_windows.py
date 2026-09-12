#!/usr/bin/env python3
"""Exercise native Host/Join and modal settings with two real Windows UDP peers."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from test_gba_windows import fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('requires a Windows desktop')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    user = C.WinDLL('user32', use_last_error=True)
    callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    user.EnumWindows.argtypes = [callback, W.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
    user.GetDlgItem.argtypes = [W.HWND, C.c_int]
    user.GetDlgItem.restype = W.HWND
    user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    user.IsWindow.argtypes = [W.HWND]
    user.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
    user.SendMessageTimeoutW.restype = C.c_ssize_t
    processes, logs, checks = [], [], []

    def wait(predicate, seconds=15):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            result = predicate()
            if result:
                return result
            for process in processes:
                if process.poll() is not None:
                    raise RuntimeError(f'player exited early: {process.returncode}')
            time.sleep(.02)
        raise TimeoutError('native friend session did not respond')

    def find_window(process, name):
        found = []
        @callback
        def inspect(window, _):
            pid, label = W.DWORD(), C.create_unicode_buffer(128)
            user.GetWindowThreadProcessId(window, C.byref(pid))
            user.GetClassNameW(window, label, len(label))
            if pid.value == process.pid and label.value == name:
                found.append(window)
            return True
        user.EnumWindows(inspect, 0)
        return found[0] if len(found) == 1 else None

    def send(window, message, wp=0, lp=0):
        result = C.c_size_t()
        if not user.SendMessageTimeoutW(window, message, wp, lp, 2, 5000, C.byref(result)):
            raise RuntimeError(f'window message {message:x} failed')
        return result.value

    def set_field(dialog, ident, value):
        data = C.create_unicode_buffer(value)
        send(user.GetDlgItem(dialog, ident), 0x0C, 0, C.addressof(data))

    def read_field(dialog, ident):
        data = C.create_unicode_buffer(1024)
        send(user.GetDlgItem(dialog, ident), 0x0D, len(data), C.addressof(data))
        return data.value

    def close_dialog(dialog):
        user.PostMessageW(dialog, 0x111, 2, 0)
        wait(lambda: not user.IsWindow(dialog))

    try:
        with tempfile.TemporaryDirectory(prefix='matchaboy native link ') as temp:
            root = Path(temp)
            captures, windows = [], []
            for index in range(2):
                rom = root / f'player-{index}.gba'
                rom.write_bytes(fixture())
                capture = output / f'player-{index}.png'
                captures.append(Path(str(capture) + '.json'))
                log = (output / f'player-{index}.log').open('w')
                logs.append(log)
                process = subprocess.Popen([str(args.binary.resolve()), str(rom), '--frames', '2', '--paused', '--capture', str(capture)], stdout=log, stderr=log)
                processes.append(process)
                windows.append(wait(lambda: find_window(process, 'MatchaboyAutopsy')))
                wait(captures[-1].exists)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as allocation:
                allocation.bind(('127.0.0.1', 0))
                port = allocation.getsockname()[1]
            user.PostMessageW(windows[0], 0x111, 1500, 0)
            host_dialog = wait(lambda: find_window(processes[0], 'MatchaboyFriendDialog'))
            code = read_field(host_dialog, 12)
            assert len(code) == 32 and all(c in '0123456789abcdefABCDEF' for c in code)
            set_field(host_dialog, 11, '0')
            user.PostMessageW(host_dialog, 0x111, 1, 0)
            time.sleep(.1)
            assert user.IsWindow(host_dialog), 'invalid UDP port incorrectly accepted'
            set_field(host_dialog, 11, str(port))
            user.PostMessageW(host_dialog, 0x111, 1, 0)
            wait(lambda: not user.IsWindow(host_dialog))
            user.PostMessageW(windows[1], 0x111, 1501, 0)
            join_dialog = wait(lambda: find_window(processes[1], 'MatchaboyFriendDialog'))
            set_field(join_dialog, 10, '127.0.0.1')
            set_field(join_dialog, 11, str(port))
            set_field(join_dialog, 12, code)
            user.PostMessageW(join_dialog, 0x111, 1, 0)
            wait(lambda: not user.IsWindow(join_dialog))
            checks.append('native Host/Join validates port and connects two freshly restarted real GBA consoles over UDP')

            def snapshot(index):
                metadata = captures[index]
                old = metadata.stat().st_mtime_ns
                send(windows[index], 0x111, 1015)
                def ready():
                    try:
                        if metadata.stat().st_mtime_ns == old:
                            return None
                        state = json.loads(metadata.read_text())
                        return state if 'keyboard_mapping' in state else None
                    except (OSError, json.JSONDecodeError):
                        return None
                return wait(ready)

            def connected():
                states = [snapshot(i) for i in range(2)]
                return states if all(s.get('netplay', {}).get('verified_frames', 0) >= 60 for s in states) else None
            before = wait(connected, 30)
            for window in windows:
                for command in (1001, 1050, 1011, 1012, 1013, 1014):
                    send(window, 0x111, command)
            for index in range(2):
                state = snapshot(index)
                assert not state['library_view'] and not state['paused'] and not state['netplay']['finished']
            checks.append('linked session rejects game switching, library, pause and debugger stepping')

            user.PostMessageW(windows[0], 0x111, 1060, 0)
            settings = wait(lambda: find_window(processes[0], '#32770'))
            user.PostMessageW(windows[1], 0x111, 1502, 0)
            details = wait(lambda: find_window(processes[1], 'MatchaboyFriendDialog'))
            # Exceeds the transport's 10-second terminal silence threshold.
            time.sleep(12)
            after = [snapshot(i) for i in range(2)]
            for index, state in enumerate(after):
                network = state['netplay']
                assert network['connected'] and not network['finished'], network
                assert network['frames'] >= before[index]['netplay']['frames'] + 120, network
                assert network['verified_frames'] > before[index]['netplay']['verified_frames'], network
                assert state['buttons'] == 0 and not state['paused']
            checks.append('Settings and Connection Details stay open for 12 seconds while both real peers keep advancing and verifying frames')
            close_dialog(settings)
            close_dialog(details)
            send(windows[0], 0x111, 1503)
            assert 'netplay' not in snapshot(0)
            send(windows[1], 0x111, 1503)
            assert 'netplay' not in snapshot(1)
            checks.append('Disconnect detaches both sessions and restores local game control')
            for window in windows:
                user.PostMessageW(window, 0x10, 0, 0)
            for process in processes:
                assert process.wait(timeout=10) == 0
        report = dict(passed=True, checks=checks)
    except Exception as error:
        report = dict(passed=False, checks=checks, error=str(error))
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
