/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2002 Intel Corp.
 * Copyright (c) 2001-2002 International Business Machines Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * This file includes part of the implementation of the add-IP extension,
 * based on <draft-ietf-tsvwg-addip-sctp-02.txt> June 29, 2001,
 * for the SCTP kernel reference Implementation.
 *
 * These functions work with the state functions in sctp_sm_statefuns.c
 * to implement the state operations.  These functions implement the
 * steps which require modifying existing data structures.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
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
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    C. Robin              <chris@hundredacre.ac.uk>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Dajiang Zhang	    <dajiang.zhang@nokia.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <net/sock.h>

#include <linux/skbuff.h>
#include <linux/random.h>	/* for get_random_bytes */
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* RFC 2960 3.3.2 Initiation (INIT) (1)
 *
 * Note 2: The ECN capable field is reserved for future use of
 * Explicit Congestion Notification.
 */
static const sctp_ecn_capable_param_t ecap_param = {
	{
		SCTP_PARAM_ECN_CAPABLE,
		__constant_htons(sizeof(sctp_ecn_capable_param_t)),
	}
};

/* A helper to initilize to initilize an op error inside a
 * provided chunk, as most cause codes will be embedded inside an
 * abort chunk.
 */
void  sctp_init_cause(sctp_chunk_t *chunk, __u16 cause_code,
		      const void *payload, size_t paylen)
{
	sctp_errhdr_t err;
	int padlen;
	__u16 len;

        /* Cause code constants are now defined in network order.  */
	err.cause = cause_code;
	len = sizeof(sctp_errhdr_t) + paylen;
	padlen = len % 4;
	err.length  = htons(len);
	len += padlen;
	sctp_addto_chunk(chunk, sizeof(sctp_errhdr_t), &err);
	chunk->subh.err_hdr = sctp_addto_chunk(chunk, paylen, payload);
}

/* 3.3.2 Initiation (INIT) (1)
 *
 * This chunk is used to initiate a SCTP association between two
 * endpoints. The format of the INIT chunk is shown below:
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |   Type = 1    |  Chunk Flags  |      Chunk Length             |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                         Initiate Tag                          |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |           Advertised Receiver Window Credit (a_rwnd)          |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |  Number of Outbound Streams   |  Number of Inbound Streams    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                          Initial TSN                          |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    \                                                               \
 *    /              Optional/Variable-Length Parameters              /
 *    \                                                               \
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *
 * The INIT chunk contains the following parameters. Unless otherwise
 * noted, each parameter MUST only be included once in the INIT chunk.
 *
 * Fixed Parameters                     Status
 * ----------------------------------------------
 * Initiate Tag                        Mandatory
 * Advertised Receiver Window Credit   Mandatory
 * Number of Outbound Streams          Mandatory
 * Number of Inbound Streams           Mandatory
 * Initial TSN                         Mandatory
 *
 * Variable Parameters                  Status     Type Value
 * -------------------------------------------------------------
 * IPv4 Address (Note 1)               Optional    5
 * IPv6 Address (Note 1)               Optional    6
 * Cookie Preservative                 Optional    9
 * Reserved for ECN Capable (Note 2)   Optional    32768 (0x8000)
 * Host Name Address (Note 3)          Optional    11
 * Supported Address Types (Note 4)    Optional    12
 */
sctp_chunk_t *sctp_make_init(const sctp_association_t *asoc,
			     const sctp_bind_addr_t *bp,
			     int priority, int vparam_len)
{
	sctp_inithdr_t init;
	union sctp_params addrs;
	size_t chunksize;
	sctp_chunk_t *retval = NULL;
	int num_types, addrs_len = 0;
	struct sctp_opt *sp;
	sctp_supported_addrs_param_t sat;
	__u16 types[2];

	/* RFC 2960 3.3.2 Initiation (INIT) (1)
	 *
	 * Note 1: The INIT chunks can contain multiple addresses that
	 * can be IPv4 and/or IPv6 in any combination.
	 */
	retval = NULL;
	addrs.v = NULL;

	/* Convert the provided bind address list to raw format */
	addrs = sctp_bind_addrs_to_raw(bp, &addrs_len, priority);
	if (!addrs.v)
		goto nodata;

	init.init_tag		   = htonl(asoc->c.my_vtag);
	init.a_rwnd		   = htonl(asoc->rwnd);
	init.num_outbound_streams  = htons(asoc->c.sinit_num_ostreams);
	init.num_inbound_streams   = htons(asoc->c.sinit_max_instreams);
	init.initial_tsn	   = htonl(asoc->c.initial_tsn);

	/* How many address types are needed? */
	sp = sctp_sk(asoc->base.sk);
	num_types = sp->pf->supported_addrs(sp, types);

	chunksize = sizeof(init) + addrs_len + SCTP_SAT_LEN(num_types);
	chunksize += sizeof(ecap_param);
	chunksize += vparam_len;

	/* RFC 2960 3.3.2 Initiation (INIT) (1)
	 *
	 * Note 3: An INIT chunk MUST NOT contain more than one Host
	 * Name address parameter. Moreover, the sender of the INIT
	 * MUST NOT combine any other address types with the Host Name
	 * address in the INIT. The receiver of INIT MUST ignore any
	 * other address types if the Host Name address parameter is
	 * present in the received INIT chunk.
	 *
	 * PLEASE DO NOT FIXME [This version does not support Host Name.]
	 */

	retval = sctp_make_chunk(asoc, SCTP_CID_INIT, 0, chunksize);
	if (!retval)
		goto nodata;

	retval->subh.init_hdr =
		sctp_addto_chunk(retval, sizeof(init), &init);
	retval->param_hdr.v =
		sctp_addto_chunk(retval, addrs_len, addrs.v);

	/* RFC 2960 3.3.2 Initiation (INIT) (1)
	 *
	 * Note 4: This parameter, when present, specifies all the
	 * address types the sending endpoint can support. The absence
	 * of this parameter indicates that the sending endpoint can
	 * support any address type.
	 */
	sat.param_hdr.type = SCTP_PARAM_SUPPORTED_ADDRESS_TYPES;
	sat.param_hdr.length = htons(SCTP_SAT_LEN(num_types));
	sctp_addto_chunk(retval, sizeof(sat), &sat);
	sctp_addto_chunk(retval, num_types * sizeof(__u16), &types);

	sctp_addto_chunk(retval, sizeof(ecap_param), &ecap_param);
nodata:
	if (addrs.v)
		kfree(addrs.v);
	return retval;
}

sctp_chunk_t *sctp_make_init_ack(const sctp_association_t *asoc,
				 const sctp_chunk_t *chunk,
				 int priority, int unkparam_len)
{
	sctp_inithdr_t initack;
	sctp_chunk_t *retval;
	union sctp_params addrs;
	int addrs_len;
	sctp_cookie_param_t *cookie;
	int cookie_len;
	size_t chunksize;

	retval = NULL;

	addrs = sctp_bind_addrs_to_raw(&asoc->base.bind_addr, &addrs_len, 
				       priority);
	if (!addrs.v)
		goto nomem_rawaddr;

	initack.init_tag	        = htonl(asoc->c.my_vtag);
	initack.a_rwnd			= htonl(asoc->rwnd);
	initack.num_outbound_streams	= htons(asoc->c.sinit_num_ostreams);
	initack.num_inbound_streams	= htons(asoc->c.sinit_max_instreams);
	initack.initial_tsn		= htonl(asoc->c.initial_tsn);

	/* FIXME:  We really ought to build the cookie right
	 * into the packet instead of allocating more fresh memory.
	 */
	cookie = sctp_pack_cookie(asoc->ep, asoc, chunk, &cookie_len,
				  addrs.v, addrs_len);
	if (!cookie)
		goto nomem_cookie;

	/* Calculate the total size of allocation, include the reserved
	 * space for reporting unknown parameters if it is specified.
	 */
	chunksize = sizeof(initack) + addrs_len + cookie_len + unkparam_len;

        /* Tell peer that we'll do ECN only if peer advertised such cap.  */
	if (asoc->peer.ecn_capable)
		chunksize += sizeof(ecap_param);

	/* Now allocate and fill out the chunk.  */
	retval = sctp_make_chunk(asoc, SCTP_CID_INIT_ACK, 0, chunksize);
	if (!retval)
		goto nomem_chunk;

	/* Per the advice in RFC 2960 6.4, send this reply to
	 * the source of the INIT packet.
	 */
	retval->transport = chunk->transport;
	retval->subh.init_hdr =
		sctp_addto_chunk(retval, sizeof(initack), &initack);
	retval->param_hdr.v = sctp_addto_chunk(retval, addrs_len, addrs.v);
	sctp_addto_chunk(retval, cookie_len, cookie);
	if (asoc->peer.ecn_capable)
		sctp_addto_chunk(retval, sizeof(ecap_param), &ecap_param);

	/* We need to remove the const qualifier at this point.  */
	retval->asoc = (sctp_association_t *) asoc;

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it received the DATA or control chunk
	 * to which it is replying.
	 *
	 * [INIT ACK back to where the INIT came from.]
	 */
	if (chunk)
		retval->transport = chunk->transport;

nomem_chunk:
	kfree(cookie);
nomem_cookie:
	kfree(addrs.v);
nomem_rawaddr:
	return retval;
}

