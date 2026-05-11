/*
  mcp4728.cpp - RP2040/Pico SDK library for MicroChip MCP4728 I2C D/A converter
  Converted from Arduino library to Pico SDK
  For implementation details, please take a look at the datasheet http://ww1.microchip.com/downloads/en/DeviceDoc/22187a.pdf
*/

/* _____PROJECT INCLUDES_____________________________________________________ */
#include "mcp4728.h"
#include <stdio.h>

// Hardware I2C pins
#define I2C_SDA_PIN 16
#define I2C_SCL_PIN 17
#define LDAC_PIN 15

/* _____PUBLIC FUNCTIONS_____________________________________________________ */
/**
Constructor.
Creates class object. Initialize buffers
*/
mcp4728::mcp4728(uint8_t deviceID)
{
  _deviceID = deviceID;
  _dev_address = (BASE_ADDR | _deviceID);
  _vdd = defaultVDD;
  _i2c = i2c0; // Use i2c0 instance
}

/*
Begin I2C, get current values (input register and eeprom) of mcp4728
*/
void mcp4728::begin()
{
  // Initialize I2C at 400kHz
  i2c_init(_i2c, 400000);
  
  // Set up GPIO pins for I2C
  gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA_PIN);
  gpio_pull_up(I2C_SCL_PIN);
  
  // Initialize LDAC pin as output (active low)
  gpio_init(LDAC_PIN);
  gpio_set_dir(LDAC_PIN, GPIO_OUT);
  gpio_put(LDAC_PIN, 1); // Set high (inactive)
  
  getStatus();
}

/*
General Call Reset of mcp4728 - EEPROM value will loaded to input register. refer to DATASHEET 5.4.1
*/
uint8_t mcp4728::reset() {
  return _simpleCommand(RESET);
}

/*
General Call Wake-Up of mcp4728 - Reset Power-Down bits (PD0,PD1 = 0,0). refer to DATASHEET 5.4.2
*/
uint8_t mcp4728::wake() {
  return _simpleCommand(WAKE);
}

/*
General Call Software update of mcp4728 - All DAC ouput update. refer to DATASHEET 5.4.3
*/
uint8_t mcp4728::update() {
  return _simpleCommand(UPDATE);
}

/*
Write input register values to each channel using multiwrite method.
Unlike analogWrite(), this ALSO sends Vref, Gain, and PowerDown bits
in every frame — guaranteeing those settings are applied regardless of
what the chip's EEPROM contains.  Use when Gain/Vref must be locked.
*/
uint8_t mcp4728::analogWriteAll(uint16_t value1, uint16_t value2,
                                 uint16_t value3, uint16_t value4) {
  _values[0] = value1;
  _values[1] = value2;
  _values[2] = value3;
  _values[3] = value4;
  return multiWrite();
}

/*
Write input register values to each channel using fastwrite method.
Values : 0-4095
*/
uint8_t mcp4728::analogWrite(uint16_t value1, uint16_t value2, uint16_t value3, uint16_t value4) {
  _values[0] = value1;
  _values[1] = value2;
  _values[2] = value3;
  _values[3] = value4;
  return fastWrite();
}

/*
Write input resister value to specified channel using fastwrite method.
Channel : 0-3, Values : 0-4095
*/
uint8_t mcp4728::analogWrite(uint8_t channel, uint16_t value) {
  _values[channel] = value;
  return fastWrite();
}

/*
Write a value to specified channel using singlewrite method.
This will update both input register and EEPROM value
Channel : 0-3, Values : 0-4095
*/
uint8_t mcp4728::eepromWrite(uint8_t channel, uint16_t value)
{
  _values[channel] = value;
  _valuesEp[channel] = value;
  return singleWrite(channel);
}

/*
Write values to each channel using SequencialWrite method.
This will update both input register and EEPROM value
Channel : 0-3, Values : 0-4095
*/
uint8_t mcp4728::eepromWrite(uint16_t value1, uint16_t value2, uint16_t value3, uint16_t value4)
{
  _valuesEp[0] = _values[0] = value1; 
  _valuesEp[1] = _values[1] = value2; 
  _valuesEp[2] = _values[2] = value3; 
  _valuesEp[3] = _values[3] = value4; 
  return seqWrite();
}

