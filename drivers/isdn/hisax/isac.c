/* $Id: isac.c,v 1.28.6.3 2001/09/23 22:24:49 kai Exp $
 *
 * ISAC specific routines
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For changes and modifications please read
 * ../../../Documentation/isdn/HiSax.cert
 *
 */

#include "hisax.h"
#include "isac.h"
#include "arcofi.h"
#include "isdnl1.h"
#include <linux/interrupt.h>
#include <linux/init.h>

#define DBUSY_TIMER_VALUE 80
#define ARCOFI_USE 1

static char *ISACVer[] __devinitdata =
{"2086/2186 V1.1", "2085 B1", "2085 B2",
 "2085 V2.3"};

static inline u8
isac_read(struct IsdnCardState *cs, u8 addr)
{
	return cs->dc_hw_ops->read_reg(cs, addr);
}

static inline void
isac_write(struct IsdnCardState *cs, u8 addr, u8 val)
{
	cs->dc_hw_ops->write_reg(cs, addr, val);
}

static inline void
isac_write_fifo(struct IsdnCardState *cs, u8 *p, int len)
{
	return cs->dc_hw_ops->write_fifo(cs, p, len);
}

static void
ISACVersion(struct IsdnCardState *cs, char *s)
{
	int val;

	val = isac_read(cs, ISAC_RBCH);
	printk(KERN_INFO "%s ISAC version (%x): %s\n", s, val, ISACVer[(val >> 5) & 3]);
}

static void
ph_command(struct IsdnCardState *cs, unsigned int command)
{
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "ph_command %x", command);
	isac_write(cs, ISAC_CIX0, (command << 2) | 3);
}


static void
isac_new_ph(struct IsdnCardState *cs)
{
	switch (cs->dc.isac.ph_state) {
		case (ISAC_IND_RS):
		case (ISAC_IND_EI):
			ph_command(cs, ISAC_CMD_DUI);
			l1_msg(cs, HW_RESET | INDICATION, NULL);
			break;
		case (ISAC_IND_DID):
			l1_msg(cs, HW_DEACTIVATE | CONFIRM, NULL);
			break;
		case (ISAC_IND_DR):
			l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
			break;
		case (ISAC_IND_PU):
			l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
			break;
		case (ISAC_IND_RSY):
			l1_msg(cs, HW_RSYNC | INDICATION, NULL);
			break;
		case (ISAC_IND_ARD):
			l1_msg(cs, HW_INFO2 | INDICATION, NULL);
			break;
		case (ISAC_IND_AI8):
			l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
			break;
		case (ISAC_IND_AI10):
			l1_msg(cs, HW_INFO4_P10 | INDICATION, NULL);
			break;
		default:
			break;
	}
}

static void
isac_bh(void *data)
{
	struct IsdnCardState *cs = data;
	struct PStack *stptr;
	
	if (!cs)
		return;
	if (test_and_clear_bit(D_CLEARBUSY, &cs->event)) {
		if (cs->debug)
			debugl1(cs, "D-Channel Busy cleared");
		stptr = cs->stlist;
		while (stptr != NULL) {
			L1L2(stptr, PH_PAUSE | CONFIRM, NULL);
			stptr = stptr->next;
		}
	}
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event))
		isac_new_ph(cs);		
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
#if ARCOFI_USE
	if (!test_bit(HW_ARCOFI, &cs->HW_Flags))
		return;
	if (test_and_clear_bit(D_RX_MON1, &cs->event))
		arcofi_fsm(cs, ARCOFI_RX_END, NULL);
	if (test_and_clear_bit(D_TX_MON1, &cs->event))
		arcofi_fsm(cs, ARCOFI_TX_END, NULL);
#endif
}

void
isac_empty_fifo(struct IsdnCardState *cs, int count)
{
	recv_empty_fifo_d(cs, count);
	isac_write(cs, ISAC_CMDR, 0x80);
}

static void
isac_fill_fifo(struct IsdnCardState *cs)
{
	int count, more;
	unsigned char *p;

	p = xmit_fill_fifo_d(cs, 32, &count, &more);
	if (!p)
		return;

	isac_write_fifo(cs, p, count);
	isac_write(cs, ISAC_CMDR, more ? 0x8 : 0xa);
	if (test_and_set_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		debugl1(cs, "isac_fill_fifo dbusytimer running");
		del_timer(&cs->dbusytimer);
	}
	init_timer(&cs->dbusytimer);
	cs->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&cs->dbusytimer);
}

