/* 
 *
 * IPACX specific routines
 *
 * Author       Joerg Petersohn
 * Derived from hisax_isac.c, isac.c, hscx.c and others
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include "hisax_if.h"
#include "hisax.h"
#include "isdnl1.h"
#include "ipacx.h"

#define DBUSY_TIMER_VALUE 80
#define TIMER3_VALUE      7000
#define MAX_DFRAME_LEN_L1 300
#define B_FIFO_SIZE       64
#define D_FIFO_SIZE       32

// ipacx interrupt mask values    
#define _MASK_IMASK     0x2E  // global mask
#define _MASKB_IMASK    0x0B
#define _MASKD_IMASK    0x03  // all on

//----------------------------------------------------------
// local function declarations
//----------------------------------------------------------
static void ph_command(struct IsdnCardState *cs, unsigned int command);
static inline void cic_int(struct IsdnCardState *cs);
static void dch_l2l1(struct PStack *st, int pr, void *arg);
static void dbusy_timer_handler(struct IsdnCardState *cs);
static void ipacx_new_ph(struct IsdnCardState *cs);
static void dch_bh(void *data);
static void dch_empty_fifo(struct IsdnCardState *cs, int count);
static void dch_fill_fifo(struct IsdnCardState *cs);
static inline void dch_int(struct IsdnCardState *cs);
static void bch_l2l1(struct PStack *st, int pr, void *arg);
static void ipacx_bc_empty_fifo(struct BCState *bcs, int count);
static void bch_int(struct IsdnCardState *cs, u8 hscx);
static void bch_mode(struct BCState *bcs, int mode, int bc);
static void bch_close_state(struct BCState *bcs);
static int bch_open_state(struct IsdnCardState *cs, struct BCState *bcs);
static int bch_setstack(struct PStack *st, struct BCState *bcs);
static void __devinit bch_init(struct IsdnCardState *cs, int hscx);
static void __init clear_pending_ints(struct IsdnCardState *cs);

static inline u8
ipacx_bc_read_reg(struct BCState *bcs, u8 addr)
{
	struct IsdnCardState *cs = bcs->cs;

	return cs->bc_hw_ops->read_reg(cs, bcs->unit, addr);
}

static inline void
ipacx_bc_write_reg(struct BCState *bcs, u8 addr, u8 val)
{
	struct IsdnCardState *cs = bcs->cs;

	cs->bc_hw_ops->write_reg(cs, bcs->unit, addr, val);
}

static inline u8
ipacx_read_reg(struct IsdnCardState *cs, u8 addr)
{
	return cs->dc_hw_ops->read_reg(cs, addr);
}

static inline void
ipacx_write_reg(struct IsdnCardState *cs, u8 addr, u8 val)
{
	cs->dc_hw_ops->write_reg(cs, addr, val);
}

static inline void
ipacx_read_fifo(struct IsdnCardState *cs, u8 *p, int len)
{
	return cs->dc_hw_ops->read_fifo(cs, p, len);
}

static inline void
ipacx_write_fifo(struct IsdnCardState *cs, u8 *p, int len)
{
	return cs->dc_hw_ops->write_fifo(cs, p, len);
}
//----------------------------------------------------------
// Issue Layer 1 command to chip
//----------------------------------------------------------
static void 
ph_command(struct IsdnCardState *cs, unsigned int command)
{
	if (cs->debug &L1_DEB_ISAC)
		debugl1(cs, "ph_command (%#x) in (%#x)", command,
			cs->dc.isac.ph_state);
	ipacx_write_reg(cs, IPACX_CIX0, (command << 4) | 0x0E);
}

//----------------------------------------------------------
// Transceiver interrupt handler
//----------------------------------------------------------
static inline void 
cic_int(struct IsdnCardState *cs)
{
	u8 event;

	event = ipacx_read_reg(cs, IPACX_CIR0) >> 4;
	if (cs->debug &L1_DEB_ISAC) debugl1(cs, "cic_int(event=%#x)", event);
	cs->dc.isac.ph_state = event;
	sched_d_event(cs, D_L1STATECHANGE);
}

//==========================================================
// D channel functions
//==========================================================

//----------------------------------------------------------
// Command entry point
//----------------------------------------------------------
static void
dch_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
  u8 cda1_cr, cda2_cr;

	switch (pr) {
		case (PH_DATA |REQUEST):
			xmit_data_req_d(cs, skb);
			break;
		case (PH_PULL |INDICATION):
			xmit_pull_ind_d(cs, skb);
			break;
		case (PH_PULL | REQUEST):
			xmit_pull_req_d(st);
			break;
		case (HW_RESET | REQUEST):
		case (HW_ENABLE | REQUEST):
			ph_command(cs, IPACX_CMD_TIM);
			break;

		case (HW_INFO3 | REQUEST):
			ph_command(cs, IPACX_CMD_AR8);
			break;

		case (HW_TESTLOOP | REQUEST):
      ipacx_write_reg(cs, IPACX_CDA_TSDP10, 0x80); // Timeslot 0 is B1
      ipacx_write_reg(cs, IPACX_CDA_TSDP11, 0x81); // Timeslot 0 is B1
      cda1_cr = ipacx_read_reg(cs, IPACX_CDA1_CR);
      cda2_cr = ipacx_read_reg(cs, IPACX_CDA2_CR);
			if ((long)arg &1) { // loop B1
        ipacx_write_reg(cs, IPACX_CDA1_CR, cda1_cr |0x0a); 
      }
      else {  // B1 off
        ipacx_write_reg(cs, IPACX_CDA1_CR, cda1_cr &~0x0a); 
      }
			if ((long)arg &2) { // loop B2
        ipacx_write_reg(cs, IPACX_CDA1_CR, cda1_cr |0x14); 
      }
      else {  // B2 off
        ipacx_write_reg(cs, IPACX_CDA1_CR, cda1_cr &~0x14); 
      }
			break;

		case (HW_DEACTIVATE | RESPONSE):
			skb_queue_purge(&cs->rq);
			skb_queue_purge(&cs->sq);
			if (cs->tx_skb) {
				dev_kfree_skb_any(cs->tx_skb);
				cs->tx_skb = NULL;
			}
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			break;

		default:
			if (cs->debug &L1_DEB_WARN) debugl1(cs, "dch_l2l1 unknown %04x", pr);
			break;
	}
}

//----------------------------------------------------------
//----------------------------------------------------------
static void
dbusy_timer_handler(struct IsdnCardState *cs)
{
	struct PStack *st;
	int	rbchd, stard;

	if (test_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		rbchd = ipacx_read_reg(cs, IPACX_RBCHD);
		stard = ipacx_read_reg(cs, IPACX_STARD);
		if (cs->debug) 
      debugl1(cs, "D-Channel Busy RBCHD %02x STARD %02x", rbchd, stard);
		if (!(stard &0x40)) { // D-Channel Busy
			set_bit(FLG_L1_DBUSY, &cs->HW_Flags);
      for (st = cs->stlist; st; st = st->next) {
				st->l2.l1l2(st, PH_PAUSE | INDICATION, NULL); // flow control on
			}
		} else {
			// seems we lost an interrupt; reset transceiver */
			clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags);
			if (cs->tx_skb) {
				dev_kfree_skb_any(cs->tx_skb);
				cs->tx_cnt = 0;
				cs->tx_skb = NULL;
			} else {
				printk(KERN_WARNING "HiSax: ISAC D-Channel Busy no skb\n");
				debugl1(cs, "D-Channel Busy no skb");
			}
			ipacx_write_reg(cs, IPACX_CMDRD, 0x01); // Tx reset, generates XPR
		}
	}
}

