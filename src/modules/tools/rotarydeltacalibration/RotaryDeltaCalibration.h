#pragma once

#include "Module.h"

#include <stdint.h>
#include <vector>

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

    std::vector<Pin *> adc_pins;
    uint32_t rotary_min, rotary_max;
};
