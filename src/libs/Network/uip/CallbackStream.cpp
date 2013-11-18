#include "CallbackStream.h"
#include "Kernel.h"

int CallbackStream::puts(const char *s)
{
    if(closed) return 0;

    if(s == NULL) return (*callback)(NULL, user);

    int len = strlen(s);
    int n;
    do {
        // call this streams result callback
        n= (*callback)(s, user);

        // if closed just pretend we sent it
        if(n == -1) {
            closed= true;
            return len;

        }else if(n == 0) {
            // if output queue is full
            // call idle until we can output more
            THEKERNEL->call_event(ON_IDLE);
        }
    } while(n == 0);

    return len;
}

extern "C" void *new_callback_stream(cb_t cb, void *u)
{
    return new CallbackStream(cb, u);
}

extern "C" void delete_callback_stream(void *p)
{
    delete (CallbackStream*)p;
}
