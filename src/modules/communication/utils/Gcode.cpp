/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/


#include "Gcode.h"
#include "libs/StreamOutput.h"
#include "utils.h"
#include <stdlib.h>
#include <algorithm>

// This is a gcode object. It reprensents a GCode string/command, and caches some important values about that command for the sake of performance.
// It gets passed around in events, and attached to the queue ( that'll change )
Gcode::Gcode(const string &command, StreamOutput *stream, bool strip)
{
    this->has_g= false;
    this->has_m= false;
    this->m= 0;
    this->g= 0;
    this->add_nl= false;
    this->stream= stream;
    this->millimeters_of_travel = 0.0F;
    this->accepted_by_module = false;
    this->valid= false;
    parse_gcode_words(command);
}

Gcode::~Gcode(){}

Gcode::Gcode(const Gcode &to_copy)
{
    this->keys                  = to_copy.keys;
    this->values                = to_copy.values;
    this->millimeters_of_travel = to_copy.millimeters_of_travel;
    this->add_nl                = to_copy.add_nl;
    this->stream                = to_copy.stream;
    this->accepted_by_module    = false;
    this->txt_after_ok.assign( to_copy.txt_after_ok );

    if((this->has_m= to_copy.has_m)) this->m= to_copy.m;
    if((this->has_g= to_copy.has_g)) this->g= to_copy.g;
}

Gcode &Gcode::operator= (const Gcode &to_copy)
{
    if( this != &to_copy ) {
        this->keys                  = to_copy.keys;
        this->values                = to_copy.values;
        this->millimeters_of_travel = to_copy.millimeters_of_travel;
        this->add_nl                = to_copy.add_nl;
        this->stream                = to_copy.stream;
        this->txt_after_ok.assign( to_copy.txt_after_ok );
        if((this->has_m= to_copy.has_m)) this->m= to_copy.m;
        if((this->has_g= to_copy.has_g)) this->g= to_copy.g;
    }
    this->accepted_by_module = false;
    return *this;
}

// Whether or not a Gcode has a letter
bool Gcode::has_letter( char letter ) const
{
    for (auto a : keys) {
        if( a == letter ) {
            return true;
        }
    }
    return false;
}

// Retrieve the value for a given letter
float Gcode::get_value( char letter) const
{
    int i= 0;
    for (auto a : keys) {
        if( a == letter ) {
            return values[i];
        }
        ++i;
    }
    return 0;
}

int Gcode::get_num_args() const
{
    return keys.size();
}

// extract the next gcode word
bool get_next_word(string &line, string::iterator& next, char& letter, float& value)
{
    if(next == line.end()) return false;

    char c= *next++;
    if(c < 'A' || c > 'Z') return false; // must be a command or parameter character
    letter= c;
    // extract float number [+|-]nnn[.yyy]
    string::iterator pos= next;
    while(next != line.end()) {
        c= *next;
        if(::isdigit(c) || c == '-' || c == '+' || c == '.') {
            next++;
        }else{
            break;
        }
    }
    if(next == pos) return false; // no number

    // convert number into float
    string v(pos, next);
    value = strtof(v.c_str(), nullptr);
    return true;
}

// split the command into gcode words.. X12.34 E43.21 F100 etc
// store in the words vector as a tuple<char, float>
// also split off the Gxxx and Mxxx
void Gcode::parse_gcode_words(const string& command)
{
    string newcmd(command);
    // strip all whitespace from command line (comments are expected to already have been removed)
    newcmd.erase(std::remove_if(newcmd.begin(), newcmd.end(), ::isspace), newcmd.end());

    char letter;
    float value;
    string::iterator next= newcmd.begin();
    while(get_next_word(newcmd, next, letter, value)) {
        if( letter == 'G' ) {
            this->has_g = true;
            this->g = value;
        } else if( letter == 'M' ) {
            this->has_m = true;
            this->m = value;
        } else {
            keys.push_back(letter);
            values.push_back(value);
        }
    }
    keys.shrink_to_fit();
    values.shrink_to_fit();
    valid= (next == newcmd.end()); // make sure we processed the entire line
}

void Gcode::mark_as_taken()
{
    this->accepted_by_module = true;
}

// strip off X Y Z I J K parameters if G0/1/2/3
void Gcode::strip_parameters()
{
    if(has_g && g < 4){
        // strip the words of the XYZIJK parameters
        size_t i= 0;
        while(i < keys.size()) {
            char c= keys[i];
            if( (c >= 'X' && c <= 'Z') || (c >= 'I' && c <= 'K') ) {
                keys.erase(keys.begin()+i);
                values.erase(values.begin()+i);
            }else{
                ++i;
            }
        }
        keys.shrink_to_fit();
        values.shrink_to_fit();
    }
}

void Gcode::dump()
{
    int i= 0;
    for(auto a : keys) {
        stream->printf("%c %f\n", a, values[i]);
        ++i;
    }
    if(has_m) stream->printf("M%d\n", m);
    if(has_g) stream->printf("G%d\n", g);

}