void
isac_interrupt(struct IsdnCardState *cs, u8 val)
{
	u8 exval, v1;
	unsigned int count;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "ISAC interrupt %x", val);
	if (val & 0x80) {	/* RME */
		exval = isac_read(cs, ISAC_RSTA);
		if ((exval & 0x70) != 0x20) {
			if (exval & 0x40) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "ISAC RDO");
#ifdef ERROR_STATISTIC
				cs->err_rx++;
#endif
			}
			if (!(exval & 0x20)) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "ISAC CRC error");
#ifdef ERROR_STATISTIC
				cs->err_crc++;
#endif
			}
			isac_write(cs, ISAC_CMDR, 0x80);
			cs->rcvidx = 0;
		} else {
			count = isac_read(cs, ISAC_RBCL) & 0x1f;
			if (count == 0)
				count = 32;
			isac_empty_fifo(cs, count);
 			recv_rme_d(cs);
		}
		cs->rcvidx = 0;
		sched_d_event(cs, D_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		isac_empty_fifo(cs, 32);
	}
	if (val & 0x20) {	/* RSC */
		/* never */
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ISAC RSC interrupt");
	}
	if (val & 0x10) {	/* XPR */
		xmit_xpr_d(cs);
	}
	if (val & 0x04) {	/* CISQ */
		exval = isac_read(cs, ISAC_CIR0);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC CIR0 %02X", exval );
		if (exval & 2) {
			cs->dc.isac.ph_state = (exval >> 2) & 0xf;
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "ph_state change %x", cs->dc.isac.ph_state);
			sched_d_event(cs, D_L1STATECHANGE);
		}
		if (exval & 1) {
			exval = isac_read(cs, ISAC_CIR1);
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "ISAC CIR1 %02X", exval );
		}
	}
	if (val & 0x02) {	/* SIN */
		/* never */
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ISAC SIN interrupt");
	}
	if (val & 0x01) {	/* EXI */
		exval = isac_read(cs, ISAC_EXIR);
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ISAC EXIR %02x", exval);
		if (exval & 0x80) {  /* XMR */
			debugl1(cs, "ISAC XMR");
			printk(KERN_WARNING "HiSax: ISAC XMR\n");
		}
		if (exval & 0x40) {  /* XDU */
			xmit_xdu_d(cs, NULL);
		}
		if (exval & 0x04) {  /* MOS */
			v1 = isac_read(cs, ISAC_MOSR);
			if (cs->debug & L1_DEB_MONITOR)
				debugl1(cs, "ISAC MOSR %02x", v1);
#if ARCOFI_USE
			if (v1 & 0x08) {
				if (!cs->dc.isac.mon_rx) {
					if (!(cs->dc.isac.mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ISAC MON RX out of memory!");
						cs->dc.isac.mocr &= 0xf0;
						cs->dc.isac.mocr |= 0x0a;
						isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
						goto afterMONR0;
					} else
						cs->dc.isac.mon_rxp = 0;
				}
				if (cs->dc.isac.mon_rxp >= MAX_MON_FRAME) {
					cs->dc.isac.mocr &= 0xf0;
					cs->dc.isac.mocr |= 0x0a;
					isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
					cs->dc.isac.mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ISAC MON RX overflow!");
					goto afterMONR0;
				}
				cs->dc.isac.mon_rx[cs->dc.isac.mon_rxp++] = isac_read(cs, ISAC_MOR0);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ISAC MOR0 %02x", cs->dc.isac.mon_rx[cs->dc.isac.mon_rxp -1]);
				if (cs->dc.isac.mon_rxp == 1) {
					cs->dc.isac.mocr |= 0x04;
					isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
				}
			}
		      afterMONR0:
			if (v1 & 0x80) {
				if (!cs->dc.isac.mon_rx) {
					if (!(cs->dc.isac.mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ISAC MON RX out of memory!");
						cs->dc.isac.mocr &= 0x0f;
						cs->dc.isac.mocr |= 0xa0;
						isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
						goto afterMONR1;
					} else
						cs->dc.isac.mon_rxp = 0;
				}
				if (cs->dc.isac.mon_rxp >= MAX_MON_FRAME) {
					cs->dc.isac.mocr &= 0x0f;
					cs->dc.isac.mocr |= 0xa0;
					isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
					cs->dc.isac.mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ISAC MON RX overflow!");
					goto afterMONR1;
				}
				cs->dc.isac.mon_rx[cs->dc.isac.mon_rxp++] = isac_read(cs, ISAC_MOR1);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ISAC MOR1 %02x", cs->dc.isac.mon_rx[cs->dc.isac.mon_rxp -1]);
				cs->dc.isac.mocr |= 0x40;
				isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
			}
		      afterMONR1:
			if (v1 & 0x04) {
				cs->dc.isac.mocr &= 0xf0;
				isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
				cs->dc.isac.mocr |= 0x0a;
				isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
				sched_d_event(cs, D_RX_MON0);
			}
			if (v1 & 0x40) {
				cs->dc.isac.mocr &= 0x0f;
				isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
				cs->dc.isac.mocr |= 0xa0;
				isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
				sched_d_event(cs, D_RX_MON1);
			}
			if (v1 & 0x02) {
				if ((!cs->dc.isac.mon_tx) || (cs->dc.isac.mon_txc && 
					(cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc) && 
					!(v1 & 0x08))) {
					cs->dc.isac.mocr &= 0xf0;
					isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
					cs->dc.isac.mocr |= 0x0a;
					isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
					if (cs->dc.isac.mon_txc &&
						(cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc))
						sched_d_event(cs, D_TX_MON0);
					goto AfterMOX0;
				}
				if (cs->dc.isac.mon_txc && (cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc)) {
					sched_d_event(cs, D_TX_MON0);
					goto AfterMOX0;
				}
				isac_write(cs, ISAC_MOX0,
					cs->dc.isac.mon_tx[cs->dc.isac.mon_txp++]);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ISAC %02x -> MOX0", cs->dc.isac.mon_tx[cs->dc.isac.mon_txp -1]);
			}
		      AfterMOX0:
			if (v1 & 0x20) {
				if ((!cs->dc.isac.mon_tx) || (cs->dc.isac.mon_txc && 
					(cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc) && 
					!(v1 & 0x80))) {
					cs->dc.isac.mocr &= 0x0f;
					isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
					cs->dc.isac.mocr |= 0xa0;
					isac_write(cs, ISAC_MOCR, cs->dc.isac.mocr);
					if (cs->dc.isac.mon_txc &&
						(cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc))
						sched_d_event(cs, D_TX_MON1);
					goto AfterMOX1;
				}
				if (cs->dc.isac.mon_txc && (cs->dc.isac.mon_txp >= cs->dc.isac.mon_txc)) {
					sched_d_event(cs, D_TX_MON1);
					goto AfterMOX1;
				}
				isac_write(cs, ISAC_MOX1,
					cs->dc.isac.mon_tx[cs->dc.isac.mon_txp++]);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ISAC %02x -> MOX1", cs->dc.isac.mon_tx[cs->dc.isac.mon_txp -1]);
			}
		      AfterMOX1:;
#endif
		}
	}
}

static void
ISAC_l1hw(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	int  val;

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
			if ((cs->dc.isac.ph_state == ISAC_IND_EI) ||
				(cs->dc.isac.ph_state == ISAC_IND_DR) ||
				(cs->dc.isac.ph_state == ISAC_IND_RS))
			        ph_command(cs, ISAC_CMD_TIM);
			else
				ph_command(cs, ISAC_CMD_RS);
			break;
		case (HW_ENABLE | REQUEST):
			ph_command(cs, ISAC_CMD_TIM);
			break;
		case (HW_INFO3 | REQUEST):
			ph_command(cs, ISAC_CMD_AR8);
			break;
		case (HW_TESTLOOP | REQUEST):
			val = 0;
			if (1 & (long) arg)
				val |= 0x0c;
			if (2 & (long) arg)
				val |= 0x3;
			if (test_bit(HW_IOM1, &cs->HW_Flags)) {
				/* IOM 1 Mode */
				if (!val) {
					isac_write(cs, ISAC_SPCR, 0xa);
					isac_write(cs, ISAC_ADF1, 0x2);
				} else {
					isac_write(cs, ISAC_SPCR, val);
					isac_write(cs, ISAC_ADF1, 0xa);
				}
			} else {
				/* IOM 2 Mode */
				isac_write(cs, ISAC_SPCR, val);
				if (val)
					isac_write(cs, ISAC_ADF1, 0x8);
				else
					isac_write(cs, ISAC_ADF1, 0x0);
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
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				sched_d_event(cs, D_CLEARBUSY);
			break;
		default:
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "isac_l1hw unknown %04x", pr);
			break;
	}
}

