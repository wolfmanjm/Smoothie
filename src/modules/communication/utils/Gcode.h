/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef GCODE_H
#define GCODE_H
#include <string>
#include <vector>
#include <tuple>

using std::string;

class StreamOutput;

// Object to represent a Gcode command
class Gcode {
    public:
        Gcode(const string&, StreamOutput*, bool strip=true);
        Gcode(const Gcode& to_copy);
        Gcode& operator= (const Gcode& to_copy);
        ~Gcode();

        bool has_letter ( char letter ) const;
        float get_value ( char letter ) const;
        int get_num_args() const;
        void mark_as_taken();
        void strip_parameters();
        bool is_valid() { return valid; }

        // FIXME these should be private
        union {
            unsigned int m;
            unsigned int g;
        };

        float millimeters_of_travel;

        struct {
            bool add_nl:1;
            bool has_m:1;
            bool has_g:1;
            bool accepted_by_module:1;
            bool valid:1;
        };

        StreamOutput* stream;
        string txt_after_ok;

        void dump();
    private:
        void parse_gcode_words(const string&);

        std::vector<std::tuple<char,float>> words;
};
#endif
