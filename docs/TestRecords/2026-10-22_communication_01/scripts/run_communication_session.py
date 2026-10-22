"""Detached bench supervisor: smoke, RS485 count, concurrent long windows, raw UART.

No MCU reset, debugger writes, retry, or sleep/power-setting changes.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[1]


def utc():
    return datetime.now(timezone.utc).isoformat()


def save(path, value):
    temporary = path.with_suffix('.writing')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rs485-port', required=True)
    parser.add_argument('--uart-port', required=True)
    parser.add_argument('--can-dll', type=Path, required=True)
    parser.add_argument('--can-rounds', type=int, default=1800)
    args = parser.parse_args()
    if args.rs485_port == args.uart_port or args.can_rounds <= 0:
        parser.error('distinct serial ports and positive CAN rounds required')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    firmware = out / 'firmware'
    firmware.mkdir()
    for name in ('IndustrialTerminal.axf', 'IndustrialTerminal.hex', 'IndustrialTerminal.build_log.htm'):
        shutil.copy2(ROOT / 'Output' / name, firmware / name)
    image = firmware / 'IndustrialTerminal.axf'
    setup = {'started_utc': utc(), 'pid': os.getpid(), 'rs485_port': args.rs485_port,
             'uart_port': args.uart_port, 'can_channel': '0x51', 'can_rounds': args.can_rounds,
             'firmware_sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
             'binding': 'User reports Keil build/download and manual RUN; UI showed Verify OK at 15:25:49. No target readback.',
             'load': 'RS485 10Hz/3600s overlaps CAN 1Hz/1800 requests; SHT30/UART active',
             'ports': [{'device': p.device, 'description': p.description, 'hwid': p.hwid} for p in list_ports.comports()]}
    save(out / 'setup.json', setup)
    state = {'pid': os.getpid(), 'started_utc': utc(), 'stage': 'uart_start',
             'uart': {'bytes': 0, 'error': None}, 'runs': {}}
    stop, ready = threading.Event(), threading.Event()

    def capture():
        try:
            with serial.Serial(args.uart_port, 115200, timeout=0.2) as port, (out / 'uart.raw').open('xb') as raw, (out / 'uart_chunks.jsonl').open('x', encoding='utf-8', buffering=1) as journal:
                ready.set()
                while not stop.is_set():
                    data = port.read(4096)
                    if data:
                        raw.write(data)
                        raw.flush()
                        state['uart']['bytes'] += len(data)
                        journal.write(json.dumps({'utc': utc(), 'monotonic_s': time.monotonic(), 'hex': data.hex()}) + '\n')
        except Exception as exc:
            state['uart']['error'] = f'{type(exc).__name__}: {exc}'
            ready.set()

    thread = threading.Thread(target=capture, daemon=True)
    thread.start()
    children, handles = {}, []
    base = ['--firmware', str(image), '--setup-note', 'See parent setup.json; user connected devices, built/downloaded firmware, clicked RUN; no reset during window']

    def launch(name, script, options):
        stdout = (out / (name + '.stdout.txt')).open('x', encoding='utf-8')
        stderr = (out / (name + '.stderr.txt')).open('x', encoding='utf-8')
        handles.extend([stdout, stderr])
        command = [sys.executable, '-X', 'utf8', '-u', str(ROOT / 'Tools' / script), *options,
                   *base, '--output', str(out / name)]
        proc = subprocess.Popen(command, cwd=ROOT, stdout=stdout, stderr=stderr,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        children[name] = proc
        state['runs'][name] = {'pid': proc.pid, 'started_utc': utc(), 'command': command}
        save(out / 'state.json', state)
        return proc

    rs = ['--port', args.rs485_port, '--response-timeout-ms', '500']
    can = ['--dll', str(args.can_dll.resolve()), '--channel', '0x51', '--response-timeout-ms', '500']
    try:
        if not ready.wait(5) or state['uart']['error']:
            raise RuntimeError('UART capture could not start: ' + str(state['uart']['error']))
        # Actual smoke results are persisted before any long window can start.
        for name, script, options in (
            ('rs485_smoke', 'pc_rs485_ping_pong.py', [*rs, '--rounds', '1']),
            ('can_smoke', 'pc_can_request_bench.py', [*can, '--rounds', '1']),
            ('rs485_1000', 'pc_rs485_ping_pong.py', [*rs, '--rounds', '1000'])):
            state['stage'] = name
            proc = launch(name, script, options)
            code = proc.wait(timeout=120)
            state['runs'][name].update(exit_code=code, finished_utc=utc())
            if code:
                raise RuntimeError(f'{name} did not pass (exit {code}); no retry')
        state['stage'] = 'formal_windows_running'
        formal_started = time.monotonic()
        launch('rs485_1hour', 'pc_rs485_ping_pong.py', [*rs, '--duration-seconds', '3600', '--interval-ms', '100'])
        launch('can_1800', 'pc_can_request_bench.py', [*can, '--rounds', str(args.can_rounds), '--interval-ms', '1000'])
        while any(proc.poll() is None for proc in children.values()):
            if time.monotonic() - formal_started > max(3660, args.can_rounds + 60):
                raise TimeoutError('supervisor deadline exceeded; incomplete run')
            for name, proc in children.items():
                if proc.poll() is not None and 'exit_code' not in state['runs'][name]:
                    state['runs'][name].update(exit_code=proc.returncode, finished_utc=utc())
            state['updated_utc'] = utc()
            save(out / 'state.json', state)
            time.sleep(2)
        for name, proc in children.items():
            state['runs'][name].setdefault('exit_code', proc.returncode)
            state['runs'][name].setdefault('finished_utc', utc())
        state['stage'] = 'finished_pending_review'
    except BaseException as exc:
        state.update(stage='stopped_with_error', error=f'{type(exc).__name__}: {exc}')
    finally:
        for name, proc in children.items():
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=10)
                state['runs'][name].update(exit_code=proc.returncode, forced_stop=True)
        stop.set()
        thread.join(timeout=3)
        for handle in handles:
            handle.close()
        state['finished_utc'] = utc()
        save(out / 'state.json', state)
    return 0 if state['stage'] == 'finished_pending_review' else 1


if __name__ == '__main__':
    raise SystemExit(main())