//----------------------------------------------------------
// L1 state machine intermediate layer to isdnl1 module
//----------------------------------------------------------
static void
ipacx_new_ph(struct IsdnCardState *cs)
{
	switch (cs->dc.isac.ph_state) {
		case (IPACX_IND_RES):
			ph_command(cs, IPACX_CMD_DI);
			l1_msg(cs, HW_RESET | INDICATION, NULL);
			break;
      
		case (IPACX_IND_DC):
			l1_msg(cs, HW_DEACTIVATE | CONFIRM, NULL);
			break;
      
		case (IPACX_IND_DR):
			l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
			break;
      
		case (IPACX_IND_PU):
			l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
			break;

		case (IPACX_IND_RSY):
			l1_msg(cs, HW_RSYNC | INDICATION, NULL);
			break;

		case (IPACX_IND_AR):
			l1_msg(cs, HW_INFO2 | INDICATION, NULL);
			break;
      
		case (IPACX_IND_AI8):
			l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
			break;
      
		case (IPACX_IND_AI10):
			l1_msg(cs, HW_INFO4_P10 | INDICATION, NULL);
			break;
      
		default:
			break;
	}
}

//----------------------------------------------------------
// bottom half handler for D channel
//----------------------------------------------------------
static void
dch_bh(void *data)
{
	struct IsdnCardState *cs = data;
	struct PStack *st;
	
	if (!cs) return;
  
	if (test_and_clear_bit(D_CLEARBUSY, &cs->event)) {
		if (cs->debug) debugl1(cs, "D-Channel Busy cleared");
		for (st = cs->stlist; st; st = st->next) {
			st->l2.l1l2(st, PH_PAUSE | CONFIRM, NULL);
		}
	}
  
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event)) {
		DChannel_proc_rcv(cs);
  }  
  
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event)) {
		DChannel_proc_xmt(cs);
  }  
  
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event)) {
    ipacx_new_ph(cs);
  }  
}

