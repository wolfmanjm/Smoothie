#ifndef _WEBSERVER_H
#define _WEBSERVER_H

#include "timer.h"
#include "LPC17XX_Ethernet.h"
#include "Module.h"

#include "uip.h"

#define webserver_module_enable_checksum CHECKSUM("webserver_enable")
#define webserver_mac_override_checksum CHECKSUM("webserver_mac")
#define webserver_ip_address_checksum CHECKSUM("webserver_ipaddr")
#define webserver_ip_gateway_checksum CHECKSUM("webserver_ipgateway")
#define webserver_ip_mask_checksum CHECKSUM("webserver_ipmask")

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
    uint8_t mac_address[6];
    uint8_t ipaddr[4];
    uint8_t ipmask[4];
    uint8_t ipgw[4];
};

#endif
