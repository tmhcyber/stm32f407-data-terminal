"""Read the STM32's three input registers with PyModbus 3.11.4.

Explicit serial port, no scanning or firmware download. JSONL timestamps
are host receive/trace times, not sensor sampling times.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import time

QUALITY = ('not_available', 'valid', 'stale', 'offline')


def decode_registers(registers):
    if len(registers) != 3 or any(not isinstance(v, int) or not 0 <= v <= 65535 for v in registers):
        raise ValueError('expected exactly three unsigned 16-bit registers')
    temperature, humidity, quality = registers
    if quality > 3 or (quality != 0 and humidity > 10000):
        raise ValueError('invalid quality or humidity')
    signed = temperature if temperature < 32768 else temperature - 65536
    return dict(temperature_c=signed / 100 if quality else None,
                humidity_rh=humidity / 100 if quality else None,
                quality=QUALITY[quality], registers=list(registers))


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def run_session(port, folder, rounds=10, interval_ms=100, firmware=None, client_factory=None):
    if rounds <= 0 or interval_ms < 0:
        raise ValueError('rounds must be positive; interval must be nonnegative')
    if client_factory is None:
        from pymodbus.client import ModbusSerialClient
        client_factory = ModbusSerialClient
    folder = Path(folder)
    # Validate the firmware path before creating output or touching the port.
    firmware_info = None
    if firmware:
        image = Path(firmware)
        firmware_info = dict(path=str(image.resolve()), sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                             note='candidate image; this tool cannot verify what is flashed')
    folder.mkdir(parents=True, exist_ok=False)
    summary = dict(port=port, baudrate=115200, bytesize=8, parity='E', stopbits=1,
                   slave=1, function=4, address=0, count=3, requested_rounds=rounds,
                   interval_ms=interval_ms, firmware=firmware_info, started_utc=utc_now(),
                   attempts=0, valid_responses=0, errors=0, interrupted=False,
                   qualities={name: 0 for name in QUALITY})
    started = time.monotonic()
    with (folder / 'packets.jsonl').open('x', encoding='utf-8') as packets, \
         (folder / 'readings.jsonl').open('x', encoding='utf-8') as readings:
        def trace(sending, data):
            packets.write(json.dumps(dict(host_utc=utc_now(), elapsed_s=time.monotonic()-started,
                                         direction='tx' if sending else 'rx', chunk_hex=data.hex())) + '\n')
            packets.flush()
            return data
        client = None
        try:
            client = client_factory(port=port, baudrate=115200, bytesize=8, parity='E',
                                    stopbits=1, timeout=0.5, retries=0, trace_packet=trace)
            if not client.connect():
                raise ConnectionError('could not open the selected serial port')
            for index in range(rounds):
                request_started = time.monotonic()
                summary['attempts'] += 1
                row = dict(round=index+1)
                try:
                    response = client.read_input_registers(address=0, count=3, device_id=1)
                    if response.isError():
                        raise ValueError(str(response))
                    row.update(decode_registers(response.registers))
                    summary['valid_responses'] += 1
                    summary['qualities'][row['quality']] += 1
                except Exception as error:  # log failure, retain subsequent recovery observations
                    summary['errors'] += 1
                    row['error'] = str(error)
                row.update(host_utc=utc_now(), elapsed_s=time.monotonic()-started,
                           round_trip_ms=(time.monotonic()-request_started)*1000)
                readings.write(json.dumps(row, ensure_ascii=False) + '\n')
                readings.flush()
                print(json.dumps(row, ensure_ascii=False), flush=True)
                if index + 1 < rounds:
                    time.sleep(max(0, interval_ms/1000 - (time.monotonic()-request_started)))
        except KeyboardInterrupt:
            summary['interrupted'] = True
        except Exception as error:
            summary['errors'] += 1
            summary['session_error'] = str(error)
        finally:
            if client is not None:
                try:
                    client.close()
                except Exception as error:
                    summary['errors'] += 1
                    summary['close_error'] = str(error)
            summary.update(ended_utc=utc_now(), duration_s=time.monotonic()-started)
            (folder / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='explicit USB-RS485 port, never the CH340 uplink port')
    parser.add_argument('--output', type=Path, required=True, help='new output directory')
    parser.add_argument('--rounds', type=int, default=10)
    parser.add_argument('--interval-ms', type=int, default=100)
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--library-dir', type=Path, help='optional isolated PyModbus install directory')
    args = parser.parse_args()
    if args.rounds <= 0 or args.interval_ms < 10:
        parser.error('rounds must be positive; interval-ms must be at least 10')
    if args.library_dir:
        sys.path.insert(0, str(args.library_dir.resolve()))
    try:
        summary = run_session(args.port, args.output, args.rounds, args.interval_ms, args.firmware)
    except (ImportError, FileNotFoundError, FileExistsError) as error:
        parser.exit(2, str(error)+'\nInstall pymodbus==3.11.4 and pyserial, and use a new output directory.\n')
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary['errors'] == 0 and not summary['interrupted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
