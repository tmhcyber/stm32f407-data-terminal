import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('pc_modbus_read', ROOT/'Tools/pc_modbus_read.py')
PC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PC)


class ReaderTests(unittest.TestCase):
    def test_signed_quality_and_ranges(self):
        self.assertEqual(PC.decode_registers([0xFB2E, 6012, 1])['temperature_c'], -12.34)
        self.assertIsNone(PC.decode_registers([0, 0, 0])['temperature_c'])
        self.assertEqual(PC.decode_registers([2534, 6012, 3])['quality'], 'offline')
        for registers in ([1, 2], [1, 2, 4], [1, 10001, 1], [-1, 2, 1]):
            with self.assertRaises(ValueError):
                PC.decode_registers(registers)

    def test_session_records_error_recovery_and_configuration(self):
        options = {}
        class Response:
            registers = [2534, 6012, 1]
            def __init__(self, error=False): self.error = error
            def isError(self): return self.error
        class Client:
            def __init__(self, **kwargs): options.update(kwargs); self.calls = 0
            def connect(self): return True
            def close(self): options['closed'] = True
            def read_input_registers(self, **kwargs):
                self.assertion = kwargs == dict(address=0, count=3, device_id=1)
                if not self.assertion: raise AssertionError(kwargs)
                self.calls += 1
                options['trace_packet'](False, b'\x01\x04')
                return Response(self.calls == 1)
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            output = Path(directory)/'new'
            result = PC.run_session('TEST_ONLY', output, 2, 0, client_factory=Client)
            self.assertEqual((result['attempts'], result['valid_responses'], result['errors']), (2, 1, 1))
            self.assertTrue(options['closed'])
            self.assertEqual((options['baudrate'], options['parity'], options['retries']), (115200, 'E', 0))
            self.assertEqual(len((output/'readings.jsonl').read_text().splitlines()), 2)
            self.assertEqual(json.loads((output/'summary.json').read_text())['errors'], 1)
            with self.assertRaises(FileExistsError):
                PC.run_session('TEST_ONLY', output, client_factory=Client)

    def test_connect_failure_is_recorded(self):
        class Client:
            def __init__(self, **kwargs): pass
            def connect(self): return False
            def close(self): pass
        with tempfile.TemporaryDirectory() as directory:
            result = PC.run_session('TEST_ONLY', Path(directory)/'new', client_factory=Client)
            self.assertEqual(result['attempts'], 0)
            self.assertEqual(result['errors'], 1)
            self.assertIn('session_error', result)


if __name__ == '__main__':
    unittest.main()
