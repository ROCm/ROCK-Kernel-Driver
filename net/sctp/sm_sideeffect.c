/* SCTP kernel reference Implementation
 * Copyright (c) 1999 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2002 International Business Machines Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * These functions work with the state functions in sctp_sm_statefuns.c
 * to implement that state operations.  These functions implement the
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
 *    Jon Grimm             <jgrimm@austin.ibm.com>
 *    Hui Huang		    <hui.huang@nokia.com>
 *    Dajiang Zhang	    <dajiang.zhang@nokia.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/ip.h>
#include <net/sock.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Do forward declarations of static functions.  */
static void sctp_do_ecn_ce_work(sctp_association_t *asoc,
				__u32 lowest_tsn);
static sctp_chunk_t *sctp_do_ecn_ecne_work(sctp_association_t *asoc,
					   __u32 lowest_tsn,
					   sctp_chunk_t *);
static void sctp_do_ecn_cwr_work(sctp_association_t *asoc,
				 __u32 lowest_tsn);

static void sctp_do_8_2_transport_strike(sctp_association_t *asoc,
					 sctp_transport_t *transport);
static void sctp_cmd_init_failed(sctp_cmd_seq_t *, sctp_association_t *asoc);
static void sctp_cmd_assoc_failed(sctp_cmd_seq_t *, sctp_association_t *asoc);
static void sctp_cmd_process_init(sctp_cmd_seq_t *, sctp_association_t *asoc,
				  sctp_chunk_t *chunk,
				  sctp_init_chunk_t *peer_init,
				  int priority);
static void sctp_cmd_hb_timers_start(sctp_cmd_seq_t *, sctp_association_t *);
static void sctp_cmd_set_bind_addrs(sctp_cmd_seq_t *, sctp_association_t *,
				    sctp_bind_addr_t *);
static void sctp_cmd_transport_reset(sctp_cmd_seq_t *, sctp_association_t *,
				     sctp_transport_t *);
static void sctp_cmd_transport_on(sctp_cmd_seq_t *, sctp_association_t *,
				  sctp_transport_t *, sctp_chunk_t *);
static int sctp_cmd_process_sack(sctp_cmd_seq_t *, sctp_association_t *,
				 sctp_sackhdr_t *);
static void sctp_cmd_setup_t2(sctp_cmd_seq_t *, sctp_association_t *,
			      sctp_chunk_t *);

/* These three macros allow us to pull the debugging code out of the
 * main flow of sctp_do_sm() to keep attention focused on the real
 * functionality there.
 */
#define DEBUG_PRE \
	SCTP_DEBUG_PRINTK("sctp_do_sm prefn: " \
			  "ep %p, %s, %s, asoc %p[%s], %s\n", \
			  ep, sctp_evttype_tbl[event_type], \
			  (*debug_fn)(subtype), asoc, \
			  sctp_state_tbl[state], state_fn->name)

#define DEBUG_POST \
	SCTP_DEBUG_PRINTK("sctp_do_sm postfn: " \
			  "asoc %p, status: %s\n", \
			  asoc, sctp_status_tbl[status])

#define DEBUG_POST_SFX \
	SCTP_DEBUG_PRINTK("sctp_do_sm post sfx: error %d, asoc %p[%s]\n", \
			  error, asoc, \
			  sctp_state_tbl[sctp_id2assoc(ep->base.sk, \
			  sctp_assoc2id(asoc))?asoc->state:SCTP_STATE_CLOSED])

/*
 * This is the master state machine processing function.
 *
 * If you want to understand all of lksctp, this is a
 * good place to start.
 */
int sctp_do_sm(sctp_event_t event_type, sctp_subtype_t subtype,
	       sctp_state_t state,
	       sctp_endpoint_t *ep,
	       sctp_association_t *asoc,
	       void *event_arg,
	       int priority)
{
	sctp_cmd_seq_t commands;
	sctp_sm_table_entry_t *state_fn;
	sctp_disposition_t status;
	int error = 0;
	typedef const char *(printfn_t)(sctp_subtype_t);

	static printfn_t *table[] = {
		NULL, sctp_cname, sctp_tname, sctp_oname, sctp_pname,
	};
	printfn_t *debug_fn  __attribute__ ((unused)) = table[event_type];

	/* Look up the state function, run it, and then process the
	 * side effects.  These three steps are the heart of lksctp.
	 */
	state_fn = sctp_sm_lookup_event(event_type, state, subtype);

	sctp_init_cmd_seq(&commands);

	DEBUG_PRE;
	status = (*state_fn->fn)(ep, asoc, subtype, event_arg, &commands);
	DEBUG_POST;

	error = sctp_side_effects(event_type, subtype, state,
				  ep, asoc, event_arg,
				  status, &commands,
				  priority);
	DEBUG_POST_SFX;

	return error;
}

#undef DEBUG_PRE
#undef DEBUG_POST

/*****************************************************************
 * This the master state function side effect processing function.
 *****************************************************************/
