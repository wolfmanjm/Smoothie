#include "JohannKosselSolution.h"
#include <fastmath.h>
#include "checksumm.h"
#include "ConfigValue.h"
#include "libs/Kernel.h"

#include "libs/nuts_bolts.h"

#include "libs/Config.h"
#include "Vector3.h"

#define arm_length_checksum         CHECKSUM("arm_length")
#define arm_radius_checksum         CHECKSUM("arm_radius")
#define tower1_offset_checksum      CHECKSUM("delta_tower1_offset")
#define tower2_offset_checksum      CHECKSUM("delta_tower2_offset")
#define tower3_offset_checksum      CHECKSUM("delta_tower3_offset")


#define SQ(x) powf(x, 2)
#define ROUND(x, y) (roundf(x * 1e ## y) / 1e ## y)

JohannKosselSolution::JohannKosselSolution(Config *config)
{
    // arm_length is the length of the arm from hinge to hinge
    arm_length = config->value(arm_length_checksum)->by_default(250.0f)->as_number();
    // arm_radius is the horizontal distance from hinge to hinge when the effector is centered
    arm_radius = config->value(arm_radius_checksum)->by_default(124.0f)->as_number();

    tower1_offset = config->value(tower1_offset_checksum)->by_default(0.0f)->as_number();
    tower2_offset = config->value(tower2_offset_checksum)->by_default(0.0f)->as_number();
    tower3_offset = config->value(tower3_offset_checksum)->by_default(0.0f)->as_number();

    init();
}

void JohannKosselSolution::init()
{
    arm_length_squared = SQ(arm_length);

    // Effective X/Y positions of the three vertical towers.
    float delta_radius = arm_radius;

    const float SIN_60   = 0.8660254037844386F;
    const float COS_60   = 0.5F;

    delta_tower1_x = -SIN_60 * (delta_radius + tower1_offset); // front left tower
    delta_tower1_y = -COS_60 * (delta_radius + tower1_offset);

    delta_tower2_x =  SIN_60 * (delta_radius + tower2_offset); // front right tower
    delta_tower2_y = -COS_60 * (delta_radius + tower2_offset);

    delta_tower3_x = 0.0F; // back middle tower
    delta_tower3_y = delta_radius + tower3_offset;
}

void JohannKosselSolution::cartesian_to_actuator( float cartesian_mm[], float actuator_mm[] )
{
    actuator_mm[ALPHA_STEPPER] = sqrtf(this->arm_length_squared
                                       - SQ(delta_tower1_x - cartesian_mm[X_AXIS])
                                       - SQ(delta_tower1_y - cartesian_mm[Y_AXIS])
                                      ) + cartesian_mm[Z_AXIS];
    actuator_mm[BETA_STEPPER ] = sqrtf(this->arm_length_squared
                                       - SQ(delta_tower2_x - cartesian_mm[X_AXIS])
                                       - SQ(delta_tower2_y - cartesian_mm[Y_AXIS])
                                      ) + cartesian_mm[Z_AXIS];
    actuator_mm[GAMMA_STEPPER] = sqrtf(this->arm_length_squared
                                       - SQ(delta_tower3_x - cartesian_mm[X_AXIS])
                                       - SQ(delta_tower3_y - cartesian_mm[Y_AXIS])
                                      ) + cartesian_mm[Z_AXIS];
}

void JohannKosselSolution::actuator_to_cartesian( float actuator_mm[], float cartesian_mm[] )
{
    // from http://en.wikipedia.org/wiki/Circumscribed_circle#Barycentric_coordinates_from_cross-_and_dot-products
    // based on https://github.com/ambrop72/aprinter/blob/2de69a/aprinter/printer/DeltaTransform.h#L81
    Vector3 tower1( delta_tower1_x, delta_tower1_y, actuator_mm[0] );
    Vector3 tower2( delta_tower2_x, delta_tower2_y, actuator_mm[1] );
    Vector3 tower3( delta_tower3_x, delta_tower3_y, actuator_mm[2] );

    Vector3 s12 = tower1.sub(tower2);
    Vector3 s23 = tower2.sub(tower3);
    Vector3 s13 = tower1.sub(tower3);

    Vector3 normal = s12.cross(s23);

    float magsq_s12 = s12.magsq();
    float magsq_s23 = s23.magsq();
    float magsq_s13 = s13.magsq();

    float inv_nmag_sq = 1.0F / normal.magsq();
    float q = 0.5F * inv_nmag_sq;

    float a = q * magsq_s23 * s12.dot(s13);
    float b = q * magsq_s13 * s12.dot(s23) * -1.0F; // negate because we use s12 instead of s21
    float c = q * magsq_s12 * s13.dot(s23);

    Vector3 circumcenter( delta_tower1_x * a + delta_tower2_x * b + delta_tower3_x * c,
                          delta_tower1_y * a + delta_tower2_y * b + delta_tower3_y * c,
                          actuator_mm[0] * a + actuator_mm[1] * b + actuator_mm[2] * c );

    float r_sq = 0.5F * q * magsq_s12 * magsq_s23 * magsq_s13;
    float dist = sqrtf(inv_nmag_sq * (arm_length_squared - r_sq));

    Vector3 cartesian = circumcenter.sub(normal.mul(dist));

    cartesian_mm[0] = ROUND(cartesian[0], 4);
    cartesian_mm[1] = ROUND(cartesian[1], 4);
    cartesian_mm[2] = ROUND(cartesian[2], 4);
}

bool JohannKosselSolution::set_optional(const arm_options_t &options)
{

    arm_options_t::const_iterator i;

    i = options.find('L');
    if(i != options.end()) {
        arm_length = i->second;

    }
    i = options.find('R');
    if(i != options.end()) {
        arm_radius = i->second;
    }

    i = options.find('A');
    if(i != options.end()) {
        tower1_offset = i->second;
    }
    i = options.find('B');
    if(i != options.end()) {
        tower2_offset = i->second;
    }
    i = options.find('C');
    if(i != options.end()) {
        tower3_offset = i->second;
    }

    init();
    return true;
}

bool JohannKosselSolution::get_optional(arm_options_t &options)
{
    options['L'] = this->arm_length;
    options['R'] = this->arm_radius;
    options['A'] = this->tower1_offset;
    options['B'] = this->tower2_offset;
    options['C'] = this->tower3_offset;

    return true;
};
