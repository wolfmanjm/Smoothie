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

#include "stdlib.h"
#include "shell.h"
#include "uip.h"
#include <string.h>
#include "checksumm.h"
#include "utils.h"
#include "stdio.h"
#include "stdlib.h"
#include "telnetd.h"

struct ptentry {
    uint16_t command_cs;
    void (* pfunc)(char *str, Telnetd *telnet);
};

#define SHELL_PROMPT "> "

/*---------------------------------------------------------------------------*/
bool Shell::parse(register char *str, struct ptentry *t)
{
    struct ptentry *p;
    for (p = t; p->command_cs != 0; ++p) {
        if (get_checksum(str) == p->command_cs) {
            break;
        }
    }

    p->pfunc(str, telnet);

    return p->command_cs != 0;
}
/*---------------------------------------------------------------------------*/
static void help(char *str, Telnetd *telnet)
{
    telnet->output("Available commands: All others are passed on\n");
    telnet->output("netstat     - show network info\n");
    telnet->output("?           - show network help\n");
    telnet->output("help        - show command help\n");
    telnet->output("exit, quit  - exit shell\n");
}

/*---------------------------------------------------------------------------*/
static const char *states[] = {
  "CLOSED",
  "SYN_RCVD",
  "SYN_SENT",
  "ESTABLISHED",
  "FIN_WAIT_1",
  "FIN_WAIT_2",
  "CLOSING",
  "TIME_WAIT",
  "LAST_ACK",
  "NONE",
  "RUNNING",
  "CALLED"
};
static void connections(char *str, Telnetd *telnet)
{
    char istr[128];
    struct uip_conn* connr;
    snprintf(istr, sizeof(istr), "Initial MSS: %d, MSS: %d\n", uip_initialmss(), uip_mss());
    telnet->output(istr);
    telnet->output("Current connections: \n");

    for(connr = &uip_conns[0]; connr <= &uip_conns[UIP_CONNS - 1]; ++connr) {
        snprintf(istr, sizeof(istr), "%d, %u.%u.%u.%u:%u, %s, %u, %u, %c %c\n",
            HTONS(connr->lport),
            uip_ipaddr1(connr->ripaddr), uip_ipaddr2(connr->ripaddr),  uip_ipaddr3(connr->ripaddr), uip_ipaddr4(connr->ripaddr),
            HTONS(connr->rport),
            states[connr->tcpstateflags & UIP_TS_MASK],
            connr->nrtx,
            connr->timer,
            (uip_outstanding(connr))? '*':' ',
            (uip_stopped(connr))? '!':' ');

        telnet->output(istr);
    }
}

static void quit(char *str, Telnetd *telnet)
{
    telnet->close();
}

//#include "clock.h"
static void test(char *str, Telnetd *telnet)
{
    printf("In Test\n");

    // struct timer t;
    // u16_t ticks=  CLOCK_SECOND*5;
    // timer_set(&t, ticks);
    // printf("Wait....\n");
    // while(!timer_expired(&t)) {

    // }
    // printf("Done\n");
/*
    const char *fn= "/sd/test6.txt";
    uint16_t *buf= (uint16_t *)malloc(200*2);
    int cnt= 0;
    FILE *fp;
    for(int i=0;i<10;i++) {
        fp= fopen(fn, i == 0 ? "w" : "a");
        if(fp == NULL) {
            printf("failed to open file\n");
            return;
        }
        for (int x = 0; x < 200; ++x) {
            buf[x]= x+cnt;
        }
        cnt+=200;
        int n= fwrite(buf, 2, 200, fp);
        printf("wrote %d, %d\n", i, n);
        fclose(fp);
    }

    fp= fopen(fn, "r");
    if(fp == NULL) {
        printf("failed to open file for read\n");
        return;
    }
    printf("Opened file %s for read\n", fn);
    do {
        int n= fread(buf, 2, 200, fp);
        if(n <= 0) break;
        for(int x=0;x<n;x++) {
            printf("%04X, ", buf[x]);
        }
    }while(1);
    fclose(fp);
    free(buf);
    */
}

/*---------------------------------------------------------------------------*/

static void unknown(char *str, Telnetd *telnet)
{
    // its some other command, so queue it for mainloop to find
    if (strlen(str) > 0) {
        CommandQueue::getInstance()->add(str, 2);
    }
}
/*---------------------------------------------------------------------------*/
static struct ptentry parsetab[] = {
    {CHECKSUM("netstat"), connections},
    {CHECKSUM("exit"), quit},
    {CHECKSUM("quit"), quit},
    {CHECKSUM("test"), test},
    {CHECKSUM("?"), help},

    /* Default action */
    {0, unknown}
};
/*---------------------------------------------------------------------------*/
// this callback gets the results of a command, line by line
// NULL means command completed
// static
int Shell::command_result(const char *str, void *ti)
{
    // FIXME problem when telnet is deleted and this gets called from slow command
    // need a way to know telnet was closed
    Telnetd *telnet= (Telnetd*)ti;
    if(str == NULL) {
        // indicates command is complete
        // only prompt when command is completed
       telnet->output_prompt(SHELL_PROMPT);
       return 0;

    }else{
        if(telnet->can_output()) {
            if(telnet->output(str) == -1) return -1; // connection was closed
            return 1;
        }
        // we are stalled
        return 0;
    }
}

void Shell::init(void)
{
    // FIXME need to allow multiple callbacks with different telnets
    CommandQueue::getInstance()->registerCallback(command_result, 2, telnet);
}

/*---------------------------------------------------------------------------*/
void Shell::start()
{
    telnet->output("Smoothie command shell\r\n> ");
}

int Shell::queue_size()
{
   return CommandQueue::getInstance()->size();
}
/*---------------------------------------------------------------------------*/
void Shell::input(char *cmd)
{
    if(parse(cmd, parsetab)) {
        telnet->output_prompt(SHELL_PROMPT);
    }
}
/*---------------------------------------------------------------------------*/