int sctp_side_effects(sctp_event_t event_type, sctp_subtype_t subtype,
		      sctp_state_t state,
		      sctp_endpoint_t *ep,
		      sctp_association_t *asoc,
		      void *event_arg,
		      sctp_disposition_t status,
		      sctp_cmd_seq_t *commands,
		      int priority)
{
	int error;

	/* FIXME - Most of the dispositions left today would be categorized
	 * as "exceptional" dispositions.  For those dispositions, it
	 * may not be proper to run through any of the commands at all.
	 * For example, the command interpreter might be run only with
	 * disposition SCTP_DISPOSITION_CONSUME.
	 */
	if (0 != (error = sctp_cmd_interpreter(event_type, subtype, state,
					       ep, asoc,
					       event_arg, status,
					       commands, priority)))
		goto bail;

	switch (status) {
	case SCTP_DISPOSITION_DISCARD:
		SCTP_DEBUG_PRINTK("Ignored sctp protocol event - state %d, "
				  "event_type %d, event_id %d\n",
				  state, event_type, subtype.chunk);
		break;

	case SCTP_DISPOSITION_NOMEM:
		/* We ran out of memory, so we need to discard this
		 * packet.
		 */
		/* BUG--we should now recover some memory, probably by
		 * reneging...
		 */
		break;

        case SCTP_DISPOSITION_DELETE_TCB:
		/* This should now be a command. */
		break;

	case SCTP_DISPOSITION_CONSUME:
	case SCTP_DISPOSITION_ABORT:
		/*
		 * We should no longer have much work to do here as the
		 * real work has been done as explicit commands above.
		 */
		break;

	case SCTP_DISPOSITION_VIOLATION:
		printk(KERN_ERR "sctp protocol violation state %d "
		       "chunkid %d\n", state, subtype.chunk);
		break;

	case SCTP_DISPOSITION_NOT_IMPL:
		printk(KERN_WARNING "sctp unimplemented feature in state %d, "
		       "event_type %d, event_id %d\n",
		       state, event_type, subtype.chunk);
		break;

	case SCTP_DISPOSITION_BUG:
		printk(KERN_ERR "sctp bug in state %d, "
		       "event_type %d, event_id %d\n",
		       state, event_type, subtype.chunk);
		BUG();
		break;

	default:
		printk(KERN_ERR "sctp impossible disposition %d "
		       "in state %d, event_type %d, event_id %d\n",
		       status, state, event_type, subtype.chunk);
		BUG();
		break;
	};

bail:
	return error;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* This is the side-effect interpreter.  */
int sctp_cmd_interpreter(sctp_event_t event_type, sctp_subtype_t subtype,
			 sctp_state_t state, sctp_endpoint_t *ep,
			 sctp_association_t *asoc, void *event_arg,
			 sctp_disposition_t status, sctp_cmd_seq_t *commands,
			 int priority)
{
	int error = 0;
	int force;
	sctp_cmd_t *command;
	sctp_chunk_t *new_obj;
	sctp_chunk_t *chunk;
	sctp_packet_t *packet;
	struct list_head *pos;
	struct timer_list *timer;
	unsigned long timeout;
	sctp_transport_t *t;
	sctp_sackhdr_t sackh;

	chunk = (sctp_chunk_t *) event_arg;

	/* Note:  This whole file is a huge candidate for rework.
	 * For example, each command could either have its own handler, so
	 * the loop would look like:
	 *     while (cmds)
	 *         cmd->handle(x, y, z)
	 * --jgrimm
	 */
	while (NULL != (command = sctp_next_cmd(commands))) {
		switch (command->verb) {
		case SCTP_CMD_NOP:
			/* Do nothing. */
			break;

		case SCTP_CMD_NEW_ASOC:
			/* Register a new association.  */
			asoc = command->obj.ptr;
			/* Register with the endpoint.  */
			sctp_endpoint_add_asoc(ep, asoc);
			sctp_hash_established(asoc);
			break;

		case SCTP_CMD_UPDATE_ASSOC:
		       sctp_assoc_update(asoc, command->obj.ptr);
		       break;

		case SCTP_CMD_PURGE_OUTQUEUE:
		       sctp_outqueue_teardown(&asoc->outqueue);
		       break;

		case SCTP_CMD_DELETE_TCB:
			/* Delete the current association.  */
			sctp_unhash_established(asoc);
			sctp_association_free(asoc);
			asoc = NULL;
			break;

		case SCTP_CMD_NEW_STATE:
			/* Enter a new state.  */
			asoc->state = command->obj.state;
			asoc->state_timestamp = jiffies;
			break;

		case SCTP_CMD_REPORT_TSN:
			/* Record the arrival of a TSN.  */
			sctp_tsnmap_mark(&asoc->peer.tsn_map,
					 command->obj.u32);
			break;

		case SCTP_CMD_GEN_SACK:
			/* Generate a Selective ACK.
			 * The argument tells us whether to just count
			 * the packet and MAYBE generate a SACK, or
			 * force a SACK out.
			 */
			force = command->obj.i32;
			error = sctp_gen_sack(asoc, force, commands);
			break;

		case SCTP_CMD_PROCESS_SACK:
			/* Process an inbound SACK.  */
			error = sctp_cmd_process_sack(commands, asoc,
						      command->obj.ptr);
			break;

		case SCTP_CMD_GEN_INIT_ACK:
			/* Generate an INIT ACK chunk.  */
			new_obj = sctp_make_init_ack(asoc, chunk, GFP_ATOMIC,
						     0);
			if (!new_obj)
				goto nomem;

			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(new_obj));
			break;

		case SCTP_CMD_PEER_INIT:
			/* Process a unified INIT from the peer.  */
			sctp_cmd_process_init(commands, asoc, chunk,
					      command->obj.ptr, priority);
			break;

		case SCTP_CMD_GEN_COOKIE_ECHO:
			/* Generate a COOKIE ECHO chunk.  */
			new_obj = sctp_make_cookie_echo(asoc, chunk);
			if (!new_obj) {
				if (command->obj.ptr)
					sctp_free_chunk(command->obj.ptr);
				goto nomem;
			}
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(new_obj));

			/* If there is an ERROR chunk to be sent along with
			 * the COOKIE_ECHO, send it, too.
			 */
			if (command->obj.ptr)
				sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
						SCTP_CHUNK(command->obj.ptr));
			break;

		case SCTP_CMD_GEN_SHUTDOWN:
			/* Generate SHUTDOWN when in SHUTDOWN_SENT state.
			 * Reset error counts.
			 */
			asoc->overall_error_count = 0;

			/* Generate a SHUTDOWN chunk.  */
			new_obj = sctp_make_shutdown(asoc);
			if (!new_obj)
				goto nomem;
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(new_obj));
			break;

		case SCTP_CMD_CHUNK_ULP:
			/* Send a chunk to the sockets layer.  */
			SCTP_DEBUG_PRINTK("sm_sideff: %s %p, %s %p.\n",
					  "chunk_up:",
					  command->obj.ptr,
					  "ulpq:",
					  &asoc->ulpq);
			sctp_ulpqueue_tail_data(&asoc->ulpq,
						command->obj.ptr,
						GFP_ATOMIC);
			break;

		case SCTP_CMD_EVENT_ULP:
			/* Send a notification to the sockets layer.  */
			SCTP_DEBUG_PRINTK("sm_sideff: %s %p, %s %p.\n",
					  "event_up:",
					  command->obj.ptr,
					  "ulpq:",
					  &asoc->ulpq);
			sctp_ulpqueue_tail_event(&asoc->ulpq,
						 command->obj.ptr);
			break;

		case SCTP_CMD_REPLY:
			/* Send a chunk to our peer.  */
			error = sctp_push_outqueue(&asoc->outqueue,
						   command->obj.ptr);
			break;

		case SCTP_CMD_SEND_PKT:
			/* Send a full packet to our peer.  */
			packet = command->obj.ptr;
			sctp_packet_transmit(packet);
			sctp_ootb_pkt_free(packet);
			break;

		case SCTP_CMD_RETRAN:
			/* Mark a transport for retransmission.  */
			sctp_retransmit(&asoc->outqueue,
					command->obj.transport, 0);
			break;

		case SCTP_CMD_TRANSMIT:
			/* Kick start transmission. */
			error = sctp_flush_outqueue(&asoc->outqueue, 0);
			break;

		case SCTP_CMD_ECN_CE:
			/* Do delayed CE processing.   */
			sctp_do_ecn_ce_work(asoc, command->obj.u32);
			break;

		case SCTP_CMD_ECN_ECNE:
			/* Do delayed ECNE processing. */
			new_obj = sctp_do_ecn_ecne_work(asoc,
							command->obj.u32,
							chunk);
			if (new_obj) {
				sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
						SCTP_CHUNK(new_obj));
			}
			break;

		case SCTP_CMD_ECN_CWR:
			/* Do delayed CWR processing.  */
			sctp_do_ecn_cwr_work(asoc, command->obj.u32);
			break;

		case SCTP_CMD_SETUP_T2:
			sctp_cmd_setup_t2(commands, asoc, command->obj.ptr);
			break;

		case SCTP_CMD_TIMER_START:
			timer = &asoc->timers[command->obj.to];
			timeout = asoc->timeouts[command->obj.to];
			if (!timeout)
				BUG();

			timer->expires = jiffies + timeout;
			sctp_association_hold(asoc);
			add_timer(timer);
			break;

		case SCTP_CMD_TIMER_RESTART:
			timer = &asoc->timers[command->obj.to];
			timeout = asoc->timeouts[command->obj.to];
			if (!mod_timer(timer, jiffies + timeout))
				sctp_association_hold(asoc);
			break;

		case SCTP_CMD_TIMER_STOP:
			timer = &asoc->timers[command->obj.to];
			if (timer_pending(timer) && del_timer(timer))
				sctp_association_put(asoc);
			break;

		case SCTP_CMD_INIT_RESTART:

			/* Do the needed accounting and updates
			 * associated with restarting an initialization
			 * timer.
			 */
			asoc->counters[SCTP_COUNTER_INIT_ERROR]++;
			asoc->timeouts[command->obj.to] *= 2;
			if (asoc->timeouts[command->obj.to] >
			    asoc->max_init_timeo) {
				asoc->timeouts[command->obj.to] =
					asoc->max_init_timeo;
			}

			/* If we've sent any data bundled with
			 * COOKIE-ECHO we need to resend.
			 */
			list_for_each(pos, &asoc->peer.transport_addr_list) {
				t = list_entry(pos, sctp_transport_t,
					       transports);
				sctp_retransmit_mark(&asoc->outqueue, t, 0);
			}

			sctp_add_cmd_sf(commands,
					SCTP_CMD_TIMER_RESTART,
					SCTP_TO(command->obj.to));
			break;

		case SCTP_CMD_INIT_FAILED:
			sctp_cmd_init_failed(commands, asoc);
			break;

		case SCTP_CMD_ASSOC_FAILED:
			sctp_cmd_assoc_failed(commands, asoc);
			break;

		case SCTP_CMD_COUNTER_INC:
			asoc->counters[command->obj.counter]++;
			break;

		case SCTP_CMD_COUNTER_RESET:
			asoc->counters[command->obj.counter] = 0;
			break;

		case SCTP_CMD_REPORT_DUP:
			if (asoc->peer.next_dup_tsn < SCTP_MAX_DUP_TSNS) {
				asoc->peer.dup_tsns[asoc->peer.next_dup_tsn++] =
					ntohl(command->obj.u32);
			}
			break;

		case SCTP_CMD_REPORT_BIGGAP:
			SCTP_DEBUG_PRINTK("Big gap: %x to %x\n",
					  sctp_tsnmap_get_ctsn(
						  &asoc->peer.tsn_map),
					  command->obj.u32);
			break;

		case SCTP_CMD_REPORT_BAD_TAG:
			SCTP_DEBUG_PRINTK("vtag mismatch!\n");
			break;

		case SCTP_CMD_SET_BIND_ADDR:
		        sctp_cmd_set_bind_addrs(commands, asoc,
						command->obj.bp);
			break;

		case SCTP_CMD_STRIKE:
			/* Mark one strike against a transport.  */
			sctp_do_8_2_transport_strike(asoc,
						     command->obj.transport);
			break;

		case SCTP_CMD_TRANSPORT_RESET:
			t = command->obj.transport;
			sctp_cmd_transport_reset(commands, asoc, t);
			break;

		case SCTP_CMD_TRANSPORT_ON:
			t = command->obj.transport;
			sctp_cmd_transport_on(commands, asoc, t, chunk);
			break;

		case SCTP_CMD_HB_TIMERS_START:
			sctp_cmd_hb_timers_start(commands, asoc);
			break;

		case SCTP_CMD_REPORT_ERROR:
			error = command->obj.error;
			break;

		case SCTP_CMD_PROCESS_CTSN:
			/* Dummy up a SACK for processing. */
			sackh.cum_tsn_ack = command->obj.u32;
			sackh.a_rwnd = 0;
			sackh.num_gap_ack_blocks = 0;
			sackh.num_dup_tsns = 0;
			sctp_add_cmd_sf(commands,
					SCTP_CMD_PROCESS_SACK,
					SCTP_SACKH(&sackh));
			break;

		case SCTP_CMD_DISCARD_PACKET:
			/* We need to discard the whole packet.  */
			chunk->pdiscard = 1;
			break;

		default:
			printk(KERN_WARNING "Impossible command: %u, %p\n",
			       command->verb, command->obj.ptr);
			break;
		};
	}

	return error;

