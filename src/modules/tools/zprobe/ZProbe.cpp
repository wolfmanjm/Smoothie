/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#include "ZProbe.h"

#include "Kernel.h"
#include "BaseSolution.h"
#include "Config.h"
#include "Robot.h"
#include "StepperMotor.h"
#include "StreamOutputPool.h"
#include "Gcode.h"
#include "Conveyor.h"
#include "Stepper.h"
#include "checksumm.h"
#include "ConfigValue.h"
#include "SlowTicker.h"
#include "Planner.h"

#include <tuple>
#include <algorithm>

#define zprobe_checksum          CHECKSUM("zprobe")
#define enable_checksum          CHECKSUM("enable")
#define probe_pin_checksum       CHECKSUM("probe_pin")
#define debounce_count_checksum  CHECKSUM("debounce_count")
#define slow_feedrate_checksum   CHECKSUM("slow_feedrate")
#define fast_feedrate_checksum   CHECKSUM("fast_feedrate")
#define probe_radius_checksum    CHECKSUM("probe_radius")
#define endstop_radius_checksum  CHECKSUM("endstop_radius")
#define probe_height_checksum    CHECKSUM("probe_height")

// from endstop section
#define delta_homing_checksum    CHECKSUM("delta_homing")
#define arm_radius_checksum      CHECKSUM("arm_radius")

#define alpha_steps_per_mm_checksum      CHECKSUM("alpha_steps_per_mm")
#define beta_steps_per_mm_checksum       CHECKSUM("beta_steps_per_mm")
#define gamma_steps_per_mm_checksum      CHECKSUM("gamma_steps_per_mm")

#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2

#include "Vector3.h"
// define a plane given three points
class Plane
{
private:
    Vector3 normal;
    float d;

public:
    Plane(const Vector3 &v1, const Vector3 &v2, const Vector3 &v3)
    {
        // get the normal of the plane
        Vector3 ab = v1.sub(v2);
        Vector3 ac = v1.sub(v3);

        Vector3 cp = ab.cross(ac);
        normal = cp.unit();

        // ax+by+cz+d=0
        // solve for d
        Vector3 dv = normal.mul(v1);
        d = -dv[0] - dv[1] - dv[2];
    }

    // solve for z given x and y
    // z= (-ax - by - d)/c
    float getz(float x, float y)
    {
        return ((-normal[0] * x) - (normal[1] * y) - d) / normal[2];
    }

    Vector3 getNormal() const
    {
        return normal;
    }
};

void ZProbe::on_module_loaded()
{
    // if the module is disabled -> do nothing
    this->enabled = THEKERNEL->config->value( zprobe_checksum, enable_checksum )->by_default(false)->as_bool();
    if( !(this->enabled) ) {
        // as this module is not needed free up the resource
        delete this;
        return;
    }
    this->running = false;

    // load settings
    this->on_config_reload(this);
    // register event-handlers
    register_for_event(ON_CONFIG_RELOAD);
    register_for_event(ON_GCODE_RECEIVED);
    register_for_event(ON_IDLE);

    THEKERNEL->slow_ticker->attach( THEKERNEL->stepper->acceleration_ticks_per_second , this, &ZProbe::acceleration_tick );
}

