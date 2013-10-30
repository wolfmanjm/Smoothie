// EthernetStream cpp
//

#include "EthernetStream.h"
#include "Kernel.h"
#include "shell.h"

EthernetStream::EthernetStream()
{
}

int EthernetStream::puts(const char *s)
{
    int len = strlen(s);
    // check if full and idle_loop until not
    if(shell_has_space() == -1) return len; // closed so act as null

    while(shell_has_space() < 4){
        // call idle until we can output more
        THEKERNEL->call_event(ON_IDLE);
    }

    shell_response(s);
    return len;
}

/*
// Then in your code
//

// First create your Stream object and store it

this->ethernet_stream = new EthernetStream();

// When you send something into the system, pass your Stream along

struct SerialMessage message;
message.message = received;
message.stream = this->ethernet_stream;
this->kernel->call_event(ON_CONSOLE_LINE_RECEIVED, &message );

// And then you have to periodically check if you have something to send back

if( this->ethernet_stream->to_send.size() > 0 ){
    this->send_string_back( this->ethernet_stream->to_send );
}
*/
