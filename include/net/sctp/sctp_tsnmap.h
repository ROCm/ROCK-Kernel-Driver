/* SCTP kernel reference Implementation Copyright (C) 1999-2001
 * Cisco, Motorola, Intel, and International Business Machines Corp.
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * $Header: /cvsroot/lksctp/lksctp/sctp_cvs/include/net/sctp/sctp_tsnmap.h,v 1.8 2002/07/16 14:51:58 jgrimm Exp $
 * 
 * These are the definitions needed for the tsnmap type.  The tsnmap is used
 * to track out of order TSNs received.
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
#include <net/sctp/sctp_constants.h>

#ifndef __sctp_tsnmap_h__
#define __sctp_tsnmap_h__



/* RFC 2960 12.2 Parameters necessary per association (i.e. the TCB)
 * Mapping  An array of bits or bytes indicating which out of
 * Array    order TSN's have been received (relative to the
 *          Last Rcvd TSN). If no gaps exist, i.e. no out of
 *          order packets have been received, this array
 *          will be set to all zero. This structure may be
 *          in the form of a circular buffer or bit array.
 */
typedef struct sctp_tsnmap {


	/* This array counts the number of chunks with each TSN.
	 * It points at one of the two buffers with which we will
	 * ping-pong between.
	 */
	__u8 *tsn_map;

	/* This marks the tsn which overflows the tsn_map, when the
	 * cumulative ack point reaches this point we know we can switch
	 * maps (tsn_map and overflow_map swap).
	 */
	__u32 overflow_tsn;

	/* This is the overflow array for tsn_map.
	 * It points at one of the other ping-pong buffers.
	 */
	__u8 *overflow_map;

	/* This is the TSN at tsn_map[0].  */
	__u32 base_tsn;

	/* Last Rcvd   : This is the last TSN received in
	 * TSN	       : sequence. This value is set initially by
	 *             : taking the peer's Initial TSN, received in
	 *             : the INIT or INIT ACK chunk, and subtracting
	 *             : one from it.
	 *
	 * Throughout most of the specification this is called the
	 * "Cumulative TSN ACK Point".  In this case, we
	 * ignore the advice in 12.2 in favour of the term
	 * used in the bulk of the text.
	 */
	__u32 cumulative_tsn_ack_point;

	/* This is the minimum number of TSNs we can track.  This corresponds
	 * to the size of tsn_map.   Note: the overflow_map allows us to
	 * potentially track more than this quantity.
	 */
	__u16 len;

	/* This is the highest TSN we've marked.  */
	__u32 max_tsn_seen;

	/* No. of data chunks pending receipt. used by SCTP_STATUS sockopt */
	__u16 pending_data;

	int malloced;

	__u8 raw_map[0];
} sctp_tsnmap_t;

typedef struct sctp_tsnmap_iter {
	__u32 start;
} sctp_tsnmap_iter_t;


/* Create a new tsnmap.  */
sctp_tsnmap_t *sctp_tsnmap_new(__u16 len, __u32 initial_tsn,
			       int priority);

/* Dispose of a tsnmap.  */
void sctp_tsnmap_free(sctp_tsnmap_t *map);

/* This macro assists in creation of external storage for variable length
 * internal buffers.  We double allocate so the overflow map works.
 */
#define sctp_tsnmap_storage_size(count) (sizeof(__u8) * (count) * 2)

/* Initialize a block of memory as a tsnmap.  */
sctp_tsnmap_t *sctp_tsnmap_init(sctp_tsnmap_t *map, __u16 len, __u32 initial_tsn);



/* Test the tracking state of this TSN.
 * Returns:
 *   0 if the TSN has not yet been seen
 *  >0 if the TSN has been seen (duplicate)
 *  <0 if the TSN is invalid (too large to track)
 */
int sctp_tsnmap_check(const sctp_tsnmap_t *map, __u32 tsn);

/* Mark this TSN as seen.  */
void sctp_tsnmap_mark(sctp_tsnmap_t *map, __u32 tsn);

/* Retrieve the Cumulative TSN ACK Point.  */
__u32 sctp_tsnmap_get_ctsn(const sctp_tsnmap_t *map);

/* Retrieve the highest TSN we've seen.  */
__u32 sctp_tsnmap_get_max_tsn_seen(const sctp_tsnmap_t *map);

/* Is there a gap in the TSN map? */
int sctp_tsnmap_has_gap(const sctp_tsnmap_t *map);

/* Initialize a gap ack block interator from user-provided memory.  */
void sctp_tsnmap_iter_init(const sctp_tsnmap_t *map, sctp_tsnmap_iter_t *iter);

/* Get the next gap ack blocks.  We return 0 if there are no more
 * gap ack blocks.
 */
int sctp_tsnmap_next_gap_ack(const sctp_tsnmap_t *map, sctp_tsnmap_iter_t *iter,
			     __u16 *start, __u16 *end);


#endif /* __sctp_tsnmap_h__ */