static int
setstack_isac(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = ISAC_l1hw;
	return 0;
}

static void 
DC_Close_isac(struct IsdnCardState *cs) {
	if (cs->dc.isac.mon_rx) {
		kfree(cs->dc.isac.mon_rx);
		cs->dc.isac.mon_rx = NULL;
	}
	if (cs->dc.isac.mon_tx) {
		kfree(cs->dc.isac.mon_tx);
		cs->dc.isac.mon_tx = NULL;
	}
}

static void
dbusy_timer_handler(struct IsdnCardState *cs)
{
	struct PStack *stptr;
	int	rbch, star;

	if (test_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		rbch = isac_read(cs, ISAC_RBCH);
		star = isac_read(cs, ISAC_STAR);
		if (cs->debug) 
			debugl1(cs, "D-Channel Busy RBCH %02x STAR %02x",
				rbch, star);
		if (rbch & ISAC_RBCH_XAC) { /* D-Channel Busy */
			test_and_set_bit(FLG_L1_DBUSY, &cs->HW_Flags);
			stptr = cs->stlist;
			while (stptr != NULL) {
				L1L2(stptr, PH_PAUSE | INDICATION, NULL);
				stptr = stptr->next;
			}
		} else {
			/* discard frame; reset transceiver */
			test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags);
			if (cs->tx_skb) {
				dev_kfree_skb_any(cs->tx_skb);
				cs->tx_cnt = 0;
				cs->tx_skb = NULL;
			} else {
				printk(KERN_WARNING "HiSax: ISAC D-Channel Busy no skb\n");
				debugl1(cs, "D-Channel Busy no skb");
			}
			isac_write(cs, ISAC_CMDR, 0x01); /* Transmitter reset */
			cs->card_ops->irq_func(cs->irq, cs, NULL);
		}
	}
}