/* 3.3.11 Cookie Echo (COOKIE ECHO) (10):
 *
 * This chunk is used only during the initialization of an association.
 * It is sent by the initiator of an association to its peer to complete
 * the initialization process. This chunk MUST precede any DATA chunk
 * sent within the association, but MAY be bundled with one or more DATA
 * chunks in the same packet.
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |   Type = 10   |Chunk  Flags   |         Length                |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     /                     Cookie                                    /
 *     \                                                               \
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Chunk Flags: 8 bit
 *
 *   Set to zero on transmit and ignored on receipt.
 *
 * Length: 16 bits (unsigned integer)
 *
 *   Set to the size of the chunk in bytes, including the 4 bytes of
 *   the chunk header and the size of the Cookie.
 *
 * Cookie: variable size
 *
 *   This field must contain the exact cookie received in the
 *   State Cookie parameter from the previous INIT ACK.
 *
 *   An implementation SHOULD make the cookie as small as possible
 *   to insure interoperability.
 */
sctp_chunk_t *sctp_make_cookie_echo(const sctp_association_t *asoc,
				    const sctp_chunk_t *chunk)
{
	sctp_chunk_t *retval;
	void *cookie;
	int cookie_len;

	cookie = asoc->peer.cookie;
	cookie_len = asoc->peer.cookie_len;

	/* Build a cookie echo chunk.  */
	retval = sctp_make_chunk(asoc, SCTP_CID_COOKIE_ECHO, 0, cookie_len);
	if (!retval)
		goto nodata;
	retval->subh.cookie_hdr =
		sctp_addto_chunk(retval, cookie_len, cookie);

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it * received the DATA or control chunk
	 * to which it is replying.
	 *
	 * [COOKIE ECHO back to where the INIT ACK came from.]
	 */
	if (chunk)
		retval->transport = chunk->transport;

nodata:
	return retval;
}

/* 3.3.12 Cookie Acknowledgement (COOKIE ACK) (11):
 *
 * This chunk is used only during the initialization of an
 * association.  It is used to acknowledge the receipt of a COOKIE
 * ECHO chunk.  This chunk MUST precede any DATA or SACK chunk sent
 * within the association, but MAY be bundled with one or more DATA
 * chunks or SACK chunk in the same SCTP packet.
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |   Type = 11   |Chunk  Flags   |     Length = 4                |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Chunk Flags: 8 bits
 *
 *   Set to zero on transmit and ignored on receipt.
 */
sctp_chunk_t *sctp_make_cookie_ack(const sctp_association_t *asoc,
				   const sctp_chunk_t *chunk)
{
	sctp_chunk_t *retval;

	retval = sctp_make_chunk(asoc, SCTP_CID_COOKIE_ACK, 0, 0);

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it * received the DATA or control chunk
	 * to which it is replying.
	 *
	 * [COOKIE ACK back to where the COOKIE ECHO came from.]
	 */
	if (retval && chunk)
		retval->transport = chunk->transport;

	return retval;
}

/*
 *  Appendix A: Explicit Congestion Notification:
 *  CWR:
 *
 *  RFC 2481 details a specific bit for a sender to send in the header of
 *  its next outbound TCP segment to indicate to its peer that it has
 *  reduced its congestion window.  This is termed the CWR bit.  For
 *  SCTP the same indication is made by including the CWR chunk.
 *  This chunk contains one data element, i.e. the TSN number that
 *  was sent in the ECNE chunk.  This element represents the lowest
 *  TSN number in the datagram that was originally marked with the
 *  CE bit.
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | Chunk Type=13 | Flags=00000000|    Chunk Length = 8           |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                      Lowest TSN Number                        |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *     Note: The CWR is considered a Control chunk.
 */
sctp_chunk_t *sctp_make_cwr(const sctp_association_t *asoc,
			    const __u32 lowest_tsn,
			    const sctp_chunk_t *chunk)
{
	sctp_chunk_t *retval;
	sctp_cwrhdr_t cwr;

	cwr.lowest_tsn = htonl(lowest_tsn);
	retval = sctp_make_chunk(asoc, SCTP_CID_ECN_CWR, 0,
				 sizeof(sctp_cwrhdr_t));

	if (!retval)
		goto nodata;

	retval->subh.ecn_cwr_hdr =
		sctp_addto_chunk(retval, sizeof(cwr), &cwr);

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it * received the DATA or control chunk
	 * to which it is replying.
	 *
	 * [Report a reduced congestion window back to where the ECNE
	 * came from.]
	 */
	if (chunk)
		retval->transport = chunk->transport;

nodata:
	return retval;
}

/* Make an ECNE chunk.  This is a congestion experienced report.  */
sctp_chunk_t *sctp_make_ecne(const sctp_association_t *asoc,
			     const __u32 lowest_tsn)
{
	sctp_chunk_t *retval;
	sctp_ecnehdr_t ecne;

	ecne.lowest_tsn = htonl(lowest_tsn);
	retval = sctp_make_chunk(asoc, SCTP_CID_ECN_ECNE, 0,
				 sizeof(sctp_ecnehdr_t));
	if (!retval)
		goto nodata;
	retval->subh.ecne_hdr =
		sctp_addto_chunk(retval, sizeof(ecne), &ecne);

nodata:
	return retval;
}

/* Make a DATA chunk for the given association from the provided
 * parameters.  However, do not populate the data payload.
 */
sctp_chunk_t *sctp_make_datafrag_empty(sctp_association_t *asoc,
				       const struct sctp_sndrcvinfo *sinfo,
				       int data_len, __u8 flags, __u16 ssn)
{
	sctp_chunk_t *retval;
	sctp_datahdr_t dp;
	int chunk_len;

	/* We assign the TSN as LATE as possible, not here when
	 * creating the chunk.
	 */
	dp.tsn= 1000000;       /* This marker is a debugging aid. */
	dp.stream = htons(sinfo->sinfo_stream);
	dp.ppid   = htonl(sinfo->sinfo_ppid);
	dp.ssn    = htons(ssn);

	/* Set the flags for an unordered send.  */
	if (sinfo->sinfo_flags & MSG_UNORDERED)
		flags |= SCTP_DATA_UNORDERED;

	chunk_len = sizeof(dp) + data_len;
	retval = sctp_make_chunk(asoc, SCTP_CID_DATA, flags, chunk_len);
	if (!retval)
		goto nodata;

	retval->subh.data_hdr = sctp_addto_chunk(retval, sizeof(dp), &dp);
	memcpy(&retval->sinfo, sinfo, sizeof(struct sctp_sndrcvinfo));

nodata:
	return retval;
}

/* Make a DATA chunk for the given association.  Populate the data
 * payload.
 */
sctp_chunk_t *sctp_make_datafrag(sctp_association_t *asoc,
				 const struct sctp_sndrcvinfo *sinfo,
				 int data_len, const __u8 *data,
				 __u8 flags, __u16 ssn)
{
	sctp_chunk_t *retval;

	retval = sctp_make_datafrag_empty(asoc, sinfo, data_len, flags, ssn);
	if (retval)
		sctp_addto_chunk(retval, data_len, data);

	return retval;
}

/* Make a DATA chunk for the given association to ride on stream id
 * 'stream', with a payload id of 'payload', and a body of 'data'.
 */
sctp_chunk_t *sctp_make_data(sctp_association_t *asoc,
			     const struct sctp_sndrcvinfo *sinfo,
			     int data_len, const __u8 *data)
{
	sctp_chunk_t *retval = NULL;

	retval = sctp_make_data_empty(asoc, sinfo, data_len);
	if (retval)
		sctp_addto_chunk(retval, data_len, data);
        return retval;
}

/* Make a DATA chunk for the given association to ride on stream id
 * 'stream', with a payload id of 'payload', and a body big enough to
 * hold 'data_len' octets of data.  We use this version when we need
 * to build the message AFTER allocating memory.
 */
sctp_chunk_t *sctp_make_data_empty(sctp_association_t *asoc,
				   const struct sctp_sndrcvinfo *sinfo,
				   int data_len)
{
	__u8 flags = SCTP_DATA_NOT_FRAG;

	return sctp_make_datafrag_empty(asoc, sinfo, data_len, flags, 0);
}

