"""Build portable, headed test records without touching live evidence files.

Run again after completion. Existing raw UART/JSONL input is never moved or edited.
"""
import argparse
from datetime import datetime, timezone, timedelta
import gzip
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
LOCAL = timezone(timedelta(hours=8))
BEGIN = '<!-- TEST_RESULTS_BEGIN -->'
END = '<!-- TEST_RESULTS_END -->'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write(path, text):
    path.write_text(text, encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if source == output or source in output.parents:
        parser.error('archive must be separate from the live source directory')
    output.mkdir(parents=True, exist_ok=True)
    setup = read_json(source / 'setup.json')
    observations = read_json(output / 'observations.json')
    tests = [
        ('rs485_1000', 'RS485 连续轮次', '1000轮，停止等待', '115200 bit/s，8N1'),
        ('rs485_1hour', 'RS485 持续通信', '3600秒，目标10 Hz；前30分钟叠加CAN', '115200 bit/s，8N1'),
        ('can_1800', 'CAN 周期请求', '1800次，1 Hz；叠加RS485', '500 kbit/s，标准数据帧'),
    ]
    rows, metadata = [], {}
    for name, label, scale, baud in tests:
        manifest = read_json(source / name / 'manifest.json')
        provenance = manifest['provenance']
        head = provenance['git_head']
        dirty = bool(provenance['git_status'])
        revision = head[:7] + ('+dirty' if dirty else '')
        scripts = {key: value for key, value in provenance['source_sha256'].items()
                   if key in ('Tools/communication_bench.py', 'Tools/pc_rs485_ping_pong.py',
                              'Tools/pc_can_request_bench.py', 'Tools/run_communication_session.py')}
        for relative, expected in scripts.items():
            current = ROOT / relative
            if digest(current.read_bytes()) == expected:
                (output / 'scripts').mkdir(exist_ok=True)
                shutil.copyfile(current, output / 'scripts' / current.name)
        adapter = observations['can'] if name.startswith('can') else observations['rs485']
        summary_path = source / name / 'summary.json'
        summary = read_json(summary_path) if summary_path.exists() else None
        result = '进行中，未验收'
        if summary is not None:
            raw = (source / name / 'events.jsonl').read_bytes()
            if digest(raw) != summary['events_sha256']:
                raise ValueError(name + ': raw log SHA256 differs from summary')
            events = [json.loads(line) for line in raw.splitlines()]
            rounds = [event for event in events if event['event'] == 'round']
            ok = sum(event['status'] == 'ok' for event in rounds)
            if summary.get('completed') and (len(rounds) != summary['requests'] or ok != summary['successes']):
                raise ValueError(name + ': summary counts do not match round log')
            passed = summary['completed'] and summary.get('periodic_target_met', False)
            result = ('PC链路通过' if passed else '未通过，保留失败记录') + f"：{summary.get('successes', 0)}/{summary.get('requests', 0)}"
            date = datetime.fromisoformat(events[0]['utc']).astimezone(LOCAL).strftime('%Y-%m-%d')
        else:
            date = datetime.fromisoformat(setup['started_utc']).astimezone(LOCAL).strftime('%Y-%m-%d')
        fields = {
            'schema': 'TestRecord-v1', 'date': date, 'timezone': 'Asia/Taipei (UTC+8)',
            'commit': head, 'dirty_worktree': dirty,
            'board': '野火 STM32F407ZG 霸天虎 V2', 'adapter': adapter,
            'indicators': observations['indicators'], 'baudrate': baud,
            'script_version_sha256': scripts, 'firmware_axf_sha256': setup['firmware_sha256'],
            'test': label, 'scale': scale, 'result': result,
        }
        metadata[name] = {'header': fields, 'summary': summary}
        header = '\n'.join('# ' + key + ': ' + (json.dumps(value, ensure_ascii=False) if isinstance(value, (dict, bool)) else str(value)) for key, value in fields.items())
        detail = ['# ' + label, '', '```text', header, '```', '',
                  '提交栏为测试时基线；+dirty表示包含未提交改动。归档/发布提交另行记录，不替代测试版本。', '',
                  '固件版本由用户确认Keil编译/下载，主机保存AXF哈希；未做芯片映像读回。', '']
        if summary is not None:
            payload = (header + '\n# END_HEADER: raw original events.jsonl follows\n').encode('utf-8') + raw
            packed = gzip.compress(payload, mtime=0)
            if gzip.decompress(packed) != payload:
                raise ValueError('compression round-trip failed')
            (output / (name + '.log.gz')).write_bytes(packed)
            write(output / (name + '.summary.json'), json.dumps(summary, ensure_ascii=False, indent=2))
            detail += [f'[完整带头日志（gzip）]({name}.log.gz) · [统计原文]({name}.summary.json)', '',
                       f'原始JSONL正文SHA256：`{summary["events_sha256"]}`；解压后去掉END_HEADER行及其之前的头部即可得到逐字节原文。', '',
                       f'实测时长：{summary.get("elapsed_s", 0):.6f}秒；超时：{summary.get("timeouts", 0)}；错误：{summary.get("errors", 0)}；跳过时隙：{summary.get("missed_slots", 0)}。', '',
                       '该判定只覆盖PC请求/应答和周期目标，板端计数、采集连续性与复位需另核对UART及Watch。']
        else:
            detail += ['正式窗口仍在运行，结束后重新执行归档工具生成完整带头日志；不复制半截日志冒充最终结果。']
        write(output / (name + '.md'), '\n'.join(detail) + '\n')
        rows.append(f'| {date} | `{revision}` | {label} | {scale} | {result} | [记录与日志](Docs/TestRecords/{output.name}/{name}.md) |')
    host = output.parent / '2026-10-22_host_01'
    if host.exists():
        rows.append(f'| 2026-10-22 | `{head[:7]}+dirty` | 主机自动化检查 | Python 119项；生产C 477项断言；CAN 16场景及测试组 | 各自通过，分开计数；C复用同日结果 | [检查记录](Docs/TestRecords/{host.name}/README.md) |')
    table = '\n'.join(['## 测试结果', '',
                       '| 日期 | 提交（测试基线） | 测试项目 | 规模 | 结果 | 日志链接 |',
                       '| --- | --- | --- | --- | --- | --- |', *rows, '',
                       '测试提交含未提交修改（+dirty），由记录中的固件及脚本SHA256进一步绑定。长测前30分钟两条总线并行；主机检查、C断言和实板轮次分别计数。'])
    write(output / 'metadata.json', json.dumps(metadata, ensure_ascii=False, indent=2))
    write(output / 'README_TABLE.md', table + '\n')
    local_table = table.replace('](Docs/TestRecords/' + output.name + '/', '](').replace('](Docs/TestRecords/2026-10-22_host_01/', '](../2026-10-22_host_01/')
    write(output / 'README.md', '# 本轮测试记录\n\n' + local_table + '\n')
    readme = ROOT / 'README.md'
    text = readme.read_text(encoding='utf-8')
    block = BEGIN + '\n' + table + '\n' + END
    if BEGIN in text:
        start, rest = text.split(BEGIN, 1)
        _, tail = rest.split(END, 1)
        text = start + block + tail
    else:
        text = text.replace('## 开始工作', block + '\n\n## 开始工作', 1)
    write(readme, text)
    # Raw UART is archived only after the supervisor has closed it.
    state = read_json(source / 'state.json')
    if state['stage'] in ('finished_pending_review', 'stopped_with_error'):
        for name in ('uart.raw', 'uart_chunks.jsonl'):
            raw_uart = (source / name).read_bytes()
            (output / (name + '.gz')).write_bytes(gzip.compress(raw_uart, mtime=0))
    print(json.dumps({'archive': str(output), 'complete_records': [name for name, value in metadata.items() if value['summary'] is not None]}, ensure_ascii=False))


if __name__ == '__main__':
    main()