void ZProbe::on_config_reload(void *argument)
{
    this->pin.from_string( THEKERNEL->config->value(zprobe_checksum, probe_pin_checksum)->by_default("nc" )->as_string())->as_input();
    this->debounce_count = THEKERNEL->config->value(zprobe_checksum, debounce_count_checksum)->by_default(0  )->as_number();

    // Note this could be overriden by M665
    float delta_radius = THEKERNEL->config->value(arm_radius_checksum)->by_default(124.0f)->as_number();

    // default is half the delta radius
    this->probe_radius =  THEKERNEL->config->value(zprobe_checksum, probe_radius_checksum)->by_default(delta_radius / 2)->as_number();
    this->endstop_radius =  THEKERNEL->config->value(zprobe_checksum, endstop_radius_checksum)->by_default(delta_radius)->as_number();
    this->probe_height =  THEKERNEL->config->value(zprobe_checksum, probe_height_checksum)->by_default(5.0F)->as_number();

    this->steppers[0] = THEKERNEL->robot->alpha_stepper_motor;
    this->steppers[1] = THEKERNEL->robot->beta_stepper_motor;
    this->steppers[2] = THEKERNEL->robot->gamma_stepper_motor;

    // we need to know steps per mm
    // FIXME we need to get this after config loaded from robot as the config settings can be overriden or trap M92
    this->steps_per_mm[0] = THEKERNEL->config->value(alpha_steps_per_mm_checksum)->as_number();
    this->steps_per_mm[1] = THEKERNEL->config->value(beta_steps_per_mm_checksum)->as_number();
    this->steps_per_mm[2] = THEKERNEL->config->value(gamma_steps_per_mm_checksum)->as_number();

    this->slow_feedrate = THEKERNEL->config->value(zprobe_checksum, slow_feedrate_checksum)->by_default(5)->as_number(); // feedrate in mm/sec
    this->fast_feedrate = THEKERNEL->config->value(zprobe_checksum, fast_feedrate_checksum)->by_default(100)->as_number(); // feedrate in mm/sec

    // see what type of arm solution we need to use
    this->is_delta =  THEKERNEL->config->value(delta_homing_checksum)->by_default(false)->as_bool();
}

bool ZProbe::wait_for_probe(int steps[3])
{
    unsigned int debounce = 0;
    while(true) {
        THEKERNEL->call_event(ON_IDLE);
        // if no stepper is moving, moves are finished and there was no touch
        if( !this->steppers[X_AXIS]->moving && !this->steppers[Y_AXIS]->moving && !this->steppers[Z_AXIS]->moving ) {
            return false;
        }

        // if the touchprobe is active...
        if( this->pin.get() ) {
            //...increase debounce counter...
            if( debounce < debounce_count) {
                // ...but only if the counter hasn't reached the max. value
                debounce++;
            } else {
                // ...otherwise stop the steppers, return its remaining steps
                for( int i = X_AXIS; i <= Z_AXIS; i++ ) {
                    steps[i] = 0;
                    if ( this->steppers[i]->moving ) {
                        steps[i] =  this->steppers[i]->stepped;
                        this->steppers[i]->move(0, 0);
                    }
                }
                return true;
            }
        } else {
            // The probe was not hit yet, reset debounce counter
            debounce = 0;
        }
    }
}

void ZProbe::on_idle(void *argument)
{
}

// single probe and report amount moved
bool ZProbe::run_probe(int& steps, bool fast)
{
    // Enable the motors
    THEKERNEL->stepper->turn_enable_pins_on();
    this->current_feedrate = (fast ? this->fast_feedrate : this->slow_feedrate) * this->steps_per_mm[Z_AXIS]; // steps/sec

    // move Z down
    this->running = true;
    this->steppers[Z_AXIS]->set_speed(0); // will be increased by acceleration tick
    this->steppers[Z_AXIS]->move(true, 1000 * this->steps_per_mm[Z_AXIS]); // always probes down, no more than 1000mm TODO should be 2*maxz
    if(this->is_delta) {
        // for delta need to move all three actuators
        this->steppers[X_AXIS]->set_speed(0);
        this->steppers[X_AXIS]->move(true, 1000 * this->steps_per_mm[X_AXIS]);
        this->steppers[Y_AXIS]->set_speed(0);
        this->steppers[Y_AXIS]->move(true, 1000 * this->steps_per_mm[Y_AXIS]);
    }

    int s[3];
    bool r = wait_for_probe(s);
    steps= s[2]; // only need z
    this->running = false;
    return r;
}

