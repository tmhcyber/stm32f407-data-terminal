"""Cross-check actual production C packets with PyModbus (no serial port)."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library-dir', type=Path)
    parser.add_argument('--bridge', type=Path, default=Path('tmp/c-tests/modbus_bridge.exe'))
    args = parser.parse_args()
    if args.library_dir:
        sys.path.insert(0, str(args.library_dir.resolve()))
    from pymodbus.framer.rtu import FramerRTU
    from pymodbus.pdu import DecodePDU
    framer = FramerRTU(DecodePDU(False))
    checks = 0
    for temperature in (-12345, 25340):
        for quality in range(4):
            expected = [int(temperature/10) & 65535, 6012, quality] if quality else [0, 0, 0]
            for start, count in ((0, 3), (0, 1), (1, 2), (2, 1), (2, 2), (65535, 2), (0, 0), (0, 126)):
                packet = framer.encode(b'\x04' + struct.pack('>HH', start, count), 1, 0)
                response = bytes.fromhex(subprocess.check_output(
                    [str(args.bridge.resolve()), packet.hex(), str(temperature), '60120', str(quality)], text=True).strip())
                used, address, _, payload = framer.decode(response)
                assert used == len(response) and address == 1, response.hex()
                pdu = framer.decoder.decode(payload)
                if count == 0 or count > 125:
                    assert pdu.isError() and pdu.exception_code == 3
                elif start >= 3 or count > 3-start:
                    assert pdu.isError() and pdu.exception_code == 2
                else:
                    assert not pdu.isError() and pdu.registers == expected[start:start+count]
                checks += 1
    for address, corrupt in ((0, False), (2, False), (1, True)):
        packet = framer.encode(b'\x04\x00\x00\x00\x03', address, 0)
        if corrupt: packet = packet[:-1] + bytes([packet[-1] ^ 1])
        output = subprocess.check_output([str(args.bridge.resolve()), packet.hex(), '0', '0', '1'], text=True)
        assert output.strip() == ''
        checks += 1
    packet = framer.encode(b'\x03\x00\x00\x00\x03', 1, 0)
    output = subprocess.check_output([str(args.bridge.resolve()), packet.hex(), '0', '0', '1'], text=True)
    pdu = framer.decoder.decode(framer.decode(bytes.fromhex(output))[3])
    assert pdu.isError() and pdu.exception_code == 1
    print(f'PyModbus 3.11.4 / production C packet interoperability: {checks+1} cases passed (no hardware)')


if __name__ == '__main__':
    main()