/* Create a selective ackowledgement (SACK) for the given
 * association.  This reports on which TSN's we've seen to date,
 * including duplicates and gaps.
 */
sctp_chunk_t *sctp_make_sack(const sctp_association_t *asoc)
{
	sctp_chunk_t *retval;
	sctp_sackhdr_t sack;
	sctp_gap_ack_block_t gab;
	int length;
	__u32 ctsn;
	struct sctp_tsnmap_iter iter;
	__u16 num_gabs, num_dup_tsns;
	struct sctp_tsnmap *map = (struct sctp_tsnmap *)&asoc->peer.tsn_map;

	ctsn = sctp_tsnmap_get_ctsn(map);
	SCTP_DEBUG_PRINTK("sackCTSNAck sent is 0x%x.\n", ctsn);

	/* Count the number of Gap Ack Blocks.  */
	sctp_tsnmap_iter_init(map, &iter);
	for (num_gabs = 0;
	     sctp_tsnmap_next_gap_ack(map, &iter, &gab.start, &gab.end);
	     num_gabs++) {
		/* Do nothing. */
	}

	num_dup_tsns = sctp_tsnmap_num_dups(map);

	/* Initialize the SACK header.  */
	sack.cum_tsn_ack	    = htonl(ctsn);
	sack.a_rwnd 		    = htonl(asoc->a_rwnd);
	sack.num_gap_ack_blocks     = htons(num_gabs);
	sack.num_dup_tsns           = htons(num_dup_tsns);

	length = sizeof(sack)
		+ sizeof(sctp_gap_ack_block_t) * num_gabs
		+ sizeof(__u32) * num_dup_tsns;

	/* Create the chunk.  */
	retval = sctp_make_chunk(asoc, SCTP_CID_SACK, 0, length);
	if (!retval)
		goto nodata;

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, etc.) to the same destination transport
	 * address from which it received the DATA or control chunk to
	 * which it is replying.  This rule should also be followed if
	 * the endpoint is bundling DATA chunks together with the
	 * reply chunk.
	 *
	 * However, when acknowledging multiple DATA chunks received
	 * in packets from different source addresses in a single
	 * SACK, the SACK chunk may be transmitted to one of the
	 * destination transport addresses from which the DATA or
	 * control chunks being acknowledged were received.
	 *
	 * [BUG:  We do not implement the following paragraph.
	 * Perhaps we should remember the last transport we used for a
	 * SACK and avoid that (if possible) if we have seen any
	 * duplicates. --piggy]
	 *
	 * When a receiver of a duplicate DATA chunk sends a SACK to a
	 * multi- homed endpoint it MAY be beneficial to vary the
	 * destination address and not use the source address of the
	 * DATA chunk.  The reason being that receiving a duplicate
	 * from a multi-homed endpoint might indicate that the return
	 * path (as specified in the source address of the DATA chunk)
	 * for the SACK is broken.
	 *
	 * [Send to the address from which we last received a DATA chunk.]
	 */
	retval->transport = asoc->peer.last_data_from;

	retval->subh.sack_hdr =
		sctp_addto_chunk(retval, sizeof(sack), &sack);

	/* Put the Gap Ack Blocks into the chunk.  */
	sctp_tsnmap_iter_init(map, &iter);
	while(sctp_tsnmap_next_gap_ack(map, &iter, &gab.start, &gab.end)) {
		gab.start = htons(gab.start);
		gab.end = htons(gab.end);
		sctp_addto_chunk(retval, sizeof(sctp_gap_ack_block_t), &gab);
	}

	/* Register the duplicates.  */
	sctp_addto_chunk(retval, sizeof(__u32) * num_dup_tsns,
			 sctp_tsnmap_get_dups(map));

nodata:
	return retval;
}

/* Make a SHUTDOWN chunk. */
sctp_chunk_t *sctp_make_shutdown(const sctp_association_t *asoc)
{
	sctp_chunk_t *retval;
	sctp_shutdownhdr_t shut;
	__u32 ctsn;

	ctsn = sctp_tsnmap_get_ctsn(&asoc->peer.tsn_map);
	shut.cum_tsn_ack = htonl(ctsn);

	retval = sctp_make_chunk(asoc, SCTP_CID_SHUTDOWN, 0,
				 sizeof(sctp_shutdownhdr_t));
	if (!retval)
		goto nodata;

	retval->subh.shutdown_hdr =
		sctp_addto_chunk(retval, sizeof(shut), &shut);
nodata:
	return retval;
}

sctp_chunk_t *sctp_make_shutdown_ack(const sctp_association_t *asoc,
				     const sctp_chunk_t *chunk)
{
	sctp_chunk_t *retval;

	retval = sctp_make_chunk(asoc, SCTP_CID_SHUTDOWN_ACK, 0, 0);

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it * received the DATA or control chunk
	 * to which it is replying.
	 *
	 * [ACK back to where the SHUTDOWN came from.]
	 */
	if (retval && chunk)
		retval->transport = chunk->transport;

	return retval;
}

sctp_chunk_t *sctp_make_shutdown_complete(const sctp_association_t *asoc,
					  const sctp_chunk_t *chunk)
{
	sctp_chunk_t *retval;
	__u8 flags = 0;

	/* Maybe set the T-bit if we have no association. */
	flags |= asoc ? 0 : SCTP_CHUNK_FLAG_T;

	retval = sctp_make_chunk(asoc, SCTP_CID_SHUTDOWN_COMPLETE, flags, 0);

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it * received the DATA or control chunk
	 * to which it is replying.
	 *
	 * [Report SHUTDOWN COMPLETE back to where the SHUTDOWN ACK
	 * came from.]
	 */
	if (retval && chunk)
		retval->transport = chunk->transport;

        return retval;
}

/* Create an ABORT.  Note that we set the T bit if we have no
 * association.
 */
sctp_chunk_t *sctp_make_abort(const sctp_association_t *asoc,
			      const sctp_chunk_t *chunk,
			      const size_t hint)
{
	sctp_chunk_t *retval;
	__u8 flags = 0;

	/* Maybe set the T-bit if we have no association.  */
	flags |= asoc ? 0 : SCTP_CHUNK_FLAG_T;

	retval = sctp_make_chunk(asoc, SCTP_CID_ABORT, flags, hint);

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it * received the DATA or control chunk
	 * to which it is replying.
	 *
	 * [ABORT back to where the offender came from.]
	 */
	if (retval && chunk)
		retval->transport = chunk->transport;

	return retval;
}

/* Helper to create ABORT with a NO_USER_DATA error.  */
sctp_chunk_t *sctp_make_abort_no_data(const sctp_association_t *asoc,
				      const sctp_chunk_t *chunk, __u32 tsn)
{
	sctp_chunk_t *retval;
	__u32 payload;

	retval = sctp_make_abort(asoc, chunk, sizeof(sctp_errhdr_t)
				 + sizeof(tsn));

	if (!retval)
		goto no_mem;

	/* Put the tsn back into network byte order.  */
	payload = htonl(tsn);
	sctp_init_cause(retval, SCTP_ERROR_NO_DATA, (const void *)&payload,
			sizeof(payload));

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it * received the DATA or control chunk
	 * to which it is replying.
	 *
	 * [ABORT back to where the offender came from.]
	 */
	if (chunk)
		retval->transport = chunk->transport;

no_mem:
	return retval;
}

/* Helper to create ABORT with a SCTP_ERROR_USER_ABORT error.  */
sctp_chunk_t *sctp_make_abort_user(const sctp_association_t *asoc,
				   const sctp_chunk_t *chunk,
				   const struct msghdr *msg)
{
	sctp_chunk_t *retval;
	void *payload = NULL, *payoff;
	size_t paylen;
	struct iovec *iov = msg->msg_iov;
	int iovlen = msg->msg_iovlen;

	paylen = get_user_iov_size(iov, iovlen);
	retval = sctp_make_abort(asoc, chunk, sizeof(sctp_errhdr_t) + paylen);
	if (!retval)
		goto err_chunk;

	if (paylen) {
		/* Put the msg_iov together into payload.  */
		payload = kmalloc(paylen, GFP_ATOMIC);
		if (!payload)
			goto err_payload;
		payoff = payload;

		for (; iovlen > 0; --iovlen) {
			if (copy_from_user(payoff, iov->iov_base, iov->iov_len))
				goto err_copy;
			payoff += iov->iov_len;
			iov++;
		}
	}

	sctp_init_cause(retval, SCTP_ERROR_USER_ABORT, payload, paylen);

	if (paylen)
		kfree(payload);

	return retval;

err_copy:
	kfree(payload);
err_payload:
	sctp_free_chunk(retval);
	retval = NULL;
err_chunk:
	return retval;
}

