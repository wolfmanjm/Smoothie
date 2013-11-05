#ifndef NETWORKSTREAM_H
#define NETWORKSTREAM_H

#include "libs/StreamOutput.h"


class NetworkStream : public StreamOutput {
    public:
        typedef int (*cb_t)(const char *);
        NetworkStream(cb_t cb) { callback= cb; };
        int puts(const char*);

    private:
        cb_t callback;

};

#endif
