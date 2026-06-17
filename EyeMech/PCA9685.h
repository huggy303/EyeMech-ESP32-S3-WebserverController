#ifndef PCA9685_H
#define PCA9685_H

#include <Arduino.h>
#include <Wire.h>

// PCA9685 register addresses (only the ones this driver actually uses)
#define PCA9685_ADDRESS  0x40
#define MODE1            0x00
#define PRESCALE         0xFE
#define LED0_ON_L        0x06
#define LED0_ON_H        0x07
#define LED0_OFF_L       0x08
#define LED0_OFF_H       0x09

class PCA9685 {
public:
    PCA9685(uint8_t address = PCA9685_ADDRESS);
    void begin();
    void reset();
    void setPWMFreq(float freq_hz);
    void setPWM(uint8_t channel, uint16_t on, uint16_t off);
    void setServoAngle(uint8_t channel, int angle);

    // Bench-test helpers
    bool isConnected();          // pings the device, true if it ACKs over I2C
    void setDebug(bool enable);  // when true, every command is logged to Serial

private:
    uint8_t _address;
    bool _debug = false;
    bool _connected = false;     // updated on every I2C write
    void writeByte(uint8_t reg, uint8_t value);
    uint8_t readByte(uint8_t reg);
};

#endif
