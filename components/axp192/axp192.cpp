#include "axp192.h"
#include "axp192_math.h"
#include "esphome/core/log.h"
#include "esp_sleep.h"
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
      if (ok && GetStartupReason() == "ESP_RST_POWERON") {
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
    {
        // Set DCDC3 (TFT_LED & TFT) 3.0V
        if (!Write1Byte(0x27, 0xcc)) return false;
        // Set LDO2 & LDO3(TFT_LED & TFT) 3.0V
        if (!Write1Byte(0x28, 0xcc)) return false;
        break;
    }
    case AXP192_M5TOUGH:
    {
        // Set DCDC3 (TFT_LED & TFT) 3.0V
        if (!Write1Byte(0x27, 0xcc)) return false;
        // Set LDO2 & LDO3(TFT_LED & TFT) 3.0V
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

uint8_t AXP192Component::Read8bit(uint8_t addr) {
  uint8_t value = 0;
  this->TryRead8(addr, &value);
  return value;
}

uint16_t AXP192Component::Read12Bit( uint8_t Addr)
{
    uint16_t Data = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(Addr, 2, buf)) return 0;
    Data = detail::decode12(buf[0], buf[1]);
    return Data;
}

uint16_t AXP192Component::Read13Bit( uint8_t Addr)
{
    uint16_t Data = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(Addr, 2, buf)) return 0;
    Data = detail::decode13(buf[0], buf[1]);
    return Data;
}

uint16_t AXP192Component::Read16bit( uint8_t Addr )
{
    uint32_t ReData = 0;
    uint8_t Buff[2] = {};
    if (!ReadBuff(Addr, sizeof(Buff), Buff)) return 0;
    return detail::decode16(Buff[0], Buff[1]);
}

uint32_t AXP192Component::Read24bit( uint8_t Addr )
{
    uint32_t ReData = 0;
    uint8_t Buff[3] = {};
    if (!ReadBuff(Addr, sizeof(Buff), Buff)) return 0;
    return detail::decode24(Buff[0], Buff[1], Buff[2]);
}

uint32_t AXP192Component::Read32bit( uint8_t Addr )
{
    uint32_t ReData = 0;
    uint8_t Buff[4] = {};
    if (!ReadBuff(Addr, sizeof(Buff), Buff)) return 0;
    return detail::decode32(Buff[0], Buff[1], Buff[2], Buff[3]);
}

bool AXP192Component::ReadBuff(uint8_t addr, uint8_t size, uint8_t *buffer) {
  if (!this->read_bytes(addr, buffer, size)) {
    ESP_LOGW(TAG, "I2C block read failed at register 0x%02X", addr);
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
      {
        uint8_t buf = 0;
        if (!TryRead8(0x27, &buf)) return false;
        if (!Write1Byte(0x27, (buf & 0x80) | (ubri << 3))) return false;
        break;
      }
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

bool AXP192Component::GetBatState()
{
    if( Read8bit(0x01) & 0x20 )
        return true;
    else
        return false;
}

bool AXP192Component::GetChargingState()
{
    // Charge-in-progress flag is bit 6 of register 0x01.
    if( (Read8bit(0x01) & 0x40) != 0 )
        return true;
    else
        return false;
}

uint8_t AXP192Component::GetBatData()
{
    return Read8bit(0x75);
}
//---------coulombcounter_from_here---------
//enable: void EnableCoulombcounter(void);
//disable: void DisableCOulombcounter(void);
//stop: void StopCoulombcounter(void);
//clear: void ClearCoulombcounter(void);
//get charge data: uint32_t GetCoulombchargeData(void);
//get discharge data: uint32_t GetCoulombdischargeData(void);
//get coulomb val affter calculation: float GetCoulombData(void);
//------------------------------------------
void  AXP192Component::EnableCoulombcounter(void)
{
    Write1Byte( 0xB8 , 0x80 );
}

void  AXP192Component::DisableCoulombcounter(void)
{
    Write1Byte( 0xB8 , 0x00 );
}

void  AXP192Component::StopCoulombcounter(void)
{
    Write1Byte( 0xB8 , 0xC0 );
}

void  AXP192Component::ClearCoulombcounter(void)
{
    Write1Byte( 0xB8 , 0xA0 );
}

uint32_t AXP192Component::GetCoulombchargeData(void)
{
    return Read32bit(0xB0);
}

uint32_t AXP192Component::GetCoulombdischargeData(void)
{
    return Read32bit(0xB4);
}

float AXP192Component::GetCoulombData(void)
{

  uint32_t coin = 0;
  uint32_t coout = 0;

  coin = GetCoulombchargeData();
  coout = GetCoulombdischargeData();

  //c = 65536 * current_LSB * (coin - coout) / 3600 / ADC rate
  //Adc rate can be read from 84H ,change this variable if you change the ADC reate
  float ccc = static_cast<float>(detail::coulomb_mAh(coin, coout));
  return ccc;

}
//----------coulomb_end_at_here----------

uint16_t AXP192Component::GetVbatData(void){

    uint16_t vbat = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(0x78, 2, buf)) return 0;
    vbat = ((buf[0] << 4) + buf[1]); // V
    return vbat;
}

uint16_t AXP192Component::GetVinData(void)
{
    uint16_t vin = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(0x56, 2, buf)) return 0;
    vin = ((buf[0] << 4) + buf[1]); // V
    return vin;
}

