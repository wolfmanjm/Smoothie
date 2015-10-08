#pragma once

#include "Module.h"
#include "Pin.h"
#include "Pwm.h"

#include <stdint.h>
#include <vector>

class LedRing : public Module
{
public:
    LedRing(){};
    ~LedRing(){};

    void on_module_loaded();
    void on_idle( void* argument );
    void on_second_tick(void* argument);
    void on_gcode_received(void *argument);

private:
    void setLeds(uint8_t r, uint8_t g, uint8_t b);

    Pwm red_pin, blue_pin, green_pin, aux_pin;
    uint32_t last_time_us{0};
    uint16_t blink_timeout;
    uint16_t seconds;
    uint16_t queue_cnt{0};
    std::vector<uint16_t> temp_controllers;

    struct {
        bool reached_temp:1;
        bool fade_dir:1;
        bool halted:1;
        bool print_finished:1;
        bool printing:1;
    };

};
