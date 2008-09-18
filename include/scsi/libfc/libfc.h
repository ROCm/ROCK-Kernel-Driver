/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _LIBFC_H_
#define _LIBFC_H_

#include <linux/timer.h>
#include <linux/if.h>

#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

#include <scsi/fc/fc_fcp.h>
#include <scsi/fc/fc_ns.h>
#include <scsi/fc/fc_els.h>

#include <scsi/libfc/fc_frame.h>

#define LIBFC_DEBUG

#ifdef LIBFC_DEBUG
/*
 * Log message.
 */
#define FC_DBG(fmt, args...)						\
	do {								\
		printk(KERN_INFO "%s " fmt, __func__, ##args);	\
	} while (0)
#else
#define FC_DBG(fmt, args...)
#endif

/*
 * libfc error codes
 */
#define	FC_NO_ERR	0	/* no error */
#define	FC_EX_TIMEOUT	1	/* Exchange timeout */
#define	FC_EX_CLOSED	2	/* Exchange closed */

/* some helpful macros */

#define ntohll(x) be64_to_cpu(x)
#define htonll(x) cpu_to_be64(x)

#define ntoh24(p)	(((p)[0] << 16) | ((p)[1] << 8) | ((p)[2]))

#define hton24(p, v)	do { \
	p[0] = (((v) >> 16) & 0xFF); \
	p[1] = (((v) >> 8) & 0xFF); \
	p[2] = ((v) & 0xFF); \
} while (0)

struct fc_exch_mgr;

/*
 * tgt_flags
 */
#define FC_TGT_REC_SUPPORTED	    (1 << 0)

/*
 * FC HBA status
 */
#define FC_PAUSE		    (1 << 1)
#define FC_LINK_UP		    (1 << 0)

/* for fc_softc */
#define FC_MAX_OUTSTANDING_COMMANDS 1024

/*
 * Transport Capabilities
 */
#define TRANS_C_SG		    (1 << 0)  /* Scatter gather */

enum fc_lport_state {
	LPORT_ST_NONE = 0,
	LPORT_ST_FLOGI,
	LPORT_ST_DNS,
	LPORT_ST_REG_PN,
	LPORT_ST_REG_FT,
	LPORT_ST_SCR,
	LPORT_ST_READY,
	LPORT_ST_DNS_STOP,
	LPORT_ST_LOGO,
	LPORT_ST_RESET
};

enum fc_rport_state {
	RPORT_ST_NONE = 0,
	RPORT_ST_INIT,		/* initialized */
	RPORT_ST_STARTED,	/* started */
	RPORT_ST_PLOGI,		/* waiting for PLOGI completion */
	RPORT_ST_PLOGI_RECV,	/* received PLOGI (as target) */
	RPORT_ST_PRLI,		/* waiting for PRLI completion */
	RPORT_ST_RTV,		/* waiting for RTV completion */
	RPORT_ST_ERROR,		/* error */
	RPORT_ST_READY,		/* ready for use */
	RPORT_ST_LOGO,		/* port logout sent */
};

/**
 * struct fc_rport_libfc_priv - libfc internal information about a remote port
 * @local_port: Fibre Channel host port instance
 * @rp_state: state tracks progress of PLOGI, PRLI, and RTV exchanges
 * @flags: REC and RETRY supported flags
 * @max_seq: maximum number of concurrent sequences
 * @retries: retry count in current state
 * @e_d_tov: error detect timeout value (in msec)
 * @r_a_tov: resource allocation timeout value (in msec)
 * @rp_lock: lock protects state
 * @retry_work:
 */
struct fc_rport_libfc_priv {
	struct fc_lport		*local_port;
	enum fc_rport_state rp_state;
	u16			flags;
	#define FC_RP_FLAGS_REC_SUPPORTED	(1 << 0)
	#define FC_RP_FLAGS_RETRY		(1 << 1)
	u16		max_seq;
	unsigned int	retries;
	unsigned int	e_d_tov;
	unsigned int	r_a_tov;
	spinlock_t	rp_lock;
	struct delayed_work	retry_work;
};

static inline void fc_rport_set_name(struct fc_rport *rport, u64 wwpn, u64 wwnn)
{
	rport->node_name = wwnn;
	rport->port_name = wwpn;
}

/*
 * fcoe stats structure
 */
