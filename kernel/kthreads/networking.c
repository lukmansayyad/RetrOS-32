/**
 * @file networking.c
 * @author Joe Bayer (joexbayer)
 * @brief Main process for handling all networking traffic. 
 * @version 0.1
 * @date 2022-06-04
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <kutils.h>
#include <scheduler.h>
#include <serial.h>
#include <assert.h>
#include <kthreads.h>
#include <work.h>

#include <net/interface.h>
#include <net/netdev.h>
#include <net/net.h>
#include <net/packet.h>
#include <net/skb.h>
#include <net/ethernet.h>
#include <net/ipv4.h>
#include <net/tcp.h>
#include <net/icmp.h>
#include <net/socket.h>
#include <net/net.h>
#include <net/dhcp.h>
#include <net/arp.h>

#define MAX_PACKET_SIZE 0x600

enum NETD_STATES {
    NETD_UNINITIALIZED,
    NETD_STARTED
};

static struct networkmanager;
static struct network_manager_ops {
    void (*start)();
    void (*stop)();
    void (*restart)();

    void (*get_info)(struct net_info* info);
    void (*send_skb)(struct networkmanager* nm, struct sk_buff* skb);
};

static struct networkmanager {
    int state;

    uint16_t packets;
    struct skb_queue* skb_tx_queue;
    struct skb_queue* skb_rx_queue;

    struct net_info stats;

    struct network_manager_ops ops;

    struct net_interface* ifs[4];
    uint8_t if_count;

    struct pcb* instance;

} netd = {
    .ops = {
        .start = NULL,
        .stop = NULL,
        .restart = NULL
    },
    .state = NETD_UNINITIALIZED,
    .packets = 0,
    .stats.dropped = 0, 
    .stats.recvd = 0,
    .stats.sent = 0,
    .instance = NULL,
};

static struct net_interface* __net_find_interface(char* dev)
{
    int len = strlen(dev);
    for (int i = 0; i < netd.if_count; i++){
        if(strncmp(netd.ifs[i]->name, dev, strlen(len)) == 0) return netd.ifs[i];
    }
    return NULL;
}

static struct net_interface* __net_interface(struct netdev* dev)
{
    for (int i = 0; i < netd.if_count; i++){
        if(netd.ifs[i]->device == dev) return netd.ifs[i];
    }
    return NULL;
}

struct net_interface** net_get_interfaces()
{
    return netd.ifs;
}

static void __net_config_loopback()
{
    struct net_interface* interface = __net_find_interface("lo0");
    if(interface == NULL) return;

    interface->ip = 0x7f000001;
    interface->netmask = 0xff000000;
    interface->gateway = 0x7f000001;

    struct arp_entry entry = {
        .sip = ntohl(LOOPBACK_IP), /* Store IP in host byte order */
        .smac = {0x69, 0x00, 0x00, 0x00, 0x00, 0x00}
    };
    net_arp_add_entry(&entry);
}

static void __net_transmit_skb(struct sk_buff* skb)
{
    if(skb == NULL || skb->interface == NULL) return;

    int ret = skb->interface->ops->send(skb->interface, skb->head, skb->len);
    if(ret < 0) return;    
   
    netd.packets++;
    netd.stats.sent++;
}

static int net_drop_packet(struct sk_buff* skb)
{
    current_netdev.dropped++;
    netd.stats.dropped++;
    skb_free(skb);
    return 0;
}

/* Exposed functions */

int net_configure_iface(char* dev, uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    struct net_interface* interface = __net_find_interface(dev);
    if(interface == NULL) return -1;

    interface->ip = ntohl(ip);
    interface->netmask = 0xffffff00;
    interface->gateway = ntohl(gateway);
    interface->ops->configure(interface, "eth0");

    return 0;
}

void __callback net_incoming_packet(struct netdev* dev)
{
    if(dev == NULL) return;

    struct net_interface* interface = __net_interface(dev);
    if(interface == NULL) return;

    struct sk_buff* skb = skb_new();
    skb->len = dev->read(skb->data, MAX_PACKET_SIZE);
    if(skb->len <= 0) {
        dbgprintf("Received an empty packet.\n");
        skb_free(skb);
        return;
    }
    skb->interface = interface;

    dbgprintf("Adding SKB to RX queue from %s\n", interface->name);

    netd.skb_rx_queue->ops->add(netd.skb_rx_queue, skb);
    netd.packets++;
    netd.stats.recvd++;

    if(netd.instance != NULL && netd.instance->state == BLOCKED){ 
        netd.instance->state = RUNNING;
    }

}

struct net_interface* net_get_iface(uint32_t ip)
{
    struct net_interface* best_match = NULL;
    int longest_prefix_length = -1;

