// EthernetStream cpp
//

#include "EthernetStream.h"

EthernetStream::EthernetStream(){
	to_send= "";
}

int EthernetStream::puts(const char* s){
    int len= strlen(s);
	this->to_send.append(s, len);
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
