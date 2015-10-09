#ifndef _PWM_H
#define _PWM_H

#include <stdint.h>

#include "Pin.h"

class Pwm : public Pin {
public:
    Pwm();

    uint32_t on_tick(uint32_t);

    Pwm*     max_pwm(int);
    int      max_pwm(void);

    void     pwm(int);
    int      get_pwm() const { return _pwm; }
    void     set(bool);

private:
    int  _max;
    int  _pwm;
    int  _sd_accumulator;
    bool _sd_direction;
};

#endif /* _PWM_H */