    for (int i = 0; i < netd.if_count; i++) {
        int prefix_length = 0;
        uint32_t mask = 0x80000000; /* Start with the most significant bit */

        while (mask && (netd.ifs[i]->ip & mask) == (ip & mask)) {
            prefix_length++;
            mask >>= 1; /* Move to the next bit */
        }

        if (prefix_length > longest_prefix_length) {
            longest_prefix_length = prefix_length;
            best_match = netd.ifs[i];
        }
    }

    dbgprintf("Found interface %s for %i\n", best_match->name, ip);

    return best_match;
}


int net_iface_up(char* dev)
{
    struct net_interface* interface = __net_find_interface(dev);
    if(interface == NULL) return -1;

    interface->state = NET_IFACE_UP;
    return 0;
}

int net_iface_down(char* dev)
{
    struct net_interface* interface = __net_find_interface(dev);
    if(interface == NULL) return -1;

    interface->state = NET_IFACE_DOWN;
    return 0;
}

int net_list_ifaces()
{
    for (int i = 0; i < netd.if_count; i++){
        twritef("%s: %s mtu 1500\n", netd.ifs[i]->name, netd.ifs[i]->state == NET_IFACE_UP ? "UP" : "DOWN");
        twritef("   inet %i netmask %i\n", ntohl(netd.ifs[i]->ip), ntohl(netd.ifs[i]->netmask));
        twritef("   tx %d   rx %d\n", netd.ifs[i]->device->sent, netd.ifs[i]->device->received);
    }
    
    return netd.if_count;
}

int net_register_interface(struct net_interface* interface)
{
    if(netd.if_count >= 4) return -1;
    netd.ifs[netd.if_count++] = interface;
    return 0;
}

int net_send_skb(struct sk_buff* skb)
{
    ERR_ON_NULL(netd.skb_tx_queue);


    if (skb->interface == NULL){
        warningf("No interface specified for SKB. Dropping packet.\n");
        skb_free(skb);
        return -1;
    }
    

    RETURN_ON_ERR(netd.skb_tx_queue->ops->add(netd.skb_tx_queue, skb));
    netd.packets++;
    dbgprintf("Added SKB to TX queue\n");

    if(netd.instance != NULL && netd.instance->state == BLOCKED){ 
        netd.instance->state = RUNNING;
    }

    return 0;
    
}

error_t net_get_info(struct net_info* info)
{
    *info = netd.stats;
    return ERROR_OK;
}

int net_debug_packet(struct sk_buff* skb)
{
    /* Print all packet information */
    dbgprintf("Printing packet information\n");
    dbgprintf("Ethernet header:\n");
    dbgprintf("  Destination MAC: %x:%x:%x:%x:%x:%x\n", skb->hdr.eth->dmac[0], skb->hdr.eth->dmac[1], skb->hdr.eth->dmac[2], skb->hdr.eth->dmac[3], skb->hdr.eth->dmac[4], skb->hdr.eth->dmac[5]);
    dbgprintf("  Source MAC: %x:%x:%x:%x:%x:%x\n", skb->hdr.eth->smac[0], skb->hdr.eth->smac[1], skb->hdr.eth->smac[2], skb->hdr.eth->smac[3], skb->hdr.eth->smac[4], skb->hdr.eth->smac[5]);

    switch(ntohs(skb->hdr.eth->ethertype)){
        case ARP:
            dbgprintf("  Ethertype: ARP\n");
            break;
        case IP:
            dbgprintf("  Ethertype: IP\n");
            dbgprintf("IP header:\n");
            dbgprintf("  Version: %d\n", skb->hdr.ip->version);
            dbgprintf("  IHL: %d\n", skb->hdr.ip->ihl);
            dbgprintf("  TOS: %d\n", skb->hdr.ip->tos);
            dbgprintf("  Length: %d\n", skb->hdr.ip->len);
            dbgprintf("  Fragment offset: %d\n", skb->hdr.ip->frag_offset);
            dbgprintf("  TTL: %d\n", skb->hdr.ip->ttl);
            dbgprintf("  Protocol: %d\n", skb->hdr.ip->proto);
            dbgprintf("  Source IP: %i\n", ntohl(skb->hdr.ip->saddr));
            dbgprintf("  Destination IP: %i\n", ntohl(skb->hdr.ip->daddr));

            switch(skb->hdr.ip->proto){
                case ICMPV4:
                    dbgprintf("  Protocol: ICMP\n");
                    dbgprintf("ICMP header:\n");
                    dbgprintf("  Type: %d\n", skb->hdr.icmp->type);
                    dbgprintf("  Code: %d\n", skb->hdr.icmp->code);
                    dbgprintf("  Checksum: %d\n", skb->hdr.icmp->csum);
                    break;
                case UDP:
                    dbgprintf("  Protocol: UDP\n");
                    dbgprintf("UDP header:\n");
                    dbgprintf("  Source port: %d\n", ntohs(skb->hdr.udp->srcport));
                    dbgprintf("  Destination port: %d\n", ntohs(skb->hdr.udp->destport));
                    dbgprintf("  Length: %d\n", skb->hdr.udp->udp_length);
                    dbgprintf("  Checksum: %d\n", skb->hdr.udp->checksum);
                    break;
                case TCP:
                    dbgprintf("  Protocol: TCP\n");
                    dbgprintf("TCP header:\n");
                    dbgprintf("  Source port: %d\n", ntohs(skb->hdr.tcp->source));
                    dbgprintf("  Destination port: %d\n", ntohs(skb->hdr.tcp->dest));
                    dbgprintf("  Sequence number: %d\n", skb->hdr.tcp->seq);
                    dbgprintf("  Acknowledgement number: %d\n", skb->hdr.tcp->ack);
                    dbgprintf("  Data offset: %d\n", skb->hdr.tcp->doff);
                    dbgprintf("  Window size: %d\n", skb->hdr.tcp->window);
                    dbgprintf("  Checksum: %d\n", skb->hdr.tcp->check);
                    dbgprintf("  Urgent pointer: %d\n", skb->hdr.tcp->urg_ptr);
                    break;
                default:
                    dbgprintf("  Protocol: Unknown\n");
                    break;
            }
            
            break;
        default:
            dbgprintf("  Ethertype: Unknown\n");
            break;
    }
}

