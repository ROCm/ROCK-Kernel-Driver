/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 International Business Machines Corp.
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
 * Note 4: This parameter, when present, specifies all the
 * address types the sending endpoint can support. The absence
 * of this parameter indicates that the sending endpoint can
 * support any address type.
 */
static const sctp_supported_addrs_param_t sat_param = {
	{           
		SCTP_PARAM_SUPPORTED_ADDRESS_TYPES,
		__constant_htons(SCTP_SAT_LEN),
	},
	{               /* types[] */
		SCTP_PARAM_IPV4_ADDRESS,
		SCTP_V6(SCTP_PARAM_IPV6_ADDRESS,)
	}
};

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
	len += padlen;
	err.length  = htons(len);
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
			     int priority)
{
	sctp_inithdr_t init;
	sctpParam_t addrs;
	size_t chunksize;
	sctp_chunk_t *retval = NULL;
	int addrs_len = 0;

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

	chunksize = sizeof(init) + addrs_len + SCTP_SAT_LEN;
	chunksize += sizeof(ecap_param);

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
	sctp_addto_chunk(retval, SCTP_SAT_LEN, &sat_param);
	sctp_addto_chunk(retval, sizeof(ecap_param), &ecap_param);

nodata:
	if (addrs.v)
		kfree(addrs.v);
	return retval;
}

sctp_chunk_t *sctp_make_init_ack(const sctp_association_t *asoc,
				 const sctp_chunk_t *chunk,
				 int priority)
{
	sctp_inithdr_t initack;
	sctp_chunk_t *retval;
	sctpParam_t addrs;
	int addrs_len;
	sctp_cookie_param_t *cookie;
	int cookie_len;
	size_t chunksize;
	int error;
	sctp_scope_t scope;
	sctp_bind_addr_t *bp = NULL;
	int flags;

	retval = NULL;

	/* Build up the bind address list for the association based on
	 * info from the local endpoint and the remote peer.
	 */
	bp = sctp_bind_addr_new(priority);
	if (!bp)
		goto nomem_bindaddr;

	/* Look for supported address types parameter and then build
	 * our address list based on that.
	 */
	scope = sctp_scope(&asoc->peer.active_path->ipaddr);
	flags = (PF_INET6 == asoc->base.sk->family) ? SCTP_ADDR6_ALLOWED : 0;
	if (asoc->peer.ipv4_address)
		flags |= SCTP_ADDR4_PEERSUPP;
	if (asoc->peer.ipv6_address)
		flags |= SCTP_ADDR6_PEERSUPP;
	error = sctp_bind_addr_copy(bp, &asoc->ep->base.bind_addr,
				    scope, priority, flags);
	if (error)
		goto nomem_copyaddr;

	addrs = sctp_bind_addrs_to_raw(bp, &addrs_len, priority);
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

	chunksize = sizeof(initack) + addrs_len + cookie_len;

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
nomem_copyaddr:
	sctp_bind_addr_free(bp);
nomem_bindaddr:
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
	__u16 ssn;
	__u8 flags = SCTP_DATA_NOT_FRAG;

	/* Sockets API Extensions for SCTP 5.2.2
	 *  MSG_UNORDERED - This flag requests the un-ordered delivery of the
	 *  message.  If this flag is clear, the datagram is considered an
	 *  ordered send and a new ssn is generated.  The flags field is set
	 *  in the inner routine - sctp_make_datafrag_empty().
	 */
	if (sinfo->sinfo_flags & MSG_UNORDERED) {
		ssn = 0;
	} else {
		ssn = __sctp_association_get_next_ssn(asoc,
						      sinfo->sinfo_stream);
	}

	return sctp_make_datafrag_empty(asoc, sinfo, data_len, flags, ssn);
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
	sctp_tsnmap_iter_t iter;
	__u16 num_gabs;
	__u16 num_dup_tsns = asoc->peer.next_dup_tsn;
	const sctp_tsnmap_t *map = &asoc->peer.tsn_map;

	ctsn = sctp_tsnmap_get_ctsn(map);
	SCTP_DEBUG_PRINTK("make_sack: sackCTSNAck sent is 0x%x.\n",
			  ctsn);

	/* Count the number of Gap Ack Blocks.  */
	sctp_tsnmap_iter_init(map, &iter);
	for (num_gabs = 0;
	     sctp_tsnmap_next_gap_ack(map, &iter, &gab.start, &gab.end);
	     num_gabs++) {
		/* Do nothing. */
	}

	/* Initialize the SACK header.  */
	sack.cum_tsn_ack	    = htonl(ctsn);
	sack.a_rwnd 		    = htonl(asoc->rwnd);
	sack.num_gap_ack_blocks     = htons(num_gabs);
	sack.num_dup_tsns  = htons(num_dup_tsns);

	length = sizeof(sack)
		+ sizeof(sctp_gap_ack_block_t) * num_gabs
		+ sizeof(sctp_dup_tsn_t) * num_dup_tsns;

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
		sctp_addto_chunk(retval,
				 sizeof(sctp_gap_ack_block_t),
				 &gab);
	}

	/* Register the duplicates.  */
	sctp_addto_chunk(retval,
			 sizeof(sctp_dup_tsn_t) * num_dup_tsns,
			 &asoc->peer.dup_tsns);