struct fcoe_dev_stats {
	u64		SecondsSinceLastReset;
	u64		TxFrames;
	u64		TxWords;
	u64		RxFrames;
	u64		RxWords;
	u64		ErrorFrames;
	u64		DumpedFrames;
	u64		LinkFailureCount;
	u64		LossOfSignalCount;
	u64		InvalidTxWordCount;
	u64		InvalidCRCCount;
	u64		InputRequests;
	u64		OutputRequests;
	u64		ControlRequests;
	u64		InputMegabytes;
	u64		OutputMegabytes;
};

/*
 * els data is used for passing ELS respone specific
 * data to send ELS response mainly using infomation
 * in exchange and sequence in EM layer.
 */
struct fc_seq_els_data {
	struct fc_frame *fp;
	enum fc_els_rjt_reason reason;
	enum fc_els_rjt_explan explan;
};

struct libfc_function_template {

	/**
	 * Mandatory Fields
	 *
	 * These handlers must be implemented by the LLD.
	 */

	/*
	 * Interface to send a FC frame
	 */
	int (*frame_send)(struct fc_lport *lp, struct fc_frame *fp);

	/**
	 * Optional Fields
	 *
	 * The LLD may choose to implement any of the following handlers.
	 * If LLD doesn't specify hander and leaves its pointer NULL then
	 * the default libfc function will be used for that handler.
	 */

	/**
	 * Exhance Manager interfaces
	 */

	/*
	 * Send the FC frame payload using a new exchange and sequence.
	 *
	 * The frame pointer with some of the header's fields must be
	 * filled before calling exch_seq_send(), those fields are,
	 *
	 * - routing control
	 * - FC header type
	 * - parameter or relative offset
	 *
	 * The exchange response handler is set in this routine to resp()
	 * function pointer. It can be called in two scenarios: if a timeout
	 * occurs or if a response frame is received for the exchange. The
	 * fc_frame pointer in response handler will also indicate timeout
	 * as error using IS_ERR related macros.
	 *
	 * The response handler argumemt resp_arg is passed back to resp
	 * handler when it is invoked by EM layer in above mentioned
	 * two scenarios.
	 *
	 * The timeout value (in msec) for an exchange is set if non zero
	 * timer_msec argument is specified. The timer is canceled when
	 * it fires or when the exchange is done. The exchange timeout handler
	 * is registered by EM layer.
	 *
	 * The caller also need to specify FC sid, did and frame control field.
	 */
	struct fc_seq *(*exch_seq_send)(struct fc_lport *lp,
					struct fc_frame *fp,
					void (*resp)(struct fc_seq *,
						     struct fc_frame *fp,
						     void *arg),
					void *resp_arg,	unsigned int timer_msec,
					u32 sid, u32 did, u32 f_ctl);

	/*
	 * send a frame using existing sequence and exchange.
	 */
	int (*seq_send)(struct fc_lport *lp, struct fc_seq *sp,
			struct fc_frame *fp, u32 f_ctl);

	/*
	 * Send ELS response using mainly infomation
	 * in exchange and sequence in EM layer.
	 */
	void (*seq_els_rsp_send)(struct fc_seq *sp, enum fc_els_cmd els_cmd,
				 struct fc_seq_els_data *els_data);

	/*
	 * Abort an exchange and sequence. Generally called because of a
	 * exchange timeout or an abort from the upper layer.
	 *
	 * A timer_msec can be specified for abort timeout, if non-zero
	 * timer_msec value is specified then exchange resp handler
	 * will be called with timeout error if no response to abort.
	 */
	int (*seq_exch_abort)(const struct fc_seq *req_sp,
			      unsigned int timer_msec);

	/*
	 * Indicate that an exchange/sequence tuple is complete and the memory
	 * allocated for the related objects may be freed.
	 */
	void (*exch_done)(struct fc_seq *sp);

	/*
	 * Assigns a EM and a free XID for an new exchange and then
	 * allocates a new exchange and sequence pair.
	 * The fp can be used to determine free XID.
	 */
	struct fc_exch *(*exch_get)(struct fc_lport *lp, struct fc_frame *fp);

	/*
	 * Release previously assigned XID by exch_get API.
	 * The LLD may implement this if XID is assigned by LLD
	 * in exch_get().
	 */
	void (*exch_put)(struct fc_lport *lp, struct fc_exch_mgr *mp,
			 u16 ex_id);

	/*
	 * Start a new sequence on the same exchange/sequence tuple.
	 */
	struct fc_seq *(*seq_start_next)(struct fc_seq *sp);