bool ZProbe::return_probe(int steps)
{
    // move probe back to where it was
    this->current_feedrate = this->fast_feedrate * this->steps_per_mm[Z_AXIS]; // feedrate in steps/sec
    bool dir= steps < 0;
    steps= abs(steps);

    this->running = true;
    this->steppers[Z_AXIS]->set_speed(0); // will be increased by acceleration tick
    this->steppers[Z_AXIS]->move(dir, steps);
    if(this->is_delta) {
        this->steppers[X_AXIS]->set_speed(0);
        this->steppers[X_AXIS]->move(dir, steps);
        this->steppers[Y_AXIS]->set_speed(0);
        this->steppers[Y_AXIS]->move(dir, steps);
    }
    while(this->steppers[X_AXIS]->moving || this->steppers[Y_AXIS]->moving || this->steppers[Z_AXIS]->moving) {
        // wait for it to complete
        THEKERNEL->call_event(ON_IDLE);
    }

    this->running = false;

    return true;
}

// calculate the X and Y positions for the three towers given the radius from the center
static std::tuple<float, float, float, float, float, float> getCoordinates(float radius)
{
    float px = 0.866F * radius; // ~sin(60)
    float py = 0.5F * radius; // cos(60)
    float t1x = -px, t1y = -py; // X Tower
    float t2x = px, t2y = -py; // Y Tower
    float t3x = 0.0F, t3y = radius; // Z Tower
    return std::make_tuple(t1x, t1y, t2x, t2y, t3x, t3y);
}

bool ZProbe::probe_delta_tower(int& steps, float x, float y)
{
    int s;
    // move to tower
    coordinated_move(x, y, NAN, this->fast_feedrate);
    if(!run_probe(s)) return false;

    // return to original Z
    return_probe(s);
    steps= s;

    return true;
}

/* Run a calibration routine for a delta
    1. Home
    2. probe for z bed
    3. probe initial tower positions
    4. set initial trims such that trims will be minimal negative values
    5. home, probe three towers again
    6. calculate trim offset and apply to all trims
    7. repeat 5, 6 4 times to converge on a solution
    8. home, Probe center
    9. calculate delta radius and apply it
    10. check level
*/

