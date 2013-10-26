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

struct ptentry {
    uint16_t command_cs;
    void (* pfunc)(char *str);
};

#define SHELL_PROMPT "> "

/*---------------------------------------------------------------------------*/
static void
parse(register char *str, struct ptentry *t)
{
    struct ptentry *p;
    for (p = t; p->command_cs != 0; ++p) {
        if (get_checksum(str) == p->command_cs) {
            break;
        }
    }

    p->pfunc(str);
}
/*---------------------------------------------------------------------------*/
static void
inttostr(register char *str, unsigned int i)
{
    str[0] = '0' + i / 100;
    if (str[0] == '0') {
        str[0] = ' ';
    }
    str[1] = '0' + (i / 10) % 10;
    if (str[0] == ' ' && str[1] == '0') {
        str[1] = ' ';
    }
    str[2] = '0' + i % 10;
    str[3] = ' ';
    str[4] = 0;
}
/*---------------------------------------------------------------------------*/
static void
help(char *str)
{
    shell_output("Available commands:", "");
    shell_output("stats   - show network statistics", "");
    shell_output("conn    - show TCP connections", "");
    shell_output("help, ? - show help", "");
    shell_output("exit    - exit shell", "");
}

/*---------------------------------------------------------------------------*/
static void connections(char *str)
{
    char istr[5];
    struct uip_conn* uip_connr;
    shell_output("Current TCP connections: ", "");
    for(uip_connr = &uip_conns[0]; uip_connr <= &uip_conns[UIP_CONNS - 1]; ++uip_connr) {
        if(uip_connr->tcpstateflags != UIP_CLOSED) {
            inttostr(istr, HTONS(uip_connr->lport));
            shell_output(istr, "");
        }
    }
}

/*---------------------------------------------------------------------------*/
static void
unknown(char *str)
{
    if (strlen(str) > 0) {
        shell_output("Unknown command: ", str);
    }
}
/*---------------------------------------------------------------------------*/
static struct ptentry parsetab[] = {
    {CHECKSUM("stats"), help},
    {CHECKSUM("conn"), connections},
    {CHECKSUM("help"), help},
    {CHECKSUM("exit"), shell_quit},
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
shell_start(void)
{
    shell_output("Smoothie command shell", "");
    shell_prompt(SHELL_PROMPT);
}
/*---------------------------------------------------------------------------*/
void
shell_input(char *cmd)
{
    parse(cmd, parsetab);
    shell_prompt(SHELL_PROMPT);
}
/*---------------------------------------------------------------------------*/
