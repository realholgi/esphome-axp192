#include "axp192.h"
#include "axp192_math.h"
#include "esphome/core/log.h"
#include "esp_system.h"

namespace esphome {
namespace axp192 {

static const char *TAG = "axp192.sensor";
void AXP192Component::setup() {
  if (this->i2c_errors_sensor_ != nullptr) this->i2c_errors_sensor_->publish_state(0);
  bool ok = false;
  switch (this->model_) {
    case AXP192_M5STICKC:
      ok = this->begin(false, false, false, false, false);
      break;
    case AXP192_M5CORE2:
      // LDO3 powers the vibrator on this device.
      ok = this->begin(false, true, false, false, false);
      break;
    case AXP192_M5TOUGH:
      ok = this->begin(false, false, false, false, false);
      if (ok && ::esp_reset_reason() == ESP_RST_POWERON) {
        ESP_LOGD(TAG, "First power on, restarting ESP...");
        esp_restart();
      }
      break;
  }
  if (!ok) {
    ESP_LOGE(TAG, "AXP192 initialization failed; stopping component");
    this->mark_failed();
  }
}

void AXP192Component::dump_config() {
  ESP_LOGCONFIG(TAG, "AXP192:");
  LOG_I2C_DEVICE(this);
  LOG_SENSOR("  ", "Battery Level", this->batterylevel_sensor_);
  LOG_SENSOR("  ", "I2C Errors", this->i2c_errors_sensor_);
}

float AXP192Component::get_setup_priority() const { return setup_priority::DATA; }

void AXP192Component::update() {
    if (this->batterylevel_sensor_ != nullptr || this->battery_voltage_sensor_ != nullptr) {
      uint8_t raw[2] = {};
      if (this->read_bytes(0x78, raw, sizeof(raw))) {
        const uint16_t adc = detail::decode12(raw[0], raw[1]);
        const float vbat = detail::battery_voltage(adc);
        // Voltage-only approximation; this is not a calibrated fuel gauge.
        const float measured_level = this->battery_curve_lipo_ ? detail::battery_level_lipo(vbat)
                                                                  : detail::battery_level(vbat);
        const float battery_level = this->has_last_battery_level_
            ? detail::smooth_battery_level(this->last_battery_level_, measured_level, this->battery_smoothing_)
            : measured_level;
        this->last_battery_level_ = battery_level;
        this->has_last_battery_level_ = true;
        ESP_LOGD(TAG, "Got Battery Level=%f (%f)", battery_level, vbat);
        if (this->batterylevel_sensor_ != nullptr) this->batterylevel_sensor_->publish_state(battery_level);
        if (this->battery_voltage_sensor_ != nullptr) this->battery_voltage_sensor_->publish_state(vbat);
      } else {
        ESP_LOGW(TAG, "Battery voltage read failed; keeping previous sensor state");
        this->record_i2c_error_();
      }
    }

    if (this->charging_sensor_ != nullptr) {
      uint8_t status = 0;
      if (this->read_byte(0x01, &status)) {
        const bool charging = detail::charging(status);
        const bool battery_present = detail::battery_present(status);
        ESP_LOGD(TAG, "Got Charging=%d state=%d", charging, battery_present);
        this->charging_sensor_->publish_state(charging);
      } else {
        ESP_LOGW(TAG, "Charging status read failed; keeping previous sensor state");
        this->record_i2c_error_();
      }
    }


    // AXP192 battery current ADC channels are 13-bit with 0.5 mA/LSB;
    // USB VBUS voltage and current ADC channels are 12-bit.
    auto publish_adc = [this](sensor::Sensor *entity, uint8_t reg, bool is_13_bit, float scale) {
      if (entity == nullptr) return;
      uint8_t bytes[2] = {};
      if (!this->read_bytes(reg, bytes, sizeof(bytes))) {
        ESP_LOGW(TAG, "ADC read failed at 0x%02X; keeping last value", reg);
        this->record_i2c_error_();
        return;
      }
      const uint16_t raw = is_13_bit ? detail::decode13(bytes[0], bytes[1])
                                     : detail::decode12(bytes[0], bytes[1]);
      entity->publish_state(raw * scale);
    };
    publish_adc(this->battery_charge_current_sensor_, 0x7A, true, 0.5f);
    publish_adc(this->battery_discharge_current_sensor_, 0x7C, true, 0.5f);
    publish_adc(this->usb_voltage_sensor_, 0x5A, false, 0.0017f);
    publish_adc(this->usb_current_sensor_, 0x5C, false, 0.375f);

    if (this->usb_connected_sensor_ != nullptr) {
      uint8_t status = 0;
      if (this->TryRead8(0x00, &status)) {
        this->usb_connected_sensor_->publish_state(detail::usb_connected(status));
      } else {
        ESP_LOGW(TAG, "USB presence read failed; keeping last value");
      }
    }

    this->apply_brightness();
}

bool AXP192Component::begin(bool disableLDO2, bool disableLDO3, bool disableRTC, bool disableDCDC1, bool disableDCDC3)
{
  switch (this->model_) {
    case AXP192_M5STICKC:
    {
        // Set LDO2 & LDO3(TFT_LED & TFT) 3.0V
        if (!Write1Byte(0x28, 0xcc)) return false;
        break;
    }
    case AXP192_M5CORE2:
    case AXP192_M5TOUGH:
    {
        // Both devices use DCDC3 for backlight and LDO outputs for TFT.
        if (!Write1Byte(0x27, 0xcc)) return false;
        if (!Write1Byte(0x28, 0xcc)) return false;
        break;
    }
  }

    // Set ADC sample rate to 200hz
    if (!Write1Byte(0x84, 0b11110010)) return false;

    // Set ADC to All Enable
    if (!Write1Byte(0x82, 0xff)) return false;

    // Bat charge voltage to 4.2, Current 100MA
    if (!Write1Byte(0x33, 0xc0)) return false;

    // Depending on configuration enable LDO2, LDO3, DCDC1, DCDC3.
    uint8_t power = 0;
    if (!TryRead8(0x12, &power)) return false;
    uint8_t buf = (power & 0xef) | 0x4D;
    if(disableLDO3) buf &= ~(1<<3);
    if(disableLDO2) buf &= ~(1<<2);
    if(disableDCDC3) buf &= ~(1<<1);
    if(disableDCDC1) buf &= ~(1<<0);
    if (!Write1Byte(0x12, buf)) return false;

    // 128ms power on, 4s power off
    if (!Write1Byte(0x36, 0x0C)) return false;

    if(!disableRTC)
    {
        // Set RTC voltage to 3.3V
        if (!Write1Byte(0x91, 0xF0)) return false;

        // Set GPIO0 to LDO
        if (!Write1Byte(0x90, 0x02)) return false;
    }

    // Disable vbus hold limit
    if (!Write1Byte(0x30, 0x80)) return false;

    // Set temperature protection
    if (!Write1Byte(0x39, 0xfc)) return false;

    // Enable RTC BAT charge
    if (!Write1Byte(0x35, 0xa2 & (disableRTC ? 0x7F : 0xFF))) return false;

    // Enable bat detection
    if (!Write1Byte(0x32, 0x46)) return false;

    return true;
}

void AXP192Component::record_i2c_error_() {
  this->i2c_error_count_++;
  this->status_set_warning();
  if (this->i2c_errors_sensor_ != nullptr)
    this->i2c_errors_sensor_->publish_state(this->i2c_error_count_);
}

bool AXP192Component::Write1Byte(uint8_t addr, uint8_t data) {
  if (!this->write_byte(addr, data)) {
    ESP_LOGW(TAG, "I2C write failed at register 0x%02X", addr);
    this->record_i2c_error_();
    return false;
  }
  return true;
}

bool AXP192Component::TryRead8(uint8_t addr, uint8_t *data) {
  *data = 0;
  if (!this->read_byte(addr, data)) {
    ESP_LOGW(TAG, "I2C read failed at register 0x%02X", addr);
    this->record_i2c_error_();
    return false;
  }
  return true;
}

bool AXP192Component::UpdateBrightness()
{
    const float requested = this->display_enabled_ ? brightness_ : 0.0f;
    if (requested == curr_brightness_)
    {
        return true;
    }

    ESP_LOGD(TAG, "Brightness=%f (Curr: %f)", brightness_, curr_brightness_);
    // Record the requested level only after the write succeeds.

    const uint8_t c_min = 7;
    const uint8_t c_max = 12;
    auto ubri = c_min + static_cast<uint8_t>(requested * (c_max - c_min));

    if (ubri > c_max)
    {
        ubri = c_max;
    }
    switch (this->model_) {
      case AXP192_M5STICKC:
      {
        uint8_t buf = 0;
        if (!TryRead8(0x28, &buf)) return false;
        if (!Write1Byte(0x28, (buf & 0x0f) | (ubri << 4))) return false;
        // Only a requested zero brightness disables LDO2. Restore it if
        // brightness is raised again. Default brightness remains unchanged.
        if ((requested == 0.0f || curr_brightness_ == 0.0f) &&
            !SetLDO2(requested > 0.0f)) return false;
        break;
      }
      case AXP192_M5CORE2:
      case AXP192_M5TOUGH:
      {
        uint8_t buf = 0;
        if (!TryRead8(0x27, &buf)) return false;
        if (!Write1Byte(0x27, (buf & 0x80) | (ubri << 3))) return false;
        break;
      }
    }
    curr_brightness_ = requested;
    return true;
}

bool AXP192Component::SetLDO2(bool enabled) {
  uint8_t power_control = 0;
  if (!TryRead8(0x12, &power_control)) return false;
  const uint8_t desired = detail::set_ldo2_enabled(power_control, enabled);
  return desired == power_control || Write1Byte(0x12, desired);
}

void AXP192DisplaySwitch::setup() {
  bool value = this->initial_value_;
  if (this->restore_value_) {
    this->pref_ = this->make_entity_preference<bool>();
    if (!this->pref_.load(&value)) value = this->initial_value_;
  }
  if (!this->parent_->set_display_enabled(value)) {
    ESP_LOGW(TAG, "Could not restore display power state");
    return;
  }
  this->publish_state(value);
  if (value) this->reset_auto_off_timer();
}

void AXP192DisplaySwitch::dump_config() {
  LOG_SWITCH("", "AXP192 Display", this);
}

void AXP192DisplaySwitch::write_state(bool state) {
  if (!this->parent_->set_display_enabled(state)) {
    ESP_LOGW(TAG, "Display power transition failed");
    return;
  }
  this->publish_state(state);
  if (this->restore_value_) this->pref_.save(&state);
  if (state) {
    this->reset_auto_off_timer();
  } else {
    this->cancel_timeout("display_auto_off");
  }
}

void AXP192DisplaySwitch::reset_auto_off_timer() {
  if (this->auto_off_ms_ == 0 || !this->state) return;
  // Replaces the previous deadline after an ON command or brightness change.
  this->set_timeout("display_auto_off", this->auto_off_ms_, [this]() {
    this->turn_off();
  });
}

void AXP192Component::notify_display_activity() {
  if (this->display_switch_ != nullptr) this->display_switch_->reset_auto_off_timer();
}

void AXP192BrightnessNumber::setup() {
  float value = this->initial_value_;
  if (this->restore_value_) {
    this->pref_ = this->make_entity_preference<float>();
    if (!this->pref_.load(&value))
      value = this->initial_value_;
  }
  this->apply_value_(value, false);
}

void AXP192BrightnessNumber::dump_config() {
  LOG_NUMBER("", "AXP192 display brightness", this);
  ESP_LOGCONFIG(TAG, "  Restore value: %s", YESNO(this->restore_value_));
}

void AXP192BrightnessNumber::control(float value) {
  this->apply_value_(value, true);
}

void AXP192BrightnessNumber::apply_value_(float value, bool persist) {
  // Restoration and MQTT commands use the same 0-100% range.
  value = detail::clamp_percent(value);
  const float previous = this->parent_->get_brightness();
  this->parent_->set_brightness(value / 100.0f);
  if (!this->parent_->apply_brightness()) {
    this->parent_->set_brightness(previous);
    ESP_LOGW(TAG, "Brightness write failed; keeping previous setpoint");
    return;
  }
  // This state is the requested setpoint, not a measurement of the backlight.
  this->publish_state(value);
  if (persist) {
    this->parent_->notify_display_activity();
    if (this->restore_value_) this->pref_.save(&value);
  }
}

}
}
