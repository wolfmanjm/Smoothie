/*
    This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
    Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
    Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
    You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/


#include "SimpleShell.h"
#include "libs/Kernel.h"
#include "libs/nuts_bolts.h"
#include "libs/utils.h"
#include "libs/SerialMessage.h"
#include "libs/StreamOutput.h"
#include "modules/robot/Conveyor.h"
#include "DirHandle.h"
#include "mri.h"
#include "version.h"
#include "PublicDataRequest.h"
#include "FileStream.h"
#include "checksumm.h"
#include "PublicData.h"
#include "Gcode.h"

#include "modules/tools/temperaturecontrol/TemperatureControlPublicAccess.h"
#include "modules/robot/RobotPublicAccess.h"
#include "NetworkPublicAccess.h"
#include "platform_memory.h"
#include "SwitchPublicAccess.h"

#include "system_LPC17xx.h"
#include "LPC17xx.h"

extern unsigned int g_maximumHeapAddress;

#include <malloc.h>
#include <mri.h>
#include <stdio.h>
#include <stdint.h>

extern "C" uint32_t  __end__;
extern "C" uint32_t  __malloc_free_list;
extern "C" uint32_t  _sbrk(int size);

// command lookup table
const SimpleShell::ptentry_t SimpleShell::commands_table[] = {
    {"ls",       SimpleShell::ls_command},
    {"cd",       SimpleShell::cd_command},
    {"pwd",      SimpleShell::pwd_command},
    {"cat",      SimpleShell::cat_command},
    {"rm",       SimpleShell::rm_command},
    {"reset",    SimpleShell::reset_command},
    {"dfu",      SimpleShell::dfu_command},
    {"break",    SimpleShell::break_command},
    {"help",     SimpleShell::help_command},
    {"?",        SimpleShell::help_command},
    {"version",  SimpleShell::version_command},
    {"mem",      SimpleShell::mem_command},
    {"get",      SimpleShell::get_command},
    {"set_temp", SimpleShell::set_temp_command},
    {"switch",   SimpleShell::switch_command},
    {"net",      SimpleShell::net_command},
    {"load",     SimpleShell::load_command},
    {"save",     SimpleShell::save_command},


    {"test",     SimpleShell::test_command},

    // unknown command
    {NULL, NULL}
};

int SimpleShell::reset_delay_secs= 0;

// Adam Greens heap walk from http://mbed.org/forum/mbed/topic/2701/?page=4#comment-22556
static uint32_t heapWalk(StreamOutput *stream, bool verbose)
{
    uint32_t chunkNumber = 1;
    // The __end__ linker symbol points to the beginning of the heap.
    uint32_t chunkCurr = (uint32_t)&__end__;
    // __malloc_free_list is the head pointer to newlib-nano's link list of free chunks.
    uint32_t freeCurr = __malloc_free_list;
    // Calling _sbrk() with 0 reserves no more memory but it returns the current top of heap.
    uint32_t heapEnd = _sbrk(0);
    // accumulate totals
    uint32_t freeSize = 0;
    uint32_t usedSize = 0;

    stream->printf("Used Heap Size: %lu\n", heapEnd - chunkCurr);

    // Walk through the chunks until we hit the end of the heap.
    while (chunkCurr < heapEnd) {
        // Assume the chunk is in use.  Will update later.
        int      isChunkFree = 0;
        // The first 32-bit word in a chunk is the size of the allocation.  newlib-nano over allocates by 8 bytes.
        // 4 bytes for this 32-bit chunk size and another 4 bytes to allow for 8 byte-alignment of returned pointer.
        uint32_t chunkSize = *(uint32_t *)chunkCurr;
        // The start of the next chunk is right after the end of this one.
        uint32_t chunkNext = chunkCurr + chunkSize;

        // The free list is sorted by address.
        // Check to see if we have found the next free chunk in the heap.
        if (chunkCurr == freeCurr) {
            // Chunk is free so flag it as such.
            isChunkFree = 1;
            // The second 32-bit word in a free chunk is a pointer to the next free chunk (again sorted by address).
            freeCurr = *(uint32_t *)(freeCurr + 4);
        }

        // Skip past the 32-bit size field in the chunk header.
        chunkCurr += 4;
        // 8-byte align the data pointer.
        chunkCurr = (chunkCurr + 7) & ~7;
        // newlib-nano over allocates by 8 bytes, 4 bytes for the 32-bit chunk size and another 4 bytes to allow for 8
        // byte-alignment of the returned pointer.
        chunkSize -= 8;
        if (verbose)
            stream->printf("  Chunk: %lu  Address: 0x%08lX  Size: %lu  %s\n", chunkNumber, chunkCurr, chunkSize, isChunkFree ? "CHUNK FREE" : "");

        if (isChunkFree) freeSize += chunkSize;
        else usedSize += chunkSize;

        chunkCurr = chunkNext;
        chunkNumber++;
    }
    stream->printf("Allocated: %lu, Free: %lu\r\n", usedSize, freeSize);
    return freeSize;
}


void SimpleShell::on_module_loaded()
{
    this->register_for_event(ON_CONSOLE_LINE_RECEIVED);
	this->register_for_event(ON_GCODE_RECEIVED);
	this->register_for_event(ON_SECOND_TICK);

    reset_delay_secs = 0;
}

void SimpleShell::on_second_tick(void *)
{
    // we are timing out for the reset
    if (reset_delay_secs > 0) {
        if (--reset_delay_secs == 0) {
            system_reset(false);
        }
    }
}

// handle backspace or delete in command by removing preceding character
string SimpleShell::handle_bs(string cmd) {
    unsigned int n;
    while((n=cmd.find_first_of("\008\177")) != string::npos) {
        cmd= cmd.substr(0, n) + cmd.substr(n+1);
    }
    return cmd;
}

void SimpleShell::on_gcode_received(void *argument)
{
    Gcode *gcode = static_cast<Gcode *>(argument);
    string args= get_arguments(gcode->get_command());

    if (gcode->has_m) {
        if (gcode->m == 20) { // list sd card
            gcode->mark_as_taken();
            gcode->stream->printf("Begin file list\r\n");
            ls_command("/sd", gcode->stream);
            gcode->stream->printf("End file list\r\n");

        } else if (gcode->m == 30) { // remove file
            gcode->mark_as_taken();
            rm_command("/sd/" + args, gcode->stream);

        }else if(gcode->m == 501) { // load config override
            gcode->mark_as_taken();
            if(args.empty()) {
                load_command("/sd/config-override", gcode->stream);
            }else{
                load_command("/sd/config-override." + args, gcode->stream);
            }

        }else if(gcode->m == 504) { // save to specific config override file
            gcode->mark_as_taken();
            if(args.empty()) {
                save_command("/sd/config-override", gcode->stream);
            }else{
                save_command("/sd/config-override." + args, gcode->stream);
            }
        }
    }
}

bool SimpleShell::parse_command(const char *cmd, string args, StreamOutput *stream)
{
    for (const ptentry_t *p = commands_table; p->command != NULL; ++p) {
        if (strncasecmp(cmd, p->command, strlen(p->command)) == 0) {
            p->func(args, stream);
            return true;
        }
    }

    return false;
}

// When a new line is received, check if it is a command, and if it is, act upon it
void SimpleShell::on_console_line_received( void *argument )
{
    SerialMessage new_message = *static_cast<SerialMessage *>(argument);

    // ignore comments and blank lines and if this is a G code then also ignore it
    char first_char = new_message.message[0];
    if(strchr(";( \n\rGMTN", first_char) != NULL) return;

    string possible_command = new_message.message;

    //new_message.stream->printf("Received %s\r\n", possible_command.c_str());
    string cmd = shift_parameter(possible_command);

    // find command and execute it

    bool handled= parse_command(cmd.c_str(), possible_command, new_message.stream);
    if(handled)
        last_command= possible_command;
}

// Act upon an ls command
// Convert the first parameter into an absolute path, then list the files in that path
void SimpleShell::ls_command( string parameters, StreamOutput *stream )
{
    string folder = absolute_from_relative( parameters );
    DIR *d;
    struct dirent *p;
    d = opendir(folder.c_str());
    if (d != NULL) {
        while ((p = readdir(d)) != NULL) {
            stream->printf("%s\r\n", lc(string(p->d_name)).c_str());
        }
        closedir(d);
    } else {
        stream->printf("Could not open directory %s \r\n", folder.c_str());
    }
}

// Delete a file
void SimpleShell::rm_command( string parameters, StreamOutput *stream )
{
    const char *fn= absolute_from_relative(shift_parameter( parameters )).c_str();
    int s = remove(fn);
    if (s != 0) stream->printf("Could not delete %s \r\n", fn);
}

// Change current absolute path to provided path
void SimpleShell::cd_command( string parameters, StreamOutput *stream )
{
    string folder = absolute_from_relative( parameters );

    DIR *d;
    d = opendir(folder.c_str());
    if (d == NULL) {
        stream->printf("Could not open directory %s \r\n", folder.c_str() );
    } else {
        THEKERNEL->current_path = folder;
        closedir(d);
    }
}

// Responds with the present working directory
void SimpleShell::pwd_command( string parameters, StreamOutput *stream )
{
    stream->printf("%s\r\n", THEKERNEL->current_path.c_str());
}

// Output the contents of a file, first parameter is the filename, second is the limit ( in number of lines to output )
void SimpleShell::cat_command( string parameters, StreamOutput *stream )
{
    // Get parameters ( filename and line limit )
    string filename          = absolute_from_relative(shift_parameter( parameters ));
    string limit_paramater   = shift_parameter( parameters );
    int limit = -1;
    if ( limit_paramater != "" ) {
        char *e = NULL;
        limit = strtol(limit_paramater.c_str(), &e, 10);
        if (e <= limit_paramater.c_str())
            limit = -1;
    }

    // Open file
    FILE *lp = fopen(filename.c_str(), "r");
    if (lp == NULL) {
        stream->printf("File not found: %s\r\n", filename.c_str());
        return;
    }
    string buffer;
    int c;
    int newlines = 0;
    int linecnt= 0;
    // Print each line of the file
    while ((c = fgetc (lp)) != EOF) {
        buffer.append((char *)&c, 1);
        if ( char(c) == '\n' || ++linecnt > 80) {
            newlines++;
            stream->puts(buffer.c_str());
            buffer.clear();
            if(linecnt > 80) linecnt= 0;
        }
        if ( newlines == limit ) {
            break;
        }
    };
    fclose(lp);

}

// loads the specified config-override file
void SimpleShell::load_command( string parameters, StreamOutput *stream )
{
    // Get parameters ( filename )
    string filename = absolute_from_relative(parameters);
    if(filename == "/") {
        filename = THEKERNEL->config_override_filename();
    }

    FILE *fp= fopen(filename.c_str(), "r");
    if(fp != NULL) {
        char buf[132];
        stream->printf("Loading config override file: %s...\n", filename.c_str());
        while(fgets(buf, sizeof buf, fp) != NULL) {
            stream->printf("  %s", buf);
            if(buf[0] == ';') continue; // skip the comments
            struct SerialMessage message= {&(StreamOutput::NullStream), buf};
            THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message);
        }
        stream->printf("config override file executed\n");
        fclose(fp);

    }else{
        stream->printf("File not found: %s\n", filename.c_str());
    }
}

// saves the specified config-override file
void SimpleShell::save_command( string parameters, StreamOutput *stream )
{
    // Get parameters ( filename )
    string filename = absolute_from_relative(parameters);
    if(filename == "/") {
        filename = THEKERNEL->config_override_filename();
    }

    // replace stream with one that writes to config-override file
    FileStream *gs = new FileStream(filename.c_str());
    if(!gs->is_open()) {
        stream->printf("Unable to open File %s for write\n", filename.c_str());
        return;
    }

    // issue a M500 which will store values in the file stream
    Gcode *gcode = new Gcode("M500", gs);
    THEKERNEL->call_event(ON_GCODE_RECEIVED, gcode );
    delete gs;
    delete gcode;

    stream->printf("Settings Stored to %s\r\n", filename.c_str());
}

// show free memory
void SimpleShell::mem_command( string parameters, StreamOutput *stream)
{
    bool verbose = shift_parameter( parameters ).find_first_of("Vv") != string::npos ;
    unsigned long heap = (unsigned long)_sbrk(0);
    unsigned long m = g_maximumHeapAddress - heap;
    stream->printf("Unused Heap: %lu bytes\r\n", m);

    uint32_t f= heapWalk(stream, verbose);
    stream->printf("Total Free RAM: %lu bytes\r\n", m + f);

    stream->printf("Free AHB0: %lu, AHB1: %lu\r\n", AHB0.free(), AHB1.free());
    if (verbose)
    {
        AHB0.debug(stream);
        AHB1.debug(stream);
    }
}

static uint32_t getDeviceType()
{
#define IAP_LOCATION 0x1FFF1FF1
    uint32_t command[1];
    uint32_t result[5];
    typedef void (*IAP)(uint32_t *, uint32_t *);
    IAP iap = (IAP) IAP_LOCATION;

    __disable_irq();

    command[0] = 54;
    iap(command, result);

    __enable_irq();

    return result[1];
}

// get network config
void SimpleShell::net_command( string parameters, StreamOutput *stream)
{
    void *returned_data;
    bool ok= PublicData::get_value( network_checksum, get_ipconfig_checksum, &returned_data );
    if(ok) {
        char *str= (char *)returned_data;
        stream->printf("%s\r\n", str);
        free(str);

    }else{
        stream->printf("No network detected\n");
    }
}

// print out build version
void SimpleShell::version_command( string parameters, StreamOutput *stream)
{
    Version vers;
    uint32_t dev = getDeviceType();
    const char *mcu = (dev & 0x00100000) ? "LPC1769" : "LPC1768";
    stream->printf("Build version: %s, Build date: %s, MCU: %s, System Clock: %ldMHz\r\n", vers.get_build(), vers.get_build_date(), mcu, SystemCoreClock / 1000000);
}

// Reset the system
void SimpleShell::reset_command( string parameters, StreamOutput *stream)
{
    stream->printf("Smoothie out. Peace. Rebooting in 5 seconds...\r\n");
    reset_delay_secs = 5; // reboot in 5 seconds
}

// go into dfu boot mode
void SimpleShell::dfu_command( string parameters, StreamOutput *stream)
{
    stream->printf("Entering boot mode...\r\n");
    system_reset(true);
}

// Break out into the MRI debugging system
void SimpleShell::break_command( string parameters, StreamOutput *stream)
{
    stream->printf("Entering MRI debug mode...\r\n");
    __debugbreak();
}

// used to test out the get public data events
void SimpleShell::get_command( string parameters, StreamOutput *stream)
{
    string what = shift_parameter( parameters );
    void *returned_data;

    if (what == "temp") {
        string type = shift_parameter( parameters );
        bool ok = PublicData::get_value( temperature_control_checksum, get_checksum(type), current_temperature_checksum, &returned_data );

        if (ok) {
            struct pad_temperature temp =  *static_cast<struct pad_temperature *>(returned_data);
            stream->printf("%s temp: %f/%f @%d\r\n", type.c_str(), temp.current_temperature, temp.target_temperature, temp.pwm);
        } else {
            stream->printf("%s is not a known temperature device\r\n", type.c_str());
        }

    } else if (what == "pos") {
        bool ok = PublicData::get_value( robot_checksum, current_position_checksum, &returned_data );

        if (ok) {
            float *pos = static_cast<float *>(returned_data);
            stream->printf("Position X: %f, Y: %f, Z: %f\r\n", pos[0], pos[1], pos[2]);

        } else {
            stream->printf("get pos command failed\r\n");
        }
    }
}

// used to test out the get public data events
void SimpleShell::set_temp_command( string parameters, StreamOutput *stream)
{
    string type = shift_parameter( parameters );
    string temp = shift_parameter( parameters );
    float t = temp.empty() ? 0.0 : strtof(temp.c_str(), NULL);
    bool ok = PublicData::set_value( temperature_control_checksum, get_checksum(type), &t );

    if (ok) {
        stream->printf("%s temp set to: %3.1f\r\n", type.c_str(), t);
    } else {
        stream->printf("%s is not a known temperature device\r\n", type.c_str());
    }
}

// used to test out the get public data events for switch
void SimpleShell::switch_command( string parameters, StreamOutput *stream)
{
    string type = shift_parameter( parameters );
    string value = shift_parameter( parameters );
    bool ok= false;
    if(value == "on" || value == "off") {
        bool b= value == "on";
        ok = PublicData::set_value( switch_checksum, get_checksum(type), state_checksum, &b );
    }else{
        float v = strtof(value.c_str(), NULL);
        ok = PublicData::set_value( switch_checksum, get_checksum(type), value_checksum, &v );
    }
    if (ok) {
        stream->printf("switch %s set to: %s\r\n", type.c_str(), value.c_str());
    } else {
        stream->printf("%s is not a known switch device\r\n", type.c_str());
    }
}

void SimpleShell::help_command( string parameters, StreamOutput *stream )
{
    stream->printf("Commands:\r\n");
    stream->printf("version\r\n");
    stream->printf("mem [-v]\r\n");
    stream->printf("ls [folder]\r\n");
    stream->printf("cd folder\r\n");
    stream->printf("pwd\r\n");
    stream->printf("cat file [limit]\r\n");
    stream->printf("rm file\r\n");
    stream->printf("play file [-v]\r\n");
    stream->printf("progress - shows progress of current play\r\n");
    stream->printf("abort - abort currently playing file\r\n");
    stream->printf("reset - reset smoothie\r\n");
    stream->printf("dfu - enter dfu boot loader\r\n");
    stream->printf("break - break into debugger\r\n");
    stream->printf("config-get [<configuration_source>] <configuration_setting>\r\n");
    stream->printf("config-set [<configuration_source>] <configuration_setting> <value>\r\n");
    stream->printf("get temp [bed|hotend]\r\n");
    stream->printf("set_temp bed|hotend 185\r\n");
    stream->printf("get pos\r\n");
    stream->printf("net\r\n");
    stream->printf("load [file] - loads a configuration override file from soecified name or config-override\r\n");
    stream->printf("save [file] - saves a configuration override file as specified filename or as config-override\r\n");
}




#if 0
#include "BaseSolution.h"
#include "LinearDeltaSolution.h"
#include "Config.h"
#include "Pin.h"
#include "mbed.h"
#include "Pin.h"
#include "Extruder.h"
#include <functional>
#endif

#if 0
class TestModule : public Module
{
public:
    TestModule() {i=0;}
    ~TestModule() {}
    uint32_t test(uint32_t p) { return p;}
    void on_main_loop(void * argument) { i++; }
    int i;
};
TestModule t;

class DoFunc {
public:
    void dofunc(StreamOutput *stream) {
        using std::placeholders::_1;
        func= std::bind(&TestModule::test, &t, _1);
        int i= func(10);
        stream->printf("%d\n", i);
    }
    std::function<uint32_t(uint32_t)> func;
};
#endif

#if 0
#include "StreamOutputPool.h"
class TestMem
{
public:
    TestMem(){
        THEKERNEL->streams->printf("ctor %p - %p:\n", this, a);
        for (unsigned i = 0; i < sizeof a; ++i) {
            THEKERNEL->streams->printf("%d ", a[i]);
        }
        THEKERNEL->streams->printf("\n");
    }
    virtual ~TestMem() {
        THEKERNEL->streams->printf("dtor %p\n", this);
    }

private:
    uint8_t a[10];
};
#endif

void SimpleShell::test_command( string parameters, StreamOutput *stream)
{
#if 0
    // test memory allocation
    size_t n= sizeof(TestMem);
    void *v = AHB0.alloc(n);
    memset(v, 0, n);
    TestMem *a= new(v) TestMem;
    delete a;
    stream->printf("%d\n", n);
#endif

#if 0
    mem_command("", stream);
    stream->printf("\n");

    DoFunc *df= new DoFunc();
    df->dofunc(stream);

    mem_command("", stream);
    stream->printf("%d\n", sizeof(DoFunc));
    // 36 bytes of heap used, 4 when no func, func uses 32 bytes
    delete df;

    mem_command("", stream);
    stream->printf("\n");
    mem_command("", stream);
    Module *m= new Extruder(1);
    mem_command("", stream);
    stream->printf("\n");

    m->on_block_end(0);
    mem_command("", stream);
    stream->printf("\n");

    stream->printf("%p, %d\n", m, sizeof(Extruder));
    stream->printf("%d\n", sizeof(Pin));

    Module *t= new TestModule();
    stream->printf("%p, %d\n", t, sizeof(TestModule));
    mem_command("", stream);
    stream->printf("\n");

    delete m;
    delete t;

    // 72 data actual allocated is 112, sizeof= 104, 40 overhead vtable around 32
    // empty module is 4 bytes
#endif

#if 0
    THEKERNEL->config->config_cache_load();
    float millimeters[3]= {10.0, 20.0, 2.0};
    float a[3], m[3];
    BaseSolution* k= new LinearDeltaSolution(THEKERNEL->config);
    k->cartesian_to_actuator(millimeters, a);
    k->actuator_to_cartesian(a, m);
    stream->printf("%f, %f, %f, %f, %f, %f, %f, %f, %f\n",
        millimeters[0], millimeters[1], millimeters[2],
        a[0], a[1], a[2],
        m[0], m[1], m[2]);
    delete k;
    THEKERNEL->config->config_cache_clear();
#endif

#if 0
    Timer timer;
    double a= 100.0, b= 200.0, f, x= 0.0;
    timer.start();
    for(int i=0;i<1000000;i++) {
        f= hypot(a, b);
        x += f;
    }
    timer.stop();
    float t= timer.read();
    stream->printf("hypot: %f, result: %f\n", t, f);

    timer.reset();
    timer.start();
    for(int i=0;i<1000000;i++) {
        double a2= pow(a, 2);
        double b2= pow(b, 2);
        f= sqrt(a2+b2);
        x += f;
    }
    timer.stop();
    t= timer.read();
    stream->printf("sqrt: %f, result: %f, x: %f\n", t, f, x);
#endif

#if 0
    float millimeters[3]= {100.0, 200.0, 300.0};
    int steps[3];
    BaseSolution* r= new RostockSolution(THEKERNEL->config);
    BaseSolution* k= new JohannKosselSolution(THEKERNEL->config);
    Timer timer;
    timer.start();
    for(int i=0;i<10;i++) r->millimeters_to_steps(millimeters, steps);
    timer.stop();
    float tr= timer.read();
    timer.reset();
    timer.start();
    for(int i=0;i<10;i++) k->millimeters_to_steps(millimeters, steps);
    timer.stop();
    float tk= timer.read();
    stream->printf("time RostockSolution: %f, time JohannKosselSolution: %f\n", tr, tk);
    delete r;
    delete k;
#endif
#if 0
// time idle loop
#include "mbed.h"
static int tmin = 1000000;
static int tmax = 0;
void time_idle()
{
    Timer timer;
    timer.start();
    int begin, end;
    for (int i = 0; i < 1000; ++i) {
        begin = timer.read_us();
        THEKERNEL->call_event(ON_IDLE);
        end = timer.read_us();
        int d = end - begin;
        if (d < tmin) tmin = d;
        if (d > tmax) tmax = d;
    }
}
static Timer timer;
static int lastt = 0;
#endif
#if 0
    stream->printf("Testing SPI\n");

    // test SPI
    Pin cs_pin;
    cs_pin.from_string("0.16")->as_output();

    Pin busy_pin;
    busy_pin.from_string("2.13")->as_input();

    mbed::SPI *spi = new mbed::SPI(P0_18, P0_17, P0_15);
    cs_pin.set(1);

    spi->frequency(1000000);
    int cnt= 0;
    uint8_t r;
    int a= 'A';
    int d= 0;
    wait_ms(100);
    for (int j = 0; j < 10000000; ++j) {
        // cs_pin.set(0);
        // spi->write(0x21);
        // wait_us(40);
        // r= spi->write(0);
        // cs_pin.set(1);
        // if(r > 0) {
        //     stream->printf("%d - %02X\n", ++cnt, r);
        // }

        // cs_pin.set(0);
        // spi->write(0x20);
        // wait_us(40);
        // r= spi->write(0);
        // cs_pin.set(1);
        // if(r != 0) {
        //     stream->printf("%d - %02X\n", ++cnt, r);
        // }

        if(d++ > 25) {
            d= 0;
            cs_pin.set(0);
            spi->write(4<<5); // lcd clear
            wait_us(40);
            for (int k = 0; k < 4; ++k) {
                spi->write((2<<5) | 1); // lcd setcursor
                wait_us(40);
                spi->write( (k << 5) | (0 & 0x1F));
                wait_us(40);

                uint8_t cmd = (3<<5) | (10 & 0x1F);
                spi->write(cmd);
                wait_us(40);
                for (int i = 0; i < 10; ++i) {
                    spi->write(a+i);
                    wait_us(40);
                }
            }
            if(++a > 'Z'-20) a= 'A';
            cs_pin.set(1);
        }
        if(busy_pin.get() == 1) {
            stream->printf("BUSY..............\n");
        }
        // wait 20ms
        uint32_t start = us_ticker_read();
        while ((us_ticker_read() - start) < 20*1000) {
            THEKERNEL->call_event(ON_IDLE);
        }

    }
    delete spi;
    stream->printf("Done testing SPI\n");
#endif
}


