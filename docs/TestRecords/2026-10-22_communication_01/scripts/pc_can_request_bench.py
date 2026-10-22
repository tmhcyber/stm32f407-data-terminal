"""Classic CAN 0x123/01 -> 0x322/5 bench using the bundled PCAN-Basic ABI.

ABI/values: PCAN/二次开发（参考）/PCAN-Basic-Windows.zip,
PCAN-Basic-Windows/Include/PCANBasic.h. No driver install or firmware update.
"""
import argparse
import ctypes as C
from pathlib import Path
import sys
import time

from communication_bench import (
    add_campaign_arguments, execute, positive, validate_campaign_arguments,
)


class Message(C.Structure):
    _fields_ = [("ID", C.c_uint32), ("MSGTYPE", C.c_uint8),
                ("LEN", C.c_uint8), ("DATA", C.c_uint8 * 8)]


class Timestamp(C.Structure):
    _fields_ = [("millis", C.c_uint32), ("millis_overflow", C.c_uint16),
                ("micros", C.c_uint16)]


def decode_response(frame):
    if frame["id"] != 0x322 or frame["type"] != 0 or frame["dlc"] != 5 or len(frame["data"]) != 5:
        raise ValueError("expected standard data frame 0x322 / DLC=5")
    data = bytes(frame["data"])
    quality = data[0]
    temperature = int.from_bytes(data[1:3], "big", signed=True)
    humidity = int.from_bytes(data[3:5], "big")
    if quality > 3 or humidity > 10000 or (quality == 0 and any(data[1:])):
        raise ValueError("invalid quality/humidity/no-data encoding")
    return {"quality": quality, "temperature_c": temperature / 100,
            "humidity_rh": humidity / 100}


class Pcan:
    EMPTY = 0x20

    def __init__(self, dll, channel):
        if sys.platform != "win32":
            raise OSError("PCAN-Basic bench requires Windows")
        self.dll = C.WinDLL(str(Path(dll).resolve()))
        self.channel = channel
        bindings = {
            "CAN_Initialize": [C.c_uint16, C.c_uint16, C.c_uint8, C.c_uint32, C.c_uint16],
            "CAN_Uninitialize": [C.c_uint16],
            "CAN_GetStatus": [C.c_uint16],
            "CAN_Read": [C.c_uint16, C.POINTER(Message), C.POINTER(Timestamp)],
            "CAN_Write": [C.c_uint16, C.POINTER(Message)],
            "CAN_GetValue": [C.c_uint16, C.c_uint8, C.c_void_p, C.c_uint32],
        }
        for name, arguments in bindings.items():
            function = getattr(self.dll, name)
            function.argtypes = arguments
            function.restype = C.c_uint32
        self.check(self.dll.CAN_Initialize(channel, 0x001C, 0, 0, 0))
        try:
            # Verify normal active reception; do not inherit a shared listen-only channel.
            for parameter, expected in ((0x08, 0), (0x0F, 1)):
                value = C.c_uint32()
                self.check(self.dll.CAN_GetValue(channel, parameter, C.byref(value), C.sizeof(value)))
                if value.value != expected:
                    raise RuntimeError(f"PCAN parameter 0x{parameter:02X}={value.value}; close other CAN apps")
            self.check(self.dll.CAN_GetStatus(channel))
        except BaseException:
            self.dll.CAN_Uninitialize(channel)
            raise

    @staticmethod
    def check(status):
        if status:
            raise OSError(f"PCAN-Basic status=0x{status:08X}")

    def read(self):
        message, timestamp = Message(), Timestamp()
        status = self.dll.CAN_Read(self.channel, C.byref(message), C.byref(timestamp))
        if status == self.EMPTY:
            return None
        self.check(status)
        return {"id": message.ID, "type": message.MSGTYPE, "dlc": message.LEN,
                "data": list(message.DATA[:min(message.LEN, 8)]),
                "device_timestamp_us": timestamp.micros + 1000 * (
                    timestamp.millis + (timestamp.millis_overflow << 32))}

    def send(self):
        message = Message()
        message.ID, message.MSGTYPE, message.LEN, message.DATA[0] = 0x123, 0, 1, 1
        self.check(self.dll.CAN_Write(self.channel, C.byref(message)))

    def close(self):
        self.check(self.dll.CAN_Uninitialize(self.channel))


class CanAdapter:
    def __init__(self, bus, emit, clock=time.monotonic, sleep=time.sleep):
        self.bus, self.emit, self.clock, self.sleep = bus, emit, clock, sleep

    def read(self):
        frame = self.bus.read()
        if frame is not None:
            self.emit(event="rx", **frame)
        return frame

    def check_quiet(self):
        # Echo is a PCAN message type bit (0x20), never an application response.
        for _ in range(256):
            frame = self.read()
            if frame is None:
                return
            if frame["type"] == 0x20 and frame["id"] == 0x123 and frame["data"] == [1]:
                continue
            raise RuntimeError("unsolicited/duplicate CAN frame between requests")
        raise RuntimeError("continuous CAN input")

    def send(self):
        self.bus.send()
        self.emit(event="tx", id=0x123, type=0, dlc=1, data=[1])

    def receive(self, timeout):
        deadline = self.clock() + timeout
        while self.clock() < deadline:
            frame = self.read()
            if self.clock() >= deadline:
                return {"status": "timeout"}
            if frame is None:
                self.sleep(0.001)
                continue
            if frame["type"] == 0x20 and frame["id"] == 0x123 and frame["data"] == [1]:
                continue
            try:
                decoded = decode_response(frame)
            except ValueError as exc:
                return {"status": "invalid_response", "detail": str(exc)}
            # Nominal sensor run: stale/offline/no-data frames remain valid protocol
            # evidence but do not pass this healthy-sensor acceptance criterion.
            if decoded["quality"] != 1:
                return {"status": "sensor_not_valid", **decoded}
            return {"status": "ok", **decoded}
        return {"status": "timeout"}

    def close(self):
        self.bus.close()


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", required=True, type=Path)
    parser.add_argument("--channel", type=lambda value: int(value, 0), default=0x51)
    parser.add_argument("--response-timeout-ms", type=positive, default=500)
    add_campaign_arguments(parser, default_rounds=600)
    parser.set_defaults(interval_ms=1000)
    args = validate_campaign_arguments(parser, parser.parse_args(argv))
    if not args.dll.is_file():
        parser.error("--dll must name an existing PCANBasic.dll matching Python bitness")
    if not 0 < args.channel <= 0xFFFF:
        parser.error("invalid channel handle")
    return args


def main(argv=None):
    args = parse_arguments(argv)
    try:
        return execute(lambda emit: CanAdapter(Pcan(args.dll, args.channel), emit), args, "can")
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
