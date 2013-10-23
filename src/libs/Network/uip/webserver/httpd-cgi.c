/**
 * \addtogroup httpd
 * @{
 */

/**
 * \file
 *         Web server script interface
 * \author
 *         Adam Dunkels <adam@sics.se>
 *
 */

/*
 * Copyright (c) 2001-2006, Adam Dunkels.
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
 * $Id: httpd-cgi.c,v 1.2 2006/06/11 21:46:37 adam Exp $
 *
 */

#include "uip.h"
#include "psock.h"
#include "httpd.h"
#include "httpd-cgi.h"
#include "httpd-fs.h"

#include <stdio.h>
#include <string.h>
#include "LPC17xx.h"

unsigned int port_stat;		//global variable to toggle IO pin

HTTPD_CGI_CALL(port, "port-status", port_status);
HTTPD_CGI_CALL(toggle, "port-toggle", port_toggle);
HTTPD_CGI_CALL(num1, "port-number1", port_number1);
HTTPD_CGI_CALL(num2, "port-number2", port_number2);
HTTPD_CGI_CALL(adc1, "port-adc1", port_adc1);

static const struct httpd_cgi_call *calls[] = {&port, &toggle, &num1, &num2, &adc1, NULL };

/*---------------------------------------------------------------------------*/
static
PT_THREAD(nullfunction(struct httpd_state *s, char *ptr))
{
  PSOCK_BEGIN(&s->sout);
  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
httpd_cgifunction
httpd_cgi(char *name)
{
  const struct httpd_cgi_call **f;

  /* Find the matching name in the table, return the function. */
  for(f = calls; *f != NULL; ++f) {
    if(strncmp((*f)->name, name, strlen((*f)->name)) == 0) {
      return (*f)->function;
    }
  }
  return nullfunction;
}
/*---------------------------------------------------------------------------*/
static const char closed[] =   /*  "CLOSED",*/
{0x43, 0x4c, 0x4f, 0x53, 0x45, 0x44, 0};
static const char syn_rcvd[] = /*  "SYN-RCVD",*/
{0x53, 0x59, 0x4e, 0x2d, 0x52, 0x43, 0x56,
 0x44,  0};
static const char syn_sent[] = /*  "SYN-SENT",*/
{0x53, 0x59, 0x4e, 0x2d, 0x53, 0x45, 0x4e,
 0x54,  0};
static const char established[] = /*  "ESTABLISHED",*/
{0x45, 0x53, 0x54, 0x41, 0x42, 0x4c, 0x49, 0x53, 0x48,
 0x45, 0x44, 0};
static const char fin_wait_1[] = /*  "FIN-WAIT-1",*/
{0x46, 0x49, 0x4e, 0x2d, 0x57, 0x41, 0x49,
 0x54, 0x2d, 0x31, 0};
static const char fin_wait_2[] = /*  "FIN-WAIT-2",*/
{0x46, 0x49, 0x4e, 0x2d, 0x57, 0x41, 0x49,
 0x54, 0x2d, 0x32, 0};
static const char closing[] = /*  "CLOSING",*/
{0x43, 0x4c, 0x4f, 0x53, 0x49,
 0x4e, 0x47, 0};
static const char time_wait[] = /*  "TIME-WAIT,"*/
{0x54, 0x49, 0x4d, 0x45, 0x2d, 0x57, 0x41,
 0x49, 0x54, 0};
static const char last_ack[] = /*  "LAST-ACK"*/
{0x4c, 0x41, 0x53, 0x54, 0x2d, 0x41, 0x43,
 0x4b, 0};


/*---------------------------------------------------------------------------*/
static unsigned short
generate_port_status(void *arg)
{
	//port_stat = GPIOGetValue(1, 25);
	return snprintf((char *)uip_appdata, UIP_APPDATA_SIZE,
			"%s ",  (port_stat ? "On":"Off"));
}

static
PT_THREAD(port_status(struct httpd_state *s, char *ptr))
{

  PSOCK_BEGIN(&s->sout);
  PSOCK_GENERATOR_SEND(&s->sout, generate_port_status, s);
  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(port_toggle(struct httpd_state *s, char *ptr))
{
  PSOCK_BEGIN(&s->sout);
  //GPIOSetDir(1, 25, 1);
  //GPIOSetValue(1, 25, !port_stat);
  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static unsigned short
generate_port_number1(void *arg)
{

	unsigned int port_number1;
	//GPIOSetDir(1, 26, 0);
	//GPIOSetPull(1, 26, 1);
	//port_number1 = !GPIOGetValue(1, 26);
	return snprintf((char *)uip_appdata, UIP_APPDATA_SIZE,
			"%s ",  (port_number1 ? "On":"Off"));
}

/*---------------------------------------------------------------------------*/
static PT_THREAD(port_number1(struct httpd_state *s, char *ptr))
{

  PSOCK_BEGIN(&s->sout);
  PSOCK_GENERATOR_SEND(&s->sout, generate_port_number1, s);
  PSOCK_END(&s->sout);
}

/*---------------------------------------------------------------------------*/

static unsigned short
generate_port_number2(void *arg)
{
	unsigned int port_number2;		//Variable for readout
	//GPIOSetDir(1, 27, 0);			//Make GPIO pin an input
	//GPIOSetPull(1, 27, 1);			//Enable the internal pullup resistor
	//port_number2 = !GPIOGetValue(1, 27);	//Read the GPIO pin
	return snprintf((char *)uip_appdata, UIP_APPDATA_SIZE,
			"%s ",  (port_number2 ? "On":"Off"));
}

/*---------------------------------------------------------------------------*/
static PT_THREAD(port_number2(struct httpd_state *s, char *ptr))
{

  PSOCK_BEGIN(&s->sout);
  PSOCK_GENERATOR_SEND(&s->sout, generate_port_number2, s);
  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/

static unsigned short
generate_port_adc1(void *arg)
{
	unsigned int port_adc1 = 0;
	//ADCInit(ADC_CLK);
	//port_adc1 = ADCRead(3);
	return snprintf((char *)uip_appdata, UIP_APPDATA_SIZE,
			"%16u\n",  port_adc1);
}

/*---------------------------------------------------------------------------*/
static PT_THREAD(port_adc1(struct httpd_state *s, char *ptr))
{
  PSOCK_BEGIN(&s->sout);
  PSOCK_GENERATOR_SEND(&s->sout, generate_port_adc1, s);
  PSOCK_END(&s->sout);
}


char* itoa(int value, char* result, int base)
{
	// check that the base if valid
	if (base < 2 || base > 36) { *result = '\0'; return result; }

	char* ptr = result, *ptr1 = result, tmp_char;
	int tmp_value;

	do {
		tmp_value = value;
		value /= base;
		*ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
	} while ( value );

	// Apply negative sign
	if (tmp_value < 0) *ptr++ = '-';
	*ptr-- = '\0';
	while(ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr--= *ptr1;
		*ptr1++ = tmp_char;
	}
	return result;
}
