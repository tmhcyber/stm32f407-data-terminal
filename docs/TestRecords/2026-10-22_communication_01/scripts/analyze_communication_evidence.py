"""Offline audit of completed communication logs; never opens a device."""
import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path

from pc_uplink_receiver import parse_uplink_line
from pc_can_request_bench import decode_response


def sha(data):
    return hashlib.sha256(data).hexdigest()


def audit(root):
    state = json.loads((root / 'state.json').read_text(encoding='utf-8'))
    assert state['stage'] == 'finished_pending_review', state['stage']
    assert state['uart']['error'] is None, state['uart']
    result = {'scope': 'offline raw-log verification; not board Watch counters', 'runs': {}}
    for name in ('rs485_smoke', 'can_smoke', 'rs485_1000', 'rs485_1hour', 'can_1800'):
        folder = root / name
        summary = json.loads((folder / 'summary.json').read_text(encoding='utf-8'))
        raw = (folder / 'events.jsonl').read_bytes()
        assert sha(raw) == summary['events_sha256'], name + ': digest'
        events = [json.loads(line) for line in raw.splitlines()]
        rounds = [r for r in events if r['event'] == 'round']
        tx = [r for r in events if r['event'] == 'tx']
        rx = [r for r in events if r['event'] == 'rx']
        assert state['runs'][name]['exit_code'] == 0, name + ': exit code'
        assert summary['completed'] and summary['periodic_target_met'], name + ': completion'
        assert len(rounds) == len(tx) == summary['requests'] == summary['successes'], name + ': counts'
        assert [r['round'] for r in rounds] == list(range(1, len(rounds) + 1)), name + ': round order'
        assert all(r['status'] == 'ok' for r in rounds), name + ': round status'
        assert not any(r['event'] in ('error', 'extra_rx') for r in events), name + ': extra/error'
        assert summary['timeouts'] == summary['errors'] == summary['missed_slots'] == 0, name + ': errors'
        if name.startswith('rs485'):
            assert all(bytes.fromhex(r['hex']) == b'V1|PING\r\n' and r['written'] == 9 for r in tx)
            assert b''.join(bytes.fromhex(r['hex']) for r in rx) == b'V1|PONG\r\n' * len(rounds)
            assert all(bytes.fromhex(r['response_hex']) == b'V1|PONG\r\n' for r in rounds)
        else:
            assert all(r['id'] == 0x123 and r['type'] == 0 and r['dlc'] == 1 and r['data'] == [1] for r in tx)
            responses = [r for r in rx if r['type'] != 0x20]
            assert len(responses) == len(rounds), name + ': response count'
            assert all(decode_response(r)['quality'] == 1 for r in responses)
        deltas = [b['elapsed_s'] - a['elapsed_s'] for a, b in zip(rounds, rounds[1:])]
        result['runs'][name] = {'hash_verified': True, 'requests': len(tx), 'valid_responses': len(rounds),
                               'first_round_utc': rounds[0]['utc'], 'last_round_utc': rounds[-1]['utc'],
                               'elapsed_s': summary['elapsed_s'],
                               'observed_request_interval_min_s': min(deltas) if deltas else None,
                               'observed_request_interval_max_s': max(deltas) if deltas else None}
    uart = (root / 'uart.raw').read_bytes()
    chunks_bytes = (root / 'uart_chunks.jsonl').read_bytes()
    chunks = [json.loads(line) for line in chunks_bytes.splitlines()]
    assert b''.join(bytes.fromhex(c['hex']) for c in chunks) == uart, 'UART chunk reconstruction'
    assert uart.endswith(b'\r\n'), 'incomplete UART tail'
    lines = [line for line in uart.split(b'\r\n') if line]
    records = [parse_uplink_line(line) for line in lines]
    assert records, 'empty UART'
    points = [r.points[0] for r in records]
    assert all(p.is_current for r in records for p in r.points), 'UART nonvalid data'
    seq_steps = Counter((b.sequence - a.sequence) & 0xFFFFFFFF for a, b in zip(points, points[1:]))
    tick_steps = Counter((b.value_ms - a.value_ms) & 0xFFFFFFFF for a, b in zip(points, points[1:]))
    assert set(seq_steps) == {1}, 'UART sequence gap/regression'
    assert set(tick_steps) == {1000}, 'UART sample timestamp gap/regression'
    result['uart'] = {'raw_bytes': len(uart), 'raw_sha256': sha(uart), 'chunks_sha256': sha(chunks_bytes),
                      'chunk_reconstruction_verified': True, 'records': len(records), 'parse_errors': 0,
                      'first_chunk_utc': chunks[0]['utc'], 'last_chunk_utc': chunks[-1]['utc'],
                      'host_observation_span_s': (datetime.fromisoformat(chunks[-1]['utc']) - datetime.fromisoformat(chunks[0]['utc'])).total_seconds(),
                      'first_sequence': points[0].sequence, 'last_sequence': points[-1].sequence,
                      'sequence_steps': dict(seq_steps), 'sample_tick_steps_ms': dict(tick_steps),
                      'first_sample_tick_ms': points[0].value_ms, 'last_sample_tick_ms': points[-1].value_ms,
                      'quality_status': dict(Counter(p.quality + '/' + p.status for p in points)),
                      'temperature_min_c': min(p.value for p in points) / 1000,
                      'temperature_max_c': max(p.value for p in points) / 1000,
                      'humidity_min_rh': min(r.points[1].value for r in records) / 1000,
                      'humidity_max_rh': max(r.points[1].value for r in records) / 1000,
                      'reset_conclusion': 'No sequence/timestamp reset or startup output observed in this capture; no independent reset-counter/Watch audit.'}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    verification = audit(args.source)
    verification['audit_script_sha256'] = sha(Path(__file__).read_bytes())
    args.output.write_text(json.dumps(verification, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(verification, ensure_ascii=False, indent=2))
