/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001-2002 International Business Machines Corp.
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * This file is part of the implementation of the add-IP extension,
 * based on <draft-ietf-tsvwg-addip-sctp-02.txt> June 29, 2001,
 * for the SCTP kernel reference Implementation.
 * 
 * $Header: /cvsroot/lksctp/lksctp/sctp_cvs/include/net/sctp/sctp_sm.h,v 1.34 2002/08/21 18:34:04 jgrimm Exp $
 * 
 * These are definitions needed by the state machine.
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
 * email addresses:
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 * 
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by: 
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 *    Xingang Guo <xingang.guo@intel.com>
 *    Jon Grimm <jgrimm@us.ibm.com>
 *    Dajiang Zhang <dajiang.zhang@nokia.com>
 *    Sridhar Samudrala <sri@us.ibm.com>
 *    Daisy Chang <daisyc@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */


#include <linux/config.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <net/sctp/sctp_command.h>
#include <net/sctp/sctp.h>

#ifndef __sctp_sm_h__
#define __sctp_sm_h__

/*
 * Possible values for the disposition are:
 */
typedef enum {
	SCTP_DISPOSITION_DISCARD,	 /* No further processing.  */
	SCTP_DISPOSITION_CONSUME,	 /* Process return values normally.  */
	SCTP_DISPOSITION_NOMEM,		 /* We ran out of memory--recover.  */
	SCTP_DISPOSITION_DELETE_TCB,	 /* Close the association.  */
	SCTP_DISPOSITION_ABORT,		 /* Close the association NOW.  */
	SCTP_DISPOSITION_VIOLATION,	 /* The peer is misbehaving.  */
	SCTP_DISPOSITION_NOT_IMPL,	 /* This entry is not implemented.  */
	SCTP_DISPOSITION_ERROR,		 /* This is plain old user error.  */
	SCTP_DISPOSITION_BUG,		 /* This is a bug.  */
} sctp_disposition_t;

typedef struct {
	int name;
	int action;
} sctp_sm_command_t;

typedef sctp_disposition_t (sctp_state_fn_t) (const sctp_endpoint_t *,
					      const sctp_association_t *,
					      const sctp_subtype_t type,
					      void *arg,
					      sctp_cmd_seq_t *);
typedef void (sctp_timer_event_t) (unsigned long);
typedef struct {
        sctp_state_fn_t *fn;
        char *name;
} sctp_sm_table_entry_t;

/* A naming convention of "sctp_sf_xxx" applies to all the state functions 
 * currently in use.
 */

/* Prototypes for generic state functions. */
sctp_state_fn_t sctp_sf_not_impl;
sctp_state_fn_t sctp_sf_bug;

/* Prototypes for gener timer state functions. */
sctp_state_fn_t sctp_sf_timer_ignore;

/* Prototypes for chunk state functions. */
sctp_state_fn_t sctp_sf_do_9_1_abort;
sctp_state_fn_t sctp_sf_cookie_wait_abort;
sctp_state_fn_t sctp_sf_cookie_echoed_abort;
sctp_state_fn_t sctp_sf_do_5_1B_init;
sctp_state_fn_t sctp_sf_do_5_1C_ack;
sctp_state_fn_t sctp_sf_do_5_1D_ce;
sctp_state_fn_t sctp_sf_do_5_1E_ca ;
sctp_state_fn_t sctp_sf_do_4_C;
sctp_state_fn_t sctp_sf_eat_data_6_2;
sctp_state_fn_t sctp_sf_eat_data_fast_4_4;
sctp_state_fn_t sctp_sf_eat_sack_6_2;
sctp_state_fn_t sctp_sf_tabort_8_4_8;
sctp_state_fn_t sctp_sf_operr_notify;
sctp_state_fn_t sctp_sf_t1_timer_expire;
sctp_state_fn_t sctp_sf_t2_timer_expire;
sctp_state_fn_t sctp_sf_sendbeat_8_3;
sctp_state_fn_t sctp_sf_beat_8_3;
sctp_state_fn_t sctp_sf_backbeat_8_3;
sctp_state_fn_t sctp_sf_do_9_2_final;
sctp_state_fn_t sctp_sf_do_9_2_shutdown;
sctp_state_fn_t sctp_sf_do_ecn_cwr;
sctp_state_fn_t sctp_sf_do_ecne;
sctp_state_fn_t sctp_sf_ootb;
sctp_state_fn_t sctp_sf_shut_8_4_5;
sctp_state_fn_t sctp_sf_pdiscard;
sctp_state_fn_t sctp_sf_violation;
sctp_state_fn_t sctp_sf_discard_chunk;
sctp_state_fn_t sctp_sf_do_5_2_1_siminit;
sctp_state_fn_t sctp_sf_do_5_2_2_dupinit;
sctp_state_fn_t sctp_sf_do_5_2_4_dupcook;

