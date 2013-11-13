#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

/**
 * \addtogroup apps
 * @{
 */

/**
 * \defgroup httpd Web server
 * @{
 * The uIP web server is a very simplistic implementation of an HTTP
 * server. It can serve web pages and files from a read-only ROM
 * filesystem, and provides a very small scripting language.

 */

/**
 * \file
 *         Web server
 * \author
 *         Adam Dunkels <adam@sics.se>
 */


/*
 * Copyright (c) 2004, Adam Dunkels.
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
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the uIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: httpd.c,v 1.2 2006/06/11 21:46:38 adam Exp $
 */

#include <stdio.h>

#include "uip.h"
#include "httpd.h"
#include "httpd-fs.h"
#include "http-strings.h"

#include <string.h>
#include "stdio.h"
#include "stdlib.h"

#include "CommandQueue.h"

#include "c-fifo.h"

#define STATE_WAITING 0
#define STATE_HEADERS 1
#define STATE_BODY    2
#define STATE_OUTPUT  3
#define STATE_UPLOAD  4

#define GET  1
#define POST 2

#define ISO_nl      0x0a
#define ISO_space   0x20
#define ISO_bang    0x21
#define ISO_percent 0x25
#define ISO_period  0x2e
#define ISO_slash   0x2f
#define ISO_colon   0x3a

#define DEBUG_PRINTF printf
//#define DEBUG_PRINTF(...)

// Used to save files to SDCARD during upload
static FILE *fd;
static char *output_filename= NULL;
static int file_cnt= 0;
static int open_file(const char *fn)
{
    if(output_filename != NULL) free(output_filename);
    output_filename= malloc(strlen(fn)+5);
    strcpy(output_filename, "/sd/");
    strcat(output_filename, fn);
    fd= fopen(output_filename, "w");
    if(fd == NULL) return 0;
    return 1;
}

static int save_file(char *buf, unsigned int len)
{
    if(fwrite(buf, 1, len, fd) == len){
        file_cnt+= len;
        // HACK alert work around bug causing file corruption when writing large amounts of data
        if(file_cnt >= 400) {
            file_cnt= 0;
            fclose(fd);
            fd= fopen(output_filename, "a");
        }
        return 1;
    }

    return 0;
}

static int close_file()
{
    free(output_filename);
    fclose(fd);
    return 1;
}

/*---------------------------------------------------------------------------*/
static PT_THREAD(send_command_response(struct httpd_state *s))
{
    PSOCK_BEGIN(&s->sout);

    do {
        PSOCK_WAIT_UNTIL( &s->sout, fifo_size() > 0 );
        s->strbuf= fifo_pop();
        if(s->strbuf == NULL) break;
        // send it
        DEBUG_PRINTF("Sending response: %s", s->strbuf);
        PSOCK_SEND_STR(&s->sout, s->strbuf);
        // free the strdup
        free(s->strbuf);
    }while(1);

    PSOCK_END(&s->sout);
}