/* Make a HEARTBEAT chunk.  */
sctp_chunk_t *sctp_make_heartbeat(const sctp_association_t *asoc,
				  const struct sctp_transport *transport,
				  const void *payload, const size_t paylen)
{
	sctp_chunk_t *retval = sctp_make_chunk(asoc, SCTP_CID_HEARTBEAT,
					       0, paylen);

	if (!retval)
		goto nodata;

	/* Cast away the 'const', as this is just telling the chunk
	 * what transport it belongs to.
	 */
	retval->transport = (struct sctp_transport *) transport;
	retval->subh.hbs_hdr = sctp_addto_chunk(retval, paylen, payload);

nodata:
	return retval;
}

sctp_chunk_t *sctp_make_heartbeat_ack(const sctp_association_t *asoc,
				      const sctp_chunk_t *chunk,
				      const void *payload, const size_t paylen)
{
	sctp_chunk_t *retval = sctp_make_chunk(asoc, SCTP_CID_HEARTBEAT_ACK,
					       0, paylen);

	if (!retval)
		goto nodata;
	retval->subh.hbs_hdr = sctp_addto_chunk(retval, paylen, payload);

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it * received the DATA or control chunk
	 * to which it is replying.
	 *
	 * [HBACK back to where the HEARTBEAT came from.]
	 */
	if (chunk)
		retval->transport = chunk->transport;

nodata:
	return retval;
}

/* Create an Operation Error chunk with the specified space reserved.
 * This routine can be used for containing multiple causes in the chunk.
 */
sctp_chunk_t *sctp_make_op_error_space(const sctp_association_t *asoc,
				       const sctp_chunk_t *chunk,
				       size_t size)
{
	sctp_chunk_t *retval;

	retval = sctp_make_chunk(asoc, SCTP_CID_ERROR, 0,
				 sizeof(sctp_errhdr_t) + size);
	if (!retval)
		goto nodata;

	/* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, etc.) to the same destination transport
	 * address from which it received the DATA or control chunk
	 * to which it is replying.
	 *
	 */
	if (chunk)
		retval->transport = chunk->transport;

nodata:
	return retval;
}

