#ifndef CALLBACKSTREAM_H
#define CALLBACKSTREAM_H

#include "libs/StreamOutput.h"


class CallbackStream : public StreamOutput {
    public:
        typedef int (*cb_t)(const char *, void *);
        CallbackStream(cb_t cb) { callback= cb; user= NULL; };
        CallbackStream(cb_t cb, void *u) { callback= cb; user= u; };
        int puts(const char*);

    private:
        cb_t callback;
        void *user;
};

#endif
