/*
 * Copyright (c) 2003, Adam Dunkels.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file is part of the uIP TCP/IP stack
 *
 * $Id: telnetd.c,v 1.2 2006/06/07 09:43:54 adam Exp $
 *
 */

#include "uip.h"
#include "telnetd.h"
#include "shell.h"

#include <string.h>
#include <stdlib.h>

#define ISO_nl       0x0a
#define ISO_cr       0x0d

#define STATE_NORMAL 0
#define STATE_IAC    1
#define STATE_WILL   2
#define STATE_WONT   3
#define STATE_DO     4
#define STATE_DONT   5
#define STATE_CLOSE  6

// FIXME this should be stored in uip_conn->appstate so more than one telnet session can happen
static struct telnetd_state *s= NULL;
static int prompt= 1;

#define TELNET_IAC   255
#define TELNET_WILL  251
#define TELNET_WONT  252
#define TELNET_DO    253
#define TELNET_DONT  254

#define TELNET_X_PROMPT 0x55

/*---------------------------------------------------------------------------*/
static char *
alloc_line(int size)
{
    return malloc(size);
}
/*---------------------------------------------------------------------------*/
static void
dealloc_line(char *line)
{
    free(line);
}
/*---------------------------------------------------------------------------*/
void
shell_quit(char *str)
{
    s->state = STATE_CLOSE;
}
/*---------------------------------------------------------------------------*/
static void
sendline(char *line)
{
    unsigned int i;

    for (i = 0; i < TELNETD_CONF_NUMLINES; ++i) {
        if (s->lines[i] == NULL) {
            s->lines[i] = line;
            break;
        }
    }
    if (i == TELNETD_CONF_NUMLINES) {
        dealloc_line(line);
    }
}
/*---------------------------------------------------------------------------*/
void
shell_prompt(const char *str)
{
    char *line;
    if(prompt == 0 || s == NULL) return;
    line = alloc_line(strlen(str) + 1);
    if (line != NULL) {
        strcpy(line, str);
        sendline(line);
    }
}
/*---------------------------------------------------------------------------*/
void
shell_output(const char *str)
{
    if(s == NULL) return;
    unsigned chunk = 256; // small chunk size so we don't allocate huge blocks, and must be less than mss
    unsigned len = strlen(str);
    char *line;
    if (len < chunk) {
        // can be sent in one tcp buffer
        line = alloc_line(len + 1);
        if (line != NULL) {
            strcpy(line, str);
            sendline(line);
        }
    } else {
        // need to split line over multiple send lines
        int size = chunk; // size to copy
        int off = 0;
        while (len >= chunk) {
            line = alloc_line(chunk + 1);
            if (line != NULL) {
                memcpy(line, str + off, size);
                line[size] = 0;
                sendline(line);
                len -= size;
                off += size;
            }
        }
        if (len > 0) {
            // send rest
            line = alloc_line(len + 1);
            if (line != NULL) {
                strcpy(line, str + off);
                sendline(line);
            }
        }
    }
}
// check if we can queue or if queue is full
int shell_has_space()
{
    int i;
    int cnt = 0;
    if(s == NULL) return -1;
    for (i = 0; i < TELNETD_CONF_NUMLINES; ++i) {
        if (s->lines[i] == NULL) cnt++;
    }
    return cnt;
}

