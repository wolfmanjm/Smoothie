
#include "LedRing.h"
#include "Kernel.h"
#include "Config.h"
#include "checksumm.h"
#include "ConfigValue.h"
#include "Gcode.h"
#include "libs/nuts_bolts.h"
#include "libs/utils.h"
#include "TemperatureControlPublicAccess.h"
#include "PlayerPublicAccess.h"
#include "NetworkPublicAccess.h"
#include "PublicData.h"
#include "Pauser.h"
#include "Conveyor.h"

#include "us_ticker_api.h" // mbed

#include <fastmath.h>

#define led_ring_cs           CHECKSUM("led_ring")
#define enable_cs             CHECKSUM("enable")
#define red_led_pin_cs        CHECKSUM("red_led_pin")
#define green_led_pin_cs      CHECKSUM("green_led_pin")
#define blue_led_pin_cs       CHECKSUM("blue_led_pin")
#define hot_led_pin_cs        CHECKSUM("hot_led_pin")
#define red_max_pwm_cs        CHECKSUM("red_max_pwm")
#define green_max_pwm_cs      CHECKSUM("green_max_pwm")
#define blue_max_pwm_cs       CHECKSUM("blue_max_pwm")
#define hot_max_pwm_cs        CHECKSUM("hot_max_pwm")
#define print_finished_timeout_cs CHECKSUM("print_finished_timeout")
#define hot_temp_cs           CHECKSUM("hot_temp")


/*
    Ready: Orange
    Heating Up: Cool blue to red.
    Heating Finished: Slow “thump” fade in and out red.
    Printing: White
    Print Finished: Slow “thump” white
    Error: Blink red every 3 seconds
    LED1 - P1.22 LED2 - P0.25 LED3 - P4.29 LED4 - P2.8

    M150 Rnnn Unnn Bnnn override leds RGB
    M150 set autorun
*/

/**
    example config...

    led_ring.enable        true         #
    led_ring.red_led_pin   1.22         #
    led_ring.green_led_pin 0.25         #
    led_ring.blue_led_pin  4.29         #
    led_ring.hot_led_pin   2.8          #

*/

void LedRing::on_module_loaded()
{
    // if the module is disabled -> do nothing
    if(!THEKERNEL->config->value( led_ring_cs, enable_cs )->by_default(false)->as_bool()) {
        // as this module is not needed free up the resource
        delete this;
        return;
    }

    red_pin.from_string( THEKERNEL->config->value(led_ring_cs, red_led_pin_cs)->by_default("nc" )->as_string())->as_output();
    green_pin.from_string( THEKERNEL->config->value(led_ring_cs, green_led_pin_cs)->by_default("nc" )->as_string())->as_output();
    blue_pin.from_string( THEKERNEL->config->value(led_ring_cs, blue_led_pin_cs)->by_default("nc" )->as_string())->as_output();
    hot_pin.from_string( THEKERNEL->config->value(led_ring_cs, hot_led_pin_cs)->by_default("nc" )->as_string())->as_output();

    if(!red_pin.connected() && !green_pin.connected() && !blue_pin.connected() && !hot_pin.connected()) {
        // as this module has not defined any led pins free it up
        delete this;
        return;
    }

    red_pin.max_pwm(  THEKERNEL->config->value(led_ring_cs, red_max_pwm_cs  )->by_default(255)->as_number());
    green_pin.max_pwm(THEKERNEL->config->value(led_ring_cs, green_max_pwm_cs)->by_default(255)->as_number());
    blue_pin.max_pwm( THEKERNEL->config->value(led_ring_cs, blue_max_pwm_cs )->by_default(255)->as_number());
    hot_pin.max_pwm(  THEKERNEL->config->value(led_ring_cs, hot_max_pwm_cs  )->by_default(255)->as_number());

    blink_timeout= THEKERNEL->config->value(led_ring_cs, print_finished_timeout_cs)->by_default(30)->as_number();
    hot_temp= THEKERNEL->config->value(led_ring_cs, hot_temp_cs)->by_default(50)->as_number();

    // enumerate temperature controls
    temp_controllers.clear();
    std::vector<struct pad_temperature> controllers;
    bool ok = PublicData::get_value(temperature_control_checksum, poll_controls_checksum, &controllers);
    if (ok) {
        for (auto &c : controllers) {
            temp_controllers.push_back(c.id);
        }
    }

    this->register_for_event(ON_IDLE);
    this->register_for_event(ON_SECOND_TICK);
    this->register_for_event(ON_GCODE_RECEIVED);
}