/* Create an Operation Error chunk.  */
sctp_chunk_t *sctp_make_op_error(const sctp_association_t *asoc,
				 const sctp_chunk_t *chunk,
				 __u16 cause_code, const void *payload,
				 size_t paylen)
{
	sctp_chunk_t *retval = sctp_make_op_error_space(asoc, chunk, paylen);

	if (!retval)
		goto nodata;

	sctp_init_cause(retval, cause_code, payload, paylen);

nodata:
	return retval;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* Turn an skb into a chunk.
 * FIXME: Eventually move the structure directly inside the skb->cb[].
 */
sctp_chunk_t *sctp_chunkify(struct sk_buff *skb, const sctp_association_t *asoc,
			    struct sock *sk)
{
	sctp_chunk_t *retval = t_new(sctp_chunk_t, GFP_ATOMIC);

	if (!retval)
		goto nodata;
	memset(retval, 0, sizeof(sctp_chunk_t));

	if (!sk) {
		SCTP_DEBUG_PRINTK("chunkifying skb %p w/o an sk\n", skb);
	}

	retval->skb		= skb;
	retval->asoc		= (sctp_association_t *) asoc;
	retval->num_times_sent	= 0;
	retval->has_tsn		= 0;
	retval->has_ssn         = 0;
	retval->rtt_in_progress	= 0;
	retval->sent_at	= jiffies;
	retval->singleton	= 1;
	retval->end_of_packet	= 0;
	retval->ecn_ce_done	= 0;
	retval->pdiscard	= 0;

	/* sctpimpguide-05.txt Section 2.8.2
	 * M1) Each time a new DATA chunk is transmitted
	 * set the 'TSN.Missing.Report' count for that TSN to 0. The
	 * 'TSN.Missing.Report' count will be used to determine missing chunks
	 * and when to fast retransmit.
	 */
	retval->tsn_missing_report = 0;
	retval->tsn_gap_acked = 0;
	retval->fast_retransmit = 0;

	/* Polish the bead hole.  */
	INIT_LIST_HEAD(&retval->transmitted_list);
	INIT_LIST_HEAD(&retval->frag_list);
	SCTP_DBG_OBJCNT_INC(chunk);

nodata:
	return retval;
}

/* Set chunk->source and dest based on the IP header in chunk->skb.  */
void sctp_init_addrs(sctp_chunk_t *chunk, union sctp_addr *src,
		     union sctp_addr *dest)
{
	memcpy(&chunk->source, src, sizeof(union sctp_addr));
	memcpy(&chunk->dest, dest, sizeof(union sctp_addr));
}

/* Extract the source address from a chunk.  */
const union sctp_addr *sctp_source(const sctp_chunk_t *chunk)
{
	/* If we have a known transport, use that.  */
	if (chunk->transport) {
		return &chunk->transport->ipaddr;
	} else {
		/* Otherwise, extract it from the IP header.  */
		return &chunk->source;
	}
}

/* Create a new chunk, setting the type and flags headers from the
 * arguments, reserving enough space for a 'paylen' byte payload.
 */
sctp_chunk_t *sctp_make_chunk(const sctp_association_t *asoc,
			      __u8 type, __u8 flags, int paylen)
{
	sctp_chunk_t *retval;
	sctp_chunkhdr_t *chunk_hdr;
	struct sk_buff *skb;
	struct sock *sk;

	skb = dev_alloc_skb(WORD_ROUND(sizeof(sctp_chunkhdr_t) + paylen));
	if (!skb)
		goto nodata;

	/* Make room for the chunk header.  */
	chunk_hdr = (sctp_chunkhdr_t *)skb_put(skb, sizeof(sctp_chunkhdr_t));
	skb_pull(skb, sizeof(sctp_chunkhdr_t));

	chunk_hdr->type	  = type;
	chunk_hdr->flags  = flags;
	chunk_hdr->length = htons(sizeof(sctp_chunkhdr_t));

	/* Move the data pointer back up to the start of the chunk.  */
	skb_push(skb, sizeof(sctp_chunkhdr_t));

	sk = asoc ? asoc->base.sk : NULL;
	retval = sctp_chunkify(skb, asoc, sk);
	if (!retval) {
		dev_kfree_skb(skb);
		goto nodata;
	}

	retval->chunk_hdr = chunk_hdr;
	retval->chunk_end = ((__u8 *)chunk_hdr) + sizeof(sctp_chunkhdr_t);

	/* Set the skb to the belonging sock for accounting.  */
	skb->sk = sk;

	return retval;

nodata:
	return NULL;
}

/* Release the memory occupied by a chunk.  */
void sctp_free_chunk(sctp_chunk_t *chunk)
{
	/* Make sure that we are not on any list.  */
	skb_unlink((struct sk_buff *) chunk);
	list_del(&chunk->transmitted_list);

	/* Free the chunk skb data and the SCTP_chunk stub itself. */
	dev_kfree_skb(chunk->skb);

	kfree(chunk);
	SCTP_DBG_OBJCNT_DEC(chunk);
}


/* Append bytes to the end of a chunk.  Will panic if chunk is not big
 * enough.
 */
void *sctp_addto_chunk(sctp_chunk_t *chunk, int len, const void *data)
{
	void *target;
	void *padding;
	int chunklen = ntohs(chunk->chunk_hdr->length);
	int padlen = chunklen % 4;

	padding = skb_put(chunk->skb, padlen);
	target = skb_put(chunk->skb, len);

	memset(padding, 0, padlen);
	memcpy(target, data, len);

	/* Adjust the chunk length field.  */
	chunk->chunk_hdr->length = htons(chunklen + padlen + len);
	chunk->chunk_end = chunk->skb->tail;

	return target;
}

/* Append bytes from user space to the end of a chunk.  Will panic if
 * chunk is not big enough.
 * Returns a kernel err value.
 */
static int sctp_user_addto_chunk(sctp_chunk_t *chunk, int off, int len,
				 struct iovec *data)
{
	__u8 *target;
	int err = 0;

	/* Make room in chunk for data.  */
	target = skb_put(chunk->skb, len);

	/* Copy data (whole iovec) into chunk */
	if ((err = memcpy_fromiovecend(target, data, off, len)))
		goto out;

	/* Adjust the chunk length field.  */
	chunk->chunk_hdr->length =
		htons(ntohs(chunk->chunk_hdr->length) + len);
	chunk->chunk_end = chunk->skb->tail;

out:
	return err;
}

/* A data chunk can have a maximum payload of (2^16 - 20).  Break
 * down any such message into smaller chunks.  Opportunistically, fragment
 * the chunks down to the current MTU constraints.  We may get refragmented
 * later if the PMTU changes, but it is _much better_ to fragment immediately
 * with a reasonable guess than always doing our fragmentation on the
 * soft-interrupt.
 */


int sctp_datachunks_from_user(sctp_association_t *asoc,
			      const struct sctp_sndrcvinfo *sinfo,
			      struct msghdr *msg, int msg_len,
			      struct sk_buff_head *chunks)
{
	int max, whole, i, offset, over, err;
	int len, first_len;
	sctp_chunk_t *chunk;
	__u8 frag;

	/* What is a reasonable fragmentation point right now? */
	max = asoc->pmtu;
	if (max < SCTP_MIN_PMTU)
		max = SCTP_MIN_PMTU;
	max -= SCTP_IP_OVERHEAD;

	/* Make sure not beyond maximum chunk size. */
	if (max > SCTP_MAX_CHUNK_LEN)
		max = SCTP_MAX_CHUNK_LEN;

	/* Subtract out the overhead of a data chunk header. */
	max -= sizeof(struct sctp_data_chunk);

	whole = 0;
	first_len = max;

	/* Encourage Cookie-ECHO bundling. */
	if (asoc->state < SCTP_STATE_COOKIE_ECHOED) {
		whole = msg_len / (max - SCTP_ARBITRARY_COOKIE_ECHO_LEN);

		/* Account for the DATA to be bundled with the COOKIE-ECHO. */
		if (whole) {
			first_len = max - SCTP_ARBITRARY_COOKIE_ECHO_LEN;
			msg_len -= first_len;
			whole = 1;
		}
	} 

	/* How many full sized?  How many bytes leftover? */
	whole += msg_len / max;
	over = msg_len % max;
	offset = 0;

	if (whole && over)
		SCTP_INC_STATS_USER(SctpFragUsrMsgs);

	/* Create chunks for all the full sized DATA chunks. */
	for (i=0, len=first_len; i < whole; i++) {
		frag = SCTP_DATA_MIDDLE_FRAG;

		if (0 == i)
			frag |= SCTP_DATA_FIRST_FRAG;

		if ((i == (whole - 1)) && !over)
			frag |= SCTP_DATA_LAST_FRAG;

		chunk = sctp_make_datafrag_empty(asoc, sinfo, len, frag, 0);

		if (!chunk)
			goto nomem;
		err = sctp_user_addto_chunk(chunk, offset, len, msg->msg_iov);
		if (err < 0)
			goto errout;

		offset += len;

		/* Put the chunk->skb back into the form expected by send.  */
		__skb_pull(chunk->skb, (__u8 *)chunk->chunk_hdr
			   - (__u8 *)chunk->skb->data);

		__skb_queue_tail(chunks, (struct sk_buff *)chunk);

		/* The first chunk, the first chunk was likely short 
		 * to allow bundling, so reset to full size.
		 */
		if (0 == i)
			len = max;
	}

	/* .. now the leftover bytes. */
	if (over) {
		if (!whole)
			frag = SCTP_DATA_NOT_FRAG;
		else
			frag = SCTP_DATA_LAST_FRAG;

		chunk = sctp_make_datafrag_empty(asoc, sinfo, over, frag, 0);

		if (!chunk)
			goto nomem;

		err = sctp_user_addto_chunk(chunk, offset, over, msg->msg_iov);

		/* Put the chunk->skb back into the form expected by send.  */
		__skb_pull(chunk->skb, (__u8 *)chunk->chunk_hdr
			   - (__u8 *)chunk->skb->data);
		if (err < 0)
			goto errout;

		__skb_queue_tail(chunks, (struct sk_buff *)chunk);
	}
	err = 0;
	goto out;

nomem:
	err = -ENOMEM;
errout:
	while ((chunk = (sctp_chunk_t *)__skb_dequeue(chunks)))
		sctp_free_chunk(chunk);
out:
	return err;
}

/* Helper function to assign a TSN if needed.  This assumes that both
 * the data_hdr and association have already been assigned.
 */
void sctp_chunk_assign_ssn(sctp_chunk_t *chunk)
{
	__u16 ssn;
	__u16 sid;

	if (chunk->has_ssn)
		return;

	/* This is the last possible instant to assign a SSN. */
	if (chunk->chunk_hdr->flags & SCTP_DATA_UNORDERED) {
		ssn = 0;
	} else {
		sid = htons(chunk->subh.data_hdr->stream);
		if (chunk->chunk_hdr->flags & SCTP_DATA_LAST_FRAG)
			ssn = sctp_ssn_next(&chunk->asoc->ssnmap->out, sid);
		else
			ssn = sctp_ssn_peek(&chunk->asoc->ssnmap->out, sid);
		ssn = htons(ssn);
	}

	chunk->subh.data_hdr->ssn = ssn;
	chunk->has_ssn = 1;
}

/* Helper function to assign a TSN if needed.  This assumes that both
 * the data_hdr and association have already been assigned.
 */
void sctp_chunk_assign_tsn(sctp_chunk_t *chunk)
{
	if (!chunk->has_tsn) {
		/* This is the last possible instant to
		 * assign a TSN.
		 */
		chunk->subh.data_hdr->tsn =
			htonl(sctp_association_get_next_tsn(chunk->asoc));
		chunk->has_tsn = 1;
	}
}

/* Create a CLOSED association to use with an incoming packet.  */
sctp_association_t *sctp_make_temp_asoc(const sctp_endpoint_t *ep,
					sctp_chunk_t *chunk,
					int priority)
{
	sctp_association_t *asoc;
	sctp_scope_t scope;

	/* Create the bare association.  */
	scope = sctp_scope(sctp_source(chunk));
	asoc = sctp_association_new(ep, ep->base.sk, scope, priority);
	if (!asoc)
		goto nodata;

	/* Create an entry for the source address of the packet.  */
	switch (chunk->skb->nh.iph->version) {
	case 4:
		asoc->c.peer_addr.v4.sin_family     = AF_INET;
		asoc->c.peer_addr.v4.sin_port = ntohs(chunk->sctp_hdr->source);
		asoc->c.peer_addr.v4.sin_addr.s_addr =
			chunk->skb->nh.iph->saddr;
                break;

	case 6:
		asoc->c.peer_addr.v6.sin6_family     = AF_INET6;
		asoc->c.peer_addr.v6.sin6_port
			= ntohs(chunk->sctp_hdr->source);
		asoc->c.peer_addr.v6.sin6_flowinfo = 0; /* BUG BUG BUG */
		asoc->c.peer_addr.v6.sin6_addr = chunk->skb->nh.ipv6h->saddr;
		asoc->c.peer_addr.v6.sin6_scope_id = 0; /* BUG BUG BUG */
		break;

        default:
		/* Yikes!  I never heard of this kind of address.  */
		goto fail;
	};

nodata:
	return asoc;

fail:
	sctp_association_free(asoc);
	return NULL;
}

/* Build a cookie representing asoc.
 * This INCLUDES the param header needed to put the cookie in the INIT ACK.
 */
sctp_cookie_param_t *sctp_pack_cookie(const sctp_endpoint_t *ep,
				      const sctp_association_t *asoc,
				      const sctp_chunk_t *init_chunk,
				      int *cookie_len,
				      const __u8 *raw_addrs, int addrs_len)
{
	sctp_cookie_param_t *retval;
	sctp_signed_cookie_t *cookie;
	int headersize, bodysize;

	headersize = sizeof(sctp_paramhdr_t) + SCTP_SECRET_SIZE;
	bodysize = sizeof(sctp_cookie_t)
		+ ntohs(init_chunk->chunk_hdr->length) + addrs_len;

	/* Pad out the cookie to a multiple to make the signature
	 * functions simpler to write.
	 */
	if (bodysize % SCTP_COOKIE_MULTIPLE)
		bodysize += SCTP_COOKIE_MULTIPLE
			- (bodysize % SCTP_COOKIE_MULTIPLE);
	*cookie_len = headersize + bodysize;

	retval = (sctp_cookie_param_t *)
		kmalloc(*cookie_len, GFP_ATOMIC);
	if (!retval) {
		*cookie_len = 0;
		goto nodata;
	}

	/* Clear this memory since we are sending this data structure
	 * out on the network.
	 */
	memset(retval, 0x00, *cookie_len);
	cookie = (sctp_signed_cookie_t *) retval->body;

	/* Set up the parameter header.  */
	retval->p.type = SCTP_PARAM_STATE_COOKIE;
	retval->p.length = htons(*cookie_len);

	/* Copy the cookie part of the association itself.  */
	cookie->c = asoc->c;
	/* Save the raw address list length in the cookie. */
	cookie->c.raw_addr_list_len = addrs_len;

	/* Set an expiration time for the cookie.  */
	do_gettimeofday(&cookie->c.expiration);
	tv_add(&asoc->cookie_life, &cookie->c.expiration);

	/* Copy the peer's init packet.  */
	memcpy(&cookie->c.peer_init[0], init_chunk->chunk_hdr,
	       ntohs(init_chunk->chunk_hdr->length));

	/* Copy the raw local address list of the association. */
	memcpy((__u8 *)&cookie->c.peer_init[0] +
	       ntohs(init_chunk->chunk_hdr->length), raw_addrs,
	       addrs_len);

	/* Sign the message.  */
	sctp_hash_digest(ep->secret_key[ep->current_key], SCTP_SECRET_SIZE,
			 (__u8 *) &cookie->c, bodysize, cookie->signature);

nodata:
	return retval;
}

/* Unpack the cookie from COOKIE ECHO chunk, recreating the association.  */
sctp_association_t *sctp_unpack_cookie(const sctp_endpoint_t *ep,
				       const sctp_association_t *asoc,
				       sctp_chunk_t *chunk, int priority,
				       int *error, sctp_chunk_t **err_chk_p)
{
	sctp_association_t *retval = NULL;
	sctp_signed_cookie_t *cookie;
	sctp_cookie_t *bear_cookie;
	int headersize, bodysize;
	int fixed_size;
	__u8 digest_buf[SCTP_SIGNATURE_SIZE];
	int secret;
	sctp_scope_t scope;

	headersize = sizeof(sctp_chunkhdr_t) + SCTP_SECRET_SIZE;
	bodysize = ntohs(chunk->chunk_hdr->length) - headersize;
	fixed_size = headersize + sizeof(sctp_cookie_t);

	/* Verify that the chunk looks like it even has a cookie.
	 * There must be enough room for our cookie and our peer's
	 * INIT chunk.
	 */
	if (ntohs(chunk->chunk_hdr->length) <
	    (fixed_size + sizeof(sctp_chunkhdr_t)))
		goto malformed;

	/* Verify that the cookie has been padded out. */
	if (bodysize % SCTP_COOKIE_MULTIPLE)
		goto malformed;

	/* Process the cookie.  */
	cookie = chunk->subh.cookie_hdr;
	bear_cookie = &cookie->c;

	/* Check the signature.  */
	secret = ep->current_key;
	sctp_hash_digest(ep->secret_key[secret], SCTP_SECRET_SIZE,
			 (__u8 *) bear_cookie, bodysize,
			 digest_buf);
	if (memcmp(digest_buf, cookie->signature, SCTP_SIGNATURE_SIZE)) {
		/* Try the previous key. */
		secret = ep->last_key;
		sctp_hash_digest(ep->secret_key[secret], SCTP_SECRET_SIZE,
				 (__u8 *) bear_cookie, bodysize, digest_buf);
		if (memcmp(digest_buf, cookie->signature, SCTP_SIGNATURE_SIZE)) {
			/* Yikes!  Still bad signature! */
			*error = -SCTP_IERROR_BAD_SIG;
			goto fail;
		}
	}

	/* Check to see if the cookie is stale.  If there is already
	 * an association, there is no need to check cookie's expiration
	 * for init collision case of lost COOKIE ACK.
	 */
	if (!asoc && tv_lt(bear_cookie->expiration, chunk->skb->stamp)) {
		__u16 len;
		/*
		 * Section 3.3.10.3 Stale Cookie Error (3)
		 *
		 * Cause of error
		 * ---------------
		 * Stale Cookie Error:  Indicates the receipt of a valid State
		 * Cookie that has expired.
		 */
		len = ntohs(chunk->chunk_hdr->length);
		*err_chk_p = sctp_make_op_error_space(asoc, chunk, len);
		if (*err_chk_p) {
			suseconds_t usecs = (chunk->skb->stamp.tv_sec -
				bear_cookie->expiration.tv_sec) * 1000000L +
				chunk->skb->stamp.tv_usec -
				bear_cookie->expiration.tv_usec;

			usecs = htonl(usecs);
			sctp_init_cause(*err_chk_p, SCTP_ERROR_STALE_COOKIE,
					&usecs, sizeof(usecs));
			*error = -SCTP_IERROR_STALE_COOKIE;
		} else
			*error = -SCTP_IERROR_NOMEM;

		goto fail;
	}

	/* Make a new base association.  */
	scope = sctp_scope(sctp_source(chunk));
	retval = sctp_association_new(ep, ep->base.sk, scope, priority);
	if (!retval) {
		*error = -SCTP_IERROR_NOMEM;
		goto fail;
	}

	/* Set up our peer's port number.  */
	retval->peer.port = ntohs(chunk->sctp_hdr->source);

	/* Populate the association from the cookie.  */
	retval->c = *bear_cookie;

	if (sctp_assoc_set_bind_addr_from_cookie(retval, bear_cookie,
						 GFP_ATOMIC) < 0) {
		*error = -SCTP_IERROR_NOMEM;
		goto fail;
	}

	retval->next_tsn = retval->c.initial_tsn;
	retval->ctsn_ack_point = retval->next_tsn - 1;

	/* The INIT stuff will be done by the side effects.  */
	return retval;

fail:
	if (retval)
		sctp_association_free(retval);

	return NULL;

malformed:
	/* Yikes!  The packet is either corrupt or deliberately
	 * malformed.
	 */
	*error = -SCTP_IERROR_MALFORMED;
	goto fail;
}

/********************************************************************
 * 3rd Level Abstractions
 ********************************************************************/

/*
 * Report a missing mandatory parameter.
 */
struct __sctp_missing {
	__u32 num_missing;
	__u16 type;
}  __attribute__((packed));;
static int sctp_process_missing_param(const sctp_association_t *asoc,
				      sctp_param_t paramtype,
				      sctp_chunk_t *chunk,
				      sctp_chunk_t **err_chk_p)
{
	struct __sctp_missing report;
	__u16 len;

	len = WORD_ROUND(sizeof(report));

	/* Make an ERROR chunk, preparing enough room for
	 * returning multiple unknown parameters.
	 */
	if (!*err_chk_p)
		*err_chk_p = sctp_make_op_error_space(asoc, chunk, len);

	if (*err_chk_p) {
		report.num_missing = htonl(1);
		report.type = paramtype;
		sctp_init_cause(*err_chk_p, SCTP_ERROR_INV_PARAM,
				&report, sizeof(report));
	}

	/* Stop processing this chunk. */
	return 0;
}

/* Report an Invalid Mandatory Parameter.  */
static int sctp_process_inv_mandatory(const sctp_association_t *asoc,
				      sctp_chunk_t *chunk,
				      sctp_chunk_t **err_chk_p)
{
	/* Invalid Mandatory Parameter Error has no payload. */

	if (!*err_chk_p)
		*err_chk_p = sctp_make_op_error_space(asoc, chunk, 0);

	if (*err_chk_p)
		sctp_init_cause(*err_chk_p, SCTP_ERROR_INV_PARAM, NULL, 0);

	/* Stop processing this chunk. */
	return 0;
}

/* Do not attempt to handle the HOST_NAME parm.  However, do
 * send back an indicator to the peer.
 */
static int sctp_process_hn_param(const sctp_association_t *asoc,
				 union sctp_params param,
				 sctp_chunk_t *chunk,
				 sctp_chunk_t **err_chk_p)
{
	__u16 len = ntohs(param.p->length);

	/* Make an ERROR chunk. */
	if (!*err_chk_p)
		*err_chk_p = sctp_make_op_error_space(asoc, chunk, len);

	if (*err_chk_p)
		sctp_init_cause(*err_chk_p, SCTP_ERROR_DNS_FAILED,
				param.v, len);

	/* Stop processing this chunk. */
	return 0;
}

/* RFC 3.2.1 & the Implementers Guide 2.2.
 *
 * The Parameter Types are encoded such that the
 * highest-order two bits specify the action that must be
 * taken if the processing endpoint does not recognize the
 * Parameter Type.
 *
 * 00 - Stop processing this SCTP chunk and discard it,
 *	do not process any further chunks within it.
 *
 * 01 - Stop processing this SCTP chunk and discard it,
 *	do not process any further chunks within it, and report
 *	the unrecognized parameter in an 'Unrecognized
 *	Parameter Type' (in either an ERROR or in the INIT ACK).
 *
 * 10 - Skip this parameter and continue processing.
 *
 * 11 - Skip this parameter and continue processing but
 *	report the unrecognized parameter in an
 *	'Unrecognized Parameter Type' (in either an ERROR or in
 *	the INIT ACK).
 *
 * Return value:
 * 	0 - discard the chunk
 * 	1 - continue with the chunk
 */
static int sctp_process_unk_param(const sctp_association_t *asoc,
				  union sctp_params param,
				  sctp_chunk_t *chunk,
				  sctp_chunk_t **err_chk_p)
{
	int retval = 1;

	switch (param.p->type & SCTP_PARAM_ACTION_MASK) {
	case SCTP_PARAM_ACTION_DISCARD:
		retval =  0;
		break;
	case SCTP_PARAM_ACTION_DISCARD_ERR:
		retval =  0;
		/* Make an ERROR chunk, preparing enough room for
		 * returning multiple unknown parameters.
		 */
		if (NULL == *err_chk_p)
			*err_chk_p = sctp_make_op_error_space(asoc, chunk,
					ntohs(chunk->chunk_hdr->length));

		if (*err_chk_p)
			sctp_init_cause(*err_chk_p, SCTP_ERROR_UNKNOWN_PARAM,
					param.v,
					WORD_ROUND(ntohs(param.p->length)));

		break;
	case SCTP_PARAM_ACTION_SKIP:
		break;
	case SCTP_PARAM_ACTION_SKIP_ERR:
		/* Make an ERROR chunk, preparing enough room for
		 * returning multiple unknown parameters.
		 */
		if (NULL == *err_chk_p)
			*err_chk_p = sctp_make_op_error_space(asoc, chunk,
					ntohs(chunk->chunk_hdr->length));

		if (*err_chk_p) {
			sctp_init_cause(*err_chk_p, SCTP_ERROR_UNKNOWN_PARAM,
					param.v,
					WORD_ROUND(ntohs(param.p->length)));
		} else {
			/* If there is no memory for generating the ERROR
			 * report as specified, an ABORT will be triggered
			 * to the peer and the association won't be
			 * established.
			 */
			retval = 0;
		}

		break;
	default:
		break;
	}

	return retval;
}

/* Find unrecognized parameters in the chunk.
 * Return values:
 * 	0 - discard the chunk
 * 	1 - continue with the chunk
 */
static int sctp_verify_param(const sctp_association_t *asoc,
			     union sctp_params param,
			     sctp_cid_t cid,
			     sctp_chunk_t *chunk,
			     sctp_chunk_t **err_chunk)
{
	int retval = 1;

	/* FIXME - This routine is not looking at each parameter per the
	 * chunk type, i.e., unrecognized parameters should be further
	 * identified based on the chunk id.
	 */

	switch (param.p->type) {
	case SCTP_PARAM_IPV4_ADDRESS:
	case SCTP_PARAM_IPV6_ADDRESS:
	case SCTP_PARAM_COOKIE_PRESERVATIVE:
	case SCTP_PARAM_SUPPORTED_ADDRESS_TYPES:
	case SCTP_PARAM_STATE_COOKIE:
	case SCTP_PARAM_HEARTBEAT_INFO:
	case SCTP_PARAM_UNRECOGNIZED_PARAMETERS:
	case SCTP_PARAM_ECN_CAPABLE:
		break;

	case SCTP_PARAM_HOST_NAME_ADDRESS:
		/* Tell the peer, we won't support this param.  */
		return sctp_process_hn_param(asoc, param, chunk, err_chunk);
	default:
		SCTP_DEBUG_PRINTK("Unrecognized param: %d for chunk %d.\n",
				ntohs(param.p->type), cid);
		return sctp_process_unk_param(asoc, param, chunk, err_chunk);

		break;
	}
	return retval;
}

/* Verify the INIT packet before we process it.  */
int sctp_verify_init(const sctp_association_t *asoc,
		     sctp_cid_t cid,
		     sctp_init_chunk_t *peer_init,
		     sctp_chunk_t *chunk,
		     sctp_chunk_t **err_chk_p)
{
	union sctp_params param;
	int has_cookie = 0;

	/* Verify stream values are non-zero. */
	if ((0 == peer_init->init_hdr.num_outbound_streams) ||
	    (0 == peer_init->init_hdr.num_inbound_streams)) {

		sctp_process_inv_mandatory(asoc, chunk, err_chk_p);
		return 0;
	}

	/* Check for missing mandatory parameters.  */
	sctp_walk_params(param, peer_init, init_hdr.params) {

		if (SCTP_PARAM_STATE_COOKIE == param.p->type)
			has_cookie = 1;

	} /* for (loop through all parameters) */

	/* The only missing mandatory param possible today is
	 * the state cookie for an INIT-ACK chunk.
	 */
	if ((SCTP_CID_INIT_ACK == cid) && !has_cookie) {

		sctp_process_missing_param(asoc, SCTP_PARAM_STATE_COOKIE,
					   chunk, err_chk_p);
		return 0;
	}

	/* Find unrecognized parameters. */

	sctp_walk_params(param, peer_init, init_hdr.params) {

		if (!sctp_verify_param(asoc, param, cid, chunk, err_chk_p))
			return 0;

	} /* for (loop through all parameters) */

	return 1;
}

/* Unpack the parameters in an INIT packet into an association.
 * Returns 0 on failure, else success.
 * FIXME:  This is an association method.
 */
int sctp_process_init(sctp_association_t *asoc, sctp_cid_t cid,
		      const union sctp_addr *peer_addr,
		      sctp_init_chunk_t *peer_init,
		      int priority)
{
	union sctp_params param;
	struct sctp_transport *transport;
	struct list_head *pos, *temp;
	char *cookie;

	/* We must include the address that the INIT packet came from.
	 * This is the only address that matters for an INIT packet.
	 * When processing a COOKIE ECHO, we retrieve the from address
	 * of the INIT from the cookie.
	 */

	/* This implementation defaults to making the first transport
	 * added as the primary transport.  The source address seems to
	 * be a a better choice than any of the embedded addresses.
	 */
	if (peer_addr)
		if(!sctp_assoc_add_peer(asoc, peer_addr, priority))
			goto nomem;

	/* Process the initialization parameters.  */

	sctp_walk_params(param, peer_init, init_hdr.params) {

		if (!sctp_process_param(asoc, param, peer_addr, priority))
                        goto clean_up;
	}

	/* The fixed INIT headers are always in network byte
	 * order.
	 */
	asoc->peer.i.init_tag =
		ntohl(peer_init->init_hdr.init_tag);
	asoc->peer.i.a_rwnd =
		ntohl(peer_init->init_hdr.a_rwnd);
	asoc->peer.i.num_outbound_streams =
		ntohs(peer_init->init_hdr.num_outbound_streams);
	asoc->peer.i.num_inbound_streams =
		ntohs(peer_init->init_hdr.num_inbound_streams);
	asoc->peer.i.initial_tsn =
		ntohl(peer_init->init_hdr.initial_tsn);

	/* Apply the upper bounds for output streams based on peer's
	 * number of inbound streams.
	 */
	if (asoc->c.sinit_num_ostreams  >
	    ntohs(peer_init->init_hdr.num_inbound_streams)) {
		asoc->c.sinit_num_ostreams =
			ntohs(peer_init->init_hdr.num_inbound_streams);
	}

	if (asoc->c.sinit_max_instreams >
	    ntohs(peer_init->init_hdr.num_outbound_streams)) {
		asoc->c.sinit_max_instreams =
			ntohs(peer_init->init_hdr.num_outbound_streams);
	}

	/* Copy Initiation tag from INIT to VT_peer in cookie.   */
	asoc->c.peer_vtag = asoc->peer.i.init_tag;

	/* Peer Rwnd   : Current calculated value of the peer's rwnd.  */
	asoc->peer.rwnd = asoc->peer.i.a_rwnd;

	/* Copy cookie in case we need to resend COOKIE-ECHO. */
	cookie = asoc->peer.cookie;
	if (cookie) {
		asoc->peer.cookie = kmalloc(asoc->peer.cookie_len, priority);
		if (!asoc->peer.cookie)
			goto clean_up;
		memcpy(asoc->peer.cookie, cookie, asoc->peer.cookie_len);
	}

	/* RFC 2960 7.2.1 The initial value of ssthresh MAY be arbitrarily
	 * high (for example, implementations MAY use the size of the receiver
	 * advertised window).
	 */
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, struct sctp_transport, transports);
		transport->ssthresh = asoc->peer.i.a_rwnd;
	}

	/* Set up the TSN tracking pieces.  */
	sctp_tsnmap_init(&asoc->peer.tsn_map, SCTP_TSN_MAP_SIZE,
			 asoc->peer.i.initial_tsn);

	/* RFC 2960 6.5 Stream Identifier and Stream Sequence Number
	 *
	 * The stream sequence number in all the streams shall start
	 * from 0 when the association is established.  Also, when the
	 * stream sequence number reaches the value 65535 the next
	 * stream sequence number shall be set to 0.
	 */

	/* Allocate storage for the negotiated streams. */
	asoc->ssnmap = sctp_ssnmap_new(asoc->peer.i.num_outbound_streams,
				       asoc->c.sinit_num_ostreams,
				       priority);
	if (!asoc->ssnmap)
		goto nomem_ssnmap;

	/* ADDIP Section 4.1 ASCONF Chunk Procedures
	 *
	 * When an endpoint has an ASCONF signaled change to be sent to the
	 * remote endpoint it should do the following:
	 * ...
	 * A2) A serial number should be assigned to the Chunk. The serial
	 * number should be a monotonically increasing number. All serial
	 * numbers are defined to be initialized at the start of the
	 * association to the same value as the Initial TSN.
	 */
	asoc->peer.addip_serial = asoc->peer.i.initial_tsn - 1;
	return 1;

