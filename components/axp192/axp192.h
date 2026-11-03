#ifndef __AXP192_H__
#define __AXP192_H__

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace axp192 {

enum AXP192Model {
  AXP192_M5STICKC = 0,
  AXP192_M5CORE2,
  AXP192_M5TOUGH,
};

class AXP192DisplaySwitch;

class AXP192Component : public PollingComponent, public i2c::I2CDevice {
public:
  void set_batterylevel_sensor(sensor::Sensor *batterylevel_sensor) { batterylevel_sensor_ = batterylevel_sensor; }
  void set_charging_sensor(binary_sensor::BinarySensor *charging_sensor) { charging_sensor_ = charging_sensor; }
  void set_i2c_errors_sensor(sensor::Sensor *value) { i2c_errors_sensor_ = value; }
  void set_usb_connected_sensor(binary_sensor::BinarySensor *value) { usb_connected_sensor_ = value; }
  void set_battery_voltage_sensor(sensor::Sensor *value) { battery_voltage_sensor_ = value; }
  void set_battery_charge_current_sensor(sensor::Sensor *value) { battery_charge_current_sensor_ = value; }
  void set_battery_discharge_current_sensor(sensor::Sensor *value) { battery_discharge_current_sensor_ = value; }
  void set_usb_voltage_sensor(sensor::Sensor *value) { usb_voltage_sensor_ = value; }
  void set_usb_current_sensor(sensor::Sensor *value) { usb_current_sensor_ = value; }
  void set_brightness(float brightness) { brightness_ = brightness; }
  void set_display_switch(AXP192DisplaySwitch *display_switch) { this->display_switch_ = display_switch; }
  void notify_display_activity();
  bool apply_brightness() {
    if (this->UpdateBrightness()) return true;
    // A prior I2C write may have succeeded before a later one failed.
    // Force a complete re-application rather than trusting the cache.
    this->curr_brightness_ = -1.0f;
    return false;
  }
  float get_brightness() const { return brightness_; }
  bool set_display_enabled(bool enabled) {
    const bool previous = this->display_enabled_;
    this->display_enabled_ = enabled;
    if (this->apply_brightness()) return true;
    // Keep internal state consistent with the previously published HA value.
    // The invalidated brightness cache forces a repair on the next update.
    this->display_enabled_ = previous;
    return false;
  }
  void set_model(AXP192Model model) { this->model_ = model; }
  void set_battery_curve_lipo(bool enabled) { this->battery_curve_lipo_ = enabled; }
  void set_battery_smoothing(float alpha) { this->battery_smoothing_ = alpha; }

  // ========== INTERNAL METHODS ==========
  // (In most use cases you won't need these)
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void update() override;

private:
    static std::string GetStartupReason();

protected:
    sensor::Sensor *batterylevel_sensor_;
    binary_sensor::BinarySensor *charging_sensor_;
    sensor::Sensor *i2c_errors_sensor_{nullptr};
    uint32_t i2c_error_count_{0};
    void record_i2c_error_();
    binary_sensor::BinarySensor *usb_connected_sensor_{nullptr};
    sensor::Sensor *battery_voltage_sensor_{nullptr};
    sensor::Sensor *battery_charge_current_sensor_{nullptr};
    sensor::Sensor *battery_discharge_current_sensor_{nullptr};
    sensor::Sensor *usb_voltage_sensor_{nullptr};
    sensor::Sensor *usb_current_sensor_{nullptr};
    float brightness_{1.0f};
    float curr_brightness_{-1.0f};
    bool display_enabled_{true};
    AXP192DisplaySwitch *display_switch_{nullptr};
    bool battery_curve_lipo_{false};
    float battery_smoothing_{1.0f};
    float last_battery_level_{0.0f};
    bool has_last_battery_level_{false};
    AXP192Model model_;

    /** M5 Stick Values
     * LDO2: Display backlight
     * LDO3: Display Control
     * RTC: Don't set GPIO1 as LDO
     * DCDC1: Main rail. When not set the controller shuts down.
     * DCDC3: Use unknown
     ***********************
     * M5Stack Core2 Values
     * LDO2: ILI9342C PWR (Display)
     * LD03: Vibration Motor
     */

    bool begin(bool disableLDO2 = false, bool disableLDO3 = false, bool disableRTC = false, bool disableDCDC1 = false, bool disableDCDC3 = false);
    bool UpdateBrightness();
    bool SetLDO2(bool enabled);
    bool Write1Byte(uint8_t addr, uint8_t data);
    bool TryRead8(uint8_t addr, uint8_t *data);
};

class AXP192DisplaySwitch};

class AXP192DisplaySwitch : public switch_::Switch, public Component {
 public:
  void set_parent(AXP192Component *parent) { this->parent_ = parent; }
  void set_restore_value(bool restore) { this->restore_value_ = restore; }
  void set_initial_value(bool initial) { this->initial_value_ = initial; }
  void set_auto_off_ms(uint32_t ms) { this->auto_off_ms_ = ms; }
  void reset_auto_off_timer();

  float get_setup_priority() const override { return setup_priority::LATE; }
  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;

  AXP192Component *parent_{nullptr};
  ESPPreferenceObject pref_;
  bool initial_value_{true};
  bool restore_value_{true};
  uint32_t auto_off_ms_{0};
};

class AXP192BrightnessNumber : public number::Number, public Component {
 public:
  void set_parent(AXP192Component *parent) { this->parent_ = parent; }
  void set_initial_value(float initial_value) { this->initial_value_ = initial_value; }
  void set_restore_value(bool restore) { this->restore_value_ = restore; }

  float get_setup_priority() const override { return setup_priority::LATE; }
  void setup() override;
  void dump_config() override;

 protected:
  void control(float value) override;
  void apply_value_(float value, bool persist);

  AXP192Component *parent_{nullptr};
  ESPPreferenceObject pref_;
  float initial_value_{100.0f};
  bool restore_value_{true};
};

}
}

#endif
