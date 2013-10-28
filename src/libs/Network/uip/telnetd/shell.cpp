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
* This file is part of the uIP TCP/IP stack.
*
* $Id: shell.c,v 1.1 2006/06/07 09:43:54 adam Exp $
*
*/

#include "shell.h"
#include "uip.h"
#include <string.h>
#include "checksumm.h"
#include "utils.h"
#include "stdio.h"
#include "string"

struct ptentry {
    uint16_t command_cs;
    void (* pfunc)(char *str);
};

static std::string command_q;

#define SHELL_PROMPT "> "

/*---------------------------------------------------------------------------*/
static bool parse(register char *str, struct ptentry *t)
{
    struct ptentry *p;
    for (p = t; p->command_cs != 0; ++p) {
        if (get_checksum(str) == p->command_cs) {
            break;
        }
    }

    p->pfunc(str);

    return p->command_cs != 0;
}
/*---------------------------------------------------------------------------*/
static void
help(char *str)
{
    shell_output("Available commands: All others are passed on\n");
    shell_output("conn        - show TCP connections\n");
    shell_output("?           - show network help\n");
    shell_output("help        - show command help\n");
    shell_output("exit, quit  - exit shell\n");
}

/*---------------------------------------------------------------------------*/
static void connections(char *str)
{
    char istr[16];
    struct uip_conn* uip_connr;
    snprintf(istr, sizeof(istr), "MSS: %d\n", uip_mss());
    shell_output(istr);
    shell_output("Current TCP connections: \n");
    for(uip_connr = &uip_conns[0]; uip_connr <= &uip_conns[UIP_CONNS - 1]; ++uip_connr) {
        if(uip_connr->tcpstateflags != UIP_CLOSED) {
            snprintf(istr, sizeof(istr), "%d", HTONS(uip_connr->lport));
            shell_output(istr); shell_output("\n");
        }
    }
}

//#include "clock.h"
static void shell_test(char *str)
{
    printf("In Test\n");
    // struct timer t;
    // u16_t ticks=  CLOCK_SECOND*5;
    // timer_set(&t, ticks);
    // printf("Wait....\n");
    // while(!timer_expired(&t)) {

    // }
    // printf("Done\n");
}

/*---------------------------------------------------------------------------*/
static void
unknown(char *str)
{
    // its some other command, so queue it for mainloop to find
    if (strlen(str) > 0) {
        command_q= str;
    }
}
/*---------------------------------------------------------------------------*/
static struct ptentry parsetab[] = {
    {CHECKSUM("conn"), connections},
    {CHECKSUM("exit"), shell_quit},
    {CHECKSUM("quit"), shell_quit},
    {CHECKSUM("test"), shell_test},
    {CHECKSUM("?"), help},

    /* Default action */
    {0, unknown}
};
/*---------------------------------------------------------------------------*/
void
shell_init(void)
{
}
/*---------------------------------------------------------------------------*/
void
shell_start()
{
    command_q.clear();
    shell_output("Smoothie command shell\n");
    shell_prompt(SHELL_PROMPT);
}
/*---------------------------------------------------------------------------*/
void
shell_input(char *cmd)
{
    if(parse(cmd, parsetab)) {
        shell_prompt(SHELL_PROMPT);
    }
}
/*---------------------------------------------------------------------------*/
const char *shell_get_command()
{
    if(!command_q.empty()) {
        return command_q.c_str();
    }
    return NULL;
}

void shell_got_command()
{
    command_q.clear();
}

void shell_response(const char *resp)
{
    if(resp == NULL) {
        // only prompt when command is completed (except for play without -q)
        shell_prompt(SHELL_PROMPT);
    }else{
        shell_output(resp);
    }
}
