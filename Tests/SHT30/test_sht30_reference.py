import unittest


CRC_INITIAL_VALUE = 0xFF
CRC_POLYNOMIAL = 0x31
RAW_DENOMINATOR = 65535


def calculate_crc(data):
    crc = CRC_INITIAL_VALUE

    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ CRC_POLYNOMIAL) & 0xFF
            else:
                crc = (crc << 1) & 0xFF

    return crc


def temperature_milli_c(raw_value):
    return -45000 + ((175000 * raw_value + 32767) // RAW_DENOMINATOR)


def humidity_milli_percent(raw_value):
    return (100000 * raw_value + 32767) // RAW_DENOMINATOR


def decode_response(response):
    if len(response) != 6:
        raise ValueError("response length")
    if calculate_crc(response[0:2]) != response[2]:
        raise ValueError("temperature CRC")
    if calculate_crc(response[3:5]) != response[5]:
        raise ValueError("humidity CRC")

    temperature_raw = (response[0] << 8) | response[1]
    humidity_raw = (response[3] << 8) | response[4]

    return (
        temperature_raw,
        humidity_raw,
        temperature_milli_c(temperature_raw),
        humidity_milli_percent(humidity_raw),
    )


class SHT30ReferenceTests(unittest.TestCase):
    def test_official_crc_vector(self):
        self.assertEqual(calculate_crc([0xBE, 0xEF]), 0x92)

    def test_conversion_boundaries(self):
        self.assertEqual(temperature_milli_c(0), -45000)
        self.assertEqual(temperature_milli_c(65535), 130000)
        self.assertEqual(humidity_milli_percent(0), 0)
        self.assertEqual(humidity_milli_percent(65535), 100000)

    def test_midpoint_rounding(self):
        self.assertEqual(temperature_milli_c(32768), 42501)
        self.assertEqual(humidity_milli_percent(32768), 50001)

    def test_valid_response_layout(self):
        response = [
            0xBE,
            0xEF,
            calculate_crc([0xBE, 0xEF]),
            0x80,
            0x00,
            calculate_crc([0x80, 0x00]),
        ]

        self.assertEqual(decode_response(response)[0:2], (0xBEEF, 0x8000))

    def test_temperature_crc_error(self):
        response = [
            0xBE,
            0xEF,
            0x00,
            0x80,
            0x00,
            calculate_crc([0x80, 0x00]),
        ]

        with self.assertRaisesRegex(ValueError, "temperature CRC"):
            decode_response(response)

    def test_humidity_crc_error(self):
        response = [
            0xBE,
            0xEF,
            calculate_crc([0xBE, 0xEF]),
            0x80,
            0x00,
            0x00,
        ]

        with self.assertRaisesRegex(ValueError, "humidity CRC"):
            decode_response(response)


if __name__ == "__main__":
    unittest.main()
