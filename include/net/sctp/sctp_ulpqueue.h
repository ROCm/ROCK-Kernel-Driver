/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * $Header: /cvsroot/lksctp/lksctp/sctp_cvs/include/net/sctp/sctp_ulpqueue.h,v 1.2 2002/07/12 14:50:25 jgrimm Exp $
 * 
 * These are the definitions needed for the sctp_ulpqueue type.  The 
 * sctp_ulpqueue is the interface between the Upper Layer Protocol, or ULP,
 * and the core SCTP state machine.  This is the component which handles
 * reassembly and ordering.  
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
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#ifndef __sctp_ulpqueue_h__
#define __sctp_ulpqueue_h__

/* A structure to carry information to the ULP (e.g. Sockets API) */
typedef struct sctp_ulpqueue {
	int malloced;
	spinlock_t lock;
	sctp_association_t *asoc;
	struct sk_buff_head reasm;
	struct sk_buff_head lobby;
	uint16_t ssn[0];
} sctp_ulpqueue_t;

/* This macro assists in creation of external storage for variable length 
 * internal buffers. 
 */
#define sctp_ulpqueue_storage_size(inbound) (sizeof(uint16_t) * (inbound))

sctp_ulpqueue_t *
sctp_ulpqueue_new(sctp_association_t *asoc,
		  uint16_t inbound,
		  int priority);

sctp_ulpqueue_t *
sctp_ulpqueue_init(sctp_ulpqueue_t *ulpq,
		   sctp_association_t *asoc,
		   uint16_t inbound);

void
sctp_ulpqueue_free(sctp_ulpqueue_t *);


/* Add a new DATA chunk for processing. */
int
sctp_ulpqueue_tail_data(sctp_ulpqueue_t *, 
			sctp_chunk_t *chunk,
			int priority);


/* Add a new event for propogation to the ULP. */
int 
sctp_ulpqueue_tail_event(sctp_ulpqueue_t *, 
			 sctp_ulpevent_t *event);


/* Is the ulpqueue empty. */
int
sctp_ulpqueue_is_empty(sctp_ulpqueue_t *);

int
sctp_ulpqueue_is_data_empty(sctp_ulpqueue_t *);

#endif /* __sctp_ulpqueue_h__ */






 
