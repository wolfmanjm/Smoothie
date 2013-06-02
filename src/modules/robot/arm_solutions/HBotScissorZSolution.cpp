#include "HBotScissorZSolution.h"
#include <fastmath.h>

HBotScissorZSolution::HBotScissorZSolution(Config* passed_config) : config(passed_config){
    this->alpha_steps_per_mm = this->config->value(alpha_steps_per_mm_checksum)->as_number();
    this->beta_steps_per_mm  = this->config->value( beta_steps_per_mm_checksum)->as_number();
    this->gamma_steps_per_mm = this->config->value(gamma_steps_per_mm_checksum)->as_number();

	// arm_length is the length of the arm from hinge to hinge
	this->arm_length         = this->config->value(arm_length_checksum)->by_default(250.0)->as_number();
	this->arm_length_squared = powf(this->arm_length/2, 2);

	// z offset is the offset between the request z and the height calculated
	this->z_offset           = this->config->value(z_offset_checksum)->by_default(0.0)->as_number();
}

void HBotScissorZSolution::set_offset( double millimeters[] ) {
	this->z_offset= millimeters[Z_AXIS];
}

void HBotScissorZSolution::millimeters_to_steps( double millimeters[], int steps[] ){
    int delta_x = lround( millimeters[X_AXIS] * this->alpha_steps_per_mm );
    int delta_y = lround( millimeters[Y_AXIS] * this->beta_steps_per_mm );
    steps[ALPHA_STEPPER] = delta_x + delta_y;
	steps[BETA_STEPPER ] = delta_x - delta_y;
	// we invert the requested length to get height of scissor lift where height == 0 is full down and
	// height == arm_length is full extended. Z == 0 would be close to fully extended less the z offset
    steps[GAMMA_STEPPER] = lround( solve_height((this->arm_length-millimeters[Z_AXIS]) + this->z_offset) * this->gamma_steps_per_mm );
}

// find the position on the leadscrew for the given height of the scissor lift
float HBotScissorZSolution::solve_height( float height) {
	return sqrtf(arm_length_squared - powf(height/2, 2));
}

void HBotScissorZSolution::steps_to_millimeters( int steps[], double millimeters[] ){
    int delta_alpha = steps[X_AXIS] / this->alpha_steps_per_mm;
    int delta_beta = steps[Y_AXIS] / this->beta_steps_per_mm;
    millimeters[ALPHA_STEPPER] = 0.5*(delta_alpha + delta_beta);
    millimeters[BETA_STEPPER ] = 0.5*(delta_alpha - delta_beta);
    millimeters[GAMMA_STEPPER] = steps[Z_AXIS] / this->gamma_steps_per_mm;
}

void HBotScissorZSolution::set_steps_per_millimeter( double steps[] )
{
    this->alpha_steps_per_mm = steps[0];
    this->beta_steps_per_mm  = steps[1];
    this->gamma_steps_per_mm = steps[2];
}

void HBotScissorZSolution::get_steps_per_millimeter( double steps[] )
{
    steps[0] = this->alpha_steps_per_mm;
    steps[1] = this->beta_steps_per_mm;
    steps[2] = this->gamma_steps_per_mm;
}
