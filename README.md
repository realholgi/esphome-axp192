# ESPHome AXP192 Component
ESPHome AXP192 Component

This custom component it to implement support for the AXP192 for both the M5Stick-C, and the M5Stack Core2, building on top of airy10's code. 

*Update - January 2026*

**ESP-IDF Framework Support**: @makerwolf added full compatibility with the ESP-IDF framework. The component now works with both Arduino and ESP-IDF frameworks! You can use either framework type in your ESPHome configuration.

*Update - September 2023*

**Charging Status Sensor**: @landonr added support for battery charging state detection via a binary sensor. The component now exposes a `charging` binary sensor that indicates whether the battery is currently being charged.

*Update - 17th April 2023*  

@paulchilton has added support for the M5Tough, which requires a different register configuration for the M5Tough ILI9342C display. Other changes include a fix to stop the log being spammed with brightness values continually, these are only logged on change. Also the M5Tough needs resetting once the axp192 registers are set for the display to properly initialise so this version sets up the axp and then resets the ESP32 automatically.

## Installation

Copy the components to a custom_components directory next to your .yaml configuration file, or include directly from this repository.


## ESPHome 2026 / ESP-IDF quick start

For the M5StickC Plus, see [the minimal ESP-IDF example](sample-config/m5stickcplus-esp-idf.yaml).
It uses the `m5stick-c` board definition, native ESP-IDF, and I2C pins GPIO21/GPIO22.

The AXP192 device's default I2C address is `0x34`. You may omit
`address: 0x34` from the component's YAML. The sample configurations under `sample-config/` use the ESPHome 2026
ESP-IDF syntax. The Core2 and Tough examples are configuration templates;
their display peripherals must be configured separately.

The `charging` entity reports the AXP192 charge-in-progress flag, *not* merely
whether USB power is connected. The `battery_level` sensor is a rough
voltage-based estimate, clamped to 0–100%, not a calibrated fuel gauge. After
an I2C read failure, the last value is retained until a successful read.

### M5StickC / M5StickC Plus backlight power

For `model: M5STICKC`, setting the native number to 0% turns the display
**backlight supply (AXP192 LDO2)** completely off, instead of leaving it
at the minimum brightness voltage. The TFT controller's supply is not
intentionally disabled. The default remains 100%.

```yaml
sensor:
  - platform: axp192
    id: axp_power
    model: M5STICKC
    i2c_id: bus_a
    battery_level:
      name: "M5Stick Battery Level"

number:
  - platform: axp192
    axp192_id: axp_power
    name: "Display Brightness"
    mode: slider
    restore_value: true
    initial_value: 100
```

The brightness slider is a native ESPHome `number` entity, exposed via
the native API or MQTT discovery when these integrations are enabled.
Its state is the requested percentage, not a measured brightness.
The last setting is saved to flash on user changes and restored at boot.
The former static `brightness:` option in `sensor:` is no longer accepted.

Turning the backlight completely off is supported for `M5STICKC`
only, not `M5CORE2` or `M5TOUGH`. I2C failures cause the
component to retry applying the requested setting on a later update.

### Additional diagnostics and power controls

The AXP192 provides optional telemetry entities on the existing `sensor:
- platform: axp192` entry:

- `battery_voltage` (V)
- `battery_charge_current` (mA)
- `battery_discharge_current` (mA)
- `usb_voltage` (V)
- `usb_current` (mA)
- `i2c_errors` (count since boot)

The optional binary sensor `usb_connected` reports AXP192 VBUS presence,
independently from `charging`. It can be used for a Home Assistant
notification when USB power is lost, provided Wi-Fi and MQTT/API are
still reachable.

`battery_curve: linear` preserves the original voltage-based estimate.
`battery_curve: lipo` selects an approximate piecewise LiPo curve,
which is most meaningful when the battery is resting. Both modes are
estimates rather than calibrated state-of-charge readings.
`battery_smoothing` ranges from 0.0 to 1.0, with 1.0 disabling
smoothing; for example, 0.3 retains 70% of the previous reading.

The optional `switch: - platform: axp192` turns the M5StickC display
backlight on and off while retaining the last configured `number`
brightness setpoint. Its on/off state is stored locally and restored
after reboot. A failed I2C command leaves the previous published switch
or number state unchanged; the command can be retried.

See [the complete ESP-IDF example](sample-config/m5stickcplus-esp-idf.yaml)
for the YAML syntax.

### Automatic display backlight timeout

Add `auto_off: 5min` to `switch: - platform: axp192` to turn the
M5StickC backlight off after five minutes without a display command.
By default there is **no automatic timeout**. A new ON command or a
brightness adjustment while the display is on resets the countdown.
Power-off keeps the brightness setpoint for the next wake-up.

For example, button A can wake the screen without affecting the BLE
gateway:

```yaml
switch:
  - platform: axp192
    id: display_enabled
    axp192_id: axp_power
    name: "Display Enabled"
    auto_off: 5min

binary_sensor:
  - platform: gpio
    pin:
      number: GPIO37
      inverted: true
    on_press:
      - switch.turn_on: display_enabled
```

The auto-off action uses the normal switch path, so the OFF state is
persisted when `restore_value: true` is enabled. A failed I2C write
does not publish an incorrect OFF state.

### Verify a change

Run the lightweight source regression tests without installing ESPHome:

```bash
python3 -m unittest discover -s tests -v
```

These checks guard against known regressions. GitHub Actions also compiles
an ESP-IDF firmware based on the local component source; this does not
replace a real-device test.


Run `esphome config sample-config/m5stickcplus-esp-idf.yaml` and
`esphome compile sample-config/m5stickcplus-esp-idf.yaml` with ESPHome
2026.10 or newer. Provide `wifi_ssid` and `wifi_password` via a
`secrets.yaml` alongside the example. Hardware validation on a real
M5StickC Plus is still necessary to verify battery/charging and display
power behavior.

## Configuration

Sample configurations are found in the `/sample-config` folder.

This component adds a new model configuration to the AXP192 sensor which determines which registers are needed for each device. Available models are `model: M5CORE2`, `model: M5STICKC` and `model: M5TOUGH`.

### Include axp192 component

```yaml
external_components:
  - source: github://realholgi/esphome-axp192@main
    components: [axp192]
```

### Device examples

The samples deliberately keep the power-management component separate from
device-specific TFT, touch and audio drivers. They need only Wi-Fi secrets and
an appropriate ESP32 board.

- [M5StickC / Plus](sample-config/m5stickc.yaml): Power sensors, persistent
  brightness control, optional backlight timer and button A
- [M5StickC Plus (minimal)](sample-config/m5stickcplus-esp-idf.yaml):
  Extended battery and USB diagnostics
- [M5Stack Core2](sample-config/m5core2.yaml): Core2 power rails and
  battery sensors
- [M5Stack Tough](sample-config/m5tough.yaml): Tough power rails and
  battery sensors

**Hardware note:** Only M5StickC/Plus has LDO2-based backlight power-off.
Core2/Tough brightness registers differ; no touchscreen or display support
is provided by this power-management component.