/* Prototypes for primitive event state functions.  */
sctp_state_fn_t sctp_sf_do_prm_asoc;
sctp_state_fn_t sctp_sf_do_prm_send;
sctp_state_fn_t sctp_sf_do_9_2_prm_shutdown;
sctp_state_fn_t sctp_sf_cookie_wait_prm_shutdown;
sctp_state_fn_t sctp_sf_cookie_echoed_prm_shutdown;
sctp_state_fn_t sctp_sf_do_9_1_prm_abort;
sctp_state_fn_t sctp_sf_cookie_wait_prm_abort;
sctp_state_fn_t sctp_sf_cookie_echoed_prm_abort;
sctp_state_fn_t sctp_sf_error_closed;
sctp_state_fn_t sctp_sf_error_shutdown;
sctp_state_fn_t sctp_sf_ignore_primitive;

/* Prototypes for other event state functions.  */
sctp_state_fn_t sctp_sf_do_9_2_start_shutdown;
sctp_state_fn_t sctp_sf_do_9_2_shutdown_ack;
sctp_state_fn_t sctp_sf_ignore_other;

/* Prototypes for timeout event state functions.  */
sctp_state_fn_t sctp_sf_do_6_3_3_rtx;
sctp_state_fn_t sctp_sf_do_6_2_sack;
sctp_state_fn_t sctp_sf_autoclose_timer_expire;


/* These are state functions which are either obsolete or not in use yet. 
 * If any of these functions needs to be revived, it should be renamed with
 * the "sctp_sf_xxx" prefix, and be moved to the above prototype groups.
 */ 

/* Prototypes for chunk state functions.  Not in use. */
sctp_state_fn_t sctp_sf_do_5_2_6_stale;
sctp_state_fn_t sctp_sf_do_9_2_reshutack;
sctp_state_fn_t sctp_sf_do_9_2_reshut;
sctp_state_fn_t sctp_sf_do_9_2_shutack;

sctp_state_fn_t lucky;
sctp_state_fn_t other_stupid;

/* Prototypes for timeout event state functions.  Not in use. */
sctp_state_fn_t sctp_do_4_2_reinit;
sctp_state_fn_t sctp_do_4_3_reecho;
sctp_state_fn_t sctp_do_9_2_reshut;
sctp_state_fn_t sctp_do_9_2_reshutack;
sctp_state_fn_t sctp_do_8_3_hb_err;
sctp_state_fn_t sctp_heartoff;

/* Prototypes for addip related state functions.  Not in use. */
sctp_state_fn_t sctp_addip_do_asconf;
sctp_state_fn_t sctp_addip_do_asconf_ack;

/* Prototypes for utility support functions.  */
uint8_t sctp_get_chunk_type(sctp_chunk_t *chunk);
sctp_sm_table_entry_t *sctp_sm_lookup_event(sctp_event_t event_type,
					    sctp_state_t state,
					    sctp_subtype_t event_subtype);

time_t timeval_sub(struct timeval *, struct timeval *);
sctp_association_t *sctp_make_temp_asoc(const sctp_endpoint_t *,
					sctp_chunk_t *,
					const int priority);
uint32_t sctp_generate_verification_tag(void);
sctpParam_t sctp_get_my_addrs_raw(const sctp_association_t *,
				  const int priority, int *addrs_len);

void sctp_populate_tie_tags(uint8_t *cookie, uint32_t curTag, uint32_t hisTag);



/* Prototypes for chunk-building functions.  */
sctp_chunk_t *sctp_make_init(const sctp_association_t *,
			     const sctp_bind_addr_t *,
			     int priority);
sctp_chunk_t *sctp_make_init_ack(const sctp_association_t *,
				 const sctp_chunk_t *,
				 const int priority);
sctp_chunk_t *sctp_make_cookie_echo(const sctp_association_t *,
				    const sctp_chunk_t *);
