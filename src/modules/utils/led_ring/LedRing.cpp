
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
#include "Conveyor.h"
#include "SlowTicker.h"

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
#define ready_rgb_cs          CHECKSUM("ready_rgb")
#define printing_rgb_cs       CHECKSUM("printing_rgb")

/*
    Ready: Orange (configurable)
    Heating Up: Cool blue to red.
    Heating Finished: Slow “thump” fade in and out red.
    Printing: White (configurable)
    Error: Blink red every 3 seconds
    LED1 - P1.22 LED2 - P0.25 LED3 - P4.29 LED4 - P2.8

    M150 Rnnn Unnn Bnnn override leds R G B disables autoset for leds
    M150 set autoset
*/

/**
    example config...

    led_ring.enable        true         #
    led_ring.red_led_pin   1.22         #
    led_ring.green_led_pin 0.25         #
    led_ring.blue_led_pin  4.29         #
    led_ring.hot_led_pin   2.8          #
    led_ring.ready_rgb     0,255,0      # set R,G,B of the ready light (green here orange by default)
    led_ring.printing_rgb  255,255,255  # set R,G,B of the printing light (white by default)

    # optional uncomment to set, values are the default
    #led_ring.red_max_pwm   255        # max pwm for green
    #led_ring.green_max_pwm 255        # max pwm for green
    #led_ring.blue_max_pwm  255        # max pwm for green
    #led_ring.hot_max_pwm   255        # max pwm for green

    #led_ring.print_finished_timeout 30  # timeout in seconds for the print finished sequence
    #led_ring.hot_temp               50  # temp at which things are considered hot

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
    if(hot_pin.connected()) hot_pin.max_pwm(  THEKERNEL->config->value(led_ring_cs, hot_max_pwm_cs  )->by_default(255)->as_number());

    red_pin.pwm(0);
    green_pin.pwm(0);
    blue_pin.pwm(0);
    if(hot_pin.connected()) hot_pin.pwm(0);

    string ready_rgb= THEKERNEL->config->value(led_ring_cs, ready_rgb_cs)->by_default("255,165,0")->as_string();
    std::vector<float> rgb= parse_number_list(ready_rgb.c_str());
    if(rgb.size() == 3) {
        ready_r= confine(rgb[0], 0, 255);
        ready_g= confine(rgb[1], 0, 255);
        ready_b= confine(rgb[2], 0, 255);
    }

    string printing_rgb= THEKERNEL->config->value(led_ring_cs, printing_rgb_cs)->by_default("255,255,255")->as_string();
    rgb= parse_number_list(printing_rgb.c_str());
    if(rgb.size() == 3) {
        printing_r= confine(rgb[0], 0, 255);
        printing_g= confine(rgb[1], 0, 255);
        printing_b= confine(rgb[2], 0, 255);
    }

    blink_timeout= THEKERNEL->config->value(led_ring_cs, print_finished_timeout_cs)->by_default(0)->as_number();
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

    THEKERNEL->slow_ticker->attach(2000, &red_pin, &Pwm::on_tick);
    THEKERNEL->slow_ticker->attach(2000, &green_pin, &Pwm::on_tick);
    THEKERNEL->slow_ticker->attach(2000, &blue_pin, &Pwm::on_tick);
    if(hot_pin.connected()) THEKERNEL->slow_ticker->attach(1000, &hot_pin, &Pwm::on_tick);
}

static struct pad_temperature getTemperatures(uint16_t heater_cs)
{
    struct pad_temperature temp;
    PublicData::get_value( temperature_control_checksum, current_temperature_checksum, heater_cs, &temp );
    return temp;
}

static int map2range(int x, int in_min, int in_max, int out_min, int out_max)
{
        return (((x - in_min) * (out_max - out_min + 1)) / (in_max - in_min + 1)) + out_min;
}

void LedRing::setLeds(int r, int g, int b)
{
    r= confine(roundf(r * red_pin.max_pwm() / 255.0F), 0, 255);
    g= confine(roundf(g * green_pin.max_pwm() / 255.0F), 0, 255);
    b= confine(roundf(b * blue_pin.max_pwm() / 255.0F), 0, 255);
    // scale by max_pwm so input of 255 and max_pwm of 128 would set value to 128
    red_pin.pwm(r);
    green_pin.pwm(g);
    blue_pin.pwm(b);
}

// take a value 0-255 and convert it to the logarithmic equivalent to get nicer fades
static int fade(int v)
{
    return floorf((exp2f(v/255.0F)-1)*255.0F);
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
        setLeds(printing_r, printing_g, printing_b);
        if(!printing && ++queue_cnt > 3600) {
            // if it has been not empty for over 120 seconds we will guess it is printing
            printing= true;
        }
        return;

    }else{
        if(printing) {
            // as we guessed it was printing, so now it must have finished so do the print finished thing
            print_finished= true;
            current_value= 255;
            printing= false;
            seconds= 0;
        }
        queue_cnt= 0;
    }

    int r=ready_r, g=ready_g, b=ready_b; // default ready color is orange

    // figure out percentage complete for all the things that are heating up
    bool heating= false, is_hot= false;
    int rh= 255;
    int he_cnt= 0; // count of heaters being heated
    int rt_cnt= 0; // how many reached temp count
    for(auto id : temp_controllers) {
        struct pad_temperature c= getTemperatures(id);
        if(c.current_temperature > hot_temp) is_hot= true; // anything is hot

        if(c.target_temperature > 0.1F){
            heating= true;
            he_cnt++;
            //pc= std::min(pc, c.current_temperature/c.target_temperature);
            int pc= map2range(c.current_temperature, 25, c.target_temperature, 0, 255);
            rh= std::min(pc, rh);
            if(c.current_temperature >= c.target_temperature) rt_cnt++; // if it reached temp increment cnt so we know if all have reached temp
         }
    }

    // when all heaters have reached temp flag it
    // if temps drop a bit have some hysteresis before reverting to heating sequence
    if(heating) {
        if(he_cnt == rt_cnt){
            reached_temp= true;
            cooled_cnt= 0;

        }else if(reached_temp && ++cooled_cnt > 300) {
            // 10 second reset
            reached_temp= false;
        }

    }else{
        reached_temp= false;
    }

    // If something is hot turn on the hot indicatior pin if defined
    if(hot_pin.connected()) {
        hot_pin.pwm(is_hot?255:0);
    }

    if(heating && !reached_temp) {
        // fades from blue to red as they get closer to target temp
        // rh is 0 to 255 but to get nice fades we need to make this logarithmic loge(rh/255)
        int lr= fade(rh);
        int lb= 255-lr;
        g= 0;
        r= lr;
        b= lb;
    }

    if(print_finished){
        // if we finished a print we fade the leds white on and off
        if(fade_dir) {
            current_value += 2;
            if(current_value >= 255) fade_dir= false;

        }else{
            current_value -= 2;
            if(current_value <= 0) fade_dir= true;
        }

        int v= fade(current_value);
        setLeds(v, v, v);

    } else if(reached_temp) {
        // fade in and out of red
        // TODO may need to time this, and set increment
        int r= red_pin.get_pwm();
        if(fade_dir) {
            r += 2;
            if(r >= 250) fade_dir= false;

        }else{
            r -= 2;
            if(r <= 10) fade_dir= true;
        }

        setLeds(r, 0, 0);

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

            setLeds(r, g, b);
        }
    }

}
