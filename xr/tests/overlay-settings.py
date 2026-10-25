#!/usr/bin/env python3
"""Exercise overlay settings through the real PTY/control loop, without XR or input capture."""
import argparse
import json
import math
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
args = parser.parse_args()
binary = args.binary.resolve()
signal.pthread_sigmask(signal.SIG_SETMASK, [])


def check(ok, message):
    if not ok:
        raise AssertionError(message)
    print('PASS ' + message, flush=True)


def wait(predicate, process=None):
    end = time.monotonic() + 10
    while time.monotonic() < end:
        if process is not None and process.poll() is not None:
            raise AssertionError('Terminal exited before becoming ready')
        try:
            result = predicate()
            if result:
                return result
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(.02)
    raise AssertionError('Timed out waiting for terminal settings')


def matches(value, expected):
    return all(math.isclose(value[key], number, abs_tol=1e-5) for key, number in expected.items())


with tempfile.TemporaryDirectory(prefix='flt-layout-') as directory:
    root = Path(directory)
    root.chmod(0o700)
    settings = root / 'config/flterm-xr/layout.json'
    control = root / 'flterm-xr/control.sock'
    env = {**os.environ, 'XDG_RUNTIME_DIR': directory, 'XDG_CONFIG_HOME': str(root / 'config'),
           'XDG_CACHE_HOME': str(root / 'cache'), 'XDG_STATE_HOME': str(root / 'state'),
           'HOME': directory, 'ENV': '', 'BASH_ENV': '', 'XDG_CURRENT_DESKTOP': ''}
    for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET'):
        env.pop(name, None)
    processes = []
    with (root / 'terminal.log').open('w+') as log:
        def send(*messages):
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as connection:
                for message in messages:
                    connection.sendto(message.encode(), str(control))

        def status():
            reply = root / 'reply.sock'
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as connection:
                reply.unlink(missing_ok=True)
                connection.bind(str(reply))
                connection.settimeout(1)
                try:
                    connection.sendto(b'status', str(control))
                    return json.loads(connection.recv(65536))
                finally:
                    reply.unlink(missing_ok=True)

        def start(*options):
            process = subprocess.Popen([str(binary), '--headless', '--hidden', '--no-keyboard',
                                        *options, '--', '/bin/sh', '-s'],
                                       env=env, stdout=log, stderr=log)
            processes.append(process)
            wait(lambda: control.exists() and status(), process)
            return process

        def finish(process, *messages):
            send(*(messages or ('quit',)))
            check(process.wait(timeout=5) == 0, 'Overlay exits cleanly and flushes pending settings')
            check(not control.exists(), 'Overlay releases its control socket on exit')

        def saved(path=settings):
            return json.loads(path.read_text())

        try:
            process = start('--width', '1.4', '--height', '.8', '--position', '.2', '-.1', '-1.8',
                            '--background-opacity', '.35', '--styled-background-opacity', '.55')
            initial = {'width': 1.4, 'height': .8, 'x': .2, 'y': -.1, 'z': -1.8,
                       'background_opacity': .35, 'styled_background_opacity': .55}
            check(wait(lambda: matches(saved(), initial)), 'Initial launch saves geometry and both opacity values without adjustments')
            send('scale 1.25', 'move .1 .2 -.4', 'opacity .65')
            adjusted = {**initial, 'width': 1.75, 'height': 1., 'x': .3, 'y': .1, 'z': -2.2,
                        'background_opacity': .65}
            check(wait(lambda: matches(saved(), adjusted)), 'Hidden overlay autosaves live position, scale and transparency changes')
            check(settings.stat().st_mode & 0o777 == 0o600, 'Saved overlay settings remain private')
            finish(process)

            process = start()
            restored = status()
            check(matches(restored['layout'], {key: adjusted[key] for key in ('width', 'height', 'x', 'y', 'z')})
                  and matches(restored, {'opacity': .65, 'styled_opacity': .55}),
                  'New process restores exact position, physical scale and both opacity values')
            send('opacity 0')
            check(wait(lambda: saved()['background_opacity'] == 0), 'Opacity-only adjustment persists full transparency')
            finish(process)

            process = start('--background-opacity', '.4', '--width', '2', '--distance', '3')
            overrides = {**adjusted, 'width': 2., 'z': -3., 'background_opacity': .4}
            check(matches(status()['layout'], {key: overrides[key] for key in ('width', 'height', 'x', 'y', 'z')})
                  and matches(status(), {'opacity': .4, 'styled_opacity': .55}),
                  'Explicit launch options override only the corresponding saved settings')
            check(wait(lambda: matches(saved(), overrides)), 'Launch overrides become the next saved layout')
            finish(process)

            previous = settings.read_bytes()
            process = start('--no-save-layout', '--background-opacity', '.9')
            check(matches(status()['layout'], {'width': 1.2, 'x': 0., 'y': -.15, 'z': -1.2})
                  and matches(status(), {'opacity': .9, 'styled_opacity': 1.}),
                  'No-save mode skips restoration and uses launch defaults')
            finish(process, 'opacity .1', 'position -1 .3 -2', 'quit')
            check(settings.read_bytes() == previous, 'No-save mode leaves the previous XR layout unchanged')

            process = start()
            finish(process, 'position -.4 .2 -2.7', 'scale .5', 'opacity .57', 'quit')
            check(matches(saved(), {**overrides, 'width': 1., 'height': .5, 'x': -.4, 'y': .2,
                                    'z': -2.7, 'background_opacity': .57}),
                  'Immediate quit saves the latest changes before the debounce expires')
            process = start()
            finish(process, 'opacity .23', 'tab-close')
            check(matches(saved(), {'background_opacity': .23}), 'Closing the last tab also saves the current transparency')

            alternate = root / 'alternate.json'
            previous = settings.read_bytes()
            process = start('--layout-file', str(alternate), '--background-opacity', '0')
            finish(process, 'position .1 .2 -4', 'opacity .2', 'quit')
            check(matches(saved(alternate), {'x': .1, 'y': .2, 'z': -4., 'background_opacity': .2})
                  and settings.read_bytes() == previous, 'Custom layout file saves independently of the default XR layout')

            legacy = {'version': 1, 'width': 1.4, 'height': .8, 'x': .2, 'y': -.1, 'z': -1.8}
            settings.write_text(json.dumps(legacy))
            process = start()
            check(matches(status()['layout'], {key: legacy[key] for key in ('width', 'height', 'x', 'y', 'z')})
                  and matches(status(), {'opacity': 0., 'styled_opacity': 1.}),
                  'Existing geometry-only layout files restore with default transparency')
            finish(process)
        except Exception:
            log.flush()
            print((root / 'terminal.log').read_text())
            raise
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