//----------------------------------------------------------
// Fill buffer from receive FIFO
//----------------------------------------------------------
static void 
dch_empty_fifo(struct IsdnCardState *cs, int count)
{
	recv_empty_fifo_d(cs, count);
	ipacx_write_reg(cs, IPACX_CMDRD, 0x80); // RMC
}

//----------------------------------------------------------
// Fill transmit FIFO
//----------------------------------------------------------
static void 
dch_fill_fifo(struct IsdnCardState *cs)
{
	int count, more;
	unsigned char cmd, *p;

	p = xmit_fill_fifo_d(cs, 32, &count, &more);
	if (!p)
		return;

	if (more) {
		cmd   = 0x08; // XTF
	} else {
		cmd   = 0x0A; // XTF | XME
	}
  
	ipacx_write_fifo(cs, p, count);
	ipacx_write_reg(cs, IPACX_CMDRD, cmd);
  
  // set timeout for transmission contol
	if (test_and_set_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		debugl1(cs, "dch_fill_fifo dbusytimer running");
		del_timer(&cs->dbusytimer);
	}
	init_timer(&cs->dbusytimer);
	cs->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&cs->dbusytimer);
}

//----------------------------------------------------------
// D channel interrupt handler
//----------------------------------------------------------
static inline void 
dch_int(struct IsdnCardState *cs)
{
	u8 istad, rstad;
	int count;

	istad = ipacx_read_reg(cs, IPACX_ISTAD);
  
	if (istad &0x80) {  // RME
	  rstad = ipacx_read_reg(cs, IPACX_RSTAD);
		if ((rstad &0xf0) != 0xa0) { // !(VFR && !RDO && CRC && !RAB)
			if (!(rstad &0x80))
				if (cs->debug &L1_DEB_WARN) 
					debugl1(cs, "dch_int(): invalid frame");
			if ((rstad &0x40))
				if (cs->debug &L1_DEB_WARN) 
					debugl1(cs, "dch_int(): RDO");
			if (!(rstad &0x20))
				if (cs->debug &L1_DEB_WARN) 
					debugl1(cs, "dch_int(): CRC error");
			ipacx_write_reg(cs, IPACX_CMDRD, 0x80);  // RMC
			cs->rcvidx = 0;
		} else {  // received frame ok
			count = ipacx_read_reg(cs, IPACX_RBCLD);
			// FIXME this looks flaky
			if (count) count--; // RSTAB is last byte
			count &= D_FIFO_SIZE-1;
			if (count == 0)
				count = D_FIFO_SIZE;
			dch_empty_fifo(cs, count);
			recv_rme_d(cs);
		}
	}

	if (istad &0x40) {  // RPF
		dch_empty_fifo(cs, D_FIFO_SIZE);
	}

	if (istad &0x20) {  // RFO
		if (cs->debug &L1_DEB_WARN) debugl1(cs, "dch_int(): RFO");
	  ipacx_write_reg(cs, IPACX_CMDRD, 0x40); //RRES
	}
  
	if (istad &0x10) {  // XPR
		xmit_xpr_d(cs);
	}  

	if (istad &0x0C) {  // XDU or XMR
		xmit_xdu_d(cs, NULL);
	}
}