uint16_t AXP192Component::GetIinData(void)
{
    uint16_t iin = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(0x58, 2, buf)) return 0;
    iin = ((buf[0] << 4) + buf[1]);
    return iin;
}

uint16_t AXP192Component::GetVusbinData(void)
{
    uint16_t vin = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(0x5a, 2, buf)) return 0;
    vin = ((buf[0] << 4) + buf[1]); // V
    return vin;
}

uint16_t AXP192Component::GetIusbinData(void)
{
    uint16_t iin = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(0x5C, 2, buf)) return 0;
    iin = ((buf[0] << 4) + buf[1]);
    return iin;
}

uint16_t AXP192Component::GetIchargeData(void)
{
    uint16_t icharge = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(0x7A, 2, buf)) return 0;
    icharge = ( buf[0] << 5 ) + buf[1] ;
    return icharge;
}

uint16_t AXP192Component::GetIdischargeData(void)
{
    uint16_t idischarge = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(0x7C, 2, buf)) return 0;
    idischarge = ( buf[0] << 5 ) + buf[1] ;
    return idischarge;
}

uint16_t AXP192Component::GetTempData(void)
{
    uint16_t temp = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(0x5e, 2, buf)) return 0;
    temp = ((buf[0] << 4) + buf[1]);
    return temp;
}

uint32_t AXP192Component::GetPowerbatData(void)
{
    uint32_t power = 0;
    uint8_t buf[3] = {};
    if (!ReadBuff(0x70, 3, buf)) return 0;
    power = (buf[0] << 16) + (buf[1] << 8) + buf[2];
    return power;
}

uint16_t AXP192Component::GetVapsData(void)
{
    uint16_t vaps = 0;
    uint8_t buf[2] = {};
    if (!ReadBuff(0x7e, 2, buf)) return 0;
    vaps = ((buf[0] << 4) + buf[1]);
    return vaps;
}

void AXP192Component::SetSleep(void)
{
    uint8_t v31 = 0, v90 = 0, v12 = 0;
    if (!TryRead8(0x31, &v31) || !TryRead8(0x90, &v90) ||
        !TryRead8(0x12, &v12)) return;
    if (!Write1Byte(0x31, v31 | (1 << 3))) return;
    if (!Write1Byte(0x90, v90 | 0x07)) return;
    if (!Write1Byte(0x82, 0x00)) return;
    Write1Byte(0x12, v12 & 0xA1);
}

