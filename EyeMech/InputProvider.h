#ifndef INPUT_PROVIDER_H
#define INPUT_PROVIDER_H

#include <Arduino.h>

// Holds the latest control values pushed from the web dashboard (/update).
// The old physical-joystick provider and the polymorphic InputProvider
// interface were removed: the webserver is now the only input source.
class WebInputProvider {
private:
    // ADC-style values matching CENTER_LR / CENTER_UD in scalePotentiometer()
    // (EyeMech.ino). The dashboard sends 0..4095 around those centres.
    // (Blink is no longer a held value — it's a one-shot /blink route.)
    int  _lr   = 2960;
    int  _ud   = 2970;

public:
    void update(int lr, int ud) {
        _lr = lr;
        _ud = ud;
    }

    int  getLR()   { return _lr; }
    int  getUD()   { return _ud; }
};

#endif // INPUT_PROVIDER_H
