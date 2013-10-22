#ifndef _WEBSERVER_H
#define _WEBSERVER_H

#include "timer.h"
#include "LPC17XX_Ethernet.h"
#include "Module.h"

#include "uip.h"

#define webserver_module_enable_checksum CHECKSUM("webserver_enable")

class WebServer : public Module
{
public:
    WebServer();
    virtual ~WebServer();

    void on_module_loaded();
    void on_idle(void* argument);
    void on_main_loop(void* argument);


private:
    void init();
    uint32_t tick(uint32_t dummy);
    void handlePacket();
    void tapdev_send(void *pPacket, unsigned int size);

    LPC17XX_Ethernet *ethernet;

    struct timer periodic_timer, arp_timer;
    char ipstring [20];
    uip_ipaddr_t ipaddr;  /* local IP address */
};

#endif