nomem:
	error = -ENOMEM;
	return error;
}

/* A helper function for delayed processing of INET ECN CE bit. */
static void sctp_do_ecn_ce_work(sctp_association_t *asoc, __u32 lowest_tsn)
{
	/*
	 * Save the TSN away for comparison when we receive CWR
	 * Note: dp->TSN is expected in host endian
	 */

	asoc->last_ecne_tsn = lowest_tsn;
	asoc->need_ecne = 1;
}

/* Helper function for delayed processing of SCTP ECNE chunk.  */
/* RFC 2960 Appendix A
 *
 * RFC 2481 details a specific bit for a sender to send in
 * the header of its next outbound TCP segment to indicate to
 * its peer that it has reduced its congestion window.  This
 * is termed the CWR bit.  For SCTP the same indication is made
 * by including the CWR chunk.  This chunk contains one data
 * element, i.e. the TSN number that was sent in the ECNE chunk.
 * This element represents the lowest TSN number in the datagram
 * that was originally marked with the CE bit.
 */
static sctp_chunk_t *sctp_do_ecn_ecne_work(sctp_association_t *asoc,
					   __u32 lowest_tsn,
					   sctp_chunk_t *chunk)
{
	sctp_chunk_t *repl;
	sctp_transport_t *transport;

	/* Our previously transmitted packet ran into some congestion
	 * so we should take action by reducing cwnd and ssthresh
	 * and then ACK our peer that we we've done so by
	 * sending a CWR.
	 */

	/* Find which transport's congestion variables
	 * need to be adjusted.
	 */

	transport = sctp_assoc_lookup_tsn(asoc, lowest_tsn);

	/* Update the congestion variables. */
	if (transport)
		sctp_transport_lower_cwnd(transport, SCTP_LOWER_CWND_ECNE);

	/* Save away a rough idea of when we last sent out a CWR.
	 * We compare against this value (see above) to decide if
	 * this is a fairly new request.
	 * Note that this is not a perfect solution.  We may
	 * have moved beyond the window (several times) by the
	 * next time we get an ECNE.  However, it is cute.  This idea
	 * came from Randy's reference code.
	 *
	 * Here's what RFC 2960 has to say about CWR.  This is NOT
	 * what we do.
	 *
	 * RFC 2960 Appendix A
	 *
	 *    CWR:
	 *
	 *    RFC 2481 details a specific bit for a sender to send in
	 *    the header of its next outbound TCP segment to indicate
	 *    to its peer that it has reduced its congestion window.
	 *    This is termed the CWR bit.  For SCTP the same
	 *    indication is made by including the CWR chunk.  This
	 *    chunk contains one data element, i.e. the TSN number
	 *    that was sent in the ECNE chunk.  This element
	 *    represents the lowest TSN number in the datagram that
	 *    was originally marked with the CE bit.
	 */
	asoc->last_cwr_tsn = asoc->next_tsn - 1;

	repl = sctp_make_cwr(asoc, asoc->last_cwr_tsn, chunk);

	/* If we run out of memory, it will look like a lost CWR.  We'll
	 * get back in sync eventually.
	 */
	return repl;
}

