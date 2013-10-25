#include "uip.h"
#include "psock.h"
#include <string.h>

typedef struct console_state {
    struct psock p;
    char inputbuffer[80];
    u16_t sentlen;
    u16_t sendptr;
} console_state_t;

void handle_connection(console_state_t *s, char **cmd) {
    PSOCK_BEGIN(&s->p);
    if(cmd == NULL) {
        PSOCK_CLOSE(&s->p);
        PSOCK_EXIT(&s->p);
    }
    uip_log("sending prompt\n");
    PSOCK_SEND_STR(&s->p, "cmd> ");
    uip_log("sent prompt\n");
    PSOCK_READTO(&s->p, '\n');
    uip_log("read command\n");
    int n= PSOCK_DATALEN(&s->p);
    s->inputbuffer[n-1]= 0;
    //PSOCK_SEND_STR(&s->p, "Ok ");
    *cmd= s->inputbuffer;
    PSOCK_END(&s->p);
}

void console_connected(console_state_t *s) {
    PSOCK_INIT(&s->p, s->inputbuffer, sizeof(s->inputbuffer));
}

void console_send_data(console_state_t *s, const char* data) {
    int i;
    PSOCK_BEGIN(&s->p);
    PSOCK_SEND_STR(&s->p, data);
    PSOCK_END(&s->p);
}