/*---------------------------------------------------------------------------*/
void
telnetd_init(void)
{
    s = NULL;
    uip_listen(HTONS(23));
    shell_init();
}
/*---------------------------------------------------------------------------*/
static void
acked(void)
{
    unsigned int i;

    while (s->numsent > 0) {
        dealloc_line(s->lines[0]);
        for (i = 1; i < TELNETD_CONF_NUMLINES; ++i) {
            s->lines[i - 1] = s->lines[i];
        }
        s->lines[TELNETD_CONF_NUMLINES - 1] = NULL;
        --s->numsent;
    }
}
/*---------------------------------------------------------------------------*/
static void
senddata(void)
{
    // NOTE this sends as many lines as it can fit in one tcp frame
    // we need to keep the lines under the size of the tcp frame
    char *bufptr, *lineptr;
    int buflen, linelen;

    bufptr = uip_appdata;
    buflen = 0;
    for (s->numsent = 0; s->numsent < TELNETD_CONF_NUMLINES && s->lines[s->numsent] != NULL ; ++s->numsent) {
        lineptr = s->lines[s->numsent];
        linelen = strlen(lineptr);
        if (buflen + linelen < uip_mss()) {
            memcpy(bufptr, lineptr, linelen);
            bufptr += linelen;
            buflen += linelen;
        } else {
            break;
        }
    }
    uip_send(uip_appdata, buflen);
}
/*---------------------------------------------------------------------------*/
static void
closed(void)
{
    unsigned int i;

    for (i = 0; i < TELNETD_CONF_NUMLINES; ++i) {
        if (s->lines[i] != NULL) {
            dealloc_line(s->lines[i]);
        }
    }
    shell_stop();
}
/*---------------------------------------------------------------------------*/
static void
get_char(u8_t c)
{
    if (c == ISO_cr) {
        return;
    }

    s->buf[(int)s->bufptr] = c;
    if (s->buf[(int)s->bufptr] == ISO_nl || s->bufptr == sizeof(s->buf) - 1) {
        if (s->bufptr > 0) {
            s->buf[(int)s->bufptr] = 0;
        }
        shell_input(s->buf);
        s->bufptr = 0;
    } else {
        ++s->bufptr;
    }
}
/*---------------------------------------------------------------------------*/
static void
sendopt(u8_t option, u8_t value)
{
    char *line;
    line = alloc_line(4);
    if (line != NULL) {
        line[0] = TELNET_IAC;
        line[1] = option;
        line[2] = value;
        line[3] = 0;
        sendline(line);
    }
}
/*---------------------------------------------------------------------------*/
static void
newdata(void)
{
    u16_t len;
    u8_t c;
    char *dataptr;

    len = uip_datalen();
    dataptr = (char *)uip_appdata;

    while (len > 0 && s->bufptr < sizeof(s->buf)) {
        c = *dataptr;
        ++dataptr;
        --len;
        switch (s->state) {
            case STATE_IAC:
                if (c == TELNET_IAC) {
                    get_char(c);
                    s->state = STATE_NORMAL;
                } else {
                    switch (c) {
                        case TELNET_WILL:
                            s->state = STATE_WILL;
                            break;
                        case TELNET_WONT:
                            s->state = STATE_WONT;
                            break;
                        case TELNET_DO:
                            s->state = STATE_DO;
                            break;
                        case TELNET_DONT:
                            s->state = STATE_DONT;
                            break;
                        default:
                            s->state = STATE_NORMAL;
                            break;
                    }
                }
                break;
            case STATE_WILL:
                /* Reply with a DONT */
                sendopt(TELNET_DONT, c);
                s->state = STATE_NORMAL;
                break;

            case STATE_WONT:
                /* Reply with a DONT */
                sendopt(TELNET_DONT, c);
                s->state = STATE_NORMAL;
                break;
            case STATE_DO:
               if (c == TELNET_X_PROMPT) {
                    prompt= 1;
                }else{
                     /* Reply with a WONT */
                    sendopt(TELNET_WONT, c);
                }
                s->state = STATE_NORMAL;
                break;
            case STATE_DONT:
                if (c == TELNET_X_PROMPT) {
                    prompt= 0;
                }else{
                    /* Reply with a WONT */
                    sendopt(TELNET_WONT, c);
                }
                s->state = STATE_NORMAL;
                break;
            case STATE_NORMAL:
                if (c == TELNET_IAC) {
                    s->state = STATE_IAC;
                } else {
                    get_char(c);
                }
                break;
        }
    }

    // if the command queue is getting too big we stop TCP
    if(shell_queue_size() > 10) {
        uip_stop();
    }
}
/*---------------------------------------------------------------------------*/
void
telnetd_appcall(void)
{
    unsigned int i;
    if (uip_connected()) {
        if (s != NULL) free(s);
        s = malloc(sizeof(struct telnetd_state));
        for (i = 0; i < TELNETD_CONF_NUMLINES; ++i) {
            s->lines[i] = NULL;
        }
        s->bufptr = 0;
        s->state = STATE_NORMAL;
        prompt= 1;
        shell_start();
    }

    if (s->state == STATE_CLOSE) {
        s->state = STATE_NORMAL;
        uip_close();
        return;
    }

    if (uip_closed() || uip_aborted() || uip_timedout()) {
        if (s != NULL) {
            closed();
            free(s);
            s = NULL;
        }
        return;
    }


    if (uip_acked()) {
        acked();
    }

    if (uip_newdata()) {
        newdata();
    }


    if (uip_rexmit() || uip_newdata() || uip_acked() || uip_connected() || uip_poll()) {
        senddata();
    }

    if(uip_poll() && uip_stopped(uip_conn) && shell_queue_size() < 5) {
        uip_restart();
    }
}
/*---------------------------------------------------------------------------*/