/* Helper function to do delayed processing of ECN CWR chunk.  */
static void sctp_do_ecn_cwr_work(sctp_association_t *asoc,
				 __u32 lowest_tsn)
{
	/* Turn off ECNE getting auto-prepended to every outgoing
	 * packet
	 */
	asoc->need_ecne = 0;
}

/* This macro is to compress the text a bit...  */
#define AP(v) asoc->peer.v

/* Generate SACK if necessary.  We call this at the end of a packet.  */
int sctp_gen_sack(sctp_association_t *asoc, int force, sctp_cmd_seq_t *commands)
{
	__u32 ctsn, max_tsn_seen;
	sctp_chunk_t *sack;
	int error = 0;

	if (force)
		asoc->peer.sack_needed = 1;

	ctsn = sctp_tsnmap_get_ctsn(&asoc->peer.tsn_map);
	max_tsn_seen = sctp_tsnmap_get_max_tsn_seen(&asoc->peer.tsn_map);

	/* From 12.2 Parameters necessary per association (i.e. the TCB):
	 *
	 * Ack State : This flag indicates if the next received packet
	 * 	     : is to be responded to with a SACK. ...
	 *	     : When DATA chunks are out of order, SACK's
	 *           : are not delayed (see Section 6).
	 *
	 * [This is actually not mentioned in Section 6, but we
	 * implement it here anyway. --piggy]
	 */
        if (max_tsn_seen != ctsn)
		asoc->peer.sack_needed = 1;

	/* From 6.2  Acknowledgement on Reception of DATA Chunks:
	 *
	 * Section 4.2 of [RFC2581] SHOULD be followed. Specifically,
	 * an acknowledgement SHOULD be generated for at least every
	 * second packet (not every second DATA chunk) received, and
	 * SHOULD be generated within 200 ms of the arrival of any
	 * unacknowledged DATA chunk. ...
	 */
	if (!asoc->peer.sack_needed) {
		/* We will need a SACK for the next packet.  */
		asoc->peer.sack_needed = 1;
		goto out;
	} else {
		sack = sctp_make_sack(asoc);
		if (!sack)
			goto nomem;

		asoc->peer.sack_needed = 0;
		asoc->peer.next_dup_tsn = 0;

		error = sctp_push_outqueue(&asoc->outqueue, sack);

		/* Stop the SACK timer.  */
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_SACK));
	}