	/*
	 * Reset an exchange manager, completing all sequences and exchanges.
	 * If s_id is non-zero, reset only exchanges originating from that FID.
	 * If d_id is non-zero, reset only exchanges sending to that FID.
	 */
	void (*exch_mgr_reset)(struct fc_exch_mgr *,
			       u32 s_id, u32 d_id);

	/*
	 * Get exchange Ids of a sequence
	 */
	void (*seq_get_xids)(struct fc_seq *sp, u16 *oxid, u16 *rxid);

	/*
	 * Set REC data to a sequence
	 */
	void (*seq_set_rec_data)(struct fc_seq *sp, u32 rec_data);

	/**
	 * Local Port interfaces
	 */

	/*
	 * Receive a frame to a local port.
	 */
	void (*lport_recv)(struct fc_lport *lp, struct fc_seq *sp,
			   struct fc_frame *fp);

	int (*lport_login)(struct fc_lport *);
	int (*lport_reset)(struct fc_lport *);
	int (*lport_logout)(struct fc_lport *);

	/**
	 * Remote Port interfaces
	 */

	/*
	 * Initiates the RP state machine. It is called from the LP module.
	 * This function will issue the following commands to the N_Port
	 * identified by the FC ID provided.
	 *
	 * - PLOGI
	 * - PRLI
	 * - RTV
	 */
	int (*rport_login)(struct fc_rport *rport);

	/*
	 * Logs the specified local port out of a N_Port identified
	 * by the ID provided.
	 */
	int (*rport_logout)(struct fc_rport *rport);

	void (*rport_recv_req)(struct fc_seq *, struct fc_frame *,
			       struct fc_rport *);

	struct fc_rport *(*rport_lookup)(const struct fc_lport *, u32);

	struct fc_rport *(*rport_create)(struct fc_lport *,
					 struct fc_rport_identifiers *);

	void (*rport_reset)(struct fc_rport *);

	void (*rport_reset_list)(struct fc_lport *);

	/**
	 * SCSI interfaces
	 */

	/*
	 * Used at least durring linkdown and reset
	 */
	void (*scsi_cleanup)(struct fc_lport *);

	/*
	 * Abort all I/O on a local port
	 */
	void (*scsi_abort_io)(struct fc_lport *);

	/**
	 * Discovery interfaces
	 */

	void (*disc_recv_req)(struct fc_seq *,
			      struct fc_frame *, struct fc_lport *);

	/*
	 * Start discovery for a local port.
	 */
	int (*disc_start)(struct fc_lport *);

	void (*dns_register)(struct fc_lport *);
	void (*disc_stop)(struct fc_lport *);
};

struct fc_lport {
	struct list_head list;

	/* Associations */
	struct Scsi_Host	*host;
	struct fc_exch_mgr	*emp;
	struct fc_rport		*dns_rp;
	struct fc_rport		*ptp_rp;
	void			*scsi_priv;

	/* Operational Information */
	struct libfc_function_template tt;
	u16			link_status;
	u8			ns_disc_done;
	enum fc_lport_state	state;
	unsigned long		boot_time;

	struct fc_host_statistics host_stats;
	struct fcoe_dev_stats	*dev_stats[NR_CPUS];

	u64			wwpn;
	u64			wwnn;
	u32			fid;
	u8			retry_count;
	unsigned char		ns_disc_retry_count;
	unsigned char		ns_disc_delay;
	unsigned char		ns_disc_pending;
	unsigned char		ns_disc_requested;
	unsigned short		ns_disc_seq_count;
	unsigned char		ns_disc_buf_len;

	/* Capabilities */
	char			ifname[IFNAMSIZ];
	u32			capabilities;
	u32			mfs;	/* max FC payload size */
	unsigned int		service_params;
	unsigned int		e_d_tov;
	unsigned int		r_a_tov;
	u8			max_retry_count;
	u16			link_speed;
	u16			link_supported_speeds;
	struct fc_ns_fts	fcts;	        /* FC-4 type masks */
	struct fc_els_rnid_gen	rnid_gen;	/* RNID information */

	/* Locks */
	spinlock_t		state_lock;	/* serializes state changes */

	/* Miscellaneous */
	struct fc_gpn_ft_resp	ns_disc_buf;	/* partial name buffer */
	struct timer_list	state_timer;	/* timer for state events */
	struct delayed_work	ns_disc_work;

	void			*drv_priv;
};

/**
 * FC_LPORT HELPER FUNCTIONS
 *****************************/

static inline int fc_lport_test_ready(struct fc_lport *lp)
{
	return lp->state == LPORT_ST_READY;
}

