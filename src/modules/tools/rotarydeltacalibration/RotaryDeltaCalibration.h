#pragma once

#include "Module.h"

#include <stdint.h>

class Gcode;
class Pin;

class RotaryDeltaCalibration : public Module
{
public:
    RotaryDeltaCalibration(){};
    virtual ~RotaryDeltaCalibration(){};

    void on_module_loaded();

private:
    void on_gcode_received(void *argument);
    void on_console_line_received( void *argument );
    bool get_homing_offset(float*);

    Pin *adc_x_pin;
    Pin *adc_y_pin;
    Pin *adc_z_pin;
    uint32_t rotary_min, rotary_max;
};
