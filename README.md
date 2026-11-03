# ESPHome AXP192 power management

[![CI](https://github.com/realholgi/esphome-axp192/actions/workflows/source-regressions.yml/badge.svg?branch=main)](https://github.com/realholgi/esphome-axp192/actions/workflows/source-regressions.yml)

An external [ESPHome](https://esphome.io/) component for the **AXP192 power-management IC** used by the M5StickC, M5StickC Plus, M5Stack Core2 and M5Stack Tough.

It initializes the AXP192 power rails and exposes battery, charging, USB-power and display-backlight controls to ESPHome. Entities can appear in Home Assistant through the ESPHome native API or MQTT discovery.

**Test coverage:** the M5StickC Plus with ESP-IDF has been tested on hardware for brightness, state restoration and power telemetry. GitHub Actions validates all four example configurations and compiles the M5StickC Plus example with ESPHome 2026.10.2. Core2 and Tough have not received equivalent hardware verification in this fork.

## Features

- Battery percentage (voltage-derived), battery voltage, charge current and discharge current.
- USB/VBUS presence, USB voltage and USB current. The charging flag is separate from USB presence.
- Native 0–100% `number` entity for display brightness with optional persistent settings.
- Native `switch` for display backlight, optional automatic timeout and button-controlled wake-up.
- Optional LiPo voltage curve and exponential smoothing for the reported battery percentage.
- I²C error counter, warnings, guarded register access and retry of unsuccessful brightness updates.
- ESP-IDF integration for current ESPHome versions; no Arduino-only AXP192 dependencies in the component.

This is **power management**, not a TFT/display, touchscreen, BLE-mesh or media-player driver. Those integrations are configured separately.

## Quick start: M5StickC Plus

Create `secrets.yaml` with `wifi_ssid` and `wifi_password`, then use the following ESPHome configuration. The board, I²C pins and model match the M5StickC Plus.

```yaml
esphome:
  name: m5stick

esp32:
  board: m5stick-c
  framework:
    type: esp-idf

external_components:
  - source: github://realholgi/esphome-axp192@main
    components: [axp192]
    # refresh: always  # Useful while developing the component

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

logger:

api:

ota:
  - platform: esphome

i2c:
  - id: bus_a
    sda: GPIO21
    scl: GPIO22
    scan: true

sensor:
  - platform: axp192
    id: axp_power
    model: M5STICKC
    i2c_id: bus_a
    update_interval: 30s
    battery_level:
      name: "Battery Level"
    battery_voltage:
      name: "Battery Voltage"
    charging:
      name: "Battery Charging"
    usb_connected:
      name: "USB Connected"
    i2c_errors:
      name: "AXP192 I2C Errors"

number:
  - platform: axp192
    axp192_id: axp_power
    name: "Display Brightness"
    mode: slider
    restore_value: true
    initial_value: 100

switch:
  - platform: axp192
    id: display_enabled
    axp192_id: axp_power
    name: "Display Enabled"
    restore_value: true
    initial_value: true
    auto_off: 5min

# Optional: button A restores the display backlight.
binary_sensor:
  - platform: gpio
    pin:
      number: GPIO37
      inverted: true
    name: "Button A"
    on_press:
      - switch.turn_on: display_enabled
```

The **5-minute timeout is optional**: remove `auto_off` to leave the backlight on until explicitly switched off. Button A is also optional. Neither the timeout nor the display switch puts the ESP32 to sleep: Wi-Fi, MQTT and BLE integrations can keep running.

Using **MQTT instead of `api:`** is supported through ESPHome's normal `mqtt:` configuration. The AXP192 component does not implement its own MQTT broker connection. For a BLE-mesh gateway, configure its separate ESPHome component alongside AXP192.

See the [full M5StickC Plus example](sample-config/m5stickcplus-esp-idf.yaml) for additional telemetry.

## Configuration reference

### `sensor: - platform: axp192`

`model` is required and accepts `M5STICKC`, `M5CORE2` or `M5TOUGH`. The default I²C address is `0x34`; use `i2c_id` to select a bus when needed. The default `update_interval` is `60s`.

All sensor and binary-sensor entities below are optional. Their names are chosen by your YAML:

| Option | Type / unit | Meaning |
| --- | --- | --- |
| `battery_level` | Sensor, % | Estimated charge level from battery voltage |
| `battery_voltage` | Sensor, V | Measured battery voltage |
| `battery_charge_current` | Sensor, mA | Battery charging current |
| `battery_discharge_current` | Sensor, mA | Battery discharging current |
| `charging` | Binary sensor | AXP192 charge-in-progress status |
| `usb_connected` | Binary sensor | VBUS/USB power detected |
| `usb_voltage` | Sensor, V | VBUS voltage |
| `usb_current` | Sensor, mA | VBUS current |
| `i2c_errors` | Sensor, count | Failed AXP192 I²C operations since boot |

Battery-estimation settings in the same `sensor` block:

| Option | Default | Description |
| --- | --- | --- |
| `battery_curve` | `linear` | `linear` or `lipo` voltage-to-percentage approximation |
| `battery_smoothing` | `1.0` | Smoothing factor, 0.0–1.0; 1.0 means no smoothing |

For example:

```yaml
sensor:
  - platform: axp192
    id: axp_power
    model: M5STICKC
    i2c_id: bus_a
    battery_level:
      name: "Battery Level"
    battery_curve: lipo
    battery_smoothing: 0.3
    battery_charge_current:
      name: "Charge Current"
    battery_discharge_current:
      name: "Discharge Current"
    usb_voltage:
      name: "USB Voltage"
    usb_current:
      name: "USB Current"
```

**Battery-percentage limitations:** neither curve is a calibrated fuel gauge. Charging, battery load, temperature and cell aging affect the voltage; readings can be misleading while charging or under load. The `lipo` curve uses an approximate lookup table. With `battery_smoothing: 0.3`, each new estimate contributes 30% of the next displayed value; 0.0 freezes the value after the first reading. Failed I²C reads preserve the previously published measurement.

### `number: - platform: axp192`

Creates a **0–100% brightness slider**, in 1% setpoint steps:

```yaml
number:
  - platform: axp192
    axp192_id: axp_power
    name: "Display Brightness"
    mode: slider
    initial_value: 100
    restore_value: true
```

`axp192_id` references the ID of your AXP192 sensor component. `initial_value` defaults to `100` and `restore_value` defaults to `true`. The last commanded value is stored locally and restored after reboot. The entity shows a **requested setting**, not measured light output; the hardware supports fewer distinct voltage steps than the 0–100% slider suggests.

The former `brightness:` setting inside `sensor: - platform: axp192` is **no longer supported**. Use the native `number` entity instead.

### `switch: - platform: axp192`

Creates a separate display-backlight switch. It preserves the configured brightness when switched off:

```yaml
switch:
  - platform: axp192
    id: display_enabled
    axp192_id: axp_power
    name: "Display Enabled"
    initial_value: true
    restore_value: true
    auto_off: 5min  # Optional; no timeout unless specified
```

`initial_value` defaults to `true`; `restore_value` defaults to `true`. If `auto_off` is set, a switch-on command starts a timer. Another ON command or a brightness change **while the display is on** resets it. When the timer expires, it requests OFF via the normal switch path. With restoration enabled, an automatically switched-off display remains off after a reboot until switched on again.

**Model-specific behavior:** On M5StickC/Plus, an effective brightness of **0% disables LDO2**, fully turning off the backlight supply while retaining the display controller's supply. On Core2/Tough, the existing DCDC3 brightness control is used instead; a 0% command must **not** be assumed to disconnect the backlight's power supply entirely.

If an I²C write fails, the component records an error and does not acknowledge that failed switch/number command. Partial hardware changes are possible; the component attempts to reapply the requested state on a subsequent poll. The `i2c_errors` counter is diagnostic, not a count of all failures elsewhere on the ESP32.

## Device examples

All examples use ESPHome's ESP-IDF framework and current configuration syntax:

| Device | Example | Notes |
| --- | --- | --- |
| M5StickC / Plus | [m5stickc.yaml](sample-config/m5stickc.yaml) | Brightness, switch and optional button wake |
| M5StickC Plus, extended | [m5stickcplus-esp-idf.yaml](sample-config/m5stickcplus-esp-idf.yaml) | Additional battery/USB diagnostics; used for firmware CI |
| M5Stack Core2 | [m5core2.yaml](sample-config/m5core2.yaml) | Power management; display configuration excluded |
| M5Stack Tough | [m5tough.yaml](sample-config/m5tough.yaml) | Power management; display configuration excluded |

Core2 and Tough require their own display/touch setup. A successful YAML validation does not establish complete hardware compatibility.

## Testing and development

GitHub Actions runs [source-regressions.yml](.github/workflows/source-regressions.yml) for pushes and pull requests to `main`, plus manual dispatches. Its two jobs are:

1. **Python 3.13 / native C++ tests:** structural regression tests and host-compiled register/math tests.
2. **ESPHome 2026.10.2 / ESP-IDF compile:** validates all four example YAML files and compiles the M5StickC Plus example against the **checked-out local component source** (not a cached remote branch).

Run the source tests locally without ESPHome installed:

```bash
python3 -m unittest discover -s tests -v
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -I. \
  tests/test_axp192_math.cpp -o /tmp/axp192_math_tests
/tmp/axp192_math_tests
```

For a local firmware test, copy an example YAML to your ESPHome configuration directory, place a `secrets.yaml` alongside it, then run:

```bash
esphome config m5stickcplus-esp-idf.yaml
esphome compile m5stickcplus-esp-idf.yaml
```

When actively developing from `main`, use `refresh: always` under the AXP192 external-component source or clear ESPHome's external-component cache to obtain the newest changes. For reproducible deployments, pin the external component to a known commit or release tag rather than following a moving branch.

Compilation and unit tests do **not** replace physical testing of charging, USB removal, display/backlight behavior and BLE/Wi-Fi coexistence on the target board.

## Troubleshooting

- **AXP192 not detected:** check the board model, I²C SDA/SCL wiring (GPIO21/GPIO22 in the M5StickC examples), address `0x34` and the ESPHome I²C scan log.
- **No Home Assistant entities:** configure either ESPHome's native API or MQTT integration; for MQTT discovery, check the broker and Home Assistant discovery settings. Only entities explicitly declared in YAML are exposed.
- **Display stays on:** `auto_off` belongs in the AXP192 `switch` block, not the `sensor` or `number` block. It does not trigger when omitted.
- **Brightness value seems inaccurate:** the number entity reports the requested percentage, not optical brightness; voltage steps are quantized.
- **Battery percentage jumps:** review battery voltage and whether USB/charging is active. Try `battery_smoothing: 0.3`; the result remains an estimate.
- **I²C errors increase:** inspect logs and the power/I²C connections; the counter resets after restart.

## Credits

This project builds on the earlier ESPHome AXP192 component work by **airy10** and contributions by **paulchilton** (M5Tough support), **landonr** (charging sensor) and **makerwolf** (ESP-IDF support). Further development, integration and regression tests are maintained in this fork. See the Git history for individual contributions.