sctp_chunk_t *sctp_make_cookie_ack(const sctp_association_t *,
				   const sctp_chunk_t *);
sctp_chunk_t *sctp_make_cwr(const sctp_association_t *,
				 const uint32_t lowest_tsn,
				 const sctp_chunk_t *);
sctp_chunk_t *sctp_make_datafrag(sctp_association_t *,
				 const struct sctp_sndrcvinfo *sinfo,
				 int len, const uint8_t *data,
				 uint8_t flags, uint16_t ssn);
sctp_chunk_t * sctp_make_datafrag_empty(sctp_association_t *,
					const struct sctp_sndrcvinfo *sinfo,
					int len, const uint8_t flags, 
					uint16_t ssn);
sctp_chunk_t *sctp_make_data(sctp_association_t *,
			     const struct sctp_sndrcvinfo *sinfo,
			     int len, const uint8_t *data);
sctp_chunk_t *sctp_make_data_empty(sctp_association_t *,
				   const struct sctp_sndrcvinfo *, int len);
sctp_chunk_t *sctp_make_ecne(const sctp_association_t *,
				  const uint32_t);
sctp_chunk_t *sctp_make_sack(const sctp_association_t *);
sctp_chunk_t *sctp_make_shutdown(const sctp_association_t *asoc);
sctp_chunk_t *sctp_make_shutdown_ack(const sctp_association_t *asoc,
					  const sctp_chunk_t *);
sctp_chunk_t *sctp_make_shutdown_complete(const sctp_association_t *,
					  const sctp_chunk_t *);
void sctp_init_cause(sctp_chunk_t *, uint16_t cause, const void *, size_t);
sctp_chunk_t *sctp_make_abort(const sctp_association_t *,
			      const sctp_chunk_t *,
			      const size_t hint);
sctp_chunk_t *sctp_make_abort_no_data(const sctp_association_t *,
				      const sctp_chunk_t *,
				      uint32_t tsn);
sctp_chunk_t *sctp_make_heartbeat(const sctp_association_t *,
				  const sctp_transport_t *,
				  const void *payload,
				  const size_t paylen);
sctp_chunk_t *sctp_make_heartbeat_ack(const sctp_association_t *,
				      const sctp_chunk_t *,
				      const void *payload,
				      const size_t paylen);
sctp_chunk_t *sctp_make_op_error(const sctp_association_t *,
				 const sctp_chunk_t *chunk,
				 uint16_t cause_code,
				 const void *payload,
				 size_t paylen);
void sctp_chunk_assign_tsn(sctp_chunk_t *);


/* Prototypes for statetable processing. */

int sctp_do_sm(sctp_event_t event_type, sctp_subtype_t subtype,
	       sctp_state_t state,
               sctp_endpoint_t *,
               sctp_association_t *asoc,
               void *event_arg,
               int priority);

int sctp_side_effects(sctp_event_t event_type, sctp_subtype_t subtype,
		      sctp_state_t state,
                      sctp_endpoint_t *,
                      sctp_association_t *asoc,
                      void *event_arg,
                      sctp_disposition_t status,
		      sctp_cmd_seq_t *commands,
                      int priority);

/* 2nd level prototypes */
int
sctp_cmd_interpreter(sctp_event_t event_type, sctp_subtype_t subtype,
		     sctp_state_t state,
		     sctp_endpoint_t *ep,
		     sctp_association_t *asoc,
		     void *event_arg,
		     sctp_disposition_t status,
		     sctp_cmd_seq_t *retval,
		     int priority);


int sctp_gen_sack(sctp_association_t *, int force, sctp_cmd_seq_t *);
void sctp_do_TSNdup(sctp_association_t *, sctp_chunk_t *, long gap);

void sctp_generate_t3_rtx_event(unsigned long peer);
void sctp_generate_heartbeat_event(unsigned long peer);

sctp_sackhdr_t *sctp_sm_pull_sack(sctp_chunk_t *);

sctp_cookie_param_t *
sctp_pack_cookie(const sctp_endpoint_t *, const sctp_association_t *,
		 const sctp_chunk_t *, int *cookie_len,
		 const uint8_t *, int addrs_len);
sctp_association_t *sctp_unpack_cookie(const sctp_endpoint_t *, 
				       const sctp_association_t *,
				       sctp_chunk_t *, int priority, int *err);
