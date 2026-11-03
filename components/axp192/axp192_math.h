#pragma once

#include <cstdint>

namespace esphome {
namespace axp192 {
namespace detail {

constexpr uint16_t decode12(uint8_t high, uint8_t low) {
  return (static_cast<uint16_t>(high) << 4) | (low & 0x0F);
}

constexpr uint16_t decode13(uint8_t high, uint8_t low) {
  return (static_cast<uint16_t>(high) << 5) | (low & 0x1F);
}

constexpr uint16_t decode16(uint8_t high, uint8_t low) {
  return (static_cast<uint16_t>(high) << 8) | low;
}

constexpr uint32_t decode24(uint8_t a, uint8_t b, uint8_t c) {
  return (static_cast<uint32_t>(a) << 16) | (static_cast<uint32_t>(b) << 8) | c;
}

constexpr uint32_t decode32(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(c) << 8) | d;
}

// LDO2 supplies the M5StickC/M5StickC Plus display backlight; preserve
// all unrelated power-output enable bits in register 0x12.
constexpr uint8_t set_ldo2_enabled(uint8_t power_control, bool enabled) {
  return enabled ? static_cast<uint8_t>(power_control | 0x04u)
                 : static_cast<uint8_t>(power_control & ~0x04u);
}

// Constrain user commands and possibly damaged restored preferences.
constexpr float clamp_percent(float value) {
  return value < 0.0f ? 0.0f : (value > 100.0f ? 100.0f : (value >= 0.0f ? value : 0.0f));
}

constexpr bool battery_present(uint8_t status) { return (status & 0x20) != 0; }
// AXP192 power input status register 0x00: bit 5 = VBUS present.
constexpr bool usb_connected(uint8_t status) { return (status & 0x20) != 0; }

constexpr bool charging(uint8_t status) { return (status & 0x40) != 0; }

constexpr float battery_voltage(uint16_t adc) { return adc * 0.0011f; }

constexpr float battery_level(float volts) {
  const float pct = 100.0f * ((volts - 3.0f) / (4.1f - 3.0f));
  return pct < 0.0f ? 0.0f : (pct > 100.0f ? 100.0f : pct);
}

// Approximate open-circuit LiPo voltage-to-state-of-charge curve.
// This is an estimate, not a fuel gauge (load and charging affect voltage).
constexpr float battery_level_lipo(float v) {
  constexpr float voltages[] = {3.00f, 3.30f, 3.50f, 3.60f, 3.70f, 3.75f, 3.80f,
                                 3.85f, 3.90f, 4.00f, 4.10f, 4.20f};
  constexpr float percentages[] = {0, 5, 10, 20, 40, 55, 65, 75, 85, 94, 99, 100};
  if (v <= voltages[0]) return 0.0f;
  constexpr unsigned size = sizeof(voltages) / sizeof(voltages[0]);
  for (unsigned i = 1; i < size; i++) {
    if (v <= voltages[i]) {
      const float fraction = (v - voltages[i-1]) / (voltages[i] - voltages[i-1]);
      return percentages[i-1] + fraction * (percentages[i] - percentages[i-1]);
    }
  }
  return 100.0f;
}

constexpr float smooth_battery_level(float previous, float measured, float alpha) {
  return previous + alpha * (measured - previous);
}

// AXP192 coulomb counter, configured for 200 Hz ADC sampling (register 0x84).
// Differences must be signed to represent net discharge without overflow.
constexpr double coulomb_mAh(uint32_t charge, uint32_t discharge, double sample_hz = 200.0) {
  return 65536.0 * 0.5 *
         (static_cast<double>(charge) - static_cast<double>(discharge)) /
         (3600.0 * sample_hz);
}

}  // namespace detail
}  // namespace axp192
}  // namespace esphome