//----------------------------------------------------------
//----------------------------------------------------------
static int
dch_setstack(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = dch_l2l1;
	return 0;
}

static struct dc_l1_ops ipacx_dc_l1_ops = {
	.fill_fifo  = dch_fill_fifo,
	.open       = dch_setstack,
	.bh_func    = dch_bh,
	.dbusy_func = dbusy_timer_handler,
};

//----------------------------------------------------------
//----------------------------------------------------------
static void __devinit
dch_init(struct IsdnCardState *cs)
{
	printk(KERN_INFO "HiSax: IPACX ISDN driver v0.1.0\n");

	dc_l1_init(cs, &ipacx_dc_l1_ops);

	ipacx_write_reg(cs, IPACX_TR_CONF0, 0x00);  // clear LDD
	ipacx_write_reg(cs, IPACX_TR_CONF2, 0x00);  // enable transmitter
	ipacx_write_reg(cs, IPACX_MODED,    0xC9);  // transparent mode 0, RAC, stop/go
	ipacx_write_reg(cs, IPACX_MON_CR,   0x00);  // disable monitor channel
}


//==========================================================
// B channel functions
//==========================================================

//----------------------------------------------------------
// Entry point for commands
//----------------------------------------------------------
static void
bch_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;

	switch (pr) {
		case (PH_DATA | REQUEST):
			xmit_data_req_b(st->l1.bcs, skb);
			break;
		case (PH_PULL | INDICATION):
			xmit_pull_ind_b(st->l1.bcs, skb);
			break;
		case (PH_PULL | REQUEST):
			xmit_pull_req_b(st);
			break;
		case (PH_ACTIVATE | REQUEST):
			set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			bch_mode(st->l1.bcs, st->l1.mode, st->l1.bc);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			bch_mode(st->l1.bcs, 0, st->l1.bc);
			st->l2.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

//----------------------------------------------------------
// Read B channel fifo to receive buffer
//----------------------------------------------------------
static void
ipacx_bc_empty_fifo(struct BCState *bcs, int count)
{
	recv_empty_fifo_b(bcs, count);
	ipacx_bc_write_reg(bcs, IPACX_CMDRB, 0x80);  // RMC
}

//----------------------------------------------------------
// Fill buffer to transmit FIFO
//----------------------------------------------------------
static void
ipacx_bc_fill_fifo(struct BCState *bcs)
{
	int more, count;
	unsigned char *p;

	p = xmit_fill_fifo_b(bcs, B_FIFO_SIZE, &count, &more);
	if (!p)
		return;

	while (count--)
		ipacx_bc_write_reg(bcs, IPACX_XFIFOB, *p++); 

	ipacx_bc_write_reg(bcs, IPACX_CMDRB, (more ? 0x08 : 0x0a));
}

//----------------------------------------------------------
// B channel interrupt handler
//----------------------------------------------------------

static void
reset_xmit(struct BCState *bcs)
{
	ipacx_bc_write_reg(bcs, IPACX_CMDRB, 0x01);  // XRES
}

static void
bch_int(struct IsdnCardState *cs, u8 hscx)
{
	u8 istab;
	struct BCState *bcs;
	int count;
	u8 rstab;

	bcs = cs->bcs + hscx;
	istab = ipacx_bc_read_reg(bcs, IPACX_ISTAB);
	if (!test_bit(BC_FLG_INIT, &bcs->Flag)) return;

	if (istab &0x80) {	// RME
		rstab = ipacx_bc_read_reg(bcs, IPACX_RSTAB);
		if ((rstab &0xf0) != 0xa0) { // !(VFR && !RDO && CRC && !RAB)
			if (!(rstab &0x80))
				if (cs->debug &L1_DEB_WARN) 
					debugl1(cs, "bch_int() B-%d: invalid frame", hscx);
			if ((rstab &0x40) && (bcs->mode != L1_MODE_NULL))
				if (cs->debug &L1_DEB_WARN) 
					debugl1(cs, "bch_int() B-%d: RDO mode=%d", hscx, bcs->mode);
			if (!(rstab &0x20))
				if (cs->debug &L1_DEB_WARN) 
					debugl1(cs, "bch_int() B-%d: CRC error", hscx);
			ipacx_bc_write_reg(bcs, IPACX_CMDRB, 0x80);  // RMC
			bcs->rcvidx = 0;
		}  else {  // received frame ok
			count = ipacx_bc_read_reg(bcs, IPACX_RBCLB) &(B_FIFO_SIZE-1);
			if (count == 0)
				count = B_FIFO_SIZE;

			ipacx_bc_empty_fifo(bcs, count);
			recv_rme_b(bcs);
		}
	}
  
	if (istab &0x40) {	// RPF
		ipacx_bc_empty_fifo(bcs, B_FIFO_SIZE);
		recv_rpf_b(bcs);
	}
  
	if (istab &0x20) {	// RFO
		if (cs->debug &L1_DEB_WARN) 
			debugl1(cs, "bch_int() B-%d: RFO error", hscx);
		ipacx_bc_write_reg(bcs, IPACX_CMDRB, 0x40);  // RRES
	}

	if (istab &0x10) {	// XPR
		xmit_xpr_b(bcs);
	}

	if (istab &0x04) {	// XDU
		xmit_xdu_b(bcs, reset_xmit);
	}
}

//----------------------------------------------------------
//----------------------------------------------------------
static void
bch_mode(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int hscx = bcs->unit;

        bc = bc ? 1 : 0;  // in case bc is greater than 1
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "mode_bch() switch B-% mode %d chan %d", hscx, mode, bc);
	bcs->mode = mode;
	bcs->channel = bc;
  
  // map controller to according timeslot
  if (!hscx)
  {
    ipacx_write_reg(cs, IPACX_BCHA_TSDP_BC1, 0x80 | bc);
    ipacx_write_reg(cs, IPACX_BCHA_CR,       0x88); 
  }
  else
  {
    ipacx_write_reg(cs, IPACX_BCHB_TSDP_BC1, 0x80 | bc);
    ipacx_write_reg(cs, IPACX_BCHB_CR,       0x88); 
  }

	switch (mode) {
		case (L1_MODE_NULL):
		    ipacx_bc_write_reg(bcs, IPACX_MODEB, 0xC0);  // rec off
		    ipacx_bc_write_reg(bcs, IPACX_EXMB,  0x30);  // std adj.
		    ipacx_bc_write_reg(bcs, IPACX_MASKB, 0xFF);  // ints off
		    ipacx_bc_write_reg(bcs, IPACX_CMDRB, 0x41);  // validate adjustments
		    break;
		case (L1_MODE_TRANS):
		    ipacx_bc_write_reg(bcs, IPACX_MODEB, 0x88);  // ext transp mode
		    ipacx_bc_write_reg(bcs, IPACX_EXMB,  0x00);  // xxx00000
		    ipacx_bc_write_reg(bcs, IPACX_CMDRB, 0x41);  // validate adjustments
		    ipacx_bc_write_reg(bcs, IPACX_MASKB, _MASKB_IMASK);
		    break;
		case (L1_MODE_HDLC):
		    ipacx_bc_write_reg(bcs, IPACX_MODEB, 0xC8);  // transp mode 0
		    ipacx_bc_write_reg(bcs, IPACX_EXMB,  0x01);  // idle=hdlc flags crc enabled
		    ipacx_bc_write_reg(bcs, IPACX_CMDRB, 0x41);  // validate adjustments
		    ipacx_bc_write_reg(bcs, IPACX_MASKB, _MASKB_IMASK);
		    break;
	}
}

