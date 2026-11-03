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
        self.assertIn("if (!ReadBuff(0x70, 3, buf)) return 0;", body)

    def test_adc_low_bits_are_masked(self):
        self.assertIn("detail::decode12(buf[0], buf[1])", method_body("AXP192Component::Read12Bit("))
        self.assertIn("detail::decode13(buf[0], buf[1])", method_body("AXP192Component::Read13Bit("))

    def test_sensor_publish_requires_successful_read(self):
        body = method_body("AXP192Component::update()")
        self.assertIn("if (this->read_bytes(0x78, raw, sizeof(raw)))", body)
        self.assertIn("if (this->read_byte(0x01, &status))", body)

    def test_battery_estimate_is_clamped(self):
        body = method_body("AXP192Component::update()")
        self.assertIn("detail::battery_level(vbat)", body)

    def test_coulomb_difference_is_signed(self):
        body = method_body("AXP192Component::GetCoulombData(void)")
        self.assertIn("detail::coulomb_mAh(coin, coout)", body)


    def test_i2c_read_errors_are_reported(self):
        self.assertIn("bool AXP192Component::TryRead8", CPP)
        self.assertIn("bool AXP192Component::ReadBuff", CPP)
        self.assertIn("if (!ReadBuff(Addr, sizeof(Buff), Buff)) return 0;", CPP)

    def test_brightness_preserves_register_on_failed_read(self):
        body = method_body("AXP192Component::UpdateBrightness()")
        self.assertIn("if (!TryRead8(0x28, &buf)) return;", body)
        self.assertIn("if (!TryRead8(0x27, &buf)) return;", body)
        self.assertIn("curr_brightness_ = brightness_", body)

    def test_power_register_read_is_guarded(self):
        body = method_body("AXP192Component::begin(")
        self.assertIn("if (!TryRead8(0x12, &power))", body)
        self.assertIn("this->mark_failed()", body)


if __name__ == "__main__":
    unittest.main()