bool ZProbe::calibrate_delta(Gcode *gcode)
{
    // zero trim values
    set_trim(0, 0, 0, &(StreamOutput::NullStream));

    // home
    home();

    // find bed, run at fast rate
    int s;
    if(!run_probe(s, true)) return false;

    // how far to move down from home before probe
    int probestart = s - (this->probe_height*this->steps_per_mm[Z_AXIS]);
    gcode->stream->printf("Probe start ht is %f mm\n", probestart/this->steps_per_mm[Z_AXIS]);

    // get probe points
    float t1x, t1y, t2x, t2y, t3x, t3y;
    std::tie(t1x, t1y, t2x, t2y, t3x, t3y) = getCoordinates(this->probe_radius);

    // move to start position
    home();
    return_probe(-probestart);

    // get initial probes
    // probe the base of the X tower
    if(!probe_delta_tower(s, t1x, t1y)) return false;
    float t1z= s / this->steps_per_mm[Z_AXIS];
    gcode->stream->printf("T1-1 Z:%1.4f C:%d\n", t1z, s);

    // probe the base of the Y tower
    if(!probe_delta_tower(s, t2x, t2y)) return false;
    float t2z= s / this->steps_per_mm[Z_AXIS];
    gcode->stream->printf("T2-1 Z:%1.4f C:%d\n", t2z, s);

    // probe the base of the Z tower
    if(!probe_delta_tower(s, t3x, t3y)) return false;
    float t3z= s / this->steps_per_mm[Z_AXIS];
    gcode->stream->printf("T3-1 Z:%1.4f C:%d\n", t3z, s);

    float trimscale= 1.2522F; // empirically determined

    // set initial trims to worst case so we always have a negative trim
    float min= std::min({t1z, t2z, t3z});
    float trimx= (min-t1z)*trimscale, trimy= (min-t2z)*trimscale, trimz= (min-t3z)*trimscale;

    // set initial trim
    set_trim(trimx, trimy, trimz, gcode->stream);

    for (int i = 1; i <= 4; ++i) {
        // home and move probe to start position just above the bed
        home();
        return_probe(-probestart);

        // probe the base of the X tower
        if(!probe_delta_tower(s, t1x, t1y)) return false;
        t1z= s / this->steps_per_mm[Z_AXIS];
        gcode->stream->printf("T1-2-%d Z:%1.4f C:%d\n", i, t1z, s);

        // probe the base of the Y tower
        if(!probe_delta_tower(s, t2x, t2y)) return false;
        t2z= s / this->steps_per_mm[Z_AXIS];
        gcode->stream->printf("T2-2-%d Z:%1.4f C:%d\n", i, t2z, s);

        // probe the base of the Z tower
        if(!probe_delta_tower(s, t3x, t3y)) return false;
        t3z= s / this->steps_per_mm[Z_AXIS];
        gcode->stream->printf("T3-2-%d Z:%1.4f C:%d\n", i, t3z, s);

        auto mm= std::minmax({t1z, t2z, t3z});
        if((mm.second-mm.first) < 0.03F) break; // probably as good as it gets, TODO set 0.02 as config value

        // set new trim values based on min difference
        min= mm.first;
        trimx += (min-t1z)*trimscale;
        trimy += (min-t2z)*trimscale;
        trimz += (min-t3z)*trimscale;

        // set trim
        set_trim(trimx, trimy, trimz, gcode->stream);

        // flush the output
        THEKERNEL->call_event(ON_IDLE);
    }

    // move probe to start position just above the bed
    home();
    return_probe(-probestart);

    // probe the base of the three towers again to see if we are level
    int dx= 0, dy= 0, dz= 0;
    if(!probe_delta_tower(dx, t1x, t1y)) return false;
    gcode->stream->printf("T1-final Z:%1.4f C:%d\n", dx / this->steps_per_mm[Z_AXIS], dx);
    if(!probe_delta_tower(dy, t2x, t2y)) return false;
    gcode->stream->printf("T2-final Z:%1.4f C:%d\n", dy / this->steps_per_mm[Z_AXIS], dy);
    if(!probe_delta_tower(dz, t3x, t3y)) return false;
    gcode->stream->printf("T3-final Z:%1.4f C:%d\n", dz / this->steps_per_mm[Z_AXIS], dz);

    // compare the three and report
    auto mm= std::minmax({dx, dy, dz});
    gcode->stream->printf("max endstop delta= %f\n", (mm.second-mm.first)/this->steps_per_mm[Z_AXIS]);

    // probe center
    int dc;
    if(!probe_delta_tower(dc, 0, 0)) return false;

    // TODO adjust delta radius
    gcode->stream->printf("Center Z:%1.4f C:%d\n", dc / this->steps_per_mm[Z_AXIS], dc);

    mm= std::minmax({dx, dy, dz, dc});
    gcode->stream->printf("max delta= %f\n", (mm.second-mm.first)/this->steps_per_mm[Z_AXIS]);

    return true;
}

