/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * $Header: /cvsroot/lksctp/lksctp/sctp_cvs/include/net/sctp/sctp_ulpevent.h,v 1.5 2002/07/12 14:50:25 jgrimm Exp $
 * 
 * These are the definitions needed for the sctp_ulpevent type.  The 
 * sctp_ulpevent type is used to carry information from the state machine
 * upwards to the ULP. 
 * 
 * The SCTP reference implementation  is free software; 
 * you can redistribute it and/or modify it under the terms of 
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * the SCTP reference implementation  is distributed in the hope that it 
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 * 
 * Please send any bug reports or fixes you make to one of the
 * following email addresses:
 * 
 * Jon Grimm <jgrimm@us.ibm.com>
 * La Monte H.P. Yarroll <piggy@acm.org>
 * Karl Knutson <karl@athena.chicago.il.us>
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */


#ifndef __sctp_ulpevent_h__
#define __sctp_ulpevent_h__

/* A structure to carry information to the ULP (e.g. Sockets API) */
/* Warning: This sits inside an skb.cb[] area.  Be very careful of
 * growing this structure as it is at the maximum limit now.
 */
typedef struct sctp_ulpevent {
	int malloced;
	sctp_association_t *asoc;
	struct sk_buff *parent;
	struct sctp_sndrcvinfo sndrcvinfo;
	int chunk_flags; /* Temp. until we get a new chunk_t */
	int msg_flags;
} sctp_ulpevent_t;


sctp_ulpevent_t *sctp_ulpevent_new(int size, int msg_flags, int priority);

sctp_ulpevent_t *sctp_ulpevent_init(sctp_ulpevent_t *event, struct sk_buff *skb, int msg_flags);

void sctp_ulpevent_free(sctp_ulpevent_t *event);

int sctp_ulpevent_is_notification(const sctp_ulpevent_t *event);

sctp_ulpevent_t *sctp_ulpevent_make_assoc_change(
				const struct SCTP_association *asoc,
				__u16 flags,
				__u16 state,
				__u16 error,
				__u16 outbound,
				__u16 inbound,
				int priority);

sctp_ulpevent_t *sctp_ulpevent_make_peer_addr_change(
				const struct SCTP_association *asoc,
				const struct sockaddr_storage *aaddr,
				int flags,
				int state,
				int error,
				int priority);

sctp_ulpevent_t *sctp_ulpevent_make_remote_error(
				const struct SCTP_association *asoc,
				struct SCTP_chunk *chunk,
				__u16 flags,
				int priority);
sctp_ulpevent_t *sctp_ulpevent_make_send_failed(
				const struct SCTP_association *asoc,
				struct SCTP_chunk *chunk,
				__u16 flags,
				__u32 error,
				int priority);

sctp_ulpevent_t *sctp_ulpevent_make_shutdown_event(
				const struct SCTP_association *asoc,
				__u16 flags,
				int priority);

sctp_ulpevent_t *sctp_ulpevent_make_rcvmsg(struct SCTP_association *asoc,
					   struct SCTP_chunk *chunk,
					   int priority);

void sctp_ulpevent_read_sndrcvinfo(const sctp_ulpevent_t *event,
				   struct msghdr *msghdr);

__u16 sctp_ulpevent_get_notification_type(const sctp_ulpevent_t *event);



/* Given an event subscription, is this event enabled? */
static inline int sctp_ulpevent_is_enabled(const sctp_ulpevent_t *event,
					   const struct sctp_event_subscribe *mask)
{
	const char *amask = (const char *) mask;
	__u16 sn_type;
	int enabled = 1;

	if (sctp_ulpevent_is_notification(event)) {
		sn_type = sctp_ulpevent_get_notification_type(event);
		enabled = amask[sn_type - SCTP_SN_TYPE_BASE];
	}
	return enabled;
}


#endif /* __sctp_ulpevent_h__ */







