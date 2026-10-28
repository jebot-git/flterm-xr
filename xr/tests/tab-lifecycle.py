#!/usr/bin/env python3
"""Check shell exits and tab selection through the real application event loop."""
import argparse
import json
import os
from pathlib import Path
import shlex
import signal
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--frontend', choices=('headless', 'preview', 'wayland'), default='headless')
parser.add_argument('--xvfb', default='Xvfb')
args = parser.parse_args()
signal.pthread_sigmask(signal.SIG_SETMASK, [])


def check(ok, message):
    if not ok:
        raise AssertionError(message)
    print('PASS ' + message, flush=True)


with tempfile.TemporaryDirectory(prefix='flt-tabs-') as directory:
    root = Path(directory)
    env = {**os.environ, 'XDG_RUNTIME_DIR': directory, 'HOME': directory,
           'XDG_CONFIG_HOME': str(root / 'config'), 'XDG_CACHE_HOME': str(root / 'cache'),
           'XDG_STATE_HOME': str(root / 'state'), 'SHELL': '/bin/sh', 'ENV': '', 'BASH_ENV': '',
           'XDG_CURRENT_DESKTOP': '', 'LIBGL_ALWAYS_SOFTWARE': '1'}
    # A caller may supply a private compositor for the native Wayland check.
    if args.frontend != 'wayland':
        for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET'):
            env.pop(name, None)
    elif env.get('WAYLAND_DISPLAY'):
        env['WAYLAND_DISPLAY'] = str(Path(os.environ['XDG_RUNTIME_DIR']) / env['WAYLAND_DISPLAY'])
    process = xvfb = None
    with (root / 'terminal.log').open('w+') as log:
        try:
            if args.frontend == 'preview':
                read, write = os.pipe()
                try:
                    xvfb = subprocess.Popen([args.xvfb, '-displayfd', str(write), '-screen', '0',
                                             '1024x768x24', '-nolisten', 'tcp'], pass_fds=(write,),
                                            stdout=log, stderr=log)
                finally:
                    os.close(write)
                with os.fdopen(read) as display:
                    number = display.readline().strip()
                check(bool(number), 'Private desktop display starts')
                env.update(DISPLAY=':' + number, FLTK_BACKEND='x11')
            frontend = {'headless': '--headless', 'preview': '--preview-only', 'wayland': '--wayland'}
            process = subprocess.Popen([str(args.binary.resolve()), frontend[args.frontend],
                                        '--no-save-layout', '--tabs', '5'],
                                       env=env, stdout=log, stderr=log)
            control = root / 'flterm-xr' / ('control.sock' if args.frontend == 'headless'
                                          else f'wayland-{process.pid}.sock')

            def send(message):
                with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as connection:
                    connection.sendto(message.encode(), str(control))

            def status():
                reply = root / 'reply.sock'
                with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as connection:
                    reply.unlink(missing_ok=True)
                    connection.bind(str(reply))
                    connection.settimeout(.5)
                    try:
                        connection.sendto(b'status', str(control))
                        return json.loads(connection.recv(65536))
                    finally:
                        reply.unlink(missing_ok=True)

            def wait(predicate):
                end = time.monotonic() + 8
                while time.monotonic() < end:
                    if process.poll() is not None:
                        raise AssertionError(f'Terminal exited unexpectedly: {process.returncode}')
                    try:
                        if result := predicate():
                            return result
                    except (OSError, ValueError):
                        pass
                    time.sleep(.02)
                raise AssertionError('Timed out waiting for tab state')

            def tabs(count, active):
                return wait(lambda: (s := status())['tabs'] == count and s['active'] == active)

            def shell(command):
                send('type ' + command + '\n')

            def selected(expected, label):
                marker = root / label
                shell('printf %s "$FLTERM_TAB" > ' + shlex.quote(str(marker)))
                wait(marker.exists)
                check(marker.read_text() == str(expected), label)

            tabs(5, 5)
            for index in range(1, 6):
                send(f'tab-select {index}')
                shell(f'FLTERM_TAB={index}')
                selected(index, f'Shell {index} starts independently')

            shell('exit 13')
            tabs(4, 4)
            selected(4, 'Exiting the last selected tab preserves the preceding shell')

            def delayed_exit(index, count, active, label):
                gate = root / label
                send(f'tab-select {index}')
                shell('while [ ! -f ' + shlex.quote(str(gate)) + ' ]; do sleep .02; done; exit')
                send(f'tab-select {active}')
                tabs(count, active)
                gate.touch()

            delayed_exit(1, 4, 4, 'exit-first')
            tabs(3, 3)
            selected(4, 'A background tab before the selection exits without switching shells')

            send('tab-select 2')
            shell('exit')
            tabs(2, 2)
            selected(4, 'Exiting a middle tab selects its surviving neighbour')

            delayed_exit(2, 2, 1, 'exit-after')
            tabs(1, 1)
            selected(2, 'A background tab after the selection exits without switching shells')

            send('tab-new')
            tabs(2, 2)
            shell('FLTERM_TAB=6')
            selected(6, 'A new shell starts after earlier tabs have exited')
            send('tab-close')
            tabs(1, 1)
            selected(2, 'Explicit tab close also preserves the surviving shell')

            send('tab-new')
            tabs(2, 2)
            selected('', 'New shell accepts input before EOF')
            send('type \x04')
            tabs(1, 1)
            selected(2, 'Ctrl+D closes only its shell tab')
            shell('exit')
            check(process.wait(timeout=5) == 0, 'Exiting the final shell closes the terminal cleanly')
            check(not control.exists(), 'Final shell exit releases the control socket')
        except Exception:
            log.flush()
            print((root / 'terminal.log').read_text(), flush=True)
            raise
        finally:
            for child in (process, xvfb):
                if child is not None and child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
