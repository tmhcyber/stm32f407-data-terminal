"""Full, build-only Keil checks of mutually exclusive RS485 modes.

Outputs are isolated under tmp/; no download/debug/run options or build hooks.
The default project configuration and its previous artifacts are not changed.
"""
import argparse
import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_KEIL = os.environ.get('KEIL_UVISION', 'F:/Keil_v5/UV4/uVision.com')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('0', '1', 'both'), default='both')
    parser.add_argument('--diagnostics', type=int, choices=(0, 1), default=0,
                        help='preserve diagnostic uplink when integrating with the stage6 gateway')
    parser.add_argument('--keil', type=Path, default=Path(DEFAULT_KEIL),
                        help='uVision.com path; alternatively set KEIL_UVISION')
    args = parser.parse_args()
    if not args.keil.is_file():
        parser.error('Set --keil or KEIL_UVISION to the installed uVision.com')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    output = ROOT / 'tmp' / ('modbus-build-' + stamp)
    output.mkdir(parents=True)
    builds = []
    modes = (0, 1) if args.mode == 'both' else (int(args.mode),)
    for mode in modes:
        tree = ET.parse(ROOT / 'MDK-ARM/IndustrialTerminal.uvprojx')
        for tag in ('RunUserProg1', 'RunUserProg2'):
            if any(node.text != '0' for node in tree.findall('.//' + tag)):
                raise RuntimeError('Refusing enabled build hook: ' + tag)
        target = tree.find('.//Target')
        name = 'ModbusCheck' + str(mode)
        debug_config = ROOT / 'MDK-ARM' / 'DebugConfig' / (name + '_STM32F407ZGTx.dbgconf')
        debug_config_existed = debug_config.exists()
        target.find('TargetName').text = name
        folder = output / str(mode)
        folder.mkdir()
        common = target.find('TargetOption/TargetCommonOption')
        for key in ('OutputDirectory', 'ListingPath'):
            common.find(key).text = str(folder) + '\\'
        defines = target.find('.//Cads/VariousControls/Define')
        defines.text += ',APP_RS485_MODBUS_ENABLED=' + str(mode)
        defines.text += ',APP_DIAGNOSTICS_ENABLED=' + str(args.diagnostics)
        project = ROOT / 'MDK-ARM' / ('modbus-check-' + stamp + '-' + str(mode) + '.uvprojx')
        if project.exists():
            raise FileExistsError(project)
        tree.write(project, encoding='utf-8', xml_declaration=True)
        log = folder / 'build.log'
        command = [str(args.keil), '-r', str(project), '-t', name, '-j0', '-o', str(log)]
        try:
            result = subprocess.run(command, cwd=ROOT / 'MDK-ARM', capture_output=True,
                                    timeout=240, creationflags=(subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0))
            content = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
            summary = re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)', content)
            ok = result.returncode == 0 and summary is not None and summary.group(1) == '0' and summary.group(2) == '0'
            builds.append(dict(mode=mode, diagnostics=args.diagnostics, command=command, exit_code=result.returncode,
                               passed=ok, summary=summary.group(0) if summary else None,
                               artifacts={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                          for p in folder.iterdir() if p.suffix in ('.axf', '.hex', '.map')}))
            (folder / 'project.xml').write_bytes(project.read_bytes())
            print('mode', mode, 'exit', result.returncode, content[-1600:], flush=True)
        finally:
            # Only our uniquely named generated files, never existing user files.
            for extra in project.parent.glob(project.stem + '.*'):
                if extra.is_file():
                    if extra != project:
                        (folder / extra.name).write_bytes(extra.read_bytes())
                    extra.unlink()
            if not debug_config_existed and debug_config.is_file():
                (folder / 'generated-debug-config.txt').write_bytes(debug_config.read_bytes())
                debug_config.unlink()
    sources = {}
    for directory in ('Application', 'BSP', 'Core', 'Middlewares/Modbus'):
        for path in (ROOT / directory).rglob('*'):
            if path.suffix in ('.c', '.h', '.s'):
                sources[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    report = dict(builds=builds, sources=sources, build_only=True,
                  baseline=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip())
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Evidence:', output, flush=True)
    return 0 if all(item['passed'] for item in builds) else 1


if __name__ == '__main__':
    raise SystemExit(main())
