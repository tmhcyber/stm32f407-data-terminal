"""Build the FreeRTOS terminal in a fresh directory without downloading."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
MDK = ROOT / "MDK-ARM"
TARGET = "IndustrialTerminal_RTOS"
PROJECT = MDK / "IndustrialTerminal.uvprojx"
DEFINES = ("APP_RTOS_ENABLED=1", "APP_RS485_MODBUS_ENABLED=1", "APP_DIAGNOSTICS_ENABLED=1")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compiled_dependencies(output):
    dependencies = {}
    for dependency in output.glob("*.d"):
        for line in dependency.read_text(errors="replace").splitlines():
            if ": " in line:
                path = (MDK / line.split(": ", 1)[1].strip()).resolve()
                if path.is_file():
                    dependencies[path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT)
                                 else str(path)] = digest(path)
    scatter = MDK / "IndustrialTerminal.sct"
    dependencies[scatter.relative_to(ROOT).as_posix()] = digest(scatter)
    return dependencies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keil", type=Path, default=os.environ.get(
        "KEIL_UVISION", "F:/Keil_v5/UV4/uVision.com"))
    args = parser.parse_args()
    keil = args.keil.resolve(strict=True)

    tree = ET.parse(PROJECT)
    targets = tree.findall("./Targets/Target")
    target, = (item for item in targets if item.findtext("TargetName") == TARGET)
    for name in ("RunUserProg1", "RunUserProg2"):
        if any(node.text != "0" for node in target.findall(".//" + name)):
            raise RuntimeError("Enabled build hook: " + name)
    c_defines = target.findtext(".//Cads/VariousControls/Define").split(",")
    if any(define not in c_defines for define in DEFINES):
        raise RuntimeError("Unexpected FreeRTOS target defines")

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = ROOT / "tmp" / ("rtos-build-" + stamp)
    output.mkdir(parents=True)
    common = target.find("TargetOption/TargetCommonOption")
    for key in ("OutputDirectory", "ListingPath"):
        common.find(key).text = str(output) + "\\"
    for item in targets:
        if item != target:
            tree.find("./Targets").remove(item)
    project = MDK / (output.name + ".uvprojx")
    debug_config = MDK / "DebugConfig" / (TARGET + "_STM32F407ZGTx.dbgconf")
    created_debug_config = not debug_config.exists()
    log = output / "build.log"
    command = [str(keil), "-r", str(project), "-t", TARGET, "-j0", "-o", str(log)]
    report = dict(target=TARGET, defines=c_defines, project_sha256=digest(PROJECT),
                  baseline=subprocess.check_output(["git", "rev-parse", "HEAD"],
                                                   cwd=ROOT, text=True).strip(), command=command)
    tree.write(project, encoding="utf-8", xml_declaration=True)
    try:
        result = subprocess.run(command, cwd=MDK, capture_output=True, timeout=240,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        content = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        summary = re.search(r"(\d+) Error\(s\), (\d+) Warning\(s\)", content)
        artifacts = {path.name: digest(path) for path in output.iterdir()
                     if path.suffix in (".axf", ".hex", ".map")}
        report.update(exit_code=result.returncode, summary=summary.group(0) if summary else None,
                      artifacts=artifacts, compiled_dependencies=compiled_dependencies(output))
        report["passed"] = (result.returncode == 0 and summary is not None
                            and summary.groups() == ("0", "0")
                            and all(TARGET + suffix in artifacts for suffix in (".axf", ".hex", ".map")))
        print(content[-3500:])
    finally:
        (output / "project.xml").write_bytes(project.read_bytes())
        project.unlink()
        for extra in MDK.glob(project.stem + ".*"):
            if extra.is_file():
                extra.replace(output / extra.name)
        if created_debug_config and debug_config.is_file():
            debug_config.replace(output / "generated-debug-config.txt")
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("Evidence:", output, flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