out:
	return error;

nomem:
	error = -ENOMEM;
	return error;
}

/* Handle a duplicate TSN.  */
void sctp_do_TSNdup(sctp_association_t *asoc, sctp_chunk_t *chunk, long gap)
{
#if 0
	sctp_chunk_t *sack;

	/* Caution:  gap < 2 * SCTP_TSN_MAP_SIZE
	 * 	so gap can be negative.
	 *
	 *		--xguo
	 */

	/* Count this TSN.  */
	if (gap < SCTP_TSN_MAP_SIZE) {
		asoc->peer.tsn_map[gap]++;
	} else {
		asoc->peer.tsn_map_overflow[gap - SCTP_TSN_MAP_SIZE]++;
	}

	/* From 6.2  Acknowledgement on Reception of DATA Chunks
	 *
	 * When a packet arrives with duplicate DATA chunk(s)
	 * and with no new DATA chunk(s), the endpoint MUST
	 * immediately send a SACK with no delay. If a packet
	 * arrives with duplicate DATA chunk(s) bundled with
	 * new DATA chunks, the endpoint MAY immediately send a
	 * SACK.  Normally receipt of duplicate DATA chunks
	 * will occur when the original SACK chunk was lost and
	 * the peer's RTO has expired. The duplicate TSN
	 * number(s) SHOULD be reported in the SACK as
	 * duplicate.
	 */
	asoc->counters[SctpCounterAckState] = 2;
#endif /* 0 */
} /* sctp_do_TSNdup() */

