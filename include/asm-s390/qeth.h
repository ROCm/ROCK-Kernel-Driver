/*
 * include/asm-s390/qeth.h
 *
 * ioctl definitions for qeth driver
 *
 * Copyright (C) 2004 IBM Corporation
 *
 * Author(s):	Thomas Spatzier <tspat@de.ibm.com>
 *
 */
#ifndef __ASM_S390_IOCTL_H__
#define __ASM_S390_IOCTL_H__
#include <linux/ioctl.h>

#define QETH_IOCTL_LETTER 'Q'

#define SIOC_QETH_ARP_SET_NO_ENTRIES	_IOWR(QETH_IOCTL_LETTER, 1, int)
#define SIOC_QETH_ARP_QUERY_INFO	_IOWR(QETH_IOCTL_LETTER, 2, int)
#define SIOC_QETH_ARP_ADD_ENTRY		_IOWR(QETH_IOCTL_LETTER, 3, int)
#define SIOC_QETH_ARP_REMOVE_ENTRY	_IOWR(QETH_IOCTL_LETTER, 4, int)
#define SIOC_QETH_ARP_FLUSH_CACHE	_IOWR(QETH_IOCTL_LETTER, 5, int)
#define SIOC_QETH_ADP_SET_SNMP_CONTROL	_IOWR(QETH_IOCTL_LETTER, 6, int)
#define SIOC_QETH_GET_CARD_TYPE		_IOWR(QETH_IOCTL_LETTER, 7, int)

struct qeth_arp_cache_entry {
	__u8  macaddr[6];
	__u8  reserved1[2];
	__u8  ipaddr[16]; /* for both  IPv4 and IPv6 */
	__u8  reserved2[32];
} __attribute__ ((packed));

struct qeth_arp_qi_entry7 {
	__u8 media_specific[32];
	__u8 macaddr_type;
	__u8 ipaddr_type;
	__u8 macaddr[6];
	__u8 ipaddr[4];
} __attribute__((packed));

struct qeth_arp_qi_entry5 {
	__u8 media_specific[32];
	__u8 macaddr_type;
	__u8 ipaddr_type;
	__u8 ipaddr[4];
} __attribute__((packed));

/* data sent to user space as result of query arp ioctl */
#define QETH_QARP_USER_DATA_SIZE 20000
#define QETH_QARP_MASK_OFFSET    4
#define QETH_QARP_ENTRIES_OFFSET 6
struct qeth_arp_query_user_data {
	union {
		__u32 data_len;		/* set by user space program */
		__u32 no_entries;	/* set by kernel */
	} u;
	__u16 mask_bits;
	char *entries;
} __attribute__((packed));

#endif /* __ASM_S390_IOCTL_H__ */
