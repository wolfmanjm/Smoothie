#ifndef JOHANNKOSSELSOLUTION_H
#define ROSTOCKSOLUTION_H
#include "libs/Module.h"
#include "BaseSolution.h"

class Config;

class JohannKosselSolution : public BaseSolution {
    public:
        JohannKosselSolution(Config*);
        void cartesian_to_actuator( float[], float[] );
        void actuator_to_cartesian( float[], float[] );

        bool set_optional(const arm_options_t& options);
        bool get_optional(arm_options_t& options);

    private:
        void init();

        float arm_length;
        float arm_radius;
        float arm_length_squared;

        float delta_tower1_x;
        float delta_tower1_y;
        float delta_tower2_x;
        float delta_tower2_y;
        float delta_tower3_x;
        float delta_tower3_y;
        float tower1_offset;
        float tower2_offset;
        float tower3_offset;
        float tower1_angle;
        float tower2_angle;
        float tower3_angle;
};
#endif // JOHANNKOSSELSOLUTION_H