nodata:
	return retval;
}

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

/* Make a HEARTBEAT chunk.  */
sctp_chunk_t *sctp_make_heartbeat(const sctp_association_t *asoc,
				  const sctp_transport_t *transport,
				  const void *payload, const size_t paylen)
{
	sctp_chunk_t *retval = sctp_make_chunk(asoc, SCTP_CID_HEARTBEAT,
					       0, paylen);

	if (!retval)
		goto nodata;

	/* Cast away the 'const', as this is just telling the chunk
	 * what transport it belongs to.
	 */
	retval->transport = (sctp_transport_t *) transport;
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

/* Create an Operation Error chunk.  */
sctp_chunk_t *sctp_make_op_error(const sctp_association_t *asoc,
				 const sctp_chunk_t *chunk,
				 __u16 cause_code, const void *payload,
				 size_t paylen)
{
	sctp_chunk_t *retval = sctp_make_chunk(asoc, SCTP_CID_ERROR, 0,
					       sizeof(sctp_errhdr_t) + paylen);

	if (!retval)
		goto nodata;
	sctp_init_cause(retval, cause_code, payload, paylen);

        /* RFC 2960 6.4 Multi-homed SCTP Endpoints
	 *
	 * An endpoint SHOULD transmit reply chunks (e.g., SACK,
	 * HEARTBEAT ACK, * etc.) to the same destination transport
	 * address from which it * received the DATA or control chunk
	 * to which it is replying.
	 */
	if (chunk)
		retval->transport = chunk->transport;

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

/* Set chunk->source based on the IP header in chunk->skb.  */
void sctp_init_source(sctp_chunk_t *chunk)
{
	sockaddr_storage_t *source;
	struct sk_buff *skb;
	struct sctphdr *sh;
	struct iphdr *ih4;
	struct ipv6hdr *ih6;

	source = &chunk->source;
	skb = chunk->skb;
	ih4 = skb->nh.iph;
	ih6 = skb->nh.ipv6h;
	sh = chunk->sctp_hdr;

	switch (ih4->version) {
	case 4:
		source->v4.sin_family = AF_INET;
		source->v4.sin_port = ntohs(sh->source);
		source->v4.sin_addr.s_addr = ih4->saddr;
		break;

	case 6:
		SCTP_V6(
			source->v6.sin6_family = AF_INET6;
			source->v6.sin6_port = ntohs(sh->source);
			source->v6.sin6_addr = ih6->saddr;
			/* FIXME:  What do we do with scope, etc. ? */
			break;
		)

	default:
		/* This is a bogus address type, just bail.  */
		break;
	};
}

/* Extract the source address from a chunk.  */
const sockaddr_storage_t *sctp_source(const sctp_chunk_t *chunk)
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

/* Do a deep copy of a chunk.  */
sctp_chunk_t *sctp_copy_chunk(sctp_chunk_t *chunk, const int priority)
{
	sctp_chunk_t *retval;
	long offset;

	retval = t_new(sctp_chunk_t, priority);
	if (!retval)
		goto nodata;

	/* Do the shallow copy.  */
	*retval = *chunk;

	/* Make sure that the copy does NOT think it is on any lists.  */
	retval->next = NULL;
	retval->prev = NULL;
	retval->list = NULL;
	INIT_LIST_HEAD(&retval->transmitted_list);
	INIT_LIST_HEAD(&retval->frag_list);

	/* Now we copy the deep structure.  */
	retval->skb = skb_copy(chunk->skb, priority);
	if (!retval->skb) {
		kfree(retval);
		goto nodata;
	}

	/* Move the copy headers to point into the new skb.  */
	offset = ((__u8 *)retval->skb->head)
		- ((__u8 *)chunk->skb->head);

	if (retval->param_hdr.v)
		retval->param_hdr.v += offset;
	if (retval->subh.v)
		retval->subh.v += offset;
	if (retval->chunk_end)
		((__u8 *) retval->chunk_end) += offset;
	if (retval->chunk_hdr)
		((__u8 *) retval->chunk_hdr) += offset;
	if (retval->sctp_hdr)
		((__u8 *) retval->sctp_hdr) += offset;
	SCTP_DBG_OBJCNT_INC(chunk);
	return retval;

nodata:
	return NULL;
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
int sctp_user_addto_chunk(sctp_chunk_t *chunk, int len, struct iovec *data)
{
	__u8 *target;
	int err = 0;

	/* Make room in chunk for data.  */
	target = skb_put(chunk->skb, len);

	/* Copy data (whole iovec) into chunk */
	if ((err = memcpy_fromiovec(target, data, len)))
		goto out;

	/* Adjust the chunk length field.  */
	chunk->chunk_hdr->length =
		htons(ntohs(chunk->chunk_hdr->length) + len);
	chunk->chunk_end = chunk->skb->tail;

out:
	return err;
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
			htonl(__sctp_association_get_next_tsn(chunk->asoc));
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
				       int *error)
{
	sctp_association_t *retval = NULL;
	sctp_signed_cookie_t *cookie;
	sctp_cookie_t *bear_cookie;
	int headersize, bodysize;
	int fixed_size, var_size1, var_size2, var_size3;
	__u8 digest_buf[SCTP_SIGNATURE_SIZE];
	int secret;
	sctp_scope_t scope;
	__u8 *raw_addr_list;

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
	var_size1 = ntohs(chunk->chunk_hdr->length) - fixed_size;
	var_size2 = ntohs(bear_cookie->peer_init->chunk_hdr.length);
	var_size3 = bear_cookie->raw_addr_list_len;

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
		*error = -SCTP_IERROR_STALE_COOKIE;
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

	/* Build the bind address list based on the cookie.  */
	raw_addr_list = (__u8 *) bear_cookie +
		sizeof(sctp_cookie_t) + var_size2;
	if (sctp_raw_to_bind_addrs(&retval->base.bind_addr, raw_addr_list,
				   var_size3, retval->base.bind_addr.port,
				   priority)) {
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

/* Unpack the parameters in an INIT packet.
 * FIXME:  There is no return status to allow callers to do
 * error handling.
 */
void sctp_process_init(sctp_association_t *asoc, sctp_cid_t cid,
		       const sockaddr_storage_t *peer_addr,
		       sctp_init_chunk_t *peer_init,
		       int priority)
{
	sctpParam_t param;
	__u8 *end;
	sctp_transport_t *transport;
	list_t *pos, *temp;

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
		sctp_assoc_add_peer(asoc, peer_addr, priority);

	/* Process the initialization parameters.  */
	end = ((__u8 *)peer_init + ntohs(peer_init->chunk_hdr.length));
	for (param.v = peer_init->init_hdr.params;
	     param.v < end;
	     param.v += WORD_ROUND(ntohs(param.p->length))) {
		if (!sctp_process_param(asoc, param, peer_addr, cid, 
					priority))
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

	/* Copy Initiation tag from INIT to VT_peer in cookie.   */
	asoc->c.peer_vtag = asoc->peer.i.init_tag;

	/* Peer Rwnd   : Current calculated value of the peer's rwnd.  */
	asoc->peer.rwnd = asoc->peer.i.a_rwnd;

	/* RFC 2960 7.2.1 The initial value of ssthresh MAY be arbitrarily
	 * high (for example, implementations MAY use the size of the receiver
	 * advertised window).
	 */
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, sctp_transport_t, transports);
		transport->ssthresh = asoc->peer.i.a_rwnd;
	}

	/* Set up the TSN tracking pieces.  */
	sctp_tsnmap_init(&asoc->peer.tsn_map, SCTP_TSN_MAP_SIZE,
			 asoc->peer.i.initial_tsn);

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
	return;

clean_up:
	/* Release the transport structures. */
	list_for_each_safe(pos, temp, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, sctp_transport_t, transports);
		list_del(pos);
		sctp_transport_free(transport);
	}
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
int sctp_process_param(sctp_association_t *asoc, sctpParam_t param,
		       const sockaddr_storage_t *peer_addr,
		       sctp_cid_t cid, int priority)
{
	sockaddr_storage_t addr;
	int j;
	int i;
	int retval = 1;
	sctp_scope_t scope;

	/* We maintain all INIT parameters in network byte order all the
	 * time.  This allows us to not worry about whether the parameters
	 * came from a fresh INIT, and INIT ACK, or were stored in a cookie.
	 */
	switch (param.p->type) {
	case SCTP_PARAM_IPV4_ADDRESS:
		if (SCTP_CID_INIT != cid) {
			sctp_param2sockaddr(&addr, param, asoc->peer.port);
			scope = sctp_scope(peer_addr);
			if (sctp_in_scope(&addr, scope))
				sctp_assoc_add_peer(asoc, &addr, priority);
		}
		break;

	case SCTP_PARAM_IPV6_ADDRESS:
		if (SCTP_CID_INIT != cid) {
			if (PF_INET6 == asoc->base.sk->family) {
				sctp_param2sockaddr(&addr, param,
						    asoc->peer.port);
				scope = sctp_scope(peer_addr);
				if (sctp_in_scope(&addr, scope))
					sctp_assoc_add_peer(asoc, &addr,
							    priority);
			}
		}
		break;

	case SCTP_PARAM_COOKIE_PRESERVATIVE:
		asoc->cookie_preserve =
			ntohl(param.bht->lifespan_increment);
		break;

	case SCTP_PARAM_HOST_NAME_ADDRESS:
		SCTP_DEBUG_PRINTK("unimplmented SCTP_HOST_NAME_ADDRESS\n");
		break;

	case SCTP_PARAM_SUPPORTED_ADDRESS_TYPES:
		/* Turn off the default values first so we'll know which
		 * ones are really set by the peer.
		 */
		asoc->peer.ipv4_address = 0;
		asoc->peer.ipv6_address = 0;

		j = (ntohs(param.p->length) -
		     sizeof(sctp_paramhdr_t)) /
			sizeof(__u16);
		for (i = 0; i < j; ++i) {
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
			ntohs(param.p->length) -
			sizeof(sctp_paramhdr_t);
		asoc->peer.cookie = param.cookie->body;
		break;

	case SCTP_PARAM_HEATBEAT_INFO:
		SCTP_DEBUG_PRINTK("unimplmented "
				  "SCTP_PARAM_HEATBEAT_INFO\n");
		break;

	case SCTP_PARAM_UNRECOGNIZED_PARAMETERS:
		SCTP_DEBUG_PRINTK("unimplemented "
				  "SCTP_PARAM_UNRECOGNIZED_PARAMETERS\n");
		break;

	case SCTP_PARAM_ECN_CAPABLE:
		asoc->peer.ecn_capable = 1;
		break;

	default:
		SCTP_DEBUG_PRINTK("Ignoring param: %d for association %p.\n",
				  ntohs(param.p->type), asoc);
		/* FIXME:  The entire parameter processing really needs
		 * redesigned.  For now, always return success as doing
		 * otherwise craters the system.
		 */
		retval = 1;

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
	/* I believe that this random number generator complies with RFC1750.  */
  	__u32 retval;

	get_random_bytes(&retval, sizeof(__u32));
	return retval;
}

/********************************************************************
 * 4th Level Abstractions
 ********************************************************************/

/* Convert from an SCTP IP parameter to a sockaddr_storage_t.  */
void sctp_param2sockaddr(sockaddr_storage_t *addr, sctpParam_t param, __u16 port)
{
	switch(param.p->type) {
	case SCTP_PARAM_IPV4_ADDRESS:
		addr->v4.sin_family = AF_INET;
		addr->v4.sin_port = port;
		addr->v4.sin_addr.s_addr = param.v4->addr.s_addr;
		break;

	case SCTP_PARAM_IPV6_ADDRESS:
		addr->v6.sin6_family = AF_INET6;
		addr->v6.sin6_port = port;
		addr->v6.sin6_flowinfo = 0; /* BUG */
		addr->v6.sin6_addr = param.v6->addr;
		addr->v6.sin6_scope_id = 0; /* BUG */
		break;

	default:
		SCTP_DEBUG_PRINTK("Illegal address type %d\n",
				  ntohs(param.p->type));
		break;
	};
}

/* Convert an IP address in an SCTP param into a sockaddr_in.  */
/* Returns true if a valid conversion was possible.  */
int sctp_addr2sockaddr(sctpParam_t p, sockaddr_storage_t *sa)
{
        if (!p.v)
		return 0;

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

/* Convert from an IP version number to an Address Family symbol.  */
int ipver2af(__u8 ipver)
{
	int family;

	switch (ipver) {
	case 4:
		family = AF_INET;
		break;

	case 6:
		family = AF_INET6;
		break;

	default:
		family = 0;
		break;
	};

	return family;
}

/* Convert a sockaddr_in to  IP address in an SCTP para.  */
/* Returns true if a valid conversion was possible.  */
int sockaddr2sctp_addr(const sockaddr_storage_t *sa, sctpParam_t p)
{
	int len = 0;

	switch (sa->v4.sin_family) {
	case AF_INET:
		p.p->type = SCTP_PARAM_IPV4_ADDRESS;
		p.p->length = ntohs(sizeof(sctp_ipv4addr_param_t));
		len = sizeof(sctp_ipv4addr_param_t);
		p.v4->addr.s_addr = sa->v4.sin_addr.s_addr;
		break;

	case AF_INET6:
		p.p->type = SCTP_PARAM_IPV6_ADDRESS;
		p.p->length = ntohs(sizeof(sctp_ipv6addr_param_t));
		len = sizeof(sctp_ipv6addr_param_t);
		p.v6->addr = *(&sa->v6.sin6_addr);
		break;

	default:
		printk(KERN_WARNING "sockaddr2sctp_addr: Illegal family %d.\n",
		       sa->v4.sin_family);
		return 0;
	};

	return len;
}
