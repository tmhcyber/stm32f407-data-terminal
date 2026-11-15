"""Verify published source, firmware and Modbus evidence using only stdlib."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    manifest = json.loads((ROOT / 'evidence/modbus/MANIFEST.json').read_text(encoding='utf-8'))
    for name, expected in manifest['sources_sha256_lf'].items():
        actual = sha((ROOT / name).read_bytes().replace(b'\r\n', b'\n'))
        if actual != expected:
            raise ValueError('source mismatch: ' + name)
    for name, expected in manifest['evidence_sha256'].items():
        if sha((ROOT / name).read_bytes()) != expected:
            raise ValueError('evidence mismatch: ' + name)
    with zipfile.ZipFile(ROOT / 'evidence/modbus/verified-firmware.zip') as archive:
        for name, expected in manifest['firmware_members_sha256'].items():
            if sha(archive.read(name)) != expected:
                raise ValueError('firmware mismatch: ' + name)
    base = ROOT / 'evidence/modbus'
    for folder, count in (('normal-600', 600), ('address-fault', 320)):
        summary = json.loads((base / folder / 'summary.json').read_text(encoding='utf-8'))
        rows = [json.loads(line) for line in (base / folder / 'readings.jsonl').read_text(encoding='utf-8').splitlines()]
        assert summary['attempts'] == summary['valid_responses'] == len(rows) == count
        assert summary['errors'] == 0 and all('error' not in row for row in rows)
    cases = json.loads((base / 'request-cases.json').read_text(encoding='utf-8'))
    assert cases['passed'] and len(cases['cases']) == 11
    assert all(c['passed'] and c['recovery_passed'] for c in cases['cases'])
    from analyze_modbus_waveform import analyze
    timing, _, _ = analyze(base / 'waveform')
    assert timing['passed'] and len(timing['measurements']) == 10
    print(f"PASS: {len(manifest['sources_sha256_lf'])} source/tool hashes, "
          f"{len(manifest['evidence_sha256'])} evidence hashes, firmware, 920 readings, 11 cases, 10 waveforms")

if __name__ == '__main__':
    main()
