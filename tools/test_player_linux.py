#!/usr/bin/env python3
"""Verify the real Linux X11 player, persistent keyboard editor and live UDP link."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import traceback

from test_gba_windows import fixture
from verify_autopsy import png_rgb

BALANCED = [2, 0, 13, 1, 37, 40, 49, 36, 12, 34]
CLASSIC = [124, 123, 126, 125, 6, 7, 56, 36, 12, 13]
ORDER = [2, 1, 3, 0, 4, 5, 8, 9, 7, 6]
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('requires Linux with Xvfb and xdotool')
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    checks, logs, processes = [], [], []
    xvfb, active, display_read = None, None, None
    env = dict(os.environ)
    report = dict(passed=False, checks=checks)

    def wait(predicate, timeout=12, process=None):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            if process is not None and process.poll() is not None:
                raise RuntimeError(f'player exited early with {process.returncode}')
            if xvfb is not None and xvfb.poll() is not None:
                raise RuntimeError('isolated Xvfb exited early')
            time.sleep(.03)
        raise TimeoutError('Linux player did not reach the requested state')

    def xd(*command, checked=True):
        return subprocess.run(['xdotool', *map(str, command)], env=env, capture_output=True,
                              text=True, check=checked, timeout=8)

    try:
        if not shutil.which('Xvfb') or not shutil.which('xdotool'):
            raise RuntimeError('Install Xvfb and xdotool before running this desktop test')
        with tempfile.TemporaryDirectory(prefix='matchaboy linux player ') as temporary:
            root = Path(temporary)
            env.update(XDG_CONFIG_HOME=str(root / 'config'), XDG_DATA_HOME=str(root / 'data'),
                       MATCHA_GAME_LIBRARY=str(root / 'library'),
                       XAUTHORITY=str(root / 'isolated-xauthority'))
            # -displayfd reserves an unused display atomically. The test never
            # changes focus, keys, clipboard or preferences on the user's desktop.
            display_read, display_write = os.pipe()
            xvfb_log = (output / 'xvfb.log').open('w')
            logs.append(xvfb_log)
            try:
                xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(display_write), '-screen',
                                         '0', '1440x1000x24', '-nolisten', 'tcp'],
                                        env=env, pass_fds=(display_write,),
                                        stdout=xvfb_log, stderr=xvfb_log)
            finally:
                os.close(display_write)
            with selectors.DefaultSelector() as selector:
                selector.register(display_read, selectors.EVENT_READ)
                if not selector.select(12):
                    raise TimeoutError('Xvfb did not allocate an isolated display')
                display = os.read(display_read, 128).decode().strip()
            os.close(display_read)
            display_read = None
            if not display.isdecimal():
                raise RuntimeError('Xvfb returned an invalid display number')
            env['DISPLAY'] = ':' + display
            rom = root / 'keyboard fixture.gba'
            rom.write_bytes(fixture())
            capture = output / 'player.png'
            metadata = Path(str(capture) + '.json')
            window, width, height = None, 1280, 900

            def launch(label, *options):
                nonlocal active, window
                log = (output / (label + '.log')).open('w')
                logs.append(log)
                active = subprocess.Popen([str(binary), *map(str, options), '--capture',
                                           str(capture)], cwd=root, env=env,
                                          stdout=log, stderr=log)
                processes.append(active)
                def find_window():
                    found = xd('search', '--onlyvisible', '--class', '^Matchaboy$', checked=False)
                    choices = found.stdout.split()
                    return choices[0] if len(choices) == 1 else None
                window = wait(find_window, process=active)
                xd('windowfocus', '--sync', window)
                return active

            def key(chord):
                xd('key', '--delay', '30', chord)

            def click(x, y):
                xd('mousemove', '--window', window, round(x * width / 1280),
                   round(y * height / 900), 'click', '1')
                time.sleep(.05)

            def snapshot(name=None):
                nonlocal width, height
                old = metadata.stat().st_mtime_ns if metadata.exists() else 0
                key('F12')
                def ready():
                    try:
                        if metadata.stat().st_mtime_ns == old:
                            return None
                        state = json.loads(metadata.read_text())
                        return state if 'keyboard_mapping' in state else None
                    except (OSError, json.JSONDecodeError):
                        return None
                state = wait(ready, process=active)
                width, height = state['width'], state['height']
                if name:
                    shutil.copy2(capture, output / (name + '.png'))
                    (output / (name + '.json')).write_text(json.dumps(state, indent=2) + '\n')
                return state

            def crop(path, box):
                w, h, pixels = png_rgb(path)
                x0, y0, x1, y1 = box
                x0, x1 = round(x0 * w / 1280), round(x1 * w / 1280)
                y0, y1 = round(y0 * h / 900), round(y1 * h / 900)
                return b''.join(bytes(pixels[(y * w + x0) * 3:(y * w + x1) * 3])
                                for y in range(y0, y1))

            def pixel():
                w, h, pixels = png_rgb(capture)
                # Centre of the real LCD region, inside either native scale.
                x, y = round(448 * w / 1280), round(482 * h / 900)
                offset = (y * w + x) * 3
                return tuple(pixels[offset:offset + 3])

            def native_lcd(path, system):
                w, h, pixels = png_rgb(path)
                sw, sh = (160, 144) if system == 'GB' else (240, 160)
                available_w, available_h = 824 * w // 1280, 620 * h // 900
                scale = max(1, min(available_w // sw, available_h // sh))
                left = 36 * w // 1280 + (available_w - sw * scale) // 2
                top = 172 * h // 900 + (available_h - sh * scale) // 2
                # Extract one pixel from each nearest-neighbour LCD block;
                # exclude the controls guide and other changing UI elements.
                return bytes(channel for y in range(sh) for x in range(sw)
                             for channel in pixels[((top + y * scale) * w + left + x * scale) * 3:
                                                   ((top + y * scale) * w + left + x * scale) * 3 + 3])

            def stop():
                nonlocal active, window
                key('ctrl+q')
                assert active.wait(timeout=10) == 0
                active, window = None, None

            def settings():
                key('ctrl+comma')
                state = snapshot()
                assert state['settings_open'] and state['buttons'] == 0
                return state

            def select_button(bit):
                row = ORDER.index(bit)
                click(216 + row // 5 * 428 + 250, 302 + row % 5 * 65 + 19)

            # Exercise every shipped homebrew ROM through its catalog entry,
            # using the same pinned bytes and boot/input recipe as packaging.
            catalog = json.loads((ROOT / 'games/homebrew/manifest.json').read_text())['games']
            assert catalog and len({game['id'] for game in catalog}) == len(catalog)
            for game in catalog:
                source = ROOT / game['rom']
                assert hashlib.sha256(source.read_bytes()).hexdigest() == game['sha256'], source
                recipe = game['test']
                target = output / (game['id'] + '.png')
                result = subprocess.run([str(binary), '--game', game['id'], '--window-test',
                                         '--frames', str(recipe['boot_frames']),
                                         '--buttons', str(recipe['input_mask']),
                                         '--input-frames', str(recipe['input_frames']),
                                         '--capture', str(target)], cwd=root,
                                        env=env, capture_output=True, text=True, timeout=45)
                (output / (game['id'] + '.log')).write_text(result.stdout + result.stderr)
                assert result.returncode == 0, result.stderr
                state = json.loads(Path(str(target) + '.json').read_text())
                assert state['system'] == game['system']
                assert state['frames'] == recipe['boot_frames'] + recipe['input_frames']
                assert state['keyboard_mapping'] == BALANCED
                assert target.read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
                if recipe['input_mask']:
                    baseline = output / (game['id'] + '-no-input.png')
                    neutral_env = dict(env, MATCHA_GAME_LIBRARY=str(root / 'neutral-library' / game['id']))
                    result = subprocess.run([str(binary), '--game', game['id'], '--headless',
                                             '--frames', str(recipe['boot_frames']), '--buttons', '0',
                                             '--input-frames', str(recipe['input_frames']),
                                             '--capture', str(baseline)], cwd=root, env=neutral_env,
                                            capture_output=True, text=True, timeout=45)
                    (output / (game['id'] + '-no-input.log')).write_text(result.stdout + result.stderr)
                    assert result.returncode == 0, result.stderr
                    neutral = json.loads(Path(str(baseline) + '.json').read_text())
                    assert neutral['frames'] == state['frames']
                    neutral_w, neutral_h, neutral_pixels = png_rgb(baseline)
                    assert (neutral_w, neutral_h) == ((160, 144) if game['system'] == 'GB' else (240, 160))
                    assert native_lcd(target, game['system']) != bytes(neutral_pixels), \
                        f"{game['id']}: controller input did not change the LCD against an equal-duration neutral run"
                    checks.append(f"{game['id']}: real controller response changes LCD pixels against an equal-duration zero-input baseline")
            checks.append('every pinned homebrew catalog ROM executes its manifest boot/input recipe and renders a real native X11 capture')

            # The replacement catalog contains GB games; keep independent GBA
            # framebuffer coverage with the existing authored cartridge fixture.
            target = output / 'drift-circuit.png'
            result = subprocess.run([str(binary), str(ROOT / 'games/gba/roms/drift-circuit.gba'),
                                     '--window-test', '--frames', '30', '--capture', str(target)],
                                    cwd=root, env=env, capture_output=True, text=True, timeout=30)
            (output / 'drift-circuit.log').write_text(result.stdout + result.stderr)
            assert result.returncode == 0, result.stderr
            state = json.loads(Path(str(target) + '.json').read_text())
            assert state['system'] == 'GBA' and state['frames'] == 30
            assert state['keyboard_mapping'] == BALANCED
            assert target.read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
            checks.append('the authored external GBA cartridge still executes and renders through the native player')

            launch('homebrew-library', '--library', '--frames', '0')
            library = snapshot('homebrew-library')
            ids = [game['id'] for game in catalog]
            assert library['library_view'] and library['library_selected'] == ids[0]
            entries = library['library_games']
            assert [entry['id'] for entry in entries] == ids
            for entry, game in zip(entries, catalog):
                assert entry['title'] and entry['author'] and entry['license'] and entry['players']
                assert entry['system'] == game['system']
                for field in ['title', 'author', 'license', 'players']:
                    if field in game:
                        assert entry[field] == game[field], (field, entry, game)
            key('Left')
            assert snapshot('library-last-game')['library_selected'] == ids[-1]
            key('Right')
            assert snapshot()['library_selected'] == ids[0]
            for expected in ids[1:] + ids[:1]:
                key('Right')
                assert snapshot()['library_selected'] == expected
            key('Down')
            step = 2 if len(ids) > 2 else 1
            assert snapshot()['library_selected'] == ids[step % len(ids)]
            key('Up')
            assert snapshot()['library_selected'] == ids[0]
            key('Return')
            selected = snapshot()
            assert not selected['library_view'] and selected['system'] == catalog[0]['system']
            stop()
            checks.append('the native library presents the replacement catalog with credits/license/player counts, wraps keyboard selection, and starts the selected cartridge')

            launch('keyboard', rom, '--frames', '0', '--paused')
            start = snapshot('balanced-guide')
            assert start['system'] == 'GBA' and start['keyboard_mapping'] == BALANCED
            assert start['paused'] and start['controls_visible']
            codes = ['d', 'a', 'w', 's', 'l', 'k', 'space', 'Return', 'q', 'i']
            for bit, code in enumerate(codes):
                xd('keydown', code)
                state = snapshot('held-a' if bit == 4 else None)
                assert state['buttons'] == 1 << bit and state['paused'], (bit, state)
                xd('keyup', code)
                assert snapshot()['buttons'] == 0
            assert crop(output / 'held-a.png', (1087, 417, 1224, 448)) != crop(output / 'balanced-guide.png', (1087, 417, 1224, 448))
            checks.append('all ten Balanced keys reach the real controller mask; releases clear inputs; Space selects without pausing')
            xd('keydown', 'l')
            assert snapshot()['buttons'] == 16
            xd('keydown', 'Control_L')
            assert snapshot()['buttons'] == 0
            xd('keyup', 'Control_L')
            xd('keyup', 'l')
            assert snapshot()['buttons'] == 0
            checks.append('pressing Control clears a held game button immediately without leaving a stuck input')

            # Browsing the library must preserve an already-paused game.
            key('ctrl+l')
            library = snapshot()
            assert library['library_view'] and library['paused'] and library['frames'] == start['frames']
            key('ctrl+l')
            assert snapshot()['library_view']
            key('Escape')
            returned = snapshot()
            assert not returned['library_view'] and returned['paused'] and returned['frames'] == start['frames']
            checks.append('Ctrl+L enters the library and Escape restores the original paused state, including repeated library commands')

            # Inspector must not steal WASD from the player.
            key('Tab')
            assert snapshot()['inspector_view']
            xd('keydown', 's')
            state = snapshot()
            assert state['buttons'] == 8 and state['frames'] == start['frames']
            xd('keyup', 's')
            key('Tab')
            assert not snapshot()['inspector_view']
            key('ctrl+shift+c')
            assert not snapshot()['controls_visible']
            key('ctrl+shift+c')
            assert snapshot()['controls_visible']
            checks.append('Inspector preserves WASD gameplay input and the modified controls shortcut toggles the guide')

            # Real ARM instructions read KEYINPUT and change the LCD colour.
            for code, expected in [('l', (0, 255, 0)), ('q', (0, 0, 255)), ('i', (255, 255, 255))]:
                key('Escape')
                xd('keydown', code)
                def reached_colour():
                    state = snapshot()
                    assert not state['paused'], state
                    return state if pixel() == expected else None
                wait(reached_colour, timeout=12, process=active)
                key('Escape')
                xd('keyup', code)
                state = snapshot()
                assert state['paused'] and pixel() == expected, (code, pixel(), state)
            checks.append('A/L/R mappings reach actual GBA KEYINPUT and produce the expected green/blue/white LCD pixels')
            xd('keydown', 'Escape')
            time.sleep(.7)
            running = snapshot()
            assert not running['paused']
            xd('keyup', 'Escape')
            assert not snapshot()['paused']
            key('Escape')
            assert snapshot()['paused']
            checks.append('Escape toggles pause once when held; key repeat does not repeatedly pause the core')
            paused_frame = snapshot()['frames']
            key('Escape')
            wait(lambda: snapshot()['frames'] > paused_frame, process=active)
            key('ctrl+l')
            library = snapshot()
            assert library['library_view'] and library['paused']
            key('ctrl+l')
            repeated_library = snapshot()
            assert repeated_library['library_view'] and repeated_library['frames'] == library['frames']
            key('Escape')
            restored = snapshot()
            assert not restored['library_view'] and not restored['paused']
            wait(lambda: snapshot()['frames'] > library['frames'], process=active)
            key('Escape')
            assert snapshot()['paused']
            checks.append('returning from the library resumes a previously running game instead of overwriting its saved pause state')

            settings()
            select_button(9)
            key('o')
            custom = BALANCED.copy()
            custom[9] = 31
            edited = snapshot('custom-settings')
            assert edited['settings_draft'] == custom and edited['keyboard_mapping'] == BALANCED
            assert not edited['settings_error']
            click(974, 760)
            state = snapshot('custom-guide')
            assert not state['settings_open'] and state['keyboard_mapping'] == custom and state['paused']
            assert crop(output / 'custom-guide.png', (1087, 540, 1224, 571)) != crop(output / 'balanced-guide.png', (1087, 540, 1224, 571))
            stop()
            launch('restored', rom, '--frames', '0', '--paused')
            assert snapshot()['keyboard_mapping'] == custom
            xd('keydown', 'i')
            assert snapshot()['buttons'] == 0
            xd('keyup', 'i')
            xd('keydown', 'o')
            assert snapshot()['buttons'] == 512
            xd('keyup', 'o')
            assert snapshot()['buttons'] == 0
            checks.append('R shoulder remaps I to O, Apply persists across process restart, and the old key stops controlling the game')

            settings()
            select_button(9)
            key('l')
            duplicate = snapshot('duplicate-protection')
            assert duplicate['settings_draft'][9] == 37 and duplicate['settings_error']
            click(974, 760)
            assert snapshot()['settings_open']
            click(290, 760)
            assert snapshot()['keyboard_mapping'] == custom
            checks.append('duplicate mappings disable Apply; Cancel preserves the previous saved mapping')

            settings()
            select_button(9)
            key('Tab')
            click(974, 760)
            old = metadata.stat().st_mtime_ns
            key('F12')  # Also reserved; an armed editor must not accept it or save.
            time.sleep(.15)
            assert metadata.stat().st_mtime_ns == old
            key('Escape')  # Cancel only the armed key capture.
            reserved = snapshot()
            assert reserved['settings_open'] and reserved['settings_capture'] == -1
            assert reserved['settings_draft'] == custom and reserved['keyboard_mapping'] == custom
            key('Escape')
            assert not snapshot()['settings_open']
            checks.append('Tab/F12 remain reserved and Escape cancels key capture before closing Settings')

            settings()
            click(528, 255)
            assert snapshot()['settings_draft'] == CLASSIC
            click(290, 760)
            assert snapshot()['keyboard_mapping'] == custom
            settings()
            click(760, 255)
            for code in ['w', 'a', 's', 'd', 'l', 'k', 'q', 'i', 'Return', 'space']:
                key(code)
            state = snapshot()
            assert state['settings_capture'] == -1 and state['settings_draft'] == BALANCED
            click(974, 760)
            assert snapshot()['keyboard_mapping'] == BALANCED
            settings()
            click(528, 255)
            click(314, 255)
            assert snapshot()['settings_draft'] == BALANCED
            click(974, 760)
            assert snapshot()['keyboard_mapping'] == BALANCED
            checks.append('Classic and Balanced presets and all ten Set All captures work, including Enter and Space')
            settings()
            key('Tab')
            key('Return')
            state = snapshot()
            assert state['settings_focus'] == 1 and state['settings_draft'] == CLASSIC
            key('shift+Tab')
            key('space')
            state = snapshot()
            assert state['settings_focus'] == 0 and state['settings_draft'] == BALANCED
            xd('key', '--delay', '30', *(['Tab'] * 14))
            assert snapshot()['settings_focus'] == 14
            key('Return')
            state = snapshot()
            assert not state['settings_open'] and state['keyboard_mapping'] == BALANCED and state['buttons'] == 0
            checks.append('Settings is operable by Tab/Shift+Tab and Enter/Space, including keyboard-only Apply')
            stop()

            # Force an actual save-write failure without permission assumptions:
            # a directory cannot be opened as the save's temporary regular file.
            save_rom = root / 'save retry fixture.gba'
            save_rom.write_bytes(fixture())
            save_file = save_rom.with_suffix('.matchaboy.sav')
            seed = bytes([17]) + bytes(32767)
            save_file.write_bytes(seed)
            blocker = Path(str(save_file) + '.tmp')
            launch('save-retry', save_rom, '--frames', '0', '--paused')
            start_save = snapshot()
            key('Escape')
            wait(lambda: snapshot()['frames'] >= start_save['frames'] + 3, process=active)
            blocker.mkdir()
            key('ctrl+q')
            failed_save = snapshot('save-error')
            assert active.poll() is None and failed_save['ui_mode'] == 'save-error' and failed_save['paused']
            assert save_file.read_bytes() == seed, 'failed save overwrote the existing cartridge save'
            time.sleep(.1)
            assert snapshot()['frames'] == failed_save['frames']
            key('Return')  # Retry while the real filesystem error still exists.
            retried = snapshot()
            assert retried['ui_mode'] == 'save-error' and retried['frames'] == failed_save['frames']
            assert save_file.read_bytes() == seed
            click(567, 760)  # Keep playing restores the state before Quit.
            kept = snapshot()
            assert kept['ui_mode'] == 'player' and not kept['paused']
            wait(lambda: snapshot()['frames'] > failed_save['frames'], process=active)
            key('ctrl+q')
            assert snapshot()['ui_mode'] == 'save-error'
            blocker.rmdir()
            click(333, 760)  # Retry save and quit after fixing the actual folder.
            assert active.wait(timeout=10) == 0
            active, window = None, None
            saved = save_file.read_bytes()
            assert len(saved) == len(seed) and saved[0] == 18 and saved[1:] == seed[1:], 'retry lost the in-memory SRAM update'
            assert not blocker.exists()
            checks.append('failed cartridge writes preserve the old save and live core; Keep playing resumes it and Retry saves the real SRAM update before quitting')

            # Two complete real cores communicate over the actual UDP transport.
            peer_rom = root / 'remote fixture.gba'
            peer_rom.write_bytes(fixture())
            stop_file = root / 'release-headless'
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as allocation:
                allocation.bind(('127.0.0.1', 0))
                port = allocation.getsockname()[1]
            code = '13f948b06c742db29fa12a3ced0365a8'
            peer_capture = output / 'headless-peer.png'
            peer_log = output / 'headless-peer.log'
            peer_stream = peer_log.open('w')
            logs.append(peer_stream)
            peer = subprocess.Popen([str(binary), str(peer_rom), '--headless', '--frames', '1200',
                                     '--net-host', str(port), '--net-code', code,
                                     '--net-stop-file', str(stop_file), '--capture', str(peer_capture)],
                                    cwd=root, env=env, stdout=peer_stream, stderr=peer_stream)
            processes.append(peer)
            launch('linked-ui', rom, '--frames', '0', '--net-join', f'127.0.0.1:{port}', '--net-code', code)
            def linked():
                state = snapshot()
                if peer.poll() is not None:
                    raise RuntimeError('headless friend exited before the native session completed')
                if state.get('netplay', {}).get('finished'):
                    raise RuntimeError(state['netplay']['status'])
                return state if state.get('netplay', {}).get('verified_frames', 0) >= 60 else None
            before = wait(linked, timeout=30, process=active)
            key('Escape')
            assert not snapshot()['paused']
            settings()
            time.sleep(12)
            after = snapshot('linked-settings')
            assert after['settings_open'] and not after['paused'] and after['buttons'] == 0
            network = after['netplay']
            assert network['connected'] and not network['finished'], network
            assert network['frames'] >= before['netplay']['frames'] + 120, network
            assert network['verified_frames'] > before['netplay']['verified_frames'], network
            key('Escape')
            assert not snapshot()['settings_open']
            wait(lambda: 'Friend capture ready: frame 1200' in peer_log.read_text(), timeout=45, process=peer)
            completed = snapshot('linked-completed')
            assert completed['netplay']['verified_frames'] >= 1140 and not completed['netplay']['finished']
            stop_file.write_text('complete\n')
            assert peer.wait(timeout=12) == 0
            end = json.loads(Path(str(peer_capture) + '.json').read_text())
            assert end['netplay']['frames'] == 1200 and end['netplay']['verified_frames'] >= 1140
            stop()
            checks.append('native GUI and headless peer complete 1,200 real linked frames; Settings stays open for 12 seconds without pause, timeout or lost hash verification')
            report = dict(passed=True, checks=checks)
    except Exception as error:
        report = dict(passed=False, checks=checks, error=str(error), traceback=traceback.format_exc())
    finally:
        if display_read is not None:
            os.close(display_read)
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        if xvfb is not None and xvfb.poll() is None:
            xvfb.terminate()
            try:
                xvfb.wait(timeout=5)
            except subprocess.TimeoutExpired:
                xvfb.kill()
                xvfb.wait(timeout=5)
        for log in logs:
            log.close()
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