void ZProbe::on_gcode_received(void *argument)
{
    Gcode *gcode = static_cast<Gcode *>(argument);

    if( gcode->has_g) {
        // G code processing
        if( gcode->g == 30 ) { // simple Z probe
            gcode->mark_as_taken();
            // first wait for an empty queue i.e. no moves left
            THEKERNEL->conveyor->wait_for_empty_queue();

            int steps;
            if(run_probe(steps)) {
                gcode->stream->printf("Z:%1.4f C:%d\n", steps / this->steps_per_mm[Z_AXIS], steps);
                // move back to where it started, unless a Z is specified
                if(gcode->has_letter('Z')) {
                    // set Z to the specified value, and leave probe where it is
                    THEKERNEL->robot->reset_axis_position(gcode->get_value('Z'), Z_AXIS);
                } else {
                    return_probe(steps);
                }
            } else {
                gcode->stream->printf("ZProbe not triggered\n");
            }

        } else if( gcode->g == 32 ) { // auto calibration for delta, Z bed mapping for cartesian
            // first wait for an empty queue i.e. no moves left
            THEKERNEL->conveyor->wait_for_empty_queue();
            gcode->mark_as_taken();
            if(is_delta) {
                if(calibrate_delta(gcode)) {
                    gcode->stream->printf("Calibration complete, save settings with M500\n");
                } else {
                    gcode->stream->printf("Calibration failed to complete, probe not triggered\n");
                }
            } else {
                // TODO create Z height map for bed
                gcode->stream->printf("Not supported yet\n");
            }
        }

    } else if(gcode->has_m) {
        // M code processing here
        if(gcode->m == 119) {
            int c = this->pin.get();
            gcode->stream->printf(" Probe: %d", c);
            gcode->add_nl = true;
            gcode->mark_as_taken();

        }
    }
}

#define max(a,b) (((a) > (b)) ? (a) : (b))
// Called periodically to change the speed to match acceleration
uint32_t ZProbe::acceleration_tick(uint32_t dummy)
{
    if(!this->running) return(0); // nothing to do

    // foreach stepper that is moving
    for ( int c = X_AXIS; c <= Z_AXIS; c++ ) {
        if( !this->steppers[c]->moving ) continue;

        uint32_t current_rate = this->steppers[c]->steps_per_second;
        uint32_t target_rate = int(floor(this->current_feedrate));

        if( current_rate < target_rate ) {
            uint32_t rate_increase = int(floor((THEKERNEL->planner->acceleration / THEKERNEL->stepper->acceleration_ticks_per_second) * this->steps_per_mm[c]));
            current_rate = min( target_rate, current_rate + rate_increase );
        }
        if( current_rate > target_rate ) {
            current_rate = target_rate;
        }

        // steps per second
        this->steppers[c]->set_speed(max(current_rate, THEKERNEL->stepper->minimum_steps_per_second));
    }

    return 0;
}

// issue a coordinated move directly to robot, and return when done
// Only move the coordinates that are passed in as not nan
void ZProbe::coordinated_move(float x, float y, float z, float feedrate)
{
    char buf[32];
    char cmd[64] = "G0";
    if(!isnan(x)) {
        int n = snprintf(buf, sizeof(buf), " X%f", x);
        strncat(cmd, buf, n);
    }
    if(!isnan(y)) {
        int n = snprintf(buf, sizeof(buf), " Y%f", y);
        strncat(cmd, buf, n);
    }
    if(!isnan(z)) {
        int n = snprintf(buf, sizeof(buf), " Z%f", z);
        strncat(cmd, buf, n);
    }

    // use specified feedrate (mm/sec)
    int n = snprintf(buf, sizeof(buf), " F%f", feedrate * 60); // feed rate is converted to mm/min
    strncat(cmd, buf, n);

    Gcode gc(cmd, &(StreamOutput::NullStream));
    THEKERNEL->robot->on_gcode_received(&gc);
    THEKERNEL->conveyor->wait_for_empty_queue();
}

// issue home command
void ZProbe::home()
{
    Gcode gc("G28", &(StreamOutput::NullStream));
    THEKERNEL->call_event(ON_GCODE_RECEIVED, &gc);
}

void ZProbe::set_trim(float x, float y, float z, StreamOutput *stream)
{
    char buf[40];
    int n = snprintf(buf, sizeof(buf), "M666 X%1.8f Y%1.8f Z%1.8f", x, y, z);
    Gcode gc(string(buf, n), stream);
    THEKERNEL->call_event(ON_GCODE_RECEIVED, &gc);
}
