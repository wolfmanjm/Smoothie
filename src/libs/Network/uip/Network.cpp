#include "Kernel.h"

#include "Network.h"
#include "EthernetStream.h"
#include "libs/SerialMessage.h"
#include "net_util.h"
#include "shell.h"
#include "uip_arp.h"
#include "clock-arch.h"

#include <mri.h>

#define BUF ((struct uip_eth_hdr *)&uip_buf[0])

extern "C" void uip_log(char *m)
{
    printf("uIP log message: %s\n", m);
}

static bool webserver_enabled, telnet_enabled;

Network::Network()
{
    ethernet = new LPC17XX_Ethernet();
}

Network::~Network()
{
    delete ethernet;
}

static uint32_t getSerialNumberHash()
{
#define IAP_LOCATION 0x1FFF1FF1
    uint32_t command[1];
    uint32_t result[5];
    typedef void (*IAP)(uint32_t *, uint32_t *);
    IAP iap = (IAP) IAP_LOCATION;

    __disable_irq();

    command[0] = 58;
    iap(command, result);
    __enable_irq();
    return crc32((uint8_t *)&result[1], 4 * 4);
}

static bool parse_ip_str(const string &s, uint8_t *a, int len, char sep = '.')
{
    int p = 0;
    const char *n;
    for (int i = 0; i < len; i++) {
        if (i < len - 1) {
            size_t o = s.find(sep, p);
            if (o == string::npos) return false;
            n = s.substr(p, o - p).c_str();
            p = o + 1;
        } else {
            n = s.substr(p).c_str();
        }
        a[i] = atoi(n);
    }
    return true;
}

void Network::on_module_loaded()
{
    if ( !THEKERNEL->config->value( network_checksum, network_enable_checksum )->by_default(false)->as_bool() ) {
        // as not needed free up resource
        delete this;
        return;
    }

    webserver_enabled = THEKERNEL->config->value( network_checksum, network_webserver_checksum, network_enable_checksum )->by_default(false)->as_bool();
    telnet_enabled = THEKERNEL->config->value( network_checksum, network_telnet_checksum, network_enable_checksum )->by_default(false)->as_bool();

    string mac = THEKERNEL->config->value( network_checksum, network_mac_override_checksum )->by_default("")->as_string();
    if (mac.size() == 17 ) { // parse mac address
        if (!parse_ip_str(mac, mac_address, 6, ':')) {
            printf("Invalid MAC address: %s\n", mac.c_str());
            printf("Network not started due to errors in config");
            return;
        }

    } else {   // autogenerate
        uint32_t h = getSerialNumberHash();
        mac_address[0] = 0x00;   // OUI
        mac_address[1] = 0x1F;   // OUI
        mac_address[2] = 0x11;   // OUI
        mac_address[3] = 0x02;   // Openmoko allocation for smoothie board
        mac_address[4] = 0x04;   // 04-14  03 bits -> chip id, 1 bits -> hashed serial
        mac_address[5] = h & 0xFF; // 00-FF  8bits -> hashed serial
    }

    ethernet->set_mac(mac_address);

    // get IP address, mask and gateway address here....
    bool bad = false;
    string s = THEKERNEL->config->value( network_checksum, network_ip_address_checksum )->by_default("192.168.3.222")->as_string();
    if (!parse_ip_str(s, ipaddr, 4)) {
        printf("Invalid IP address: %s\n", s.c_str());
        bad = true;
    }
    s = THEKERNEL->config->value( network_checksum, network_ip_mask_checksum )->by_default("255.255.255.0")->as_string();
    if (!parse_ip_str(s, ipmask, 4)) {
        printf("Invalid IP Mask: %s\n", s.c_str());
        bad = true;
    }
    s = THEKERNEL->config->value( network_checksum, network_ip_gateway_checksum )->by_default("192.168.3.1")->as_string();
    if (!parse_ip_str(s, ipgw, 4)) {
        printf("Invalid IP gateway: %s\n", s.c_str());
        bad = true;
    }

    if (bad) {
        printf("Network not started due to errors in config");
        return;
    }

    THEKERNEL->add_module( ethernet );
    THEKERNEL->slow_ticker->attach( 100, this, &Network::tick );

    // Register for events
    this->register_for_event(ON_IDLE);
    this->register_for_event(ON_MAIN_LOOP);

    this->init();
}