nomem_ssnmap:
clean_up:
	/* Release the transport structures. */
	list_for_each_safe(pos, temp, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, struct sctp_transport, transports);
		list_del(pos);
		sctp_transport_free(transport);
	}
nomem:
	return 0;
}


/* Update asoc with the option described in param.
 *
 * RFC2960 3.3.2.1 Optional/Variable Length Parameters in INIT
 *
 * asoc is the association to update.
 * param is the variable length parameter to use for update.
 * cid tells us if this is an INIT, INIT ACK or COOKIE ECHO.
 * If the current packet is an INIT we want to minimize the amount of
 * work we do.  In particular, we should not build transport
 * structures for the addresses.
 */
int sctp_process_param(sctp_association_t *asoc, union sctp_params param,
		       const union sctp_addr *peer_addr, int priority)
{
	union sctp_addr addr;
	int i;
	__u16 sat;
	int retval = 1;
	sctp_scope_t scope;
	time_t stale;

	/* We maintain all INIT parameters in network byte order all the
	 * time.  This allows us to not worry about whether the parameters
	 * came from a fresh INIT, and INIT ACK, or were stored in a cookie.
	 */
	switch (param.p->type) {
	case SCTP_PARAM_IPV6_ADDRESS:
		if( PF_INET6 != asoc->base.sk->family)
			break;
		/* Fall through. */
	case SCTP_PARAM_IPV4_ADDRESS:
		sctp_param2sockaddr(&addr, param.addr, asoc->peer.port);
		scope = sctp_scope(peer_addr);
		if (sctp_in_scope(&addr, scope))
			if (!sctp_assoc_add_peer(asoc, &addr, priority))
				return 0;
		break;

	case SCTP_PARAM_COOKIE_PRESERVATIVE:
		if (!sctp_proto.cookie_preserve_enable)
			break;

		stale = ntohl(param.life->lifespan_increment);

		/* Suggested Cookie Life span increment's unit is msec,
		 * (1/1000sec).
		 */
		asoc->cookie_life.tv_sec += stale / 1000;
		asoc->cookie_life.tv_usec += (stale % 1000) * 1000;
		break;

	case SCTP_PARAM_HOST_NAME_ADDRESS:
		SCTP_DEBUG_PRINTK("unimplemented SCTP_HOST_NAME_ADDRESS\n");
		break;

	case SCTP_PARAM_SUPPORTED_ADDRESS_TYPES:
		/* Turn off the default values first so we'll know which
		 * ones are really set by the peer.
		 */
		asoc->peer.ipv4_address = 0;
		asoc->peer.ipv6_address = 0;

		/* Cycle through address types; avoid divide by 0. */
		sat = ntohs(param.p->length) - sizeof(sctp_paramhdr_t);
		if (sat)
			sat /= sizeof(__u16);

		for (i = 0; i < sat; ++i) {
			switch (param.sat->types[i]) {
			case SCTP_PARAM_IPV4_ADDRESS:
				asoc->peer.ipv4_address = 1;
				break;

			case SCTP_PARAM_IPV6_ADDRESS:
				asoc->peer.ipv6_address = 1;
				break;

			case SCTP_PARAM_HOST_NAME_ADDRESS:
				asoc->peer.hostname_address = 1;
				break;

			default: /* Just ignore anything else.  */
				break;
			};
		}
		break;

	case SCTP_PARAM_STATE_COOKIE:
		asoc->peer.cookie_len =
			ntohs(param.p->length) - sizeof(sctp_paramhdr_t);
		asoc->peer.cookie = param.cookie->body;
		break;

	case SCTP_PARAM_HEARTBEAT_INFO:
		/* Would be odd to receive, but it causes no problems. */
		break;

	case SCTP_PARAM_UNRECOGNIZED_PARAMETERS:
		/* Rejected during verify stage. */
		break;

	case SCTP_PARAM_ECN_CAPABLE:
		asoc->peer.ecn_capable = 1;
		break;

	default:
		/* Any unrecognized parameters should have been caught
		 * and handled by sctp_verify_param() which should be
		 * called prior to this routine.  Simply log the error
		 * here.
		 */
		SCTP_DEBUG_PRINTK("Ignoring param: %d for association %p.\n",
				  ntohs(param.p->type), asoc);
		break;
	};

	return retval;
}

