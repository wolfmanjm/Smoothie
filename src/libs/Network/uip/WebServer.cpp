#include "Kernel.h"

#include "WebServer.h"

#include "uip_arp.h"
#include "clock-arch.h"

#include <mri.h>

#define BUF ((struct uip_eth_hdr *)&uip_buf[0])
#define MYIP_1  192
#define MYIP_2  168
#define MYIP_3  3
#define MYIP_4  222

extern "C" void uip_log(char *m) {
    printf("uIP log message: %s\n", m);
}

WebServer::WebServer(){
    ethernet= new LPC17XX_Ethernet();
}

WebServer::~WebServer() {
    delete ethernet;
}

void WebServer::on_module_loaded() {
    if( !THEKERNEL->config->value( webserver_module_enable_checksum )->by_default(true)->as_bool() ){
        // as not needed free up resource
        delete this;
        return;
    }

    // TODO autogenerate or get from config
    mac_address[0] = 0xAE;
    mac_address[1] = 0xF0;
    mac_address[2] = 0x28;
    mac_address[3] = 0x5D;
    mac_address[4] = 0x66;
    mac_address[5] = 0x41;

    ethernet->set_mac(mac_address);

    // TODO get IP address, broadcast address and router address here....

    THEKERNEL->add_module( ethernet );
    THEKERNEL->slow_ticker->attach( 100, this, &WebServer::tick );

    // Register for events
    this->register_for_event(ON_IDLE);
    this->register_for_event(ON_MAIN_LOOP);

    this->init();
}

uint32_t WebServer::tick(uint32_t dummy) {
    do_tick();
    return 0;
}

void WebServer::on_idle(void* argument) {
    if(!ethernet->isUp()) return;

    int len;
    if(ethernet->_receive_frame(uip_buf, &len)) {
        uip_len= len;
        this->handlePacket();

    } else {

        if(timer_expired(&periodic_timer)) /* no packet but periodic_timer time out (0.5s)*/
        {
            timer_reset(&periodic_timer);

            for(int i = 0; i < UIP_CONNS; i++)
            {
                uip_periodic(i);
                /* If the above function invocation resulted in data that
                   should be sent out on the network, the global variable
                   uip_len is set to a value > 0. */
                if(uip_len > 0)
                {
                  uip_arp_out();
                  tapdev_send(uip_buf,uip_len);
                }
            }
        }

        /* Call the ARP timer function every 10 seconds. */
        if(timer_expired(&arp_timer))
        {
            timer_reset(&arp_timer);
            uip_arp_timer();
        }
    }
}

void WebServer::tapdev_send(void *pPacket, unsigned int size) {
    memcpy(ethernet->request_packet_buffer(), pPacket, size);
    ethernet->write_packet((uint8_t*) pPacket, size);
}

void WebServer::on_main_loop(void* argument){
    // issue commands here

}

void WebServer::init(void)
{
    // two timers for tcp/ip
    timer_set(&periodic_timer, CLOCK_SECOND / 2); /* 0.5s */
    timer_set(&arp_timer, CLOCK_SECOND * 10);   /* 10s */

    // Initialize the uIP TCP/IP stack.
    uip_init();

    uip_setethaddr(mac_address);

    // TODO these need to be setup in config
    uip_ipaddr(ipaddr, MYIP_1,MYIP_2,MYIP_3,MYIP_4);
    uip_sethostaddr(ipaddr);    /* host IP address */
    uip_ipaddr(ipaddr, MYIP_1,MYIP_2,MYIP_3,1);
    uip_setdraddr(ipaddr);  /* router IP address */
    uip_ipaddr(ipaddr, 255,255,255,0);
    uip_setnetmask(ipaddr); /* mask */

    // Initialize the HTTP server, listen to port 80.
    httpd_init();
}

void WebServer::handlePacket(void)
{
    if(uip_len > 0)     /* received packet */
    {
        //printf("handlePacket: %d\n", uip_len);

        if(BUF->type == htons(UIP_ETHTYPE_IP))  /* IP packet */
        {
            uip_arp_ipin();
            uip_input();
            /* If the above function invocation resulted in data that
                should be sent out on the network, the global variable
                uip_len is set to a value > 0. */

            if(uip_len > 0)
            {
                uip_arp_out();
                tapdev_send(uip_buf,uip_len);
            }
        }
        else if(BUF->type == htons(UIP_ETHTYPE_ARP))    /*ARP packet */
        {
            uip_arp_arpin();
            /* If the above function invocation resulted in data that
                should be sent out on the network, the global variable
                uip_len is set to a value > 0. */
            if(uip_len > 0)
            {
                tapdev_send(uip_buf,uip_len);   /* ARP ack*/
            }
        }

    }
}
