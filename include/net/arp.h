#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include <util.h>
#include <net/skb.h>

#define ARP_ETHERNET    0x0001
#define ARP_IPV4        0x0800
#define ARP_REQUEST     0x0001
#define ARP_REPLY       0x0002

struct arp_header
{
    uint16_t hwtype;
    uint16_t protype;
    uint8_t hwsize;
    uint8_t prosize;
    uint16_t opcode;
} __attribute__((packed));

struct arp_content
{
    uint8_t smac[6];
    uint32_t sip;
    uint8_t dmac[6];
    uint32_t dip;
} __attribute__((packed));

struct arp_entry
{
	uint8_t smac[6];
	uint32_t sip;
}__attribute__((packed));

void arp_parse(struct sk_buff* skb);

#endif // !ARP_H