/* Select a new verification tag.  */
__u32 sctp_generate_tag(const sctp_endpoint_t *ep)
{
	/* I believe that this random number generator complies with RFC1750.
	 * A tag of 0 is reserved for special cases (e.g. INIT).
	 */
	__u32 x;

	do {
		get_random_bytes(&x, sizeof(__u32));
	} while (x == 0);

	return x;
}

/* Select an initial TSN to send during startup.  */
__u32 sctp_generate_tsn(const sctp_endpoint_t *ep)
{
	__u32 retval;

	get_random_bytes(&retval, sizeof(__u32));
	return retval;
}

/********************************************************************
 * 4th Level Abstractions
 ********************************************************************/

/* Convert from an SCTP IP parameter to a union sctp_addr.  */
void sctp_param2sockaddr(union sctp_addr *addr, sctp_addr_param_t *param,
			 __u16 port)
{
	switch(param->v4.param_hdr.type) {
	case SCTP_PARAM_IPV4_ADDRESS:
		addr->v4.sin_family = AF_INET;
		addr->v4.sin_port = port;
		addr->v4.sin_addr.s_addr = param->v4.addr.s_addr;
		break;

	case SCTP_PARAM_IPV6_ADDRESS:
		addr->v6.sin6_family = AF_INET6;
		addr->v6.sin6_port = port;
		addr->v6.sin6_flowinfo = 0; /* BUG */
		addr->v6.sin6_addr = param->v6.addr;
		addr->v6.sin6_scope_id = 0; /* BUG */
		break;

	default:
		SCTP_DEBUG_PRINTK("Illegal address type %d\n",
				  ntohs(param->v4.param_hdr.type));
		break;
	};
}

