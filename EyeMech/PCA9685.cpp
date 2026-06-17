#include "PCA9685.h"

PCA9685::PCA9685(uint8_t address) {
    _address = address;
}

void PCA9685::begin() {
    // NB: the sketch already calls Wire.begin(4, 5) (SDA=4, SCL=5). Do NOT re-init
    // the bus here with no args — on ESP32-S3 that would reset I2C to default pins.
    _connected = isConnected();
    reset();
}

bool PCA9685::isConnected() {
    Wire.beginTransmission(_address);
    _connected = (Wire.endTransmission() == 0);  // 0 = device ACKed
    return _connected;
}

void PCA9685::setDebug(bool enable) {
    _debug = enable;
}

void PCA9685::reset() {
    // 0x20 sets the AI (auto-increment) bit. REQUIRED: setPWM() writes the four
    // ON/OFF registers as one block and relies on the register pointer advancing
    // after each byte. With AI=0 (the power-up default) the pointer never moves,
    // so all four bytes overwrite LED0_ON_L and the channel never gets a valid
    // pulse — servos stay dead even though the I2C write is ACKed. SLEEP stays 0
    // (awake); setPWMFreq() reads/preserves this mode, so AI survives the restart.
    writeByte(MODE1, 0x20);
}

void PCA9685::writeByte(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(_address);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t PCA9685::readByte(uint8_t reg) {
    Wire.beginTransmission(_address);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(_address, (uint8_t)1);
    return Wire.read();
}

void PCA9685::setPWMFreq(float freq_hz) {
    float prescaleval = 25000000.0;  // 25MHz oscillator
    prescaleval /= 4096.0;           // 12-bit resolution
    prescaleval /= freq_hz;
    prescaleval -= 1.0;
    uint8_t prescale = (uint8_t)(prescaleval + 0.5);

    uint8_t oldmode = readByte(MODE1);
    uint8_t newmode = (oldmode & 0x7F) | 0x10;  // Set sleep bit
    writeByte(MODE1, newmode);                    // Enter sleep mode
    writeByte(PRESCALE, prescale);               // Set prescaler
    writeByte(MODE1, oldmode);                   // Restore mode
    delay(5);
    writeByte(MODE1, oldmode | 0x80);            // Restart
}

void PCA9685::setPWM(uint8_t channel, uint16_t on, uint16_t off) {
    Wire.beginTransmission(_address);
    Wire.write(LED0_ON_L  + 4 * channel);
    Wire.write(on  & 0xFF);
    Wire.write(on  >> 8);
    Wire.write(off & 0xFF);
    Wire.write(off >> 8);
    _connected = (Wire.endTransmission() == 0);  // track whether the write was ACKed
}

void PCA9685::setServoAngle(uint8_t channel, int angle) {
    // Map 0-180° to pulse width counts (150 = ~0.5ms, 600 = ~2.5ms at 50Hz/4096 counts)
    int pulse_width = 150 + (angle * 450 / 180);
    setPWM(channel, 0, pulse_width);

    if (_debug) {
        Serial.printf("[PWM] CH%-2u  angle=%3d  pulse=%3d  %s\n",
                      channel, angle, pulse_width,
                      _connected ? "OK (I2C ACK)" : "-- NO ACK (sim/no board)");
    }
}