static struct pad_temperature getTemperatures(uint16_t heater_cs)
{
    struct pad_temperature temp;
    PublicData::get_value( temperature_control_checksum, current_temperature_checksum, heater_cs, &temp );
    return temp;
}

void LedRing::setLeds(uint8_t r, uint8_t g, uint8_t b)
{
    // scale by max_pwm so input of 255 and max_pwm of 128 would set value to 128
    red_pin.pwm(roundf(r * red_pin.max_pwm() / 255.0F));
    green_pin.pwm(roundf(r * green_pin.max_pwm() / 255.0F));
    blue_pin.pwm(roundf(r * blue_pin.max_pwm() / 255.0F));
}

void LedRing::on_idle( void* argument )
{
    if(!autorun) return;

    if(THEKERNEL->is_halted()) {
        // when halted on_second_tick will flash red
        return;
    }

    uint32_t tus= us_ticker_read(); // mbed call
    if(tus - last_time_us >= 33333) { // only check at 30Hz
        last_time_us= tus;

    }else{
        return;
    }

    if (!THEKERNEL->conveyor->is_queue_empty()) {
        // white when printing
        setLeds(255, 255, 255);
        if(!printing && queue_cnt++ > 300) {
            // if it has been not empty for over 10 seconds we will guess it is printing
            printing= true;
        }
        return;

    }else{
        if(printing) {
            // as we guessed it was printing, so now it must have finished so do the print finished thing
            print_finished= true;
            printing= false;
            seconds= 0;
        }
        queue_cnt= 0;
    }

    uint8_t r=255, g=165, b=0; // default ready color is orange

    // figure out percentage complete for all the things that are heating up
    bool heating= false, is_hot= false;
    float pc= 1.0F;
    for(auto id : temp_controllers) {
        struct pad_temperature c= getTemperatures(id);
        if(c.current_temperature > hot_temp) is_hot= true; // anything is hot

        if(c.target_temperature > 0){
            heating= true;
            pc= std::min(pc, c.current_temperature/c.target_temperature);
         }
    }

    if(hot_pin.connected()) {
        hot_pin.pwm(is_hot?255:0);
    }

    if(heating) {
        // fades from blue to red as they get closer to target temp
        g= 0;
        r= roundf(255*pc);
        b= roundf(255 - r);
        if(r >= 254) {
            reached_temp= true;
            fade_dir= false;
        }

    }else{
        reached_temp= false;
    }

    if(reached_temp) {
        // fade in and out of red
        // TODO may need to time this, and set increment
        int r= red_pin.get_pwm();
        if(fade_dir) {
            r += 2;
            if(r >= 255) fade_dir= false;

        }else{
            r -= 2;
            if(r <= 0) fade_dir= true;
        }

        r= confine(r, 0, 255);
        setLeds(r, 0, 0);

    }else if(print_finished){
        // if we finished a print we fade the leds white on and off
        int v= red_pin.get_pwm();
        if(fade_dir) {
            v += 2;
            if(v >= 255) fade_dir= false;

        }else{
            v -= 2;
            if(v <= 0) fade_dir= true;
        }

        v= confine(v, 0, 255);
        setLeds(v, v, v);

    }else{
        setLeds(r, g, b);
    }
}

void  LedRing::on_second_tick(void* argument)
{
    seconds++;
    if(THEKERNEL->is_halted()) {
        if((seconds%3) == 0) {
            // every 3 seconds blink red
            setLeds((red_pin.get_pwm()==255)?0:255, 0, 0);
        }
        return;
    }

    if(autorun && print_finished && seconds > blink_timeout) {
        print_finished= false;
    }
}

void  LedRing::on_gcode_received(void *argument)
{
    Gcode *gcode = static_cast<Gcode *>(argument);

     if (gcode->has_m) {
        if (gcode->m == 150) {

            if(gcode->get_num_args() == 0) {
                // M150 set leds to auto
                autorun= true;
                return;
            }

            // M150 Rnnn Unnn Bnnn override leds RGB
            int r= red_pin.get_pwm();
            int g= green_pin.get_pwm();
            int b= blue_pin.get_pwm();

            if(gcode->has_letter('R')){
                r= gcode->get_value('R');
                autorun= false;
            }

            if(gcode->has_letter('U')){
                g= gcode->get_value('U');
                autorun= false;
            }

            if(gcode->has_letter('B')){
                b= gcode->get_value('B');
                autorun= false;
            }

            r= confine(r, 0, 255);
            g= confine(g, 0, 255);
            b= confine(b, 0, 255);
            setLeds(r, g, b);
        }
    }

}