#undef AP

/* When the T3-RTX timer expires, it calls this function to create the
 * relevant state machine event.
 */
void sctp_generate_t3_rtx_event(unsigned long peer)
{
	int error;
	sctp_transport_t *transport = (sctp_transport_t *) peer;
	sctp_association_t *asoc = transport->asoc;

	/* Check whether a task is in the sock.  */

	sctp_bh_lock_sock(asoc->base.sk);
	if (__sctp_sock_busy(asoc->base.sk)) {
		SCTP_DEBUG_PRINTK("%s:Sock is busy.\n", __FUNCTION__);

		/* Try again later.  */
		if (!mod_timer(&transport->T3_rtx_timer, jiffies + (HZ/20)))
			sctp_transport_hold(transport);
		goto out_unlock;
	}

	/* Is this transport really dead and just waiting around for
	 * the timer to let go of the reference?
	 */
	if (transport->dead)
		goto out_unlock;

	/* Run through the state machine.  */
	error = sctp_do_sm(SCTP_EVENT_T_TIMEOUT,
			   SCTP_ST_TIMEOUT(SCTP_EVENT_TIMEOUT_T3_RTX),
			   asoc->state,
			   asoc->ep, asoc,
			   transport, GFP_ATOMIC);

	if (error)
		asoc->base.sk->err = -error;

out_unlock:
	sctp_bh_unlock_sock(asoc->base.sk);
	sctp_transport_put(transport);
}

/* This is a sa interface for producing timeout events.  It works
 * for timeouts which use the association as their parameter.
 */
static void sctp_generate_timeout_event(sctp_association_t *asoc,
					sctp_event_timeout_t timeout_type)
{
	int error = 0;

	sctp_bh_lock_sock(asoc->base.sk);
	if (__sctp_sock_busy(asoc->base.sk)) {
		SCTP_DEBUG_PRINTK("%s:Sock is busy: timer %d\n",
				  __FUNCTION__,
				  timeout_type);

		/* Try again later.  */
		if (!mod_timer(&asoc->timers[timeout_type], jiffies + (HZ/20)))
			sctp_association_hold(asoc);
		goto out_unlock;
	}

	/* Is this association really dead and just waiting around for
	 * the timer to let go of the reference?
	 */
	if (asoc->base.dead)
		goto out_unlock;

	/* Run through the state machine.  */
	error = sctp_do_sm(SCTP_EVENT_T_TIMEOUT,
			   SCTP_ST_TIMEOUT(timeout_type),
			   asoc->state, asoc->ep, asoc,
			   (void *)timeout_type,
			   GFP_ATOMIC);

	if (error)
		asoc->base.sk->err = -error;

out_unlock:
	sctp_bh_unlock_sock(asoc->base.sk);
	sctp_association_put(asoc);
}

void sctp_generate_t1_cookie_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T1_COOKIE);
}

void sctp_generate_t1_init_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T1_INIT);
}

void sctp_generate_t2_shutdown_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T2_SHUTDOWN);
}

void sctp_generate_t5_shutdown_guard_event(unsigned long data)
{
        sctp_association_t *asoc = (sctp_association_t *)data;
        sctp_generate_timeout_event(asoc,
				    SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD);

} /* sctp_generate_t5_shutdown_guard_event() */

void sctp_generate_autoclose_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_AUTOCLOSE);
}

/* Generate a heart beat event.  If the sock is busy, reschedule.   Make
 * sure that the transport is still valid.
 */
