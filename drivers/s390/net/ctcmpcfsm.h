#include "fsm.h"

/**
  * MPC Group Station FSM States

State Name		When In This State
======================	=======================================
MPCG_STATE_RESET	Initial State When Driver Loaded
			We receive and send NOTHING

MPCG_STATE_INOP         INOP Received.
			Group level non-recoverable error

MPCG_STATE_READY	XID exchanges for at least 1 write and
			1 read channel have completed.
			Group is ready for data transfer.

States from ctc_mpc_alloc_channel
==============================================================
MPCG_STATE_XID2INITW	Awaiting XID2(0) Initiation
			      ATTN from other side will start
			      XID negotiations.
			      Y-side protocol only.

MPCG_STATE_XID2INITX	XID2(0) negotiations are in progress.
			      At least 1, but not all, XID2(0)'s
			      have been received from partner.

MPCG_STATE_XID7INITW	XID2(0) complete
			      No XID2(7)'s have yet been received.
			      XID2(7) negotiations pending.

MPCG_STATE_XID7INITX	XID2(7) negotiations in progress.
			      At least 1, but not all, XID2(7)'s
			      have been received from partner.

MPCG_STATE_XID7INITF	XID2(7) negotiations complete.
			      Transitioning to READY.

MPCG_STATE_READY	      Ready for Data Transfer.


States from ctc_mpc_establish_connectivity call
==============================================================
MPCG_STATE_XID0IOWAIT	Initiating XID2(0) negotiations.
			      X-side protocol only.
			      ATTN-BUSY from other side will convert
			      this to Y-side protocol and the
			      ctc_mpc_alloc_channel flow will begin.

MPCG_STATE_XID0IOWAIX	XID2(0) negotiations are in progress.
			      At least 1, but not all, XID2(0)'s
			      have been received from partner.

MPCG_STATE_XID7INITI	XID2(0) complete
			      No XID2(7)'s have yet been received.
			      XID2(7) negotiations pending.

MPCG_STATE_XID7INITZ	XID2(7) negotiations in progress.
			      At least 1, but not all, XID2(7)'s
			      have been received from partner.

MPCG_STATE_XID7INITF	XID2(7) negotiations complete.
			      Transitioning to READY.

MPCG_STATE_READY	      Ready for Data Transfer.

*/

enum mpcg_events {
	MPCG_EVENT_INOP,
	MPCG_EVENT_DISCONC,
	MPCG_EVENT_XID0DO,
	MPCG_EVENT_XID2,
	MPCG_EVENT_XID2DONE,
	MPCG_EVENT_XID7DONE,
	MPCG_EVENT_TIMER,
	MPCG_EVENT_DOIO,
	NR_MPCG_EVENTS,
};

static const char *mpcg_event_names[]  = {
	"INOP Condition",
	"Discontact Received",
	"Channel Active - Start XID",
	"XID2 Received",
	"XID0 Complete",
	"XID7 Complete",
	"XID Setup Timer",
	"XID DoIO",
};


enum mpcg_states {
	MPCG_STATE_RESET,
	MPCG_STATE_INOP,
	MPCG_STATE_XID2INITW,
	MPCG_STATE_XID2INITX,
	MPCG_STATE_XID7INITW,
	MPCG_STATE_XID7INITX,
	MPCG_STATE_XID0IOWAIT,
	MPCG_STATE_XID0IOWAIX,
	MPCG_STATE_XID7INITI,
	MPCG_STATE_XID7INITZ,
	MPCG_STATE_XID7INITF,
	MPCG_STATE_FLOWC,
	MPCG_STATE_READY,
	NR_MPCG_STATES,
};


static const char *mpcg_state_names[] =  {
	"Reset",
	"INOP",
	"Passive XID- XID0 Pending Start",
	"Passive XID- XID0 Pending Complete",
	"Passive XID- XID7 Pending P1 Start",
	"Passive XID- XID7 Pending P2 Complete",
	"Active  XID- XID0 Pending Start",
	"Active  XID- XID0 Pending Complete",
	"Active  XID- XID7 Pending Start",
	"Active  XID- XID7 Pending Complete ",
	"XID        - XID7 Complete ",
	"FLOW CONTROL ON",
	"READY",
};


static void fsm_action_nop(fsm_instance *, int, void *);
static void mpc_action_go_inop(fsm_instance *, int, void *);
static void mpc_action_timeout(fsm_instance *, int, void *);
static void mpc_action_yside_xid(fsm_instance *, int, void *);
static void mpc_action_doxid0(fsm_instance *, int, void *);
static void mpc_action_doxid7(fsm_instance *, int, void *);
static void mpc_action_xside_xid(fsm_instance *, int, void *);
static void mpc_action_rcvd_xid0(fsm_instance *, int, void *);
static void mpc_action_rcvd_xid7(fsm_instance *, int, void *);
static void ctcmpc_action_attn(fsm_instance *, int, void *);
static void ctcmpc_action_attnbusy(fsm_instance *, int, void *);
static void ctcmpc_action_resend(fsm_instance *, int, void *);
static void mpc_action_go_ready(fsm_instance *, int, void *);
static void mpc_action_discontact(fsm_instance *, int, void *);


/**
 * The MPC Group Station FSM
 *   22 events
 */
