#pragma once

#include "Module.h"
#include "Pin.h"
#include "Pwm.h"

#include <stdint.h>
#include <vector>

class LedRing : public Module
{
public:
    LedRing() : autorun(true), reached_temp(false), halted(false), print_finished(false), printing(false) {};
    ~LedRing(){};

    void on_module_loaded();
    void on_idle( void* argument );
    void on_second_tick(void* argument);
    void on_gcode_received(void *argument);

private:
    void setLeds(int r, int g, int b);

    Pwm red_pin, blue_pin, green_pin, hot_pin;
    std::vector<uint16_t> temp_controllers;
    float hot_temp;
    int current_value{0};
    uint32_t last_time_us{0};
    uint16_t queue_cnt{0};
    uint16_t blink_timeout;
    uint16_t seconds{0};
    uint8_t ready_r, ready_g, ready_b;
    uint8_t printing_r, printing_g, printing_b;

    struct {
        uint16_t cooled_cnt:10;
        bool autorun:1;
        bool reached_temp:1;
        bool fade_dir:1;
        bool halted:1;
        bool print_finished:1;
        bool printing:1;
    };

};
