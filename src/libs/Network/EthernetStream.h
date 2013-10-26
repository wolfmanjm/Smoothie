#ifndef ETHERNETSTREAM_H
#define ETHERNETSTREAM_H

#include "libs/StreamOutput.h"

class EthernetStream : public StreamOutput {
	public:
		EthernetStream();
		int puts(const char*);
};

#endif
