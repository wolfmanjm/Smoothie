// EthernetStream cpp
//

#include "NetworkStream.h"
#include "Kernel.h"
#include "shell.h"

int NetworkStream::puts(const char *s)
{
    if(s == NULL) return (*callback)(NULL);

    int len = strlen(s);
    int n;
    do {
        // call this streams result callback
        n= (*callback)(s);

        // if closed just pretend we sent it
        if(n == -1) return len;
        if(n == 0) {
            // if output queue is full
            // call idle until we can output more
            THEKERNEL->call_event(ON_IDLE);
        }
    } while(n == 0);

    return len;
}