static int net_handle_recieve(struct sk_buff* skb)
{
    dbgprintf("Parsing new packet\n");
    if(net_ethernet_parse(skb) < 0) return net_drop_packet(skb);
    switch(skb->hdr.eth->ethertype){
        /* Ethernet type is IP */
        case IP:
            if(net_ipv4_parse(skb) < 0) return net_drop_packet(skb);
            switch (skb->hdr.ip->proto){
            case UDP:
                if(net_udp_parse(skb) < 0) return net_drop_packet(skb);
                break;
            
            case TCP:
                if(tcp_parse(skb) < 0) return net_drop_packet(skb);
                skb_free(skb);
                break;
            
            case ICMPV4:
                if(net_icmp_parse(skb) < 0) return net_drop_packet(skb);
                net_icmp_handle(skb);
                skb_free(skb);
                break;
            default:
                return net_drop_packet(skb);
            }
            break;

        /* Ethernet type is ARP */
        case ARP:
            if(arp_parse(skb) < 0) return net_drop_packet(skb);
            // send arp response.
            dbgprintf("Recieved ARP packet.\n");
            skb_free(skb);
            break;

        default:
            return net_drop_packet(skb);
    }
    return 1;
}

/**
 * @brief Main networking event loop.
 * 
 */
void __kthread_entry networking_main()
{
    if(netd.state == NETD_UNINITIALIZED){
        netd.skb_rx_queue = skb_new_queue();
        netd.skb_tx_queue = skb_new_queue();
    }

    netd.instance = current_running;

    /* sanity check that loopback interface exists */
    __net_config_loopback();
    struct net_interface* lo = net_get_iface(LOOPBACK_IP);
    if(lo == NULL){
        kernel_panic("Failed to initialize loopback interface.\n");
        return;
    }

    /* Start DHCP client */
    if(netd.if_count > 1){
        start("dhcpd", 0, NULL);
    }
    start("udp_server", 0, NULL);
    start("tcp_server", 0, NULL); 
    int todos =0;
    while(1){
        
        todos = netd.skb_tx_queue->size + netd.skb_rx_queue->size;
        /**
         * @brief Query RX an    TX queue for netd.packets.
         */
        if(SKB_QUEUE_READY(netd.skb_tx_queue)){
            dbgprintf("Sending new SKB from TX queue\n");
            struct sk_buff* skb = netd.skb_tx_queue->ops->remove(netd.skb_tx_queue);
            assert(skb != NULL);

            __net_transmit_skb(skb);
            skb_free(skb);
        }

        if(SKB_QUEUE_READY(netd.skb_rx_queue)){
            dbgprintf("Receiving new SKB from RX queue\n");
            struct sk_buff* skb = netd.skb_rx_queue->ops->remove(netd.skb_rx_queue);
            assert(skb != NULL);

            /* Offload skb parsing to worker thread. */
            //work_queue_add(&net_handle_recieve, (void*)skb, NULL);
            net_handle_recieve(skb);
        }

        if(todos == 0){
            current_running->state = BLOCKED;
        }

        kernel_yield();
    }
}