static inline u32 fc_lport_get_fid(const struct fc_lport *lp)
{
	return lp->fid;
}

static inline void fc_set_wwnn(struct fc_lport *lp, u64 wwnn)
{
	lp->wwnn = wwnn;
}

static inline void fc_set_wwpn(struct fc_lport *lp, u64 wwnn)
{
	lp->wwpn = wwnn;
}

static inline int fc_lport_locked(struct fc_lport *lp)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	return spin_is_locked(&lp->state_lock);
#else
	return 1;
#endif /* CONFIG_SMP || CONFIG_DEBUG_SPINLOCK */
}

/*
 * Locking code.
 */
static inline void fc_lport_lock(struct fc_lport *lp)
{
	spin_lock_bh(&lp->state_lock);
}

static inline void fc_lport_unlock(struct fc_lport *lp)
{
	spin_unlock_bh(&lp->state_lock);
}

static inline void fc_lport_state_enter(struct fc_lport *lp,
					enum fc_lport_state state)
{
	WARN_ON(!fc_lport_locked(lp));
	del_timer(&lp->state_timer);
	if (state != lp->state)
		lp->retry_count = 0;
	lp->state = state;
}


/**
 * LOCAL PORT LAYER
 *****************************/
int fc_lport_init(struct fc_lport *lp);

/*
 * Destroy the specified local port by finding and freeing all
 * fc_rports associated with it and then by freeing the fc_lport
 * itself.
 */
int fc_lport_destroy(struct fc_lport *lp);

/*
 * Logout the specified local port from the fabric
 */
int fc_fabric_logoff(struct fc_lport *lp);

/*
 * Initiate the LP state machine. This handler will use fc_host_attr
 * to store the FLOGI service parameters, so fc_host_attr must be
 * initialized before calling this handler.
 */
int fc_fabric_login(struct fc_lport *lp);

/*
 * The link is up for the given local port.
 */
void fc_linkup(struct fc_lport *);

/*
 * Link is down for the given local port.
 */
void fc_linkdown(struct fc_lport *);

/*
 * Pause and unpause traffic.
 */
void fc_pause(struct fc_lport *);
void fc_unpause(struct fc_lport *);

/*
 * Configure the local port.
 */
int fc_lport_config(struct fc_lport *);

/*
 * Reset the local port.
 */
int fc_lport_enter_reset(struct fc_lport *);

/*
 * Set the mfs or reset
 */
int fc_set_mfs(struct fc_lport *lp, u32 mfs);


/**
 * REMOTE PORT LAYER
 *****************************/
int fc_rport_init(struct fc_lport *lp);


/**
 * DISCOVERY LAYER
 *****************************/
int fc_ns_init(struct fc_lport *lp);


/**
 * SCSI LAYER
 *****************************/
/*
 * Initialize the SCSI block of libfc
 */
int fc_fcp_init(struct fc_lport *);

/*
 * This section provides an API which allows direct interaction
 * with the SCSI-ml. Each of these functions satisfies a function
 * pointer defined in Scsi_Host and therefore is always called
 * directly from the SCSI-ml.
 */
int fc_queuecommand(struct scsi_cmnd *sc_cmd,
		    void (*done)(struct scsi_cmnd *));

/*
 * Send an ABTS frame to the target device. The sc_cmd argument
 * is a pointer to the SCSI command to be aborted.
 */
int fc_eh_abort(struct scsi_cmnd *sc_cmd);

/*
 * Reset a LUN by sending send the tm cmd to the target.
 */
int fc_eh_device_reset(struct scsi_cmnd *sc_cmd);

/*
 * Reset the host adapter.
 */
int fc_eh_host_reset(struct scsi_cmnd *sc_cmd);

/*
 * Check rport status.
 */
int fc_slave_alloc(struct scsi_device *sdev);

/*
 * Adjust the queue depth.
 */
int fc_change_queue_depth(struct scsi_device *sdev, int qdepth);

/*
 * Change the tag type.
 */
int fc_change_queue_type(struct scsi_device *sdev, int tag_type);

/*
 * Free memory pools used by the FCP layer.
 */
void fc_fcp_destroy(struct fc_lport *);


/**
 * EXCHANGE MANAGER LAYER
 *****************************/
/*
 * Initializes Exchange Manager related
 * function pointers in struct libfc_function_template.
 */
int fc_exch_init(struct fc_lport *lp);

