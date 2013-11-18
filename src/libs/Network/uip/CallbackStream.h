#ifndef CALLBACKSTREAM_H
#define CALLBACKSTREAM_H

typedef int (*cb_t)(const char *, void *);

#ifdef __cplusplus
#include "libs/StreamOutput.h"


class CallbackStream : public StreamOutput {
    public:
        CallbackStream(cb_t cb, void *u) { callback= cb; user= u; closed= false; };
        int puts(const char*);

    private:
        cb_t callback;
        void *user;
        bool closed;
};

#else

extern void *new_callback_stream(cb_t cb, void *);
extern void delete_callback_stream(void *);

#endif // __cplusplus

#endif