/*
Write all input resistor values to EEPROM using SequencialWrite method.
This will update both input register and EEPROM value
This will also write current Vref, PowerDown, Gain settings to EEPROM
*/
uint8_t mcp4728::eepromWrite()
{
  return seqWrite();
}

/*
Reset EEPROM and input register to factory default. Refer datasheet TABLE 4-2
Input value = 0, Voltage Reference = 1 (internal), Gain = 0, PowerDown = 0
*/
uint8_t mcp4728::eepromReset()
{
  _values[0] = _values[1] = _values[2] = _values[3] = 0;
  _intVref[0] = _intVref[1] = _intVref[2] = _intVref[3] = 1;
  _gain[0] = _gain[1] = _gain[2] = _gain[3] = 0;
  _powerDown[0] = _powerDown[1] = _powerDown[2] = _powerDown[3] = 0;
  return seqWrite();
}

/* 
  Write Voltage reference settings to input regiters
    Vref setting = 1 (internal), Gain = 0 (x1)   ==> Vref = 2.048V
    Vref setting = 1 (internal), Gain = 1 (x2)   ==> Vref = 4.096V
    Vref setting = 0 (external), Gain = ignored  ==> Vref = VDD
*/
uint8_t mcp4728::setVref(uint8_t value1, uint8_t value2, uint8_t value3, uint8_t value4) {
  _intVref[0] = value1;
  _intVref[1] = value2;
  _intVref[2] = value3;
  _intVref[3] = value4;
  return writeVref();
}

/* 
  Write Voltage reference setting to a input regiter
*/
uint8_t mcp4728::setVref(uint8_t channel, uint8_t value) {
  _intVref[channel] = value;
  return writeVref();
}

/* 
  Write Gain settings to input regiters
    Vref setting = 1 (internal), Gain = 0 (x1)   ==> Vref = 2.048V
    Vref setting = 1 (internal), Gain = 1 (x2)   ==> Vref = 4.096V
    Vref setting = 0 (external), Gain = ignored  ==> Vref = VDD
*/
uint8_t mcp4728::setGain(uint8_t value1, uint8_t value2, uint8_t value3, uint8_t value4) {
  _gain[0] = value1;
  _gain[1] = value2;
  _gain[2] = value3;
  _gain[3] = value4;
  return writeGain();
}

/* 
  Write Gain setting to a input regiter
*/
uint8_t mcp4728::setGain(uint8_t channel, uint8_t value) {
  _gain[channel] = value;
  return writeGain();
}

/*
  Write Power-Down settings to input regiters
    0 = Normal , 1-3 = shut down most channel circuit, no voltage out and saving some power.
    1 = 1K ohms to GND, 2 = 100K ohms to GND, 3 = 500K ohms to GND
*/
uint8_t mcp4728::setPowerDown(uint8_t value1, uint8_t value2, uint8_t value3, uint8_t value4) {
  _powerDown[0] = value1;
  _powerDown[1] = value2;
  _powerDown[2] = value3;
  _powerDown[3] = value4;
  return writePowerDown();
}

/*
  Write Power-Down setting to a input regiter
*/
uint8_t mcp4728::setPowerDown(uint8_t channel, uint8_t value) {
  _powerDown[channel] = value;
  return writePowerDown();
}

/*
  Return Device ID (up to 8 devices can be used in a I2C bus, Device ID = 0-7)
*/
uint8_t mcp4728::getId()
{
  return _deviceID;
}

/*
  Return Voltage Rerference setting
*/
uint8_t mcp4728::getVref(uint8_t channel)
{
  return _intVref[channel];
}

/*
  Return Gain setting
*/
uint8_t mcp4728::getGain(uint8_t channel)
{
  return _gain[channel];
}

/*
  Return PowerDown setting
*/
uint8_t mcp4728::getPowerDown(uint8_t channel)
{
  return _powerDown[channel];
}

/*
  Return Input Regiter value
*/
uint16_t mcp4728::getValue(uint8_t channel)
{
  return _values[channel];
}

/*
  Return EEPROM Voltage Rerference setting
*/
uint8_t mcp4728::getVrefEp(uint8_t channel)
{
  return _intVrefEp[channel];
}

