#ifndef ETHERNETSTREAM_H
#define ETHERNETSTREAM_H

#include <string>
#include "libs/StreamOutput.h"


class EthernetStream : public StreamOutput {
	public:
		EthernetStream();
		int puts(const char*);

		std::string  to_send;
};

#endif