static const fsm_node mpcg_fsm[] = {
{  MPCG_STATE_RESET,           MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_INOP,		MPCG_EVENT_INOP,        fsm_action_nop},

{  MPCG_STATE_FLOWC,		MPCG_EVENT_INOP,        mpc_action_go_inop},

{  MPCG_STATE_READY,		MPCG_EVENT_DISCONC,     mpc_action_discontact},
{  MPCG_STATE_READY,		MPCG_EVENT_INOP,        mpc_action_go_inop},

{  MPCG_STATE_XID2INITW,	MPCG_EVENT_XID0DO,      mpc_action_doxid0},
{  MPCG_STATE_XID2INITW,	MPCG_EVENT_XID2,        mpc_action_rcvd_xid0},
{  MPCG_STATE_XID2INITW,	MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_XID2INITW,	MPCG_EVENT_TIMER,       mpc_action_timeout},
{  MPCG_STATE_XID2INITW,	MPCG_EVENT_DOIO,        mpc_action_yside_xid},

{  MPCG_STATE_XID2INITX,	MPCG_EVENT_XID0DO,      mpc_action_doxid0},
{  MPCG_STATE_XID2INITX,	MPCG_EVENT_XID2,        mpc_action_rcvd_xid0},
{  MPCG_STATE_XID2INITX,	MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_XID2INITX,	MPCG_EVENT_TIMER,       mpc_action_timeout},
{  MPCG_STATE_XID2INITX,	MPCG_EVENT_DOIO,        mpc_action_yside_xid},

{  MPCG_STATE_XID7INITW,	MPCG_EVENT_XID2DONE,    mpc_action_doxid7},
{  MPCG_STATE_XID7INITW,	MPCG_EVENT_DISCONC,     mpc_action_discontact},
{  MPCG_STATE_XID7INITW,	MPCG_EVENT_XID2,        mpc_action_rcvd_xid7},
{  MPCG_STATE_XID7INITW,	MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_XID7INITW,	MPCG_EVENT_TIMER,       mpc_action_timeout},
{  MPCG_STATE_XID7INITW,	MPCG_EVENT_XID7DONE,    mpc_action_doxid7},
{  MPCG_STATE_XID7INITW,	MPCG_EVENT_DOIO,        mpc_action_yside_xid},

{  MPCG_STATE_XID7INITX,	MPCG_EVENT_DISCONC,     mpc_action_discontact},
{  MPCG_STATE_XID7INITX,	MPCG_EVENT_XID2,        mpc_action_rcvd_xid7},
{  MPCG_STATE_XID7INITX,	MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_XID7INITX,	MPCG_EVENT_XID7DONE,    mpc_action_doxid7},
{  MPCG_STATE_XID7INITX,	MPCG_EVENT_TIMER,       mpc_action_timeout},
{  MPCG_STATE_XID7INITX,	MPCG_EVENT_DOIO,        mpc_action_yside_xid},

{  MPCG_STATE_XID0IOWAIT,	MPCG_EVENT_XID0DO,      mpc_action_doxid0},
{  MPCG_STATE_XID0IOWAIT,	MPCG_EVENT_DISCONC,     mpc_action_discontact},
{  MPCG_STATE_XID0IOWAIT,	MPCG_EVENT_XID2,        mpc_action_rcvd_xid0},
{  MPCG_STATE_XID0IOWAIT,	MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_XID0IOWAIT,	MPCG_EVENT_TIMER,       mpc_action_timeout},
{  MPCG_STATE_XID0IOWAIT,	MPCG_EVENT_DOIO,        mpc_action_xside_xid},

{  MPCG_STATE_XID0IOWAIX,	MPCG_EVENT_XID0DO,	mpc_action_doxid0},
{  MPCG_STATE_XID0IOWAIX,	MPCG_EVENT_DISCONC,     mpc_action_discontact},
{  MPCG_STATE_XID0IOWAIX,	MPCG_EVENT_XID2,        mpc_action_rcvd_xid0},
{  MPCG_STATE_XID0IOWAIX,	MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_XID0IOWAIX,	MPCG_EVENT_TIMER,       mpc_action_timeout},
{  MPCG_STATE_XID0IOWAIX,	MPCG_EVENT_DOIO,        mpc_action_xside_xid},

{  MPCG_STATE_XID7INITI,	MPCG_EVENT_XID2DONE,    mpc_action_doxid7},
{  MPCG_STATE_XID7INITI,	MPCG_EVENT_XID2,        mpc_action_rcvd_xid7},
{  MPCG_STATE_XID7INITI,	MPCG_EVENT_DISCONC,     mpc_action_discontact},
{  MPCG_STATE_XID7INITI,	MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_XID7INITI,	MPCG_EVENT_TIMER,       mpc_action_timeout},
{  MPCG_STATE_XID7INITI,	MPCG_EVENT_XID7DONE,    mpc_action_doxid7},
{  MPCG_STATE_XID7INITI,	MPCG_EVENT_DOIO,        mpc_action_xside_xid},

{  MPCG_STATE_XID7INITZ,	MPCG_EVENT_XID2,        mpc_action_rcvd_xid7},
{  MPCG_STATE_XID7INITZ,	MPCG_EVENT_XID7DONE,    mpc_action_doxid7},
{  MPCG_STATE_XID7INITZ,	MPCG_EVENT_DISCONC,     mpc_action_discontact},
{  MPCG_STATE_XID7INITZ,	MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_XID7INITZ,	MPCG_EVENT_TIMER,       mpc_action_timeout},
{  MPCG_STATE_XID7INITZ,	MPCG_EVENT_DOIO,        mpc_action_xside_xid},

{  MPCG_STATE_XID7INITF,	MPCG_EVENT_INOP,        mpc_action_go_inop},
{  MPCG_STATE_XID7INITF,	MPCG_EVENT_XID7DONE,	mpc_action_go_ready},

};

static const int MPCG_FSM_LEN = sizeof(mpcg_fsm) / sizeof(fsm_node);