/*
  Return EEPROM Gain setting
*/
uint8_t mcp4728::getGainEp(uint8_t channel)
{
  return _gainEp[channel];
}

/*
  Return EEPROM PowerDown setting
*/
uint8_t mcp4728::getPowerDownEp(uint8_t channel)
{
  return _powerDownEp[channel];
}

/*
  Return EEPROM value
*/
uint16_t mcp4728::getValueEp(uint8_t channel)
{
  return _valuesEp[channel];
}

/*
  Set VDD for Vout calculation
*/
void mcp4728::vdd(uint16_t currentVdd)
{
  _vdd = currentVdd;
}

/*
  Return Vout
*/
uint16_t mcp4728::getVout(uint8_t channel)
{
  uint32_t vref;
  if (_intVref[channel] == 1) {
      vref = 2048;
  }
  else {
      vref = _vdd;
  }

  uint32_t vOut = (vref * _values[channel] * (_gain[channel] + 1)) / 4096;
  if (vOut > _vdd) {
      vOut = _vdd;
  }
  return vOut;
}

/*
  write to input register of DAC. Value(mV) (V < VDD)
*/
void mcp4728::voutWrite(uint8_t channel, uint16_t vout)
{
  _vOut[channel] = vout;
  writeVout();
}

/*
  write to input registers of DACs. Value(mV) (V < VDD)
*/
void mcp4728::voutWrite(uint16_t value1, uint16_t value2, uint16_t value3, uint16_t value4)
{
  _vOut[0] = value1;
  _vOut[1] = value2;
  _vOut[2] = value3;
  _vOut[3] = value4;
  writeVout();
}

/*
  Pulse LDAC pin low to update all DAC outputs simultaneously
*/
void mcp4728::ldacPulse()
{
  gpio_put(LDAC_PIN, 0);
  sleep_us(1);
  gpio_put(LDAC_PIN, 1);
}

/* _____PRIVATE FUNCTIONS_____________________________________________________ */

/*
Get current values (input register and eeprom) of mcp4728
*/
void mcp4728::getStatus()
{
  uint8_t buffer[24];
  int bytes_read = i2c_read_blocking(_i2c, _dev_address, buffer, 24, false);
  
  if (bytes_read == 24) {
    for (int i = 0; i < 24; i += 3) {
      uint8_t deviceID = buffer[i];
      uint8_t hiByte = buffer[i + 1];
      uint8_t loByte = buffer[i + 2];
      
      int isEEPROM = (deviceID & 0B00001000) >> 3;
      int channel = (deviceID & 0B00110000) >> 4;
      
      if (isEEPROM == 1) {
        _intVrefEp[channel] = (hiByte & 0B10000000) >> 7;
        _gainEp[channel] = (hiByte & 0B00010000) >> 4;
        _powerDownEp[channel] = (hiByte & 0B01100000) >> 5;
        _valuesEp[channel] = ((hiByte & 0B00001111) << 8) | loByte;
      }
      else {
        _intVref[channel] = (hiByte & 0B10000000) >> 7;
        _gain[channel] = (hiByte & 0B00010000) >> 4;
        _powerDown[channel] = (hiByte & 0B01100000) >> 5;
        _values[channel] = ((hiByte & 0B00001111) << 8) | loByte;
      }
    }
  }
}

/*
FastWrite input register values - All DAC ouput update. refer to DATASHEET 5.6.1
DAC Input and PowerDown bits update.
No EEPROM update
*/
uint8_t mcp4728::fastWrite() {
  uint8_t data[8];
  
  for (uint8_t channel = 0; channel <= 3; channel++) {
    data[channel * 2] = (_values[channel] >> 8) & 0xFF;
    data[channel * 2 + 1] = _values[channel] & 0xFF;
  }
  
  int result = i2c_write_blocking(_i2c, _dev_address, data, 8, false);
  return (result == 8) ? 0 : 1;
}