/*
 * Allocates an Exchange Manager (EM).
 *
 * The EM manages exchanges for their allocation and
 * free, also allows exchange lookup for received
 * frame.
 *
 * The class is used for initializing FC class of
 * allocated exchange from EM.
 *
 * The min_xid and max_xid will limit new
 * exchange ID (XID) within this range for
 * a new exchange.
 * The LLD may choose to have multiple EMs,
 * e.g. one EM instance per CPU receive thread in LLD.
 * The LLD can use exch_get() of struct libfc_function_template
 * to specify XID for a new exchange within
 * a specified EM instance.
 *
 * The em_idx to uniquely identify an EM instance.
 */
struct fc_exch_mgr *fc_exch_mgr_alloc(struct fc_lport *lp,
				      enum fc_class class,
				      u16 min_xid,
				      u16 max_xid);

/*
 * Free an exchange manager.
 */
void fc_exch_mgr_free(struct fc_exch_mgr *mp);

/*
 * Receive a frame on specified local port and exchange manager.
 */
void fc_exch_recv(struct fc_lport *lp, struct fc_exch_mgr *mp,
		  struct fc_frame *fp);

/*
 * This function is for exch_seq_send function pointer in
 * struct libfc_function_template, see comment block on
 * exch_seq_send for description of this function.
 */
struct fc_seq *fc_exch_seq_send(struct fc_lport *lp,
				struct fc_frame *fp,
				void (*resp)(struct fc_seq *,
					     struct fc_frame *fp,
					     void *arg),
				void *resp_arg, u32 timer_msec,
				u32 sid, u32 did, u32 f_ctl);

/*
 * send a frame using existing sequence and exchange.
 */
int fc_seq_send(struct fc_lport *lp, struct fc_seq *sp,
		struct fc_frame *fp, u32 f_ctl);

/*
 * Send ELS response using mainly infomation
 * in exchange and sequence in EM layer.
 */
void fc_seq_els_rsp_send(struct fc_seq *sp, enum fc_els_cmd els_cmd,
			 struct fc_seq_els_data *els_data);

/*
 * This function is for seq_exch_abort function pointer in
 * struct libfc_function_template, see comment block on
 * seq_exch_abort for description of this function.
 */
int fc_seq_exch_abort(const struct fc_seq *req_sp, unsigned int timer_msec);

/*
 * Indicate that an exchange/sequence tuple is complete and the memory
 * allocated for the related objects may be freed.
 */
void fc_exch_done(struct fc_seq *sp);

/*
 * Assigns a EM and XID for a frame and then allocates
 * a new exchange and sequence pair.
 * The fp can be used to determine free XID.
 */
struct fc_exch *fc_exch_get(struct fc_lport *lp, struct fc_frame *fp);

/*
 * Allocate a new exchange and sequence pair.
 * if ex_id is zero then next free exchange id
 * from specified exchange manger mp will be assigned.
 */
struct fc_exch *fc_exch_alloc(struct fc_exch_mgr *mp, u16 ex_id);

/*
 * Start a new sequence on the same exchange as the supplied sequence.
 */
struct fc_seq *fc_seq_start_next(struct fc_seq *sp);

/*
 * Reset an exchange manager, completing all sequences and exchanges.
 * If s_id is non-zero, reset only exchanges originating from that FID.
 * If d_id is non-zero, reset only exchanges sending to that FID.
 */
void fc_exch_mgr_reset(struct fc_exch_mgr *, u32 s_id, u32 d_id);

/*
 * Get exchange Ids of a sequence
 */
void fc_seq_get_xids(struct fc_seq *sp, u16 *oxid, u16 *rxid);

/*
 * Set REC data to a sequence
 */
void fc_seq_set_rec_data(struct fc_seq *sp, u32 rec_data);

/**
 * fc_functions_template
 *****************************/
void fc_attr_init(struct fc_lport *);
void fc_get_host_port_id(struct Scsi_Host *shost);
void fc_get_host_speed(struct Scsi_Host *shost);
void fc_get_host_port_type(struct Scsi_Host *shost);
void fc_get_host_port_state(struct Scsi_Host *shost);
void fc_get_host_fabric_name(struct Scsi_Host *shost);
void fc_set_rport_loss_tmo(struct fc_rport *rport, u32 timeout);
struct fc_host_statistics *fc_get_host_stats(struct Scsi_Host *);

/*
 * module setup functions.
 */
int fc_setup_exch_mgr(void);
void fc_destroy_exch_mgr(void);


#endif /* _LIBFC_H_ */