/*---------------------------------------------------------------------------*/
static unsigned short
generate_part_of_file(void *state)
{
    struct httpd_state *s = (struct httpd_state *)state;

    if (s->file.len > uip_mss()) {
        s->len = uip_mss();
    } else {
        s->len = s->file.len;
    }
    memcpy(uip_appdata, s->file.data, s->len);

    return s->len;
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(send_file(struct httpd_state *s))
{
    PSOCK_BEGIN(&s->sout);

    do {
        PSOCK_GENERATOR_SEND(&s->sout, generate_part_of_file, s);
        s->file.len -= s->len;
        s->file.data += s->len;
    } while (s->file.len > 0);

    PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(send_headers(struct httpd_state *s, const char *statushdr))
{
    char *ptr;

    PSOCK_BEGIN(&s->sout);

    PSOCK_SEND_STR(&s->sout, statushdr);

    ptr = strrchr(s->filename, ISO_period);
    if (ptr == NULL) {
        PSOCK_SEND_STR(&s->sout, http_content_type_plain); // http_content_type_binary);
    } else if (strncmp(http_html, ptr, 5) == 0 || strncmp(http_shtml, ptr, 6) == 0) {
        PSOCK_SEND_STR(&s->sout, http_content_type_html);
    } else if (strncmp(http_css, ptr, 4) == 0) {
        PSOCK_SEND_STR(&s->sout, http_content_type_css);
    } else if (strncmp(http_png, ptr, 4) == 0) {
        PSOCK_SEND_STR(&s->sout, http_content_type_png);
    } else if (strncmp(http_gif, ptr, 4) == 0) {
        PSOCK_SEND_STR(&s->sout, http_content_type_gif);
    } else if (strncmp(http_jpg, ptr, 4) == 0) {
        PSOCK_SEND_STR(&s->sout, http_content_type_jpg);
    } else {
        PSOCK_SEND_STR(&s->sout, http_content_type_plain);
    }
    PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(handle_output(struct httpd_state *s))
{
    PT_BEGIN(&s->outputpt);

    if(s->method == POST && strcmp(s->filename, "/command") == 0) {
        DEBUG_PRINTF("Executing command post: %s\n", s->command);
        // stick the command  on the command queue
        network_add_command(s->command, 1);

        PT_WAIT_THREAD(&s->outputpt, send_headers(s, http_header_200));
        // then send response as we get it
        PT_WAIT_THREAD(&s->outputpt, send_command_response(s));

    }else if(s->method == POST && strcmp(s->filename, "/upload") == 0) {
        DEBUG_PRINTF("upload output: %d\n", s->uploadok);
        if(s->uploadok == 0) {
            PT_WAIT_THREAD(&s->outputpt, send_headers(s, http_header_503));
            PSOCK_SEND_STR(&s->sout, "FAILED\r\n");
        }else{
            PT_WAIT_THREAD(&s->outputpt, send_headers(s, http_header_200));
            PSOCK_SEND_STR(&s->sout, "OK\r\n");
        }

    }else if (!httpd_fs_open(s->filename, &s->file)) {
        DEBUG_PRINTF("404 file not found\n");
        httpd_fs_open(http_404_html, &s->file);
        strcpy(s->filename, http_404_html);
        PT_WAIT_THREAD(&s->outputpt,
                       send_headers(s,
                                    http_header_404));
        PT_WAIT_THREAD(&s->outputpt,
                       send_file(s));
    } else {
        DEBUG_PRINTF("sending file %s\n", s->filename);
        PT_WAIT_THREAD(&s->outputpt,
                       send_headers(s,
                                    http_header_200));

        PT_WAIT_THREAD(&s->outputpt, send_file(s));
    }
    PSOCK_CLOSE(&s->sout);
    PT_END(&s->outputpt);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(handle_input(struct httpd_state *s))
{
    PSOCK_BEGIN(&s->sin);

    PSOCK_READTO(&s->sin, ISO_space);

    if (strncmp(s->inputbuf, http_get, 4) == 0) {
        s->method = GET;
    } else if (strncmp(s->inputbuf, http_post, 4) == 0) {
        s->method = POST;
    } else {
        DEBUG_PRINTF("Unexpected method: %s\n", s->inputbuf);
        PSOCK_CLOSE_EXIT(&s->sin);
    }
    DEBUG_PRINTF("Method: %s\n", s->inputbuf);

    PSOCK_READTO(&s->sin, ISO_space);

    if (s->inputbuf[0] != ISO_slash) {
        PSOCK_CLOSE_EXIT(&s->sin);
    }


    if (s->inputbuf[1] == ISO_space) {
        strncpy(s->filename, http_index_html, sizeof(s->filename));
    } else {
        s->inputbuf[PSOCK_DATALEN(&s->sin) - 1] = 0;
        strncpy(s->filename, &s->inputbuf[0], sizeof(s->filename));
    }

    DEBUG_PRINTF("filename: %s\n", s->filename);

    /*  httpd_log_file(uip_conn->ripaddr, s->filename);*/

    s->state = STATE_HEADERS;
    s->content_length= 0;
    while (1) {
        if(s->state == STATE_HEADERS) {
            // read the headers of the request
            PSOCK_READTO(&s->sin, ISO_nl);
            s->inputbuf[PSOCK_DATALEN(&s->sin) - 1] = 0;
            if(s->inputbuf[0] == '\r') {
                DEBUG_PRINTF("end of headers\n");
                if(s->method == GET) {
                    s->state = STATE_OUTPUT;
                    break;
                }else if(s->method == POST) {
                    if(strcmp(s->filename, "/upload") == 0) {
                        s->state= STATE_UPLOAD;
                    }else{
                        s->state= STATE_BODY;
                    }
                }
            }else{
                DEBUG_PRINTF("reading header: %s\n", s->inputbuf);
                // handle headers here
                if(strncmp(s->inputbuf, http_content_length, sizeof(http_content_length)-1) == 0) {
                    s->inputbuf[PSOCK_DATALEN(&s->sin) - 2] = 0;
                    s->content_length= atoi(&s->inputbuf[sizeof(http_content_length)-1]);
                    DEBUG_PRINTF("Content length= %s, %d\n", &s->inputbuf[sizeof(http_content_length)-1], s->content_length);

                }else if(strncmp(s->inputbuf, "X-Filename: ", 11) == 0) {
                    s->inputbuf[PSOCK_DATALEN(&s->sin) - 2] = 0;
                    strncpy(s->upload_name, &s->inputbuf[12], sizeof(s->upload_name)-1);
                    DEBUG_PRINTF("Upload name= %s\n", s->upload_name);
                }
            }

        }else if(s->state == STATE_BODY) {
            // read the Body of the request
            if(s->content_length > 0) {
                DEBUG_PRINTF("start reading body %d...\n", s->content_length);
                while(s->content_length > 2) {
                    PSOCK_READTO(&s->sin, ISO_nl);
                    s->inputbuf[PSOCK_DATALEN(&s->sin) - 1] = 0;
                    s->content_length -= PSOCK_DATALEN(&s->sin);
                    DEBUG_PRINTF("read body: %s, %d\n", s->inputbuf, s->content_length);
                }
                strncpy(s->command, s->inputbuf, sizeof(s->command));
                DEBUG_PRINTF("Read body: %s\n", s->command);
                s->state = STATE_OUTPUT;

            }else{
                s->state = STATE_OUTPUT;
            }
            break;

        }else if(s->state == STATE_UPLOAD) {
            DEBUG_PRINTF("Uploading file: %s, %d\n", s->upload_name, s->content_length);

            // The body is the raw data to be stored to the file
            if(!open_file(s->upload_name)) {
                DEBUG_PRINTF("failed to open file\n");
                s->uploadok= 0;
            }else{
                while(s->content_length > 0) {
                    // TODO convert to a PTHREAD and read buffer directly, then binary files can be uploaded
                    // NOTE this also truncates long lines to 132 characters sizeof(inputbuf)
                    PSOCK_READTO(&s->sin, ISO_nl);
                    int n= PSOCK_DATALEN(&s->sin);
                    if(!save_file(s->inputbuf, n)) break;
                    s->content_length -= n;
                }
                close_file();
                s->uploadok= s->content_length == 0 ? 1 : 0;
                DEBUG_PRINTF("finished upload status %d\n", s->uploadok);
            }
            s->state = STATE_OUTPUT;
            break;

        }else {
            DEBUG_PRINTF("WTF State: %d", s->state);
            break;
        }
    }

    PSOCK_END(&s->sin);
}
/*---------------------------------------------------------------------------*/
static void
handle_connection(struct httpd_state *s)
{
    if (s->state != STATE_OUTPUT){
        handle_input(s);
    }
    if (s->state == STATE_OUTPUT) {
        handle_output(s);
    }
}
/*---------------------------------------------------------------------------*/
void
httpd_appcall(void)
{
    struct httpd_state *s = (struct httpd_state *)(uip_conn->appstate);

    if (uip_closed() || uip_aborted() || uip_timedout()) {
        if (s != NULL) {
            free(s);
            uip_conn->appstate = NULL;
        }

    } else if (uip_connected()) {
        s = malloc(sizeof(struct httpd_state));
        uip_conn->appstate = s;
        DEBUG_PRINTF("Connection: %d.%d.%d.%d\n", uip_ipaddr1(uip_conn->ripaddr), uip_ipaddr2(uip_conn->ripaddr), uip_ipaddr3(uip_conn->ripaddr), uip_ipaddr4(uip_conn->ripaddr));

        PSOCK_INIT(&s->sin, s->inputbuf, sizeof(s->inputbuf) - 1);
        PSOCK_INIT(&s->sout, s->inputbuf, sizeof(s->inputbuf) - 1);
        PT_INIT(&s->outputpt);
        s->state = STATE_WAITING;
        /*    timer_set(&s->timer, CLOCK_SECOND * 100);*/
        s->timer = 0;
        handle_connection(s);

    } else if (s != NULL) {
        if (uip_poll()) {
            ++s->timer;
            if (s->timer >= 20*2) { // we have a 0.5 second poll and we want 20 second timeout
                DEBUG_PRINTF("Timer expired, aborting\n");
                uip_abort();
                return;
            }
        } else {
            s->timer = 0;
        }
        handle_connection(s);

    } else {
        uip_abort();
    }
}
// this callback gets the results of a command, line by line
// need to check if we need to stall the upstream sender
// return 0 if stalled 1 if ok to keep providing more
// -1 if the connection has closed or is not in output state
// NOTE may need to see which connection to send to if more than one
static int command_result(const char *str)
{
    if(str == NULL) {
        DEBUG_PRINTF("End of command\n");
        fifo_push(NULL);

    }else{
        DEBUG_PRINTF("Got command result: %s", str);
        if(fifo_size() < 10) {
            fifo_push(strdup(str));
            return 1;
        }else{
            return 0;
        }
    }
    return 1;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      Initialize the web server
 *
 *             This function initializes the web server and should be
 *             called at system boot-up.
 */
void
httpd_init(void)
{
    uip_listen(HTONS(80));
    register_callback(command_result, 1);
}
/*---------------------------------------------------------------------------*/
/** @} */
