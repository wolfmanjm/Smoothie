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
#include "SerialMessage.h"
#include "PublicDataRequest.h"
#include "EndstopsPublicAccess.h"
#include "PublicData.h"

#include <tuple>
#include <algorithm>


#define zprobe_checksum          CHECKSUM("zprobe")
#define enable_checksum          CHECKSUM("enable")
#define probe_pin_checksum       CHECKSUM("probe_pin")
#define debounce_count_checksum  CHECKSUM("debounce_count")
#define slow_feedrate_checksum   CHECKSUM("slow_feedrate")
#define fast_feedrate_checksum   CHECKSUM("fast_feedrate")
#define probe_radius_checksum    CHECKSUM("probe_radius")
#define probe_height_checksum    CHECKSUM("probe_height")

// from endstop section
#define delta_homing_checksum    CHECKSUM("delta_homing")

#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2

#define STEPS_PER_MM(a) (this->steppers[a]->steps_per_mm)
#define Z_STEPS_PER_MM STEPS_PER_MM(Z_AXIS)

#define abs std::abs

//#define DEBUG_PRINTF(...)
#define DEBUG_PRINTF gcode->stream->printf

void ZProbe::on_module_loaded()
{
    // if the module is disabled -> do nothing
    if(!THEKERNEL->config->value( zprobe_checksum, enable_checksum )->by_default(false)->as_bool()) {
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

    THEKERNEL->slow_ticker->attach( THEKERNEL->stepper->get_acceleration_ticks_per_second() , this, &ZProbe::acceleration_tick );
}

void ZProbe::on_config_reload(void *argument)
{
    this->pin.from_string( THEKERNEL->config->value(zprobe_checksum, probe_pin_checksum)->by_default("nc" )->as_string())->as_input();
    this->debounce_count = THEKERNEL->config->value(zprobe_checksum, debounce_count_checksum)->by_default(0  )->as_number();

    // see what type of arm solution we need to use
    this->is_delta =  THEKERNEL->config->value(delta_homing_checksum)->by_default(false)->as_bool();
    if(this->is_delta) {
        // default is probably wrong
        this->probe_radius =  THEKERNEL->config->value(zprobe_checksum, probe_radius_checksum)->by_default(100.0F)->as_number();
    }

    this->probe_height =  THEKERNEL->config->value(zprobe_checksum, probe_height_checksum)->by_default(5.0F)->as_number();

    this->steppers[0] = THEKERNEL->robot->alpha_stepper_motor;
    this->steppers[1] = THEKERNEL->robot->beta_stepper_motor;
    this->steppers[2] = THEKERNEL->robot->gamma_stepper_motor;

    this->slow_feedrate = THEKERNEL->config->value(zprobe_checksum, slow_feedrate_checksum)->by_default(5)->as_number(); // feedrate in mm/sec
    this->fast_feedrate = THEKERNEL->config->value(zprobe_checksum, fast_feedrate_checksum)->by_default(100)->as_number(); // feedrate in mm/sec
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

// single probe and report steps moved down
bool ZProbe::run_probe(int &steps, bool fast)
{
    // Enable the motors
    THEKERNEL->stepper->turn_enable_pins_on();
    this->current_feedrate = (fast ? this->fast_feedrate : this->slow_feedrate) * Z_STEPS_PER_MM; // steps/sec

    // move Z down
    this->steppers[Z_AXIS]->set_speed(0); // will be increased by acceleration tick
    this->steppers[Z_AXIS]->move(true, 1000 * Z_STEPS_PER_MM); // always probes down, no more than 1000mm TODO should be 2*maxz
    if(this->is_delta) {
        // for delta need to move all three actuators
        this->steppers[X_AXIS]->set_speed(0);
        this->steppers[X_AXIS]->move(true, 1000 * STEPS_PER_MM(X_AXIS));
        this->steppers[Y_AXIS]->set_speed(0);
        this->steppers[Y_AXIS]->move(true, 1000 * STEPS_PER_MM(Y_AXIS));
    }

    this->running = true;

    int s[3];
    bool r = wait_for_probe(s);
    steps = s[Z_AXIS]; // only need z
    this->running = false;
    return r;
}

bool ZProbe::return_probe(int steps)
{
    // move probe back to where it was
    this->current_feedrate = this->fast_feedrate * Z_STEPS_PER_MM; // feedrate in steps/sec
    bool dir = steps < 0;
    steps = abs(steps);

    this->steppers[Z_AXIS]->set_speed(0); // will be increased by acceleration tick
    this->steppers[Z_AXIS]->move(dir, steps);
    if(this->is_delta) {
        this->steppers[X_AXIS]->set_speed(0);
        this->steppers[X_AXIS]->move(dir, steps);
        this->steppers[Y_AXIS]->set_speed(0);
        this->steppers[Y_AXIS]->move(dir, steps);
    }

    this->running = true;
    while(this->steppers[X_AXIS]->moving || this->steppers[Y_AXIS]->moving || this->steppers[Z_AXIS]->moving) {
        // wait for it to complete
        THEKERNEL->call_event(ON_IDLE);
    }

    this->running = false;

    return !this->pin.get(); // make probe is not triggered
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

bool ZProbe::probe_delta_tower(int &steps, float x, float y)
{
    int s;
    // move to tower
    coordinated_move(x, y, NAN, this->fast_feedrate);
    if(!run_probe(s)) return false;

    // return to original Z
    bool b = return_probe(s);
    steps = s;

    return b;
}

/* Run a calibration routine for a delta
    1. Home
    2. probe for z bed
    3. probe initial tower positions
    4. set initial trims such that trims will be minimal negative values
    5. home, probe three towers again
    6. calculate trim offset and apply to all trims
    7. repeat 5, 6 until it converges on a solution
*/

bool ZProbe::calibrate_delta_endstops(Gcode *gcode, float bedht, bool override_keep)
{
    float target = 0.03F;
    if(gcode->has_letter('I')) target = gcode->get_value('I'); // override default target
    if(gcode->has_letter('J')) this->probe_radius = gcode->get_value('J'); // override default probe radius

    bool keep = false;
    if(override_keep || gcode->has_letter('K')) keep = true; // keep current settings

    gcode->stream->printf("Calibrating Endstops: target %fmm, radius %fmm\n", target, this->probe_radius);

    // get probe points
    float t1x, t1y, t2x, t2y, t3x, t3y;
    std::tie(t1x, t1y, t2x, t2y, t3x, t3y) = getCoordinates(this->probe_radius);

    float trimx = 0.0F, trimy = 0.0F, trimz = 0.0F;
    if(!keep) {
        // zero trim values
        if(!set_trim(0, 0, 0, gcode->stream)) return false;

    } else {
        // get current trim, and continue from that
        if (get_trim(trimx, trimy, trimz)) {
            gcode->stream->printf("Current Trim X: %f, Y: %f, Z: %f\r\n", trimx, trimy, trimz);

        } else {
            gcode->stream->printf("Could not get current trim, are endstops enabled?\n");
            return false;
        }
    }

    int s;
    if(isnan(bedht)) {
        // home
        home();

        // find bed, run at fast rate
        if(!run_probe(s, true)) return false;

        bedht = s / Z_STEPS_PER_MM - this->probe_height; // distance to move from home to 5mm above bed
        gcode->stream->printf("Bed ht is %f mm\n", bedht);

        // move to start position
        home();
        coordinated_move(NAN, NAN, -bedht, this->fast_feedrate, true); // do a relative move from home to the point above the bed
    }

    // get initial probes
    // probe the base of the X tower
    if(!probe_delta_tower(s, t1x, t1y)) return false;
    float t1z = s / Z_STEPS_PER_MM;
    gcode->stream->printf("T1-0 Z:%1.4f C:%d\n", t1z, s);

    // probe the base of the Y tower
    if(!probe_delta_tower(s, t2x, t2y)) return false;
    float t2z = s / Z_STEPS_PER_MM;
    gcode->stream->printf("T2-0 Z:%1.4f C:%d\n", t2z, s);

    // probe the base of the Z tower
    if(!probe_delta_tower(s, t3x, t3y)) return false;
    float t3z = s / Z_STEPS_PER_MM;
    gcode->stream->printf("T3-0 Z:%1.4f C:%d\n", t3z, s);

    float trimscale = 1.2522F; // empirically determined

    auto mm = std::minmax({t1z, t2z, t3z});
    if((mm.second - mm.first) <= target) {
        gcode->stream->printf("trim already set within required parameters: delta %f\n", mm.second - mm.first);
        return true;
    }

    // set trims to worst case so we always have a negative trim
    trimx += (mm.first - t1z) * trimscale;
    trimy += (mm.first - t2z) * trimscale;
    trimz += (mm.first - t3z) * trimscale;

    for (int i = 1; i <= 10; ++i) {
        // set trim
        if(!set_trim(trimx, trimy, trimz, gcode->stream)) return false;

        // home and move probe to start position just above the bed
        home();
        coordinated_move(NAN, NAN, -bedht, this->fast_feedrate, true); // do a relative move from home to the point above the bed

        // probe the base of the X tower
        if(!probe_delta_tower(s, t1x, t1y)) return false;
        t1z = s / Z_STEPS_PER_MM;
        gcode->stream->printf("T1-%d Z:%1.4f C:%d\n", i, t1z, s);

        // probe the base of the Y tower
        if(!probe_delta_tower(s, t2x, t2y)) return false;
        t2z = s / Z_STEPS_PER_MM;
        gcode->stream->printf("T2-%d Z:%1.4f C:%d\n", i, t2z, s);

        // probe the base of the Z tower
        if(!probe_delta_tower(s, t3x, t3y)) return false;
        t3z = s / Z_STEPS_PER_MM;
        gcode->stream->printf("T3-%d Z:%1.4f C:%d\n", i, t3z, s);

        mm = std::minmax({t1z, t2z, t3z});
        if((mm.second - mm.first) <= target) {
            gcode->stream->printf("trim set to within required parameters: delta %f\n", mm.second - mm.first);
            break;
        }

        // set new trim values based on min difference
        trimx += (mm.first - t1z) * trimscale;
        trimy += (mm.first - t2z) * trimscale;
        trimz += (mm.first - t3z) * trimscale;

        // flush the output
        THEKERNEL->call_event(ON_IDLE);
    }

    if((mm.second - mm.first) > target) {
        gcode->stream->printf("WARNING: trim did not resolve to within required parameters: delta %f\n", mm.second - mm.first);
    }

    return true;
}

/*
    probe edges to get outer positions, then probe center
    modify the delta radius until center and X converge
*/
bool ZProbe::calibrate_delta_radius(Gcode *gcode)
{
    float target = 0.03F;
    if(gcode->has_letter('I')) target = gcode->get_value('I'); // override default target
    if(gcode->has_letter('J')) this->probe_radius = gcode->get_value('J'); // override default probe radius

    gcode->stream->printf("Calibrating delta radius: target %f, radius %f\n", target, this->probe_radius);

    BaseSolution::arm_options_t options;

    // get probe points
    float t1x, t1y, t2x, t2y, t3x, t3y;
    std::tie(t1x, t1y, t2x, t2y, t3x, t3y) = getCoordinates(this->probe_radius);

    home();
    // find bed, then move to a point 5mm above it
    int s;
    if(!run_probe(s, true)) return false;
    float bedht = s / Z_STEPS_PER_MM - this->probe_height; // distance to move from home to 5mm above bed
    gcode->stream->printf("Bed ht is %f mm\n", bedht);

    home();
    coordinated_move(NAN, NAN, -bedht, this->fast_feedrate, true); // do a relative move from home to the point above the bed

    // probe center to get reference point at this Z height
    int dc;
    if(!probe_delta_tower(dc, 0, 0)) return false;
    gcode->stream->printf("CT Z:%1.3f C:%d\n", dc / Z_STEPS_PER_MM, dc);
    float cmm = dc / Z_STEPS_PER_MM;

    // get current delta radius
    float delta_radius = 0.0F;
    if(THEKERNEL->robot->arm_solution->get_optional(options)) {
        delta_radius = options['R'];
    }
    if(delta_radius == 0.0F) {
        gcode->stream->printf("This appears to not be a delta arm solution\n");
        return false;
    }
    options.clear();

    // remember last good probes
    float t1z, t2z, t3z;
    int sx, sy, sz;
    float d;

    // first do the regular in front of tower points
    float drinc = 2.5F; // approx
    for (int i = 1; i <= 10; ++i) {
        // probe t1, t2, t3 and get average, but use coordinated moves, probing center won't change
        if(!probe_delta_tower(sx, t1x, t1y)) return false;
        t1z = sx / Z_STEPS_PER_MM;
        gcode->stream->printf("T1-%d Z:%1.3f C:%d\n", i, t1z, sx);
        if(!probe_delta_tower(sy, t2x, t2y)) return false;
        t2z = sy / Z_STEPS_PER_MM;
        gcode->stream->printf("T2-%d Z:%1.3f C:%d\n", i, t2z, sy);
        if(!probe_delta_tower(sz, t3x, t3y)) return false;
        t3z = sz / Z_STEPS_PER_MM;
        gcode->stream->printf("T3-%d Z:%1.3f C:%d\n", i, t3z, sz);

        // now look at the difference and reduce it by adjusting delta radius
        float m = (t1z + t2z + t3z) / 3.0F;
        d = cmm - m;
        gcode->stream->printf("C-%d Z-ave:%1.4f delta: %1.3f\n", i, m, d);

        if(abs(d) <= target) break; // resolution of success

        // increase delta radius to adjust for low center
        // decrease delta radius to adjust for high center
        delta_radius += (d * drinc);

        // set the new delta radius
        options['R'] = delta_radius;
        THEKERNEL->robot->arm_solution->set_optional(options);
        gcode->stream->printf("Setting delta radius to: %1.4f\n", delta_radius);

        home();
        coordinated_move(NAN, NAN, -bedht, this->fast_feedrate, true); // needs to be a relative coordinated move

        // flush the output
        THEKERNEL->call_event(ON_IDLE);
    }

    if(abs(d) > target) {
        gcode->stream->printf("WARNING: delta radius did not resolve to within required parameters: delta %f\n", d);
        return false;
    }

    gcode->stream->printf("delta radius set to within required parameters: delta %f\n", d);

    return true;
}


/*
    This code is derived from Rich Cattells autocalibration work for Marlin - https://github.com/RichCattell/Marlin.git

    This tries to calibrate everything all in one go, can take upto 100 iterations but should adjust for incorrect delta radius and arm length
    and tower positions
*/
bool ZProbe::calibrate_delta_RichCMethod(Gcode *gcode)
{
    float target = 0.03F;
    if(gcode->has_letter('I')) target = gcode->get_value('I'); // override default target
    if(gcode->has_letter('J')) this->probe_radius = gcode->get_value('J'); // override default probe radius

    gcode->stream->printf("Rich Cattells autocalibration: target %f, radius %f\n", target, this->probe_radius);

    // get current radius offsets
    bool keep = false;
    if(gcode->has_letter('K')) keep = true; // keep current settings
    bool keep_arm_length = false;
    if(gcode->has_letter('L')) keep_arm_length = true; // adjust arm length

    // delta radius offsets
    float drx = 0.0F, dry = 0.0F, drz = 0.0F;
    float da1 = 0.0F, da2 = 0.0F, da3 = 0.0F;
    float delta_radius = 0.0F;
    float arm_length = 0.0F;

    BaseSolution::arm_options_t options;
    if(THEKERNEL->robot->arm_solution->get_optional(options)) {
        if(keep) {
            drx = options['A'];
            dry = options['B'];
            drz = options['C'];
            da1 = options['D'];
            da2 = options['E'];
            da3 = options['F'];
            gcode->stream->printf("Using delta offsets: A: %1.4f B: %1.4f C: %1.4f D: %1.4f E: %1.4f F: %1.4f\n", drx, dry, drz, da1, da2, da3);
        }
        delta_radius = options['R'];
        arm_length = options['L'];
    }

    if(!keep) {
        options.clear();
        options['A'] = drx;
        options['B'] = dry;
        options['C'] = drz;
        options['D'] = da1;
        options['E'] = da2;
        options['F'] = da3;
        THEKERNEL->robot->arm_solution->set_optional(options);
        gcode->stream->printf("Setting delta offsets to: A: %1.4f B: %1.4f C: %1.4f D: %1.4f E: %1.4f F: %1.4f\n", drx, dry, drz, da1, da2, da3);
    }
    options.clear();

    // get probe points
    float t1x, t1y, t2x, t2y, t3x, t3y;
    std::tie(t1x, t1y, t2x, t2y, t3x, t3y) = getCoordinates(this->probe_radius);

    // find bed
    home();
    float pos[3];
    THEKERNEL->robot->get_axis_position(pos);
    int s;
    if(!run_probe(s, true)) return false; // returns the number of steps moved down
    float bedht = s / Z_STEPS_PER_MM - this->probe_height; // distance in mm to move from home to 5mm above bed
    float absbedht = pos[Z_AXIS] - bedht; // absolute Z to probe from
    gcode->stream->printf("Bed ht from home is %f mm, absolute Z: %f\n", bedht, absbedht);
    home();
    coordinated_move(0.0F, 0.0F, absbedht, this->fast_feedrate); // needs to be a coordinated move

    float t1z, t2z, t3z;
    float o1z, o2z, o3z;
    int sx, sy, sz, sc;
    float drinc = 0, dalinc = 0;
    float d1inc = 0, d2inc = 0, d3inc = 0;

    // Seems that first homes and adjusts endstops, then adjusts delta radu=ius and offsets without homing until done then homes and tests again
    // probe all points
    for (int i = 1; i <= 100; ++i) {
        coordinated_move(0.0F, 0.0F, absbedht, this->fast_feedrate/2); // needs to be a coordinated move

        // probe the towers, gets mm moved down, NOTE this is reversed from RichC where the probe is the actual z height
        if(!probe_delta_tower(sx, t1x, t1y)) return false;
        t1z = sx / Z_STEPS_PER_MM;
        gcode->stream->printf("T1-%d Z:%1.3f C:%d\n", i, t1z, sx);
        if(!probe_delta_tower(sy, t2x, t2y)) return false;
        t2z = sy / Z_STEPS_PER_MM;
        gcode->stream->printf("T2-%d Z:%1.3f C:%d\n", i, t2z, sy);
        if(!probe_delta_tower(sz, t3x, t3y)) return false;
        t3z = sz / Z_STEPS_PER_MM;
        gcode->stream->printf("T3-%d Z:%1.3f C:%d\n", i, t3z, sz);
        float tave = (t1z + t2z + t3z) / 3.0F;

        if(i == 1) {
            // see if endstops need adjusting, first time through only
            auto mm = std::minmax({t1z, t2z, t3z});
            if((mm.second - mm.first) > target) {
                if(!calibrate_delta_endstops(gcode, bedht, true)) return false;
                home();
                continue;
            } else {
                gcode->stream->printf("Endstops are ok\n");
            }
        }

        // probe center
        if(!probe_delta_tower(sc, 0, 0)) return false;
        float cz = sc / Z_STEPS_PER_MM;
        float dc = cz - tave;
        gcode->stream->printf("CT-%d Z:%1.3f C:%d delta: %1.3f\n", i, cz, sc, dc);

        // probe the opposite points
        if(!probe_delta_tower(sx, -t1x, -t1y)) return false;
        o1z = sx / Z_STEPS_PER_MM;
        gcode->stream->printf("OT1-%d Z:%1.4f C:%d Delta: %1.4f\n", i, o1z, sx, t1z - o1z);
        if(!probe_delta_tower(sy, -t2x, -t2y)) return false;
        o2z = sy / Z_STEPS_PER_MM;
        gcode->stream->printf("OT2-%d Z:%1.4f C:%d Delta: %1.4f\n", i, o2z, sy, t2z - o2z);
        if(!probe_delta_tower(sz, -t3x, -t3y)) return false;
        o3z = sz / Z_STEPS_PER_MM;
        gcode->stream->printf("OT3-%d Z:%1.4f C:%d Delta: %1.4f\n", i, o3z, sz, t3z - o3z);

        // see if we are calibrated yet, if all deltas are within target range
        auto mm = std::minmax({t1z, t2z, t3z, o1z, o2z, o3z, cz});
        if((mm.second - mm.first) <= target*2.0F) {
            gcode->stream->printf("All Calibration points are within target range: %f\n", (mm.second - mm.first)/2.0F);
            return true;
        }

        bool set_al = false;
        float oave = (o1z + o2z + o3z) / 3.0F;
        float dave = oave - tave;

        // set inital direction and magnitude for delta radius & diagonal rod adjustment
        // NOTE a bigger value it moved further therefore is a smaller absolute Z
        if(drinc == 0) drinc = (tave > cz) ? -1 : 1;
        if(dalinc == 0) dalinc = (tave > oave) ? -1 : 1;

        bool set_dr = false;
        if(abs(dc) > target) {
            // increase delta radius to adjust for low center
            // decrease delta radius to adjust for high center
            DEBUG_PRINTF("DEBUG: delta radius adjust: %f\n", drinc);
            delta_radius += drinc;
            set_dr = true;
        }

        if(!keep_arm_length && abs(dave) > (target * 2)) {
            DEBUG_PRINTF("DEBUG: arm length adjust: %f\n", dalinc);
            arm_length += dalinc;
            set_al = true;
        }

        // differential between the diagonals
        float d1 = t1z - o1z;
        float d2 = t2z - o2z;
        float d3 = t3z - o3z;

        // compare with other towers differentials
        bool d12 = (abs(d1 - d2) <= (target * 2));
        bool d23 = (abs(d2 - d3) <= (target * 2));
        bool d31 = (abs(d3 - d1) <= (target * 2));

        // tower position adjustment, Rich always sets these, but I think they only need to be adjusted if they are out of range
        float do1 = (o2z - o3z);
        float do2 = (o3z - o1z);
        float do3 = (o1z - o2z);
        da1= (abs(do1) > (target * 2)) ? do1 : 0.0F;
        da2= (abs(do2) > (target * 2)) ? do2 : 0.0F;
        da3= (abs(do3) > (target * 2)) ? do3 : 0.0F;

        DEBUG_PRINTF("DEBUG: d1 %f, d2 %f, d3 %f, d12 %d, d23 %d, d32 %d, do1 %f, do2 %f, do3 %f, da1 %f, da2 %f, da3 %f, tave %f, oave %f, dave %f\n",
                     d1, d2, d3, d12, d23, d31, do1, do2, do3, da1, da2, da3, tave, oave, dave);

        // determine which towers needs adjusting
        if(d12 && d23 && d31) {
            gcode->stream->printf("No tower radius errors detected: %1.4f %1.4f %1.4f\n", d1, d2, d3);
        }

        bool set_dro = false;
        if(d23 && !d12 && !d31) { // X Tower radius error
            if(d1inc == 0) d1inc = (t1z < o1z) ? -0.5F : 0.5F;
            DEBUG_PRINTF("DEBUG: X tower radius error: %f\n", d1inc);
            drx += d1inc;
            set_dro = true;
        }
        if(d31 && !d12 && !d23) { // Y Tower radius error
            if(d2inc == 0) d2inc = (t2z < o2z) ? -0.5F : 0.5F;
            DEBUG_PRINTF("DEBUG: Y tower radius error: %f\n", d2inc);
            dry += d2inc;
            set_dro = true;
        }
        if(d12 && !d23 && !d31) { // Z Tower radius error
            if(d3inc == 0) d3inc = (t3z < o3z) ? -0.5F : 0.5F;
            DEBUG_PRINTF("DEBUG: Z tower radius error: %f\n", d3inc);
            drz += d3inc;
            set_dro = true;
        }

        // set the new delta radius offsets and arm offset if needed
        options.clear();

        if(da1 != 0.0F) options['D'] = da1;
        if(da2 != 0.0F) options['E'] = da2;
        if(da3 != 0.0F) options['F'] = da3;
        if(!options.empty())
            gcode->stream->printf("Setting tower position offsets to: D: %1.4f E: %1.4f F: %1.4f\n", da1, da2, da3);

        if(set_dro) {
            options['A'] = drx;
            options['B'] = dry;
            options['C'] = drz;
            gcode->stream->printf("Setting tower radius offsets to: A: %1.4f B: %1.4f C: %1.4f\n", drx, dry, drz);
        }else{
            gcode->stream->printf("Not adjusting tower radius\n");
        }

        if(set_al) {
            options['L'] = arm_length;
            gcode->stream->printf("Setting arm length to: %1.4f\n", arm_length);
        }
        if(set_dr) {
            options['R'] = delta_radius;
            gcode->stream->printf("Setting delta radius to: %1.4f\n", delta_radius);
        }

        if(!options.empty()) {
            THEKERNEL->robot->arm_solution->set_optional(options);
        } else {
            gcode->stream->printf("Nothing to do\n");
            return true;
        }

        // adjust the increments for overshoot the way Rich does it
        if( ((drinc > 0) && (cz < tave)) || ((drinc < 0) && (cz > tave)) ) drinc = -(drinc / 2);
        if( ((dalinc > 0) && (oave < tave)) || ((dalinc < 0) && (oave > tave)) ) dalinc = -(dalinc / 2);

        // Tower radius overshot targets?
        if( ((d1inc > 0) && (t1z < o1z)) || ((d1inc < 0) && (t1z > o1z)) ) d1inc = -(d1inc / 2);
        if( ((d2inc > 0) && (t2z < o2z)) || ((d2inc < 0) && (t2z > o2z)) ) d2inc = -(d2inc / 2);
        if( ((d3inc > 0) && (t3z < o3z)) || ((d3inc < 0) && (t3z > o3z)) ) d3inc = -(d3inc / 2);

        // flush the output
        THEKERNEL->call_event(ON_IDLE);
    }

    return false;
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
                gcode->stream->printf("Z:%1.4f C:%d\n", steps / Z_STEPS_PER_MM, steps);
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

        } else if( gcode->g == 31 ) { // use rich Catells auto calibration for delta
            // first wait for an empty queue i.e. no moves left
            THEKERNEL->conveyor->wait_for_empty_queue();
            gcode->mark_as_taken();
            if(!calibrate_delta_RichCMethod(gcode)) {
                gcode->stream->printf("Calibration failed to complete, probe not triggered or did not resolve\n");
                return;
            }
            gcode->stream->printf("Calibration complete, save settings with M500\n");

        } else if( gcode->g == 32 ) { // auto calibration for delta, Z bed mapping for cartesian
            // first wait for an empty queue i.e. no moves left
            THEKERNEL->conveyor->wait_for_empty_queue();
            gcode->mark_as_taken();
            if(is_delta) {
                if(!gcode->has_letter('R')) {
                    if(!calibrate_delta_endstops(gcode)) {
                        gcode->stream->printf("Calibration failed to complete, probe not triggered\n");
                        return;
                    }
                }
                if(!gcode->has_letter('E')) {
                    if(!calibrate_delta_radius(gcode)) {
                        gcode->stream->printf("Calibration failed to complete, did not resolve or probe failed\n");
                        return;
                    }
                }
                gcode->stream->printf("Calibration complete, save settings with M500\n");

            } else {
                // TODO create Z height map for bed
                gcode->stream->printf("Cartesian bed probe not supported yet\n");
            }
        }

    } else if(gcode->has_m) {
        // M code processing here
        if(gcode->m == 119) {
            int c = this->pin.get();
            gcode->stream->printf(" Probe: %d", c);
            gcode->add_nl = true;
            gcode->mark_as_taken();

        } else if (gcode->m == 557) { // P0 Xxxx Yyyy sets probe points for G32
            // TODO will override the automatically calculated probe points for a delta, required for a cartesian

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
            uint32_t rate_increase = int(floor((THEKERNEL->planner->get_acceleration() / THEKERNEL->stepper->get_acceleration_ticks_per_second()) * STEPS_PER_MM(c)));
            current_rate = min( target_rate, current_rate + rate_increase );
        }
        if( current_rate > target_rate ) {
            current_rate = target_rate;
        }

        // steps per second
        this->steppers[c]->set_speed(max(current_rate, THEKERNEL->stepper->get_minimum_steps_per_second()));
    }

    return 0;
}

// issue a coordinated move directly to robot, and return when done
// Only move the coordinates that are passed in as not nan
void ZProbe::coordinated_move(float x, float y, float z, float feedrate, bool relative)
{
    char buf[32];
    char cmd[64];

    if(relative) strcpy(cmd, "G91 G0 ");
    else strcpy(cmd, "G0 ");

    if(!isnan(x)) {
        int n = snprintf(buf, sizeof(buf), " X%1.3f", x);
        strncat(cmd, buf, n);
    }
    if(!isnan(y)) {
        int n = snprintf(buf, sizeof(buf), " Y%1.3f", y);
        strncat(cmd, buf, n);
    }
    if(!isnan(z)) {
        int n = snprintf(buf, sizeof(buf), " Z%1.3f", z);
        strncat(cmd, buf, n);
    }

    // use specified feedrate (mm/sec)
    int n = snprintf(buf, sizeof(buf), " F%1.1f", feedrate * 60); // feed rate is converted to mm/min
    strncat(cmd, buf, n);
    if(relative) strcat(cmd, " G90");

    //THEKERNEL->streams->printf("DEBUG: move: %s\n", cmd);

    // send as a command line as may have multiple G codes in it
    struct SerialMessage message;
    message.message = cmd;
    message.stream = &(StreamOutput::NullStream);
    THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
    THEKERNEL->conveyor->wait_for_empty_queue();
}

// issue home command
void ZProbe::home()
{
    Gcode gc("G28", &(StreamOutput::NullStream));
    THEKERNEL->call_event(ON_GCODE_RECEIVED, &gc);
}

bool ZProbe::set_trim(float x, float y, float z, StreamOutput *stream)
{
    float t[3] {x, y, z};
    bool ok = THEKERNEL->public_data->set_value( endstops_checksum, trim_checksum, t);

    if (ok) {
        stream->printf("set trim to X:%f Y:%f Z:%f\n", x, y, z);
    } else {
        stream->printf("unable to set trim, is endstops enabled?\n");
    }

    return ok;
}

bool ZProbe::get_trim(float &x, float &y, float &z)
{
    void *returned_data;
    bool ok = THEKERNEL->public_data->get_value( endstops_checksum, trim_checksum, &returned_data );

    if (ok) {
        float *trim = static_cast<float *>(returned_data);
        x = trim[0];
        y = trim[1];
        z = trim[2];
        return true;
    }
    return false;
}
