#ifndef __LINUX_BRIDGE_EBT_IP_H
#define __LINUX_BRIDGE_EBT_IP_H

#define EBT_IP_SOURCE 0x01
#define EBT_IP_DEST 0x02
#define EBT_IP_TOS 0x04
#define EBT_IP_PROTO 0x08
#define EBT_IP_MASK (EBT_IP_SOURCE | EBT_IP_DEST | EBT_IP_TOS | EBT_IP_PROTO)
#define EBT_IP_MATCH "ip"

// the same values are used for the invflags
struct ebt_ip_info
{
	uint32_t saddr;
	uint32_t daddr;
	uint32_t smsk;
	uint32_t dmsk;
	uint8_t  tos;
	uint8_t  protocol;
	uint8_t  bitmask;
	uint8_t  invflags;
};

#endif
