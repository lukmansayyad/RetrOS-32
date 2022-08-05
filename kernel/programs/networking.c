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

#include <process.h>
#include <screen.h>
#include <terminal.h>

#include <net/netdev.h>
#include <net/packet.h>
#include <net/skb.h>
#include <net/ethernet.h>
#include <net/ipv4.h>
#include <net/icmp.h>
#include <net/socket.h>
#include <net/dhcp.h>

#define MAX_PACKET_SIZE 0x1000

static uint16_t packets = 0;

int add_queue(uint8_t* buffer, uint16_t size);
int get_next_queue();

void networking_print_status()
{
    twriteln("DHCP");
    int state = dhcp_get_state();
    if(state != DHCP_SUCCESS){
        twritef(" (%s)      \n", dhcp_state_names[state]);
        twritef(" IP: %s\n", "N/A");
        twritef(" DNS: %s\n", "N/A");
        twritef(" GW: %s\n", "N/A");
    } else {
        int ip = dhcp_get_ip();
        unsigned char bytes[4];
        bytes[0] = (ip >> 24) & 0xFF;
        bytes[1] = (ip >> 16) & 0xFF;
        bytes[2] = (ip >> 8) & 0xFF;
        bytes[3] = ip & 0xFF;
        twritef(" IP: %d.%d.%d.%d     \n", bytes[3], bytes[2], bytes[1], bytes[0]);

        int dns = dhcp_get_dns();
        unsigned char bytes_dns[4];
        bytes_dns[0] = (dns >> 24) & 0xFF;
        bytes_dns[1] = (dns >> 16) & 0xFF;
        bytes_dns[2] = (dns >> 8) & 0xFF;
        bytes_dns[3] = dns & 0xFF;
        twritef(" DNS: %d.%d.%d.%d     \n", bytes_dns[3], bytes_dns[2], bytes_dns[1], bytes_dns[0]);
        
        int gw = dhcp_get_gw();
        unsigned char bytes_gw[4];
        bytes_gw[0] = (gw >> 24) & 0xFF;
        bytes_gw[1] = (gw >> 16) & 0xFF;
        bytes_gw[2] = (gw >> 8) & 0xFF;
        bytes_gw[3] = gw & 0xFF;
        twritef(" GW: %d.%d.%d.%d     \n", bytes_gw[3], bytes_gw[2], bytes_gw[1], bytes_gw[0]);
    }

    twritef(" MAC: %x:%x:%x:%x:%x:%x\n", current_netdev.mac[0], current_netdev.mac[1], current_netdev.mac[2], current_netdev.mac[3], current_netdev.mac[4], current_netdev.mac[5]);
    twritef(" Packets: %d\n", packets);
    twritef(" Sockets: %d\n", get_total_sockets());

}

void list_net_devices()
{

}

void net_handle_send(struct sk_buff* skb)
{
    int read = netdev_transmit(skb->head, skb->len);
    if(read <= 0){
        twriteln("Error sending packet.");
    }
    packets++;
}

int net_drop_packet(struct sk_buff* skb)
{
    current_netdev.dropped++;
    FREE_SKB(skb);

    return 0;
}

void net_packet_handler()
{
    struct sk_buff* skb = get_skb();
    ALLOCATE_SKB(skb);
    skb->action = RECIEVE;

    int read = netdev_recieve(skb->data, MAX_PACKET_SIZE);
    if(read <= 0) {
        FREE_SKB(skb);
        return;
    }
    skb->len = read;
    packets++;
}

int net_handle_recieve(struct sk_buff* skb)
{
    int ret = ethernet_parse(skb);
    if(ret <= 0)
        return net_drop_packet(skb);

    switch(skb->hdr.eth->ethertype){
        case IP:
            if(!ip_parse(skb))
                return net_drop_packet(skb);
            
            switch (skb->hdr.ip->proto)
            {
            case UDP:
                ret = udp_parse(skb);
                break;
            
            case ICMPV4:
                ret = icmp_parse(skb);
                break;
            default:
                return net_drop_packet(skb);
            }
            break;

        case ARP:
            if(!arp_parse(skb))
                return net_drop_packet(skb);

            // send arp response.
            twriteln("Recieved ARP packet.");
            break;

        default:
            return net_drop_packet(skb);
    }

    FREE_SKB(skb);

    return 1;
}

/**
 * @brief Main networking event loop.
 * 
 */
void main()
{
    while(1)
    {
        struct sk_buff* skb = next_skb();
        if(skb == NULL)
            continue;
        skb->stage = IN_PROGRESS;

        switch (skb->action)
        {
        case RECIEVE:
            net_handle_recieve(skb);
            break;
        case SEND:
            net_handle_send(skb);
            break;
        default:
            break;
        }

        FREE_SKB(skb);
    }
}

PROGRAM(networking, &main)
ATTACH("lsnet", &list_net_devices)
ATTACH("arp -a", &arp_print_cache);
ATTACH("arp", &arp_request)
ATTACH("netdev", &netdev_print_status);
ATTACH("ip", &networking_print_status);
PROGRAM_END