/* Convert an IP address in an SCTP param into a sockaddr_in.  */
/* Returns true if a valid conversion was possible.  */
int sctp_addr2sockaddr(union sctp_params p, union sctp_addr *sa)
{
	switch (p.p->type) {
	case SCTP_PARAM_IPV4_ADDRESS:
		sa->v4.sin_addr = *((struct in_addr *)&p.v4->addr);
		sa->v4.sin_family = AF_INET;
		break;

	case SCTP_PARAM_IPV6_ADDRESS:
		*((struct in6_addr *)&sa->v4.sin_addr)
			= p.v6->addr;
		sa->v4.sin_family = AF_INET6;
		break;

        default:
                return 0;
        };

	return 1;
}

/* Convert a sockaddr_in to an IP address in an SCTP param.
 * Returns len if a valid conversion was possible.
 */
int sockaddr2sctp_addr(const union sctp_addr *sa, sctp_addr_param_t *p)
{
	int len = 0;

	switch (sa->v4.sin_family) {
	case AF_INET:
		p->v4.param_hdr.type = SCTP_PARAM_IPV4_ADDRESS;
		p->v4.param_hdr.length = ntohs(sizeof(sctp_ipv4addr_param_t));
		len = sizeof(sctp_ipv4addr_param_t);
		p->v4.addr.s_addr = sa->v4.sin_addr.s_addr;
		break;

	case AF_INET6:
		p->v6.param_hdr.type = SCTP_PARAM_IPV6_ADDRESS;
		p->v6.param_hdr.length = ntohs(sizeof(sctp_ipv6addr_param_t));
		len = sizeof(sctp_ipv6addr_param_t);
		p->v6.addr = *(&sa->v6.sin6_addr);
		break;

	default:
		printk(KERN_WARNING "sockaddr2sctp_addr: Illegal family %d.\n",
		       sa->v4.sin_family);
		return 0;
	};

	return len;
}
