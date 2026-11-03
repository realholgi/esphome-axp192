"""Source-level regression checks for previously fixed AXP192 defects.

Run with: python3 -m unittest discover -s tests -v

These are lightweight structural checks and do not replace an ESPHome compile
or a hardware test. They deliberately require no ESPHome installation.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/axp192/axp192.cpp").read_text()
PY = (ROOT / "components/axp192/sensor.py").read_text()


def method_body(signature: str) -> str:
    """Extract a C++ method body by balancing braces."""
    start = CPP.index(signature)
    brace = CPP.index("{", start)
    level = 1
    i = brace + 1
    while level:
        if CPP[i] == "{":
            level += 1
        elif CPP[i] == "}":
            level -= 1
        i += 1
    return CPP[brace + 1:i - 1]


class SourceRegressions(unittest.TestCase):
    def test_axp192_default_address(self):
        self.assertIn("i2c.i2c_device_schema(0x34)", PY)

    def test_binary_sensor_autoloaded(self):
        self.assertRegex(PY, r"AUTO_LOAD\s*=\s*\[['\"]binary_sensor['\"]\]")

    def test_status_flags_are_masked(self):
        self.assertIn("Read8bit(0x01) & 0x20", method_body("AXP192Component::GetBatState()"))
        self.assertIn("Read8bit(0x01) & 0x40", method_body("AXP192Component::GetChargingState()"))

    def test_power_adc_reads_three_bytes(self):
        body = method_body("AXP192Component::GetPowerbatData(void)")
        self.assertIn("ReadBuff(0x70,3,buf)", body)

    def test_adc_low_bits_are_masked(self):
        self.assertIn("buf[1] & 0x0F", method_body("AXP192Component::Read12Bit("))
        self.assertIn("buf[1] & 0x1F", method_body("AXP192Component::Read13Bit("))

    def test_sensor_publish_requires_successful_read(self):
        body = method_body("AXP192Component::update()")
        self.assertIn("if (this->read_bytes(0x78, raw, sizeof(raw)))", body)
        self.assertIn("if (this->read_byte(0x01, &status))", body)

    def test_battery_estimate_is_clamped(self):
        body = method_body("AXP192Component::update()")
        self.assertIn("battery_level > 100.0f", body)
        self.assertIn("battery_level < 0.0f", body)

    def test_coulomb_difference_is_signed(self):
        body = method_body("AXP192Component::GetCoulombData(void)")
        self.assertIn("static_cast<double>(coin) - static_cast<double>(coout)", body)


if __name__ == "__main__":
    unittest.main()