int sctp_addip_addr_config(sctp_association_t *, sctp_param_t,
			   struct sockaddr_storage*, int);

/* 3rd level prototypes */
uint32_t sctp_generate_tag(const sctp_endpoint_t *);
uint32_t sctp_generate_tsn(const sctp_endpoint_t *);

/* 4th level prototypes */
void sctp_param2sockaddr(sockaddr_storage_t *addr, const sctpParam_t param,
			 uint16_t port);
int sctp_addr2sockaddr(const sctpParam_t, sockaddr_storage_t *);
int sockaddr2sctp_addr(const sockaddr_storage_t *, sctpParam_t);

/* Extern declarations for major data structures.  */
sctp_sm_table_entry_t *sctp_chunk_event_lookup(sctp_cid_t, sctp_state_t);
extern sctp_sm_table_entry_t
primitive_event_table[SCTP_NUM_PRIMITIVE_TYPES][SCTP_STATE_NUM_STATES];
extern sctp_sm_table_entry_t
other_event_table[SCTP_NUM_OTHER_TYPES][SCTP_STATE_NUM_STATES];
extern sctp_sm_table_entry_t
timeout_event_table[SCTP_NUM_TIMEOUT_TYPES][SCTP_STATE_NUM_STATES];
extern sctp_timer_event_t *sctp_timer_events[SCTP_NUM_TIMEOUT_TYPES];

/* These are some handy utility macros... */


/* Get the size of a DATA chunk payload. */
static inline uint16_t
sctp_data_size(sctp_chunk_t *chunk)
{
	uint16_t size;

	size = ntohs(chunk->chunk_hdr->length);
	size -= sizeof(sctp_data_chunk_t);

	return(size);

} /* sctp_data_size( ) */

/* Compare two TSNs */

/* RFC 1982 - Serial Number Arithmetic
 *
 * 2. Comparison
 *  Then, s1 is said to be equal to s2 if and only if i1 is equal to i2,
 *  in all other cases, s1 is not equal to s2.
 *
 * s1 is said to be less than s2 if, and only if, s1 is not equal to s2,
 * and
 *
 *      (i1 < i2 and i2 - i1 < 2^(SERIAL_BITS - 1)) or
 *      (i1 > i2 and i1 - i2 > 2^(SERIAL_BITS - 1))
 *
 * s1 is said to be greater than s2 if, and only if, s1 is not equal to
 * s2, and
 *
 *      (i1 < i2 and i2 - i1 > 2^(SERIAL_BITS - 1)) or
 *      (i1 > i2 and i1 - i2 < 2^(SERIAL_BITS - 1)) 
 */

/*
 * RFC 2960
 *  1.6 Serial Number Arithmetic 
 *
 * Comparisons and arithmetic on TSNs in this document SHOULD use Serial
 * Number Arithmetic as defined in [RFC1982] where SERIAL_BITS = 32.
 */

enum {
	TSN_SIGN_BIT = (1<<31)
};

static inline int
TSN_lt(__u32 s, __u32 t)
{
	return (((s) - (t)) & TSN_SIGN_BIT);
}

static inline int
TSN_lte(__u32 s, __u32 t)
{
	return (((s) == (t)) || (((s) - (t)) & TSN_SIGN_BIT));
}

/* Compare two SSNs */

/*
 * RFC 2960
 *  1.6 Serial Number Arithmetic 
 *
 * Comparisons and arithmetic on Stream Sequence Numbers in this document 
 * SHOULD use Serial Number Arithmetic as defined in [RFC1982] where 
 * SERIAL_BITS = 16.
 */
enum {
	SSN_SIGN_BIT = (1<<15)
};

static inline int
SSN_lt(__u16 s, __u16 t)
{
	return (((s) - (t)) & SSN_SIGN_BIT);
}

static inline int
SSN_lte(__u16 s, __u16 t)
{
	return (((s) == (t)) || (((s) - (t)) & SSN_SIGN_BIT));
}

/* Run sctp_add_cmd() generating a BUG() if there is a failure.  */
static inline void
sctp_add_cmd_sf(sctp_cmd_seq_t *seq, sctp_verb_t verb, sctp_arg_t obj)
{
        if (unlikely(!sctp_add_cmd(seq, verb, obj))) {
		BUG();
	}

} /* sctp_add_cmd_sf() */


#endif /* __sctp_sm_h__ */