//----------------------------------------------------------
//----------------------------------------------------------
static void
bch_close_state(struct BCState *bcs)
{
	bch_mode(bcs, 0, bcs->channel);
	bc_close(bcs);
}

//----------------------------------------------------------
//----------------------------------------------------------
static int
bch_open_state(struct IsdnCardState *cs, struct BCState *bcs)
{
	return bc_open(bcs);
}

//----------------------------------------------------------
//----------------------------------------------------------
static int
bch_setstack(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (bch_open_state(st->l1.hardware, bcs)) return (-1);
	st->l1.bcs = bcs;
	st->l1.l2l1 = bch_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

//----------------------------------------------------------
//----------------------------------------------------------
static void __devinit
bch_init(struct IsdnCardState *cs, int hscx)
{
	cs->bcs[hscx].unit          = hscx;
	cs->bcs[hscx].cs            = cs;
	bch_mode(cs->bcs + hscx, 0, hscx);
}


//==========================================================
// Shared functions
//==========================================================

//----------------------------------------------------------
// Main interrupt handler
//----------------------------------------------------------
void 
interrupt_ipacx(struct IsdnCardState *cs)
{
	u8 ista;
	
	spin_lock(&cs->lock);
	while ((ista = ipacx_read_reg(cs, IPACX_ISTA))) {
		if (ista &0x80) bch_int(cs, 0); // B channel interrupts
		if (ista &0x40) bch_int(cs, 1);
		if (ista &0x01) dch_int(cs);    // D channel
		if (ista &0x10) cic_int(cs);    // Layer 1 state
	}  
	spin_unlock(&cs->lock);
}

//----------------------------------------------------------
// Clears chip interrupt status
//----------------------------------------------------------
static void __init
clear_pending_ints(struct IsdnCardState *cs)
{
	int ista;

  // all interrupts off
  ipacx_write_reg(cs, IPACX_MASK, 0xff);
  ipacx_write_reg(cs, IPACX_MASKD, 0xff);
  cs->bc_hw_ops->write_reg(cs, 0, IPACX_MASKB, 0xff);
  cs->bc_hw_ops->write_reg(cs, 1, IPACX_MASKB, 0xff);
  
  ista = ipacx_read_reg(cs, IPACX_ISTA); 
  if (ista &0x80) cs->bc_hw_ops->read_reg(cs, 0, IPACX_ISTAB);
  if (ista &0x40) cs->bc_hw_ops->read_reg(cs, 1, IPACX_ISTAB);
  if (ista &0x10) ipacx_read_reg(cs, IPACX_CIR0);
  if (ista &0x01) ipacx_read_reg(cs, IPACX_ISTAD); 
}

static struct bc_l1_ops ipacx_bc_l1_ops = {
	.fill_fifo = ipacx_bc_fill_fifo,
	.open      = bch_setstack,
	.close     = bch_close_state,
};

//----------------------------------------------------------
// Does chip configuration work
// Work to do depends on bit mask in part
//----------------------------------------------------------
void __init
init_ipacx(struct IsdnCardState *cs, int part)
{
	if (part &1) {  // initialise chip
		cs->bc_l1_ops = &ipacx_bc_l1_ops;
		clear_pending_ints(cs);
		bch_init(cs, 0);
		bch_init(cs, 1);
		dch_init(cs);
	}
	if (part &2) {  // reenable all interrupts and start chip
		cs->bc_hw_ops->write_reg(cs, 0, IPACX_MASKB, _MASKB_IMASK);
		cs->bc_hw_ops->write_reg(cs, 1, IPACX_MASKB, _MASKB_IMASK);
		ipacx_write_reg(cs, IPACX_MASKD, _MASKD_IMASK);
		ipacx_write_reg(cs, IPACX_MASK, _MASK_IMASK); // global mask register

    // reset HDLC Transmitters/receivers
		ipacx_write_reg(cs, IPACX_CMDRD, 0x41); 
		cs->bc_hw_ops->write_reg(cs, 0, IPACX_CMDRB, 0x41);
		cs->bc_hw_ops->write_reg(cs, 1, IPACX_CMDRB, 0x41);
		ph_command(cs, IPACX_CMD_RES);
	}
}

int
ipacx_setup(struct IsdnCardState *cs, struct dc_hw_ops *ipacx_dc_ops,
	    struct bc_hw_ops *ipacx_bc_ops)
{
	u8 val;

	cs->dc_hw_ops = ipacx_dc_ops;
	cs->bc_hw_ops = ipacx_bc_ops;
	val = ipacx_read_reg(cs, IPACX_ID) & 0x3f;
	printk(KERN_INFO "HiSax: IPACX Design Id: %#x\n", val);
	return 0;
}