static struct dc_l1_ops isac_l1_ops = {
	.fill_fifo  = isac_fill_fifo,
	.open       = setstack_isac,
	.close      = DC_Close_isac,
	.bh_func    = isac_bh,
	.dbusy_func = dbusy_timer_handler,
};

void __devinit
initisac(struct IsdnCardState *cs)
{
	int val, eval;

	dc_l1_init(cs, &isac_l1_ops);

	val = isac_read(cs, ISAC_STAR);
	debugl1(cs, "ISAC STAR %x", val);
	val = isac_read(cs, ISAC_MODE);
	debugl1(cs, "ISAC MODE %x", val);
	val = isac_read(cs, ISAC_ADF2);
	debugl1(cs, "ISAC ADF2 %x", val);
	val = isac_read(cs, ISAC_ISTA);
	debugl1(cs, "ISAC ISTA %x", val);
	if (val & 0x01) {
		eval = isac_read(cs, ISAC_EXIR);
		debugl1(cs, "ISAC EXIR %x", eval);
	}
	/* Disable all IRQ */
	isac_write(cs, ISAC_MASK, 0xFF);

	cs->dc.isac.mon_tx = NULL;
	cs->dc.isac.mon_rx = NULL;
  	cs->dc.isac.mocr = 0xaa;
	if (test_bit(HW_IOM1, &cs->HW_Flags)) {
		/* IOM 1 Mode */
		isac_write(cs, ISAC_ADF2, 0x0);
		isac_write(cs, ISAC_SPCR, 0xa);
		isac_write(cs, ISAC_ADF1, 0x2);
		isac_write(cs, ISAC_STCR, 0x70);
		isac_write(cs, ISAC_MODE, 0xc9);
	} else {
		/* IOM 2 Mode */
		if (!cs->dc.isac.adf2)
			cs->dc.isac.adf2 = 0x80;
		isac_write(cs, ISAC_ADF2, cs->dc.isac.adf2);
		isac_write(cs, ISAC_SQXR, 0x2f);
		isac_write(cs, ISAC_SPCR, 0x00);
		isac_write(cs, ISAC_STCR, 0x70);
		isac_write(cs, ISAC_MODE, 0xc9);
		isac_write(cs, ISAC_TIMR, 0x00);
		isac_write(cs, ISAC_ADF1, 0x00);
	}
	ph_command(cs, ISAC_CMD_RS);
	isac_write(cs, ISAC_MASK, 0x0);

	val = isac_read(cs, ISAC_CIR0);
	debugl1(cs, "ISAC CIR0 %x", val);
	cs->dc.isac.ph_state = (val >> 2) & 0xf;
	sched_d_event(cs, D_L1STATECHANGE);

	/* RESET Receiver and Transmitter */
	isac_write(cs, ISAC_CMDR, 0x41);
}

int
isac_setup(struct IsdnCardState *cs, struct dc_hw_ops *isac_ops)
{
	cs->dc_hw_ops = isac_ops;
	ISACVersion(cs, "HiSax:");
	return 0;
}
