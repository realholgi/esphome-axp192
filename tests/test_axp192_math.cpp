#include "components/axp192/axp192_math.h"

#include <cassert>
#include <cmath>
#include <cstdint>

using namespace esphome::axp192::detail;

static_assert(decode12(0xD8, 0xFC) == 3468, "12-bit register low nibble is masked");
static_assert(decode13(0x80, 0xFF) == 4127, "13-bit register low five bits are masked");
static_assert(decode16(0x12, 0x34) == 0x1234, "16-bit big-endian register");
static_assert(decode24(0x12, 0x34, 0x56) == 0x123456, "24-bit big-endian register");
static_assert(decode32(0x80, 0x00, 0x00, 0x01) == 0x80000001UL,
              "32-bit register must not sign-extend");
static_assert(set_ldo2_enabled(0xFF, false) == 0xFB, "disabling LDO2 preserves other rails");
static_assert(set_ldo2_enabled(0xA1, true) == 0xA5, "enabling LDO2 preserves other rails");
static_assert(set_ldo2_enabled(0xA1, false) == 0xA1, "disabling already-off LDO2 is idempotent");
static_assert(set_ldo2_enabled(0xA5, true) == 0xA5, "enabling already-on LDO2 is idempotent");
static_assert(clamp_percent(-10.0f) == 0.0f, "percentage lower bound");
static_assert(clamp_percent(110.0f) == 100.0f, "percentage upper bound");
static_assert(clamp_percent(42.0f) == 42.0f, "percentage unchanged");
static_assert(!charging(0x20) && charging(0x40) && charging(0x60), "charge flag mask");
static_assert(!battery_present(0x40) && battery_present(0x20), "battery-present flag mask");
static_assert(battery_level(2.9f) == 0.0f, "battery level lower bound");
static_assert(battery_level(4.2f) == 100.0f, "battery level upper bound");

int main() {
  // Real AXP192 reading: raw ADC 0xD8C -> 3.8148 V -> approximately 74.07%.
  const auto raw = decode12(0xD8, 0xFC);
  assert(std::fabs(battery_voltage(raw) - 3.8148f) < 0.0001f);
  assert(std::fabs(battery_level(battery_voltage(raw)) - 74.0727f) < 0.02f);
  assert(std::fabs(battery_level(3.55f) - 50.0f) < 0.01f);
  assert(coulomb_mAh(0, 0) == 0.0);
  const double positive = coulomb_mAh(200, 100);
  assert(positive > 0.0);
  assert(std::fabs(coulomb_mAh(100, 200) + positive) < 1e-9);
  assert(std::fabs(coulomb_mAh(200, 100, 100.0) - 2.0 * positive) < 1e-9);
  return 0;
}
