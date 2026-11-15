"""Execute production diagnostic code on host; export actual C formatter vectors."""
import hashlib
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
compiler=shutil.which('clang') or next(Path.home().glob('AppData/Local/Microsoft/WinGet/Packages/*LLVM-MinGW*/**/clang.exe'),None)
assert compiler,'clang required'
out=ROOT/('tmp/stage6-host-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'));out.mkdir(parents=True,exist_ok=False)
sources=['Tests/C/test_stage6.c','Application/Src/app_diagnostic.c','Application/Src/app_uplink_service.c',
         'Application/Src/app_data_service.c',
         'Application/Src/app_snapshot_text_formatter.c','Application/Src/app_system_monitor.c',
         'BSP/Src/bsp_system_diagnostics_record.c']
report=[]
for enabled in (0,1):
    exe=out/f'test-{enabled}.exe'
    cmd=[str(compiler),'-std=c11','-Wall','-Wextra','-Werror','-pedantic','-O0','-g',
         f'-DAPP_DIAGNOSTICS_ENABLED={enabled}']
    for inc in ('Application/Inc','BSP/Inc','Components/SHT30/Inc'):cmd+=['-I',str(ROOT/inc)]
    cmd += [str(ROOT/s) for s in sources]+['-o',str(exe)]
    build=subprocess.run(cmd,capture_output=True);(out/f'build-{enabled}.txt').write_bytes(build.stdout+build.stderr)
    assert build.returncode==0,build.stderr.decode(errors='replace')
    test=subprocess.run([str(exe)],capture_output=True);(out/f'test-{enabled}.txt').write_bytes(test.stdout+test.stderr)
    assert test.returncode==0,test.stdout.decode(errors='replace')+test.stderr.decode(errors='replace')
    report.append(dict(enabled=enabled,command=cmd,build_exit=build.returncode,test_exit=test.returncode,
                       exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest()))
vectors=subprocess.check_output([str(out/'test-1.exe'),'--vectors'])
(out/'vectors.bin').write_bytes(vectors)
(out/'result.json').write_text(json.dumps(dict(sources={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources},
                                            vectors_sha256=hashlib.sha256(vectors).hexdigest(),builds=report),indent=2)+'\n')
print(json.dumps(report,indent=2))