void sctp_generate_heartbeat_event(unsigned long data)
{
	int error = 0;
	sctp_transport_t *transport = (sctp_transport_t *) data;
	sctp_association_t *asoc = transport->asoc;

	sctp_bh_lock_sock(asoc->base.sk);
	if (__sctp_sock_busy(asoc->base.sk)) {
		SCTP_DEBUG_PRINTK("%s:Sock is busy.\n", __FUNCTION__);

		/* Try again later.  */
		if (!mod_timer(&transport->hb_timer, jiffies + (HZ/20)))
			sctp_transport_hold(transport);
		goto out_unlock;
	}

	/* Is this structure just waiting around for us to actually
	 * get destroyed?
	 */
	if (transport->dead)
		goto out_unlock;

	error = sctp_do_sm(SCTP_EVENT_T_TIMEOUT,
			   SCTP_ST_TIMEOUT(SCTP_EVENT_TIMEOUT_HEARTBEAT),
			   asoc->state,
			   asoc->ep, asoc,
			   transport, GFP_ATOMIC);

         if (error)
		 asoc->base.sk->err = -error;

out_unlock:
	sctp_bh_unlock_sock(asoc->base.sk);
	sctp_transport_put(transport);
}

/* Inject a SACK Timeout event into the state machine.  */
void sctp_generate_sack_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_SACK);
}

void sctp_generate_pmtu_raise_event(unsigned long data)
{
	sctp_association_t *asoc = (sctp_association_t *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_PMTU_RAISE);
}

sctp_timer_event_t *sctp_timer_events[SCTP_NUM_TIMEOUT_TYPES] = {
	NULL,
	sctp_generate_t1_cookie_event,
	sctp_generate_t1_init_event,
	sctp_generate_t2_shutdown_event,
	NULL,
	NULL,
	sctp_generate_t5_shutdown_guard_event,
	sctp_generate_heartbeat_event,
	sctp_generate_sack_event,
	sctp_generate_autoclose_event,
	sctp_generate_pmtu_raise_event,
};

/********************************************************************
 * 3rd Level Abstractions
 ********************************************************************/

/* RFC 2960 8.2 Path Failure Detection
 *
 * When its peer endpoint is multi-homed, an endpoint should keep a
 * error counter for each of the destination transport addresses of the
 * peer endpoint.
 *
 * Each time the T3-rtx timer expires on any address, or when a
 * HEARTBEAT sent to an idle address is not acknowledged within a RTO,
 * the error counter of that destination address will be incremented.
 * When the value in the error counter exceeds the protocol parameter
 * 'Path.Max.Retrans' of that destination address, the endpoint should
 * mark the destination transport address as inactive, and a
 * notification SHOULD be sent to the upper layer.
 *
 */
static void sctp_do_8_2_transport_strike(sctp_association_t *asoc,
					 sctp_transport_t *transport)
{
	/* The check for association's overall error counter exceeding the
	 * threshold is done in the state function.
	 */
	asoc->overall_error_count++;

	if (transport->state.active &&
	    (transport->error_count++ >= transport->error_threshold)) {
		SCTP_DEBUG_PRINTK("transport_strike: transport "
				  "IP:%d.%d.%d.%d failed.\n",
				  NIPQUAD(transport->ipaddr.v4.sin_addr));
		sctp_assoc_control_transport(asoc, transport,
					     SCTP_TRANSPORT_DOWN,
					     SCTP_FAILED_THRESHOLD);
	}

	/* E2) For the destination address for which the timer
	 * expires, set RTO <- RTO * 2 ("back off the timer").  The
	 * maximum value discussed in rule C7 above (RTO.max) may be
	 * used to provide an upper bound to this doubling operation.
	 */
	transport->rto = min((transport->rto * 2), transport->asoc->rto_max);
}

/* Worker routine to handle INIT command failure.  */
static void sctp_cmd_init_failed(sctp_cmd_seq_t *commands,
				 sctp_association_t *asoc)
{
	sctp_ulpevent_t *event;

	event = sctp_ulpevent_make_assoc_change(asoc,
						0,
						SCTP_CANT_STR_ASSOC,
						0, 0, 0,
						GFP_ATOMIC);

	if (event)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(event));

	/* FIXME:  We need to handle data possibly either
	 * sent via COOKIE-ECHO bundling or just waiting in
	 * the transmit queue, if the user has enabled
	 * SEND_FAILED notifications.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
}

/* Worker routine to handle SCTP_CMD_ASSOC_FAILED.  */
static void sctp_cmd_assoc_failed(sctp_cmd_seq_t *commands,
				  sctp_association_t *asoc)
{
	sctp_ulpevent_t *event;

	event = sctp_ulpevent_make_assoc_change(asoc,
						0,
						SCTP_COMM_LOST,
						0, 0, 0,
						GFP_ATOMIC);

	if (event)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(event));

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	/* FIXME:  We need to handle data that could not be sent or was not
	 * acked, if the user has enabled SEND_FAILED notifications.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
}

/* Process an init chunk (may be real INIT/INIT-ACK or an embedded INIT
 * inside the cookie.
 */