// -- sleep
void AXP192Component::DeepSleep(uint64_t time_in_us)
{
    SetSleep();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)37, 0 /* LOW */);
    if (time_in_us > 0)
    {
        esp_sleep_enable_timer_wakeup(time_in_us);
    }
    else
    {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
    (time_in_us == 0) ? esp_deep_sleep_start() : esp_deep_sleep(time_in_us);
}

void AXP192Component::LightSleep(uint64_t time_in_us)
{
    if (time_in_us > 0)
    {
        esp_sleep_enable_timer_wakeup(time_in_us);
    }
    else
    {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
    esp_light_sleep_start();
}

// 0 not press, 0x01 long press, 0x02 press
uint8_t AXP192Component::GetBtnPress()
{
    uint8_t state = 0;
    if (!TryRead8(0x46, &state)) return 0;
    if(state)
    {
        Write1Byte( 0x46 , 0x03 );
    }
    return state;
}

uint8_t AXP192Component::GetWarningLevel(void)
{
    return Read8bit(0x47) & 0x01;
}

float AXP192Component::GetBatVoltage()
{
    float ADCLSB = 1.1 / 1000.0;
    uint16_t ReData = Read12Bit( 0x78 );
    return ReData * ADCLSB;
}

float AXP192Component::GetBatCurrent()
{
    float ADCLSB = 0.5;
    uint16_t CurrentIn = Read13Bit( 0x7A );
    uint16_t CurrentOut = Read13Bit( 0x7C );
    return (static_cast<float>(CurrentIn) - static_cast<float>(CurrentOut)) * ADCLSB;
}

float AXP192Component::GetVinVoltage()
{
    float ADCLSB = 1.7 / 1000.0;
    uint16_t ReData = Read12Bit( 0x56 );
    return ReData * ADCLSB;
}

float AXP192Component::GetVinCurrent()
{
    float ADCLSB = 0.625;
    uint16_t ReData = Read12Bit( 0x58 );
    return ReData * ADCLSB;
}

float AXP192Component::GetVBusVoltage()
{
    float ADCLSB = 1.7 / 1000.0;
    uint16_t ReData = Read12Bit( 0x5A );
    return ReData * ADCLSB;
}

float AXP192Component::GetVBusCurrent()
{
    float ADCLSB = 0.375;
    uint16_t ReData = Read12Bit( 0x5C );
    return ReData * ADCLSB;
}

float AXP192Component::GetTempInAXP192()
{
    float ADCLSB = 0.1;
    const float OFFSET_DEG_C = -144.7;
    uint16_t ReData = Read12Bit( 0x5E );
    return OFFSET_DEG_C + ReData * ADCLSB;
}

float AXP192Component::GetBatPower()
{
    float VoltageLSB = 1.1;
    float CurrentLCS = 0.5;
    uint32_t ReData = Read24bit( 0x70 );
    return  VoltageLSB * CurrentLCS * ReData/ 1000.0;
}

float AXP192Component::GetBatChargeCurrent()
{
    float ADCLSB = 0.5;
    uint16_t ReData = Read13Bit( 0x7A );
    return ReData * ADCLSB;
}

float AXP192Component::GetAPSVoltage()
{
    float ADCLSB = 1.4  / 1000.0;
    uint16_t ReData = Read12Bit( 0x7E );
    return ReData * ADCLSB;
}

float AXP192Component::GetBatCoulombInput()
{
    uint32_t ReData = Read32bit( 0xB0 );
    return detail::coulomb_mAh(ReData, 0);
}

float AXP192Component::GetBatCoulombOut()
{
    uint32_t ReData = Read32bit( 0xB4 );
    return detail::coulomb_mAh(ReData, 0);
}

void AXP192Component::SetCoulombClear()
{
    Write1Byte(0xB8,0x20);
}

bool AXP192Component::SetLDO2(bool enabled) {
  uint8_t power_control = 0;
  if (!TryRead8(0x12, &power_control)) return false;
  const uint8_t desired = detail::set_ldo2_enabled(power_control, enabled);
  return desired == power_control || Write1Byte(0x12, desired);
}

void AXP192Component::SetLDO3(bool State)
{
    uint8_t buf = 0;
    if (!TryRead8(0x12, &buf)) return;
    if( State == true )
    {
        buf = (1<<3) | buf;
    }
    else
    {
        buf = ~(1<<3) & buf;
    }
    Write1Byte( 0x12 , buf );
}

void AXP192Component::SetChargeCurrent(uint8_t current)
{
    uint8_t buf = 0;
    if (!TryRead8(0x33, &buf)) return;
    buf = (buf & 0xf0) | (current & 0x07);
    Write1Byte(0x33, buf);
}

void AXP192Component::PowerOff()
{
    uint8_t value = 0;
    if (!TryRead8(0x32, &value)) return;
    Write1Byte(0x32, value | 0x80);
}

void AXP192Component::SetAdcState(bool state)
{
    Write1Byte(0x82, state ? 0xff : 0x00);
}

std::string AXP192Component::GetStartupReason() {
  esp_reset_reason_t reset_reason = ::esp_reset_reason();
  if (reset_reason == ESP_RST_DEEPSLEEP) {
    esp_sleep_source_t wake_reason = esp_sleep_get_wakeup_cause();
    if (wake_reason == ESP_SLEEP_WAKEUP_EXT0)
      return "ESP_SLEEP_WAKEUP_EXT0";
    if (wake_reason == ESP_SLEEP_WAKEUP_EXT1)
      return "ESP_SLEEP_WAKEUP_EXT1";
    if (wake_reason == ESP_SLEEP_WAKEUP_TIMER)
      return "ESP_SLEEP_WAKEUP_TIMER";
    if (wake_reason == ESP_SLEEP_WAKEUP_TOUCHPAD)
      return "ESP_SLEEP_WAKEUP_TOUCHPAD";
    if (wake_reason == ESP_SLEEP_WAKEUP_ULP)
      return "ESP_SLEEP_WAKEUP_ULP";
    if (wake_reason == ESP_SLEEP_WAKEUP_GPIO)
      return "ESP_SLEEP_WAKEUP_GPIO";
    if (wake_reason == ESP_SLEEP_WAKEUP_UART)
      return "ESP_SLEEP_WAKEUP_UART";
    return std::string{"WAKEUP_UNKNOWN_REASON"};
  }

  if (reset_reason == ESP_RST_UNKNOWN)
    return "ESP_RST_UNKNOWN";
  if (reset_reason == ESP_RST_POWERON)
    return "ESP_RST_POWERON";
  if (reset_reason == ESP_RST_SW)
    return "ESP_RST_SW";
  if (reset_reason == ESP_RST_PANIC)
    return "ESP_RST_PANIC";
  if (reset_reason == ESP_RST_INT_WDT)
    return "ESP_RST_INT_WDT";
  if (reset_reason == ESP_RST_TASK_WDT)
    return "ESP_RST_TASK_WDT";
  if (reset_reason == ESP_RST_WDT)
    return "ESP_RST_WDT";
  if (reset_reason == ESP_RST_BROWNOUT)
    return "ESP_RST_BROWNOUT";
  if (reset_reason == ESP_RST_SDIO)
    return "ESP_RST_SDIO";
  return std::string{"RESET_UNKNOWN_REASON"};
}



void AXP192DisplaySwitch::setup() {
  bool value = this->initial_value_;
  if (this->restore_value_) {
    this->pref_ = this->make_entity_preference<bool>();
    if (!this->pref_.load(&value)) value = this->initial_value_;
  }
  this->parent_->set_display_enabled(value);
  this->publish_state(value);
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
  if (persist && this->restore_value_)
    this->pref_.save(&value);
}

}
}
