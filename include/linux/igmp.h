/*
 *	Linux NET3:	Internet Group Management Protocol  [IGMP]
 *
 *	Authors:
 *		Alan Cox <Alan.Cox@linux.org>
 *
 *	Extended to talk the BSD extended IGMP protocol of mrouted 3.6
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_IGMP_H
#define _LINUX_IGMP_H

/*
 *	IGMP protocol structures
 */

/*
 *	Header in on cable format
 */

struct igmphdr
{
	__u8 type;
	__u8 code;		/* For newer IGMP */
	__u16 csum;
	__u32 group;
};

#define IGMP_HOST_MEMBERSHIP_QUERY	0x11	/* From RFC1112 */
#define IGMP_HOST_MEMBERSHIP_REPORT	0x12	/* Ditto */
#define IGMP_DVMRP			0x13	/* DVMRP routing */
#define IGMP_PIM			0x14	/* PIM routing */
#define IGMP_TRACE			0x15
#define IGMP_HOST_NEW_MEMBERSHIP_REPORT 0x16	/* New version of 0x11 */
#define IGMP_HOST_LEAVE_MESSAGE 	0x17

#define IGMP_MTRACE_RESP		0x1e
#define IGMP_MTRACE			0x1f


/*
 *	Use the BSD names for these for compatibility
 */

#define IGMP_DELAYING_MEMBER		0x01
#define IGMP_IDLE_MEMBER		0x02
#define IGMP_LAZY_MEMBER		0x03
#define IGMP_SLEEPING_MEMBER		0x04
#define IGMP_AWAKENING_MEMBER		0x05

#define IGMP_MINLEN			8

#define IGMP_MAX_HOST_REPORT_DELAY	10	/* max delay for response to */
						/* query (in seconds)	*/

#define IGMP_TIMER_SCALE		10	/* denotes that the igmphdr->timer field */
						/* specifies time in 10th of seconds	 */

#define IGMP_AGE_THRESHOLD		400	/* If this host don't hear any IGMP V1	*/
						/* message in this period of time,	*/
						/* revert to IGMP v2 router.		*/

#define IGMP_ALL_HOSTS		htonl(0xE0000001L)
#define IGMP_ALL_ROUTER 	htonl(0xE0000002L)
#define IGMP_LOCAL_GROUP	htonl(0xE0000000L)
#define IGMP_LOCAL_GROUP_MASK	htonl(0xFFFFFF00L)

/*
 * struct for keeping the multicast list in
 */

#ifdef __KERNEL__

/* ip_mc_socklist is real list now. Speed is not argument;
   this list never used in fast path code
 */

struct ip_mc_socklist
{
	struct ip_mc_socklist	*next;
	int			count;
	struct ip_mreqn		multi;
};

struct ip_mc_list
{
	struct in_device	*interface;
	unsigned long		multiaddr;
	struct ip_mc_list	*next;
	struct timer_list	timer;
	int			users;
	atomic_t		refcnt;
	spinlock_t		lock;
	char			tm_running;
	char			reporter;
	char			unsolicit_count;
	char			loaded;
};

extern int ip_check_mc(struct in_device *dev, u32 mc_addr);
extern int igmp_rcv(struct sk_buff *);
extern int ip_mc_join_group(struct sock *sk, struct ip_mreqn *imr);
extern int ip_mc_leave_group(struct sock *sk, struct ip_mreqn *imr);
extern void ip_mc_drop_socket(struct sock *sk);
extern void ip_mr_init(void);
extern void ip_mc_init_dev(struct in_device *);
extern void ip_mc_destroy_dev(struct in_device *);
extern void ip_mc_up(struct in_device *);
extern void ip_mc_down(struct in_device *);
extern int ip_mc_dec_group(struct in_device *in_dev, u32 addr);
extern void ip_mc_inc_group(struct in_device *in_dev, u32 addr);
#endif
#endif