/*
MultiWrite input register values - All DAC ouput update. refer to DATASHEET 5.6.2
DAC Input, Gain, Vref and PowerDown bits update
No EEPROM update
*/
uint8_t mcp4728::multiWrite() {
  uint8_t data[12];
  
  for (uint8_t channel = 0; channel <= 3; channel++) {
    data[channel * 3] = MULTIWRITE | (channel << 1);
    data[channel * 3 + 1] = _intVref[channel] << 7 | _powerDown[channel] << 5 | _gain[channel] << 4 | ((_values[channel] >> 8) & 0x0F);
    data[channel * 3 + 2] = _values[channel] & 0xFF;
  }
  
  int result = i2c_write_blocking(_i2c, _dev_address, data, 12, false);
  return (result == 12) ? 0 : 1;
}

/*
SingleWrite input register and EEPROM - a DAC ouput update. refer to DATASHEET 5.6.4
DAC Input, Gain, Vref and PowerDown bits update
EEPROM update
*/
uint8_t mcp4728::singleWrite(uint8_t channel) {
  uint8_t data[3];
  
  data[0] = SINGLEWRITE | (channel << 1);
  data[1] = _intVref[channel] << 7 | _powerDown[channel] << 5 | _gain[channel] << 4 | ((_values[channel] >> 8) & 0x0F);
  data[2] = _values[channel] & 0xFF;
  
  int result = i2c_write_blocking(_i2c, _dev_address, data, 3, false);
  return (result == 3) ? 0 : 1;
}

/*
SequencialWrite input registers and EEPROM - ALL DAC ouput update. refer to DATASHEET 5.6.3
DAC Input, Gain, Vref and PowerDown bits update
EEPROM update
*/
uint8_t mcp4728::seqWrite() {
  uint8_t data[9];
  
  data[0] = SEQWRITE;
  for (uint8_t channel = 0; channel <= 3; channel++) {
    data[channel * 2 + 1] = _intVref[channel] << 7 | _powerDown[channel] << 5 | _gain[channel] << 4 | ((_values[channel] >> 8) & 0x0F);
    data[channel * 2 + 2] = _values[channel] & 0xFF;
  }
  
  int result = i2c_write_blocking(_i2c, _dev_address, data, 9, false);
  return (result == 9) ? 0 : 1;
}

/*
Write Voltage reference setting to input registers. refer to DATASHEET 5.6.5
No EEPROM update
*/
uint8_t mcp4728::writeVref() {
  uint8_t data = VREFWRITE | _intVref[0] << 3 | _intVref[1] << 2 | _intVref[2] << 1 | _intVref[3];
  
  int result = i2c_write_blocking(_i2c, _dev_address, &data, 1, false);
  return (result == 1) ? 0 : 1;
}

/*
Write Gain setting to input registers. refer to DATASHEET 5.6.7
No EEPROM update
*/
uint8_t mcp4728::writeGain() {
  uint8_t data = GAINWRITE | _gain[0] << 3 | _gain[1] << 2 | _gain[2] << 1 | _gain[3];
  
  int result = i2c_write_blocking(_i2c, _dev_address, &data, 1, false);
  return (result == 1) ? 0 : 1;
}

/*
Write PowerDown setting to input registers. refer to DATASHEET 5.6.6
No EEPROM update
*/
uint8_t mcp4728::writePowerDown() {
  uint8_t data[2];
  
  data[0] = POWERDOWNWRITE | _powerDown[0] << 2 | _powerDown[1];
  data[1] = _powerDown[2] << 6 | _powerDown[3] << 4;
  
  int result = i2c_write_blocking(_i2c, _dev_address, data, 2, false);
  return (result == 2) ? 0 : 1;
}

/*
Calculate Voltage out based on current setting of Vref and gain
No EEPROM update
*/
void mcp4728::writeVout()
{
  for (uint8_t channel = 0; channel <= 3; channel++) {
    if (_intVref[channel] == 1) {
      _values[channel] = _vOut[channel] / (_gain[channel] + 1) * 2;
    }
    else {
      _values[channel] = ((uint32_t)_vOut[channel] * 4096) / _vdd;
    }
  }
  fastWrite();
}

/*
Common function for simple genenral commands
*/
uint8_t mcp4728::_simpleCommand(uint8_t simpleCommand) {
  uint8_t data = simpleCommand;
  
  int result = i2c_write_blocking(_i2c, GENERALCALL, &data, 1, false);
  return (result == 1) ? 0 : 1;
}