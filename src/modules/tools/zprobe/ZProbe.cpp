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

bool ZProbe::wait_for_probe(int steps[])
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
bool ZProbe::run_probe(int *steps, bool fast)
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

    bool r = wait_for_probe(steps);
    this->running = false;
    return r;
}

bool ZProbe::return_probe(int *steps)
{
    // move probe back to where it was
    this->current_feedrate = this->fast_feedrate * this->steps_per_mm[Z_AXIS]; // feedrate in steps/sec

    this->running = true;
    this->steppers[Z_AXIS]->set_speed(0); // will be increased by acceleration tick
    this->steppers[Z_AXIS]->move(false, steps[Z_AXIS]);
    if(this->is_delta) {
        this->steppers[X_AXIS]->set_speed(0);
        this->steppers[X_AXIS]->move(false, steps[X_AXIS]);
        this->steppers[Y_AXIS]->set_speed(0);
        this->steppers[Y_AXIS]->move(false, steps[Y_AXIS]);
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

bool ZProbe::probe_delta_towers(int steps[3][3], StreamOutput *stream)
{
    // need to calculate test points from probe radius
    float t1x, t1y, t2x, t2y, t3x, t3y;
    std::tie(t1x, t1y, t2x, t2y, t3x, t3y) = getCoordinates(this->probe_radius);

    // X tower
    coordinated_move(t1x, t1y, NAN, this->fast_feedrate);
    if(!run_probe(steps[0])) return false;
    stream->printf("T1 Z:%1.4f C:%d\n", steps[0][Z_AXIS] / this->steps_per_mm[Z_AXIS], steps[0][Z_AXIS]);
    // return to original Z
    return_probe(steps[0]);

    // Y tower
    coordinated_move(t2x, t2y, NAN, this->fast_feedrate);
    if(!run_probe(steps[1])) return false;
    stream->printf("T2 Z:%1.4f C:%d\n", steps[1][Z_AXIS] / this->steps_per_mm[Z_AXIS], steps[1][Z_AXIS]);
    return_probe(steps[1]);

    // Z tower
    coordinated_move(t3x, t3y, NAN, this->fast_feedrate);
    if(!run_probe(steps[2])) return false;
    stream->printf("T3 Z:%1.4f C:%d\n", steps[2][Z_AXIS] / this->steps_per_mm[Z_AXIS], steps[2][Z_AXIS]);
    return_probe(steps[2]);


    return true;
}

/* Run a calibration routine for a delta
    1. Home
    2. probe for z bed
    3. move to base of each tower at 5mm above bed
    4. probe down to bed
    5. probe next tower
    6. calculate trim offsets and apply them
    7. Home
    8. Probe center
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
    int steps[3][3];
    if(!run_probe(steps[0], true)) return false;

    float zl = steps[0][Z_AXIS] / this->steps_per_mm[Z_AXIS];
    gcode->stream->printf("Probe ht is %f mm\n", zl);

    // nominal Z == 0
    THEKERNEL->robot->reset_axis_position(0.0F, Z_AXIS);

    float zht = this->probe_height; // height above bed
    coordinated_move(NAN, NAN, zht, this->fast_feedrate);

    // probe the base of the three towers
    if(!probe_delta_towers(steps, gcode->stream)) return false;

    // gcode->stream->printf("%d, %d, %d\n", steps[0][0], steps[0][1], steps[0][2]);
    // gcode->stream->printf("%d, %d, %d\n", steps[1][0], steps[1][1], steps[1][2]);
    // gcode->stream->printf("%d, %d, %d\n", steps[2][0], steps[2][1], steps[2][2]);

    // set new trim values
    // this is funky, basically the head moves in a plane parallel to a plane defined by the three endstops
    // when we probe the bed we find another plane that should be parallel to that plane.
    // to make the head move in a plane parallel to the bed (ie TRAM) we change the endstops to adjust that plane to be parallel
    // Yea!

    float d1 = zht - (steps[0][Z_AXIS] / this->steps_per_mm[X_AXIS]); // All three steps should be the same so we just use the Z one
    float d2 = zht - (steps[1][Z_AXIS] / this->steps_per_mm[Y_AXIS]);
    float d3 = zht - (steps[2][Z_AXIS] / this->steps_per_mm[Z_AXIS]);

    float t1x, t1y, t2x, t2y, t3x, t3y;
    std::tie(t1x, t1y, t2x, t2y, t3x, t3y) = getCoordinates(this->probe_radius);

    gcode->stream->printf("DEBUG: T1: X:%f Y:%f\n", t1x, t1y);
    gcode->stream->printf("DEBUG: T2: X:%f Y:%f\n", t2x, t2y);
    gcode->stream->printf("DEBUG: T3: X:%f Y:%f\n", t3x, t3y);


    // define the plane of the bed (relative to the endstop plane)
    Vector3 v1(t1x, t1y, d1);
    Vector3 v2(t2x, t2y, d2);
    Vector3 v3(t3x, t3y, d3);
    Plane plane(v1, v2, v3);

    gcode->stream->printf("DEBUG: normal= %f, %f, %f\n", plane.getNormal()[0], plane.getNormal()[1], plane.getNormal()[2]);

    // now find endstops positions
    float e1x, e1y, e2x, e2y, e3x, e3y;
    std::tie(e1x, e1y, e2x, e2y, e3x, e3y) = getCoordinates(this->endstop_radius);

    gcode->stream->printf("DEBUG: E1: X:%f Y:%f\n", e1x, e1y);
    gcode->stream->printf("DEBUG: E2: X:%f Y:%f\n", e2x, e2y);
    gcode->stream->printf("DEBUG: E3: X:%f Y:%f\n", e3x, e3y);

    float t1= plane.getz(e1x, e1y);
    float t2= plane.getz(e2x, e2y);
    float t3= plane.getz(e3x, e3y);
    gcode->stream->printf("DEBUG: d1,2,3= %f, %f, %f\n", d1, d2, d3);
    gcode->stream->printf("DEBUG: t1,2,3= %f, %f, %f\n", t1, t2, t3);

    // find the smallest one and make that the normal, offset the other two from that
    // if(d1 <= d2 && d1 <= d3) {
    //     d2= d1 - d2;
    //     d3= d1 - d3;
    //     d1= 0.0F;
    // }else if(d2 <= d1 && d2 <= d3) {
    //     d1= d2 - d1;
    //     d3= d2 - d3;
    //     d2= 0.0F;
    // }else {
    //     d1= d3 - d1;
    //     d2= d3 - d2;
    //     d3= 0.0F;
    // }
    set_trim(t1, t2, t3, gcode->stream);

    home();

    // move probe back to just above the bed
    if(!run_probe(steps[0], true)) return false;
    THEKERNEL->robot->reset_axis_position(0.0F, Z_AXIS);
    coordinated_move(NAN, NAN, zht, this->fast_feedrate);

    // probe the base of the three towers again to see if we are level
    if(!probe_delta_towers(steps, gcode->stream)) return false;
    // TODO maybe test the three and warn if too far out

    // probe center
    coordinated_move(0.0F, 0.0F, NAN, this->fast_feedrate);
    if(!run_probe(steps[0])) return false;

    // TODO adjust delta radius
    gcode->stream->printf("Center Z:%1.4f C:%d\n", steps[0][Z_AXIS] / this->steps_per_mm[Z_AXIS], steps[0][Z_AXIS]);

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

            int steps[3];
            if(run_probe(steps)) {
                gcode->stream->printf("Z:%1.4f C:%d\n", steps[Z_AXIS] / this->steps_per_mm[Z_AXIS], steps[Z_AXIS]);
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
        else if (gcode->m == 999) {
            float t1x, t1y, t2x, t2y, t3x, t3y;
            std::tie(t1x, t1y, t2x, t2y, t3x, t3y) = getCoordinates(this->probe_radius);

            gcode->stream->printf("DEBUG: T1: X:%f Y:%f\n", t1x, t1y);
            gcode->stream->printf("DEBUG: T2: X:%f Y:%f\n", t2x, t2y);
            gcode->stream->printf("DEBUG: T3: X:%f Y:%f\n", t3x, t3y);


            // define the plane of the bed (relative to the endstop plane)
            Vector3 v1(t1x, t1y, 0);
            Vector3 v2(t2x, t2y, 0);
            Vector3 v3(t3x, t3y, 0);
            Plane plane(v1, v2, v3);

            gcode->stream->printf("DEBUG: normal= %f, %f, %f\n", plane.getNormal()[0], plane.getNormal()[1], plane.getNormal()[2]);

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
// Only move the coordintaes that are passed in as not nan
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
