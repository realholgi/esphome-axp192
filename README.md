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
`address: 0x34` from the component's YAML. The legacy examples under
`sample-config/` are historical and may use outdated ESPHome syntax.

The `charging` entity reports the AXP192 charge-in-progress flag, *not* merely
whether USB power is connected. The `battery_level` sensor is a rough
voltage-based estimate, clamped to 0–100%, not a calibrated fuel gauge. After
an I2C read failure, the last value is retained until a successful read.

### Verify a change

Run the lightweight source regression tests without installing ESPHome:

```bash
python3 -m unittest discover -s tests -v
```

These checks guard against known regressions but are not compilation,
integration, or hardware tests.


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

### M5Stick-C

```yaml
sensor:
  - platform: axp192
    model: M5STICKC
    address: 0x34
    i2c_id: bus_a
    update_interval: 30s
    battery_level:
      name: "M5Stick Battery Level"
      id: "m5stick_batterylevel"
    charging:
      name: "M5Stick Charging"
```

### M5Stack Core2

```yaml
sensor:
  - platform: axp192
    model: M5CORE2
    address: 0x34
    i2c_id: bus_a
    update_interval: 30s
    battery_level:
      name: "${upper_devicename} Battery Level"
      id: "${devicename}_batterylevel"
    charging:
      name: "${upper_devicename} Charging"
```

### M5Tough

```yaml
sensor:
  - platform: axp192
    model: M5Tough
    address: 0x34
    i2c_id: bus_a
    update_interval: 30s
    battery_level:
      name: "${upper_devicename} Battery Level"
      id: "${devicename}_batterylevel"
    charging:
      name: "${upper_devicename} Charging"
```

The display component required for the M5Tough is as follows:

```yaml
display:
  - platform: ili9341
    # 320x240
    model: M5STACK
    cs_pin: GPIO5
    dc_pin: GPIO15
    lambda: |-
      it.print(160, 0, id(title_font), id(color_white), TextAlign::TOP_CENTER, "Hello World");
```
