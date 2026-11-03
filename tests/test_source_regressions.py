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
NUMBER_PY = (ROOT / "components/axp192/number.py").read_text()
HEADER = (ROOT / "components/axp192/axp192.h").read_text()
SWITCH_PY = (ROOT / "components/axp192/switch.py").read_text()


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
        self.assertIn("AUTO_LOAD = ['binary_sensor', 'number', 'switch']", PY)

    def test_adc_decoding_stays_guarded(self):
        body = method_body("AXP192Component::update()")
        self.assertIn("detail::decode12(raw[0], raw[1])", body)
        self.assertIn("detail::decode13(bytes[0], bytes[1])", body)
        self.assertIn("if (!this->read_bytes(reg, bytes, sizeof(bytes)))", body)

    def test_sensor_publish_requires_successful_read(self):
        body = method_body("AXP192Component::update()")
        self.assertIn("if (this->read_bytes(0x78, raw, sizeof(raw)))", body)
        self.assertIn("if (this->read_byte(0x01, &status))", body)

    def test_battery_estimate_is_clamped(self):
        body = method_body("AXP192Component::update()")
        self.assertIn("detail::battery_level(vbat)", body)

    def test_i2c_read_errors_are_reported(self):
        self.assertIn("bool AXP192Component::TryRead8", CPP)
        self.assertIn("bool AXP192Component::Write1Byte", CPP)
        self.assertIn("this->record_i2c_error_();", CPP)

    def test_brightness_preserves_register_on_failed_read(self):
        body = method_body("AXP192Component::UpdateBrightness()")
        self.assertIn("if (!TryRead8(0x28, &buf)) return false;", body)
        self.assertIn("if (!TryRead8(0x27, &buf)) return false;", body)
        self.assertIn("curr_brightness_ = requested", body)

    def test_power_register_read_is_guarded(self):
        body = method_body("AXP192Component::begin(")
        self.assertIn("if (!TryRead8(0x12, &power))", body)
        self.assertIn("if (!TryRead8(0x12, &power)) return false;", body)
        self.assertIn("if (!Write1Byte(0x12, buf)) return false;", body)

    def test_m5stick_backlight_off_only_at_zero(self):
        body = method_body("AXP192Component::UpdateBrightness()")
        self.assertIn("requested == 0.0f || curr_brightness_ == 0.0f", body)
        self.assertIn("!SetLDO2(requested > 0.0f)", body)
        self.assertIn("curr_brightness_ = requested", body)
        ldo2 = method_body("AXP192Component::SetLDO2(")
        self.assertIn("if (!TryRead8(0x12, &power_control)) return false;", ldo2)
        self.assertIn("detail::set_ldo2_enabled(power_control, enabled)", ldo2)
        self.assertIn("desired == power_control || Write1Byte(0x12, desired)", ldo2)

    def test_native_number_registration(self):
        self.assertIn("AXP192BrightnessNumber", HEADER)
        self.assertIn("number.register_number(var, config, min_value=0, max_value=100, step=1)", NUMBER_PY)
        self.assertIn("cv.GenerateID(CONF_AXP192_ID): cv.use_id(AXP192Component)", NUMBER_PY)
        self.assertIn("cv.Optional(CONF_RESTORE_VALUE, default=True)", NUMBER_PY)

    def test_brightness_restoration_and_control(self):
        setup = method_body("AXP192BrightnessNumber::setup()")
        control = method_body("AXP192BrightnessNumber::control(float value)")
        apply = method_body("AXP192BrightnessNumber::apply_value_(")
        self.assertIn("this->pref_.load(&value)", setup)
        self.assertIn("this->apply_value_(value, false)", setup)
        self.assertIn("this->apply_value_(value, true)", control)
        self.assertIn("this->parent_->set_brightness(value / 100.0f)", apply)
        self.assertIn("this->parent_->apply_brightness()", apply)
        self.assertIn("this->publish_state(value)", apply)
        self.assertIn("this->pref_.save(&value)", apply)

    def test_static_brightness_removed_from_sensor_schema(self):
        self.assertNotIn("CONF_BRIGHTNESS", PY)

    def test_display_switch_keeps_number_setpoint(self):
        self.assertIn("AXP192DisplaySwitch::write_state(bool state)", CPP)
        self.assertIn("this->display_enabled_ ? brightness_ : 0.0f", CPP)
        self.assertIn("this->pref_.save(&state)", CPP)
        self.assertIn("this->parent_->set_display_enabled(state)", CPP)
        self.assertIn("this->parent_->set_brightness(value / 100.0f)", CPP)

    def test_i2c_errors_are_counted(self):
        self.assertIn("record_i2c_error_()", CPP)
        self.assertIn("i2c_error_count_++", CPP)
        self.assertIn("set_i2c_errors_sensor", HEADER)

    def test_display_state_rolls_back_after_failed_i2c(self):
        self.assertIn("const bool previous = this->display_enabled_", HEADER)
        self.assertIn("this->display_enabled_ = previous;", HEADER)
        self.assertIn("if (this->apply_brightness()) return true;", HEADER)
        self.assertIn("this->curr_brightness_ = -1.0f;", HEADER)
        self.assertIn("this->apply_brightness();", method_body("AXP192Component::update()"))

    def test_brightness_not_published_on_write_failure(self):
        control = method_body("AXP192BrightnessNumber::apply_value_(")
        self.assertIn("if (!this->parent_->apply_brightness())", control)
        self.assertIn("this->parent_->set_brightness(previous)", control)
        switch = method_body("AXP192DisplaySwitch::write_state(")
        self.assertIn("if (!this->parent_->set_display_enabled(state))", switch)

    def test_axp192_setup_aborts_after_i2c_failure(self):
        setup = method_body("AXP192Component::setup()")
        begin = method_body("AXP192Component::begin(")
        self.assertIn("if (!ok)", setup)
        self.assertIn("this->mark_failed();", setup)
        self.assertIn("if (ok && GetStartupReason()", setup)
        self.assertIn("bool AXP192Component::begin(", CPP)
        for register in ("0x27", "0x28", "0x84", "0x82", "0x33",
                         "0x12", "0x36", "0x91", "0x90", "0x30",
                         "0x39", "0x35", "0x32"):
            if f"Write1Byte({register}," in begin:
                self.assertRegex(begin, rf"if \(!Write1Byte\({register},")

    def test_optional_display_auto_off_timer(self):
        self.assertIn('cv.Optional(CONF_AUTO_OFF): cv.positive_time_period_milliseconds', SWITCH_PY)
        self.assertIn('set_auto_off_ms(config[CONF_AUTO_OFF].total_milliseconds)', SWITCH_PY)
        self.assertIn('parent.set_display_switch(var)', SWITCH_PY)
        timer = method_body("AXP192DisplaySwitch::reset_auto_off_timer()")
        self.assertIn('this->auto_off_ms_ == 0 || !this->state', timer)
        self.assertIn('this->set_timeout("display_auto_off"', timer)
        self.assertIn('this->turn_off();', timer)
        activity = method_body("AXP192BrightnessNumber::apply_value_(")
        self.assertIn('this->parent_->notify_display_activity()', activity)

    def test_display_auto_off_can_be_woken(self):
        write = method_body("AXP192DisplaySwitch::write_state(")
        self.assertIn('this->reset_auto_off_timer()', write)
        self.assertIn('this->cancel_timeout("display_auto_off")', write)

    def test_dead_legacy_register_api_is_removed(self):
        for name in ("Read8bit", "ReadBuff", "Read16bit", "Read24bit",
                     "Read32bit", "GetCoulombData", "GetBatCurrent",
                     "DeepSleep", "LightSleep", "PowerOff", "SetLDO3"):
            self.assertNotIn(name + "(", HEADER)
            self.assertNotIn("AXP192Component::" + name + "(", CPP)


if __name__ == "__main__":
    unittest.main()