uint32_t Network::tick(uint32_t dummy)
{
    do_tick();
    return 0;
}

void Network::on_idle(void *argument)
{
    if (!ethernet->isUp()) return;

    int len;
    if (ethernet->_receive_frame(uip_buf, &len)) {
        uip_len = len;
        this->handlePacket();

    } else {

        if (timer_expired(&periodic_timer)) { /* no packet but periodic_timer time out (0.5s)*/
            timer_reset(&periodic_timer);

            for (int i = 0; i < UIP_CONNS; i++) {
                uip_periodic(i);
                /* If the above function invocation resulted in data that
                   should be sent out on the network, the global variable
                   uip_len is set to a value > 0. */
                if (uip_len > 0) {
                    uip_arp_out();
                    tapdev_send(uip_buf, uip_len);
                }
            }
        }

        /* Call the ARP timer function every 10 seconds. */
        if (timer_expired(&arp_timer)) {
            timer_reset(&arp_timer);
            uip_arp_timer();
        }
    }
}

void Network::tapdev_send(void *pPacket, unsigned int size)
{
    memcpy(ethernet->request_packet_buffer(), pPacket, size);
    ethernet->write_packet((uint8_t *) pPacket, size);
}

void Network::init(void)
{
    // two timers for tcp/ip
    timer_set(&periodic_timer, CLOCK_SECOND / 10); /* 0.5s */
    timer_set(&arp_timer, CLOCK_SECOND * 10);   /* 10s */

    // Initialize the uIP TCP/IP stack.
    uip_init();

    uip_setethaddr(mac_address);

    uip_ipaddr_t tip;  /* local IP address */

    uip_ipaddr(tip, ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
    uip_sethostaddr(tip);    /* host IP address */
    printf("IP Addr: %d.%d.%d.%d\n", ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);

    uip_ipaddr(tip, ipgw[0], ipgw[1], ipgw[2], ipgw[3]);
    uip_setdraddr(tip);  /* router IP address */
    printf("IP GW: %d.%d.%d.%d\n", ipgw[0], ipgw[1], ipgw[2], ipgw[3]);

    uip_ipaddr(tip, ipmask[0], ipmask[1], ipmask[2], ipmask[3]);
    uip_setnetmask(tip); /* mask */
    printf("IP mask: %d.%d.%d.%d\n", ipmask[0], ipmask[1], ipmask[2], ipmask[3]);

    if (webserver_enabled) {
        // Initialize the HTTP server, listen to port 80.
        httpd_init();
        printf("Webserver initialized\n");
    }

    if (telnet_enabled) {
        // Initialize the telnet server
        telnetd_init();
        printf("Telnetd initialized\n");
    }
}

void Network::on_main_loop(void *argument)
{
    static EthernetStream ethernet_stream;
    // issue commands here if any available
    const char *cmd= shell_get_command();
    if (cmd != NULL) {
        struct SerialMessage message;
        message.message = cmd;
        message.stream = &ethernet_stream;
        shell_got_command(); // clear the command q

        THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
        shell_response(NULL); // tells shell we are done with the command
    }
}

// select between webserver and telnetd server
extern "C" void app_select_appcall(void)
{
    switch (uip_conn->lport) {
        case HTONS(80):
            if (webserver_enabled) httpd_appcall();
            break;
        case HTONS(23):
            if (telnet_enabled) telnetd_appcall();
            break;
    }
}

void Network::handlePacket(void)
{
    if (uip_len > 0) {  /* received packet */
        //printf("handlePacket: %d\n", uip_len);

        if (BUF->type == htons(UIP_ETHTYPE_IP)) { /* IP packet */
            uip_arp_ipin();
            uip_input();
            /* If the above function invocation resulted in data that
                should be sent out on the network, the global variable
                uip_len is set to a value > 0. */

            if (uip_len > 0) {
                uip_arp_out();
                tapdev_send(uip_buf, uip_len);
            }
        } else if (BUF->type == htons(UIP_ETHTYPE_ARP)) { /*ARP packet */
            uip_arp_arpin();
            /* If the above function invocation resulted in data that
                should be sent out on the network, the global variable
                uip_len is set to a value > 0. */
            if (uip_len > 0) {
                tapdev_send(uip_buf, uip_len);  /* ARP ack*/
            }
        }

    }
}
