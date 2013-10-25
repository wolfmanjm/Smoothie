#include "Kernel.h"

#include "WebServer.h"
#include "EthernetStream.h"
#include "libs/SerialMessage.h"
#include "net_util.h"

#include "uip_arp.h"
#include "clock-arch.h"

#include <mri.h>

#define BUF ((struct uip_eth_hdr *)&uip_buf[0])

extern "C" void uip_log(char *m) {
    printf("uIP log message: %s\n", m);
}

WebServer::WebServer(){
    ethernet= new LPC17XX_Ethernet();
}

WebServer::~WebServer() {
    delete ethernet;
}

static uint32_t getSerialNumberHash() {
    #define IAP_LOCATION 0x1FFF1FF1
    uint32_t command[1];
    uint32_t result[5];
    typedef void (*IAP)(uint32_t*, uint32_t*);
    IAP iap = (IAP) IAP_LOCATION;

    __disable_irq();

    command[0] = 58;
    iap(command, result);
    __enable_irq();
    return crc32((uint8_t*)&result[1], 4*4);
}

static bool parse_ip_str(const string& s, uint8_t *a, int len) {
    int p= 0;
    const char *n;
    for(int i=0;i<len;i++){
        if(i < len-1) {
            size_t o= s.find('.', p);
            if(o == string::npos) return false;
            n= s.substr(p, o-p).c_str();
            p= o+1;
        }else{
            n= s.substr(p).c_str();
        }
        a[i]= atoi(n);
    }
    return true;
}

void WebServer::on_module_loaded() {
    if( !THEKERNEL->config->value( webserver_module_enable_checksum )->by_default(true)->as_bool() ){
        // as not needed free up resource
        delete this;
        return;
    }

    string mac= THEKERNEL->config->value( webserver_mac_override_checksum )->by_default("")->as_string();
    if(mac.size() == 12 ) { // parse mac address
        // TODO

    }else{    // autogenerate
        uint32_t h= getSerialNumberHash();
        mac_address[0] = 0x00;   // OUI
        mac_address[1] = 0x1F;   // OUI
        mac_address[2] = 0x11;   // OUI
        mac_address[3] = 0x02;   // Openmoko allocation for smoothie board
        mac_address[4] = 0x04;   // 04-14  03 bits -> chip id, 1 bits -> hashed serial
        mac_address[5] = h&0xFF; // 00-FF  8bits -> hashed serial
    }

    ethernet->set_mac(mac_address);

    // get IP address, mask and gateway address here....
    bool bad= false;
    string s= THEKERNEL->config->value( webserver_ip_address_checksum )->by_default("192.168.3.222")->as_string();
    if(!parse_ip_str(s, ipaddr, 4)) {
        printf("Invalid IP address: %s\n", s.c_str());
        bad= true;
    }
    s= THEKERNEL->config->value( webserver_ip_mask_checksum )->by_default("255.255.255.0")->as_string();
    if(!parse_ip_str(s, ipmask, 4)){
        printf("Invalid IP Mask: %s\n", s.c_str());
        bad= true;
    }
    s= THEKERNEL->config->value( webserver_ip_gateway_checksum )->by_default("192.168.3.1")->as_string();
    if(!parse_ip_str(s, ipgw, 4)){
        printf("Invalid IP gateway: %s\n", s.c_str());
        bad= true;
    }
    if(bad) {
        printf("Webserver not started due to errors");
        return;
    }

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

void WebServer::init(void)
{
    // two timers for tcp/ip
    timer_set(&periodic_timer, CLOCK_SECOND / 2); /* 0.5s */
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

    // Initialize the HTTP server, listen to port 80.
    httpd_init();

    // Initialize the command server
    //uip_listen(HTONS(23));
    telnetd_init();
}

static bool got_command= false;
static bool got_response= false;
static std::string command;

void WebServer::on_main_loop(void* argument){
    // issue commands here
    if(got_command) {
        got_command= false;
        EthernetStream ethernet_stream;

        struct SerialMessage message;
        message.message= command;
        message.stream= &ethernet_stream;
        THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );

        if( ethernet_stream.to_send.size() > 0 ){
            got_response= true;
            command= ethernet_stream.to_send;
        }else{
            got_response= true;
            command= "no return data";
        }
    }
}

// TODO move into file
typedef struct console_state {
    struct psock p;
    char inputbuffer[80];
    bool waiting;
    u16_t sentlen;
    u16_t sendptr;
} console_state_t;

extern "C" {
    void handle_connection(console_state_t *, char **);
    void console_connected(console_state_t *);
    void console_send_data(console_state_t *, const char*);
}

void console_appcall() {
    console_state_t *s = (console_state_t*)(uip_conn->appstate);

    if(uip_closed()){
        printf("Closed: %p\n", s);
        if(s != NULL) {
            handle_connection(s, NULL);
            free(s);
            uip_conn->appstate= NULL;
        }
        return;
    }
    if(uip_aborted()){
        printf("aborted: %p\n", s);
        return;
    }
    if(uip_timedout()) {
        printf("timedout: %p\n", s);
        return;
    }

    if(uip_connected()) {
        s= (console_state_t*)malloc(sizeof(console_state_t));
        s->waiting= false;
        uip_conn->appstate= s;
        printf("Connected: %p\n", s);
        console_connected(s);
        got_command= false;
        got_response= false;
    }

    if(s->waiting) {
        // waiting for response from previous command, only handle one at a time
        if(got_response) {
            if(uip_acked()){
                got_response= false;
                s->waiting= false;
            }else{
                console_send_data(s, command.c_str());
            }
        }
    }else{
        // prompt for next command
        char *cmd= NULL;
        handle_connection(s, &cmd);
        if(cmd != NULL) {
            printf("Got command: <%s> %d\n", cmd, strlen(cmd));
            // handoff command to main_loop
            command= cmd;
            s->waiting= true;
            got_command= true;
        }
    }
}

// select between webserver and console server
extern "C" void app_select_appcall(void) {
    switch(uip_conn->lport) {
        case HTONS(80):
            httpd_appcall();
            break;
        case HTONS(23):
            //console_appcall();
            telnetd_appcall();
            break;
      }
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
