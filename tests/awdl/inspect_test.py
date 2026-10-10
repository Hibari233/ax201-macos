# SPDX-License-Identifier: GPL-2.0-or-later
"""Wire-boundary tests for the offline decoder; no real device identifiers."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec = importlib.util.spec_from_file_location('awdl_inspect', Path(__file__).resolve().parents[2] / 'tools/awdl_inspect.py')
inspect = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspect)


def fixture():
    packet = bytearray(40)
    packet[0] = 0xd0
    packet[24:30] = bytes.fromhex('7f0017f20810')
    sync = bytearray(33)
    sync[0] = sync[3] = 6
    struct.pack_into('<H', sync, 5, 16)
    sync[27] = 4
    sequence = bytes([15, 3, 0, 3, 255, 255]) + bytes([149, 128]) * 16
    for kind, value in [(4, sync), (18, sequence + bytes(3)), (21, bytes([0xa2, 1]))]:
        packet += struct.pack('<BH', kind, len(value)) + value
    return bytes(packet)


class DecoderTests(unittest.TestCase):
    def test_current_peer_shape(self):
        result = inspect.parse_frame(fixture())
        self.assertEqual(result['protocol_version'], '10.2')
        self.assertEqual(result['sync']['aw_period_tu'], 16)
        self.assertEqual(result['sequence']['channels'], [149] * 16)
        self.assertEqual(result['sequence']['operating_classes'], [128] * 16)

    def test_monitor_fcs(self):
        data = fixture()
        framed = data + struct.pack('<I', zlib.crc32(data))
        self.assertEqual(inspect.parse_frame(framed, True), inspect.parse_frame(data))
        with self.assertRaises(ValueError):
            inspect.parse_frame(framed[:-1] + bytes([framed[-1] ^ 1]), True)

    def test_truncated_sequence(self):
        sequence = bytes([15, 3, 0, 3, 255, 255]) + bytes([149, 128]) * 16
        for length in range(len(sequence)):
            with self.assertRaises(ValueError):
                inspect.channel_sequence(sequence[:length])

    def test_tlv_overrun(self):
        data = bytearray(fixture())
        data[41:43] = b'\xff\xff'
        with self.assertRaises(ValueError):
            inspect.parse_frame(data)

    def test_legacy_and_unknown_channel_encoding(self):
        result = inspect.channel_sequence(bytes([1, 1, 0, 3, 255, 255, 0, 6, 0, 44]))
        self.assertEqual(result['channels'], [6, 44])
        self.assertTrue(inspect.channel_sequence(bytes([0, 7, 0, 0, 0, 0]))['unsupported_encoding'])


if __name__ == '__main__':
    unittest.main()
