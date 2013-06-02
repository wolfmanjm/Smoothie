#ifndef HBOTSCISSORZSOLUTION_H
#define HBOTSCISSORZSOLUTION_H
#include "libs/Module.h"
#include "libs/Kernel.h"
#include "BaseSolution.h"
#include "libs/nuts_bolts.h"

#include "libs/Config.h"

#define alpha_steps_per_mm_checksum CHECKSUM("alpha_steps_per_mm")
#define beta_steps_per_mm_checksum  CHECKSUM("beta_steps_per_mm")
#define gamma_steps_per_mm_checksum CHECKSUM("gamma_steps_per_mm")

#define arm_length_checksum         CHECKSUM("arm_length")
#define z_offset_checksum         CHECKSUM("z_offset")

class HBotScissorZSolution : public BaseSolution {
    public:
        HBotScissorZSolution(Config* passed_config);
        void millimeters_to_steps( double millimeters[], int steps[] );
        void steps_to_millimeters( int steps[], double millimeters[] );

        void set_steps_per_millimeter( double steps[] );
        void get_steps_per_millimeter( double steps[] );
        void set_offset( double millimeters[] );

		float solve_height( float height );

		Config* config;
        double alpha_steps_per_mm;
        double beta_steps_per_mm;
        double gamma_steps_per_mm;

		float arm_length;
		float arm_length_squared;
		float z_offset;
};






#endif // HBOTSOLUTION_H