static void sctp_cmd_process_init(sctp_cmd_seq_t *commands,
				  sctp_association_t *asoc,
				  sctp_chunk_t *chunk,
				  sctp_init_chunk_t *peer_init,
				  int priority)
{
	/* The command sequence holds commands assuming that the
	 * processing will happen successfully.  If this is not the
	 * case, rewind the sequence and add appropriate  error handling
	 * to the sequence.
	 */
	sctp_process_init(asoc, chunk->chunk_hdr->type,
			  sctp_source(chunk), peer_init,
			  priority);
}

/* Helper function to break out starting up of heartbeat timers.  */
static void sctp_cmd_hb_timers_start(sctp_cmd_seq_t *cmds,
				     sctp_association_t *asoc)
{
	sctp_transport_t *t;
	struct list_head *pos;

	/* Start a heartbeat timer for each transport on the association.
	 * hold a reference on the transport to make sure none of
	 * the needed data structures go away.
	 */
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, sctp_transport_t, transports);
		if (!mod_timer(&t->hb_timer,
			       t->hb_interval + t->rto + jiffies)) {
			sctp_transport_hold(t);
		}
	}
}

/* Helper function to break out SCTP_CMD_SET_BIND_ADDR handling.  */
void sctp_cmd_set_bind_addrs(sctp_cmd_seq_t *cmds, sctp_association_t *asoc,
			     sctp_bind_addr_t *bp)
{
	struct list_head *pos, *temp;

	list_for_each_safe(pos, temp, &bp->address_list) {
		list_del_init(pos);
		list_add_tail(pos, &asoc->base.bind_addr.address_list);
	}

	/* Free the temporary bind addr header, otherwise
	 * there will a memory leak.
	 */
	sctp_bind_addr_free(bp);
}

/* Helper function to handle the reception of an HEARTBEAT ACK.  */
static void sctp_cmd_transport_on(sctp_cmd_seq_t *cmds, sctp_association_t *asoc,
				  sctp_transport_t *t, sctp_chunk_t *chunk)
{
	sctp_sender_hb_info_t *hbinfo;

	/* 8.3 Upon the receipt of the HEARTBEAT ACK, the sender of the
	 * HEARTBEAT should clear the error counter of the destination
	 * transport address to which the HEARTBEAT was sent.
	 * The association's overall error count is also cleared.
	 */
	t->error_count = 0;
	t->asoc->overall_error_count = 0;

	/* Mark the destination transport address as active if it is not so
	 * marked.
	 */
	if (!t->state.active)
		sctp_assoc_control_transport(asoc, t, SCTP_TRANSPORT_UP,
					     SCTP_HEARTBEAT_SUCCESS);

	/* The receiver of the HEARTBEAT ACK should also perform an
	 * RTT measurement for that destination transport address
	 * using the time value carried in the HEARTBEAT ACK chunk.
	 */
	hbinfo = (sctp_sender_hb_info_t *) chunk->skb->data;
	sctp_transport_update_rto(t, (jiffies - hbinfo->sent_at));
}

/* Helper function to do a transport reset at the expiry of the hearbeat
 * timer.
 */
static void sctp_cmd_transport_reset(sctp_cmd_seq_t *cmds,
				     sctp_association_t *asoc,
				     sctp_transport_t *t)
{
	sctp_transport_lower_cwnd(t, SCTP_LOWER_CWND_INACTIVE);

	/* Mark one strike against a transport.  */
	sctp_do_8_2_transport_strike(asoc, t);

	/* Update the heartbeat timer.  */
	if (!mod_timer(&t->hb_timer, t->hb_interval + t->rto + jiffies))
		sctp_transport_hold(t);
}

/* Helper function to process the process SACK command.  */
static int sctp_cmd_process_sack(sctp_cmd_seq_t *cmds, sctp_association_t *asoc,
				 sctp_sackhdr_t *sackh)
{
	int err;

	if (sctp_sack_outqueue(&asoc->outqueue, sackh)) {
		/* There are no more TSNs awaiting SACK.  */
		err = sctp_do_sm(SCTP_EVENT_T_OTHER,
				 SCTP_ST_OTHER(SCTP_EVENT_NO_PENDING_TSN),
				 asoc->state, asoc->ep, asoc, NULL,
				 GFP_ATOMIC);
	} else {
		/* Windows may have opened, so we need
		 * to check if we have DATA to transmit
		 */
		err = sctp_flush_outqueue(&asoc->outqueue, 0);
	}

	return err;
}

/* Helper function to set the timeout value for T2-SHUTDOWN timer and to set
 * the transport for a shutdown chunk.
 */
static void sctp_cmd_setup_t2(sctp_cmd_seq_t *cmds, sctp_association_t *asoc,
			      sctp_chunk_t *chunk)
{
	sctp_transport_t *t;

	t = sctp_assoc_choose_shutdown_transport(asoc);
	asoc->shutdown_last_sent_to = t;
	asoc->timeouts[SCTP_EVENT_TIMEOUT_T2_SHUTDOWN] = t->rto;
	chunk->transport = t;
}
