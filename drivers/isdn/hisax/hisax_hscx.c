/*
 * Driver for HSCX
 * High-Level Serial Communcation Controller Extended
 *
 * Author       Kai Germaschewski
 * Copyright    2001 by Kai Germaschewski  <kai.germaschewski@gmx.de>
 *              2001 by Karsten Keil       <keil@isdn4linux.de>
 * 
 * based upon Karsten Keil's original isac.c driver
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

/* TODO:
 * comments in .h
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include "hisax_hscx.h"

// debugging cruft

#define __debug_variable debug
#include "hisax_debug.h"

#ifdef CONFIG_HISAX_DEBUG
static int debug = 1;
MODULE_PARM(debug, "i");

static char *HSCXVer[] =
{"A1", "?1", "A2", "?3", "A3", "V2.1", "?6", "?7",
 "?8", "?9", "?10", "?11", "?12", "?13", "?14", "???"};
#endif

MODULE_AUTHOR("Kai Germaschewski <kai.germaschewski@gmx.de>/Karsten Keil <kkeil@suse.de>");
MODULE_DESCRIPTION("HSCX driver");

#define DBG_WARN      0x0001
#define DBG_IRQ       0x0002
#define DBG_L1M       0x0004
#define DBG_PR        0x0008
#define DBG_RFIFO     0x0100
#define DBG_RPACKET   0x0200
#define DBG_XFIFO     0x1000
#define DBG_XPACKET   0x2000

#define HSCX_ISTA      0x20
#define HSCX_ISTA_EXB  0x01
#define HSCX_ISTA_EXA  0x02
#define HSCX_ISTA_ICA  0x04
#define HSCX_ISTA_TIN  0x08
#define HSCX_ISTA_XPR  0x10
#define HSCX_ISTA_RSC  0x20
#define HSCX_ISTA_RPF  0x40
#define HSCX_ISTA_RME  0x80

#define HSCX_CMDR      0x21
#define HSCX_CMDR_RMC  0x80
#define HSCX_CMDR_RHR  0x40
#define HSCX_CMDR_RNR  0x20
#define HSCX_CMDR_STI  0x10
#define HSCX_CMDR_XTF  0x08
#define HSCX_CMDR_XIF  0x04
#define HSCX_CMDR_XME  0x02
#define HSCX_CMDR_XRES 0x01

#define HSCX_EXIR      0x24
#define HSCX_EXIR_XDU  0x40

#define HSCX_RSTA      0x27
#define HSCX_RSTA_VFR  0x80
#define HSCX_RSTA_RDO  0x40
#define HSCX_RSTA_CRC  0x20
#define HSCX_RSTA_RAB  0x10

#define HSCX_CCR1 0x2f
#define HSCX_CCR2 0x2c
#define HSCX_TSAR 0x31
#define HSCX_TSAX 0x30
#define HSCX_XCCR 0x32
#define HSCX_RCCR 0x33
#define HSCX_MODE 0x22

#define HSCX_XAD1 0x24
#define HSCX_XAD2 0x25
#define HSCX_RAH2 0x27
#define HSCX_TIMR 0x23
#define HSCX_STAR 0x21
#define HSCX_RBCL 0x25
#define HSCX_XBCH 0x2d
#define HSCX_VSTR 0x2e
#define HSCX_RLCR 0x2e
#define HSCX_MASK 0x20

static inline void B_L1L2(struct hscx *hscx, int pr, void *arg)
{
	struct hisax_if *ifc = (struct hisax_if *) &hscx->b_if;

	DBG(0x10, "pr %#x", pr);
	ifc->l1l2(ifc, pr, arg);
}

static void hscx_version(struct hscx *hscx)
{
	int val;

	val = hscx->read_hscx(hscx, HSCX_VSTR) & 0xf;
	DBG(1, "HSCX version (%x): %s", val, HSCXVer[val]);
}

static void hscx_empty_fifo(struct hscx *hscx, int count)
{
	u8 *ptr;

	DBG(DBG_IRQ, "count %d", count);

	if ((hscx->rcvidx + count) >= HSCX_BUFMAX) {
		DBG(DBG_WARN, "overrun %d", hscx->rcvidx + count);
		hscx->write_hscx(hscx, HSCX_CMDR, HSCX_CMDR_RMC);
		hscx->rcvidx = 0;
		return;
	}
	ptr = hscx->rcvbuf + hscx->rcvidx;
	hscx->rcvidx += count;
	hscx->read_hscx_fifo(hscx, ptr, count);
	hscx->write_hscx(hscx, HSCX_CMDR, HSCX_CMDR_RMC);
	DBG_PACKET(DBG_RFIFO, ptr, count);
}

static void hscx_fill_fifo(struct hscx *hscx)
{
	int count;
	unsigned char cmd;
	int fifo_size = test_bit(HSCX_IPAC, &hscx->flags)? 64: 32;
	unsigned char *ptr;

	if (!hscx->tx_skb)
		BUG();

	count = hscx->tx_skb->len;
	if (count <= 0)
		BUG();

	DBG(DBG_IRQ, "count %d", count);

	if (count > fifo_size || hscx->mode == L1_MODE_TRANS) {
		count = fifo_size;
		cmd = 0x8;
	} else {
		cmd = 0xa;
	}

	ptr = hscx->tx_skb->data;
	skb_pull(hscx->tx_skb, count);
	hscx->tx_cnt += count;
	DBG_PACKET(DBG_XFIFO, ptr, count);
	hscx->write_hscx_fifo(hscx, ptr, count);
	hscx->write_hscx(hscx, HSCX_CMDR, cmd);
}

static void hscx_retransmit(struct hscx *hscx)
{
	if (!hscx->tx_skb) {
		DBG(DBG_WARN, "no skb");
		return;
	}
	skb_push(hscx->tx_skb, hscx->tx_cnt);
	hscx->tx_cnt = 0;
	hscx->write_hscx(hscx, HSCX_CMDR, 0x01);
}

static inline void hscx_rme_interrupt(struct hscx *hscx)
{
	unsigned char val;
	int count;
	struct sk_buff *skb;
	int fifo_size = test_bit(HSCX_IPAC, &hscx->flags)? 64: 32;
	
	val = hscx->read_hscx(hscx, HSCX_RSTA);
	if ((val & (HSCX_RSTA_VFR | HSCX_RSTA_RDO | HSCX_RSTA_CRC | HSCX_RSTA_RAB) )
	     != (HSCX_RSTA_VFR | HSCX_RSTA_CRC)) {
		DBG(DBG_WARN, "RSTA %#x, dropped", val);
		hscx->write_hscx(hscx, HSCX_CMDR, HSCX_CMDR_RMC);
		goto out;
	}
	
	count = hscx->read_hscx(hscx, HSCX_RBCL) & (fifo_size-1);
	DBG(DBG_IRQ, "RBCL %#x", count);
	if (count == 0)
		count = fifo_size;

	hscx_empty_fifo(hscx, count);

	count = hscx->rcvidx;
	if (count < 1) {
		DBG(DBG_WARN, "count %d < 1", count);
		goto out;
	}

	skb = alloc_skb(count, GFP_ATOMIC);
	if (!skb) {
		DBG(DBG_WARN, "no memory, dropping\n");
		goto out;
	}
	memcpy(skb_put(skb, count), hscx->rcvbuf, count);
	DBG_SKB(DBG_RPACKET, skb);
	B_L1L2(hscx, PH_DATA | INDICATION, skb);
 out:
	hscx->rcvidx = 0;
}

static inline void hscx_xpr_interrupt(struct hscx *hscx)
{
	struct sk_buff *skb;

	skb = hscx->tx_skb;
	if (!skb)
		return;

	if (skb->len > 0) {
		hscx_fill_fifo(hscx);
		return;
	}
	hscx->tx_cnt = 0;
	hscx->tx_skb = NULL;
	B_L1L2(hscx, PH_DATA | CONFIRM, skb);
}

static inline void hscx_exi_interrupt(struct hscx *hscx)
{
	unsigned char val;

	val = hscx->read_hscx(hscx, HSCX_EXIR);
	DBG(2, "EXIR %#x", val);

	if (val & HSCX_EXIR_XDU) {
		DBG(DBG_WARN, "HSCX XDU");
		if (hscx->mode == L1_MODE_TRANS) {
			hscx_fill_fifo(hscx);
		} else {
			hscx_retransmit(hscx);
		}
	}
}

static void hscx_reg_interrupt(struct hscx *hscx, unsigned char val)
{
	struct sk_buff *skb;

	if (val & HSCX_ISTA_XPR) {
		DBG(DBG_IRQ, "XPR");
		hscx_xpr_interrupt(hscx);
	}
	if (val & HSCX_ISTA_RME) {
		DBG(DBG_IRQ, "RME");
		hscx_rme_interrupt(hscx);
	}
	if (val & HSCX_ISTA_RPF) {
		int fifo_size = test_bit(HSCX_IPAC, &hscx->flags)? 64: 32;

		DBG(DBG_IRQ, "RPF");
		hscx_empty_fifo(hscx, fifo_size);
		if (hscx->mode == L1_MODE_TRANS) {
			skb = dev_alloc_skb(fifo_size);
			if (!skb) {
				DBG(DBG_WARN, "no memory, dropping\n");
				goto out;
			}
			memcpy(skb_put(skb, fifo_size), hscx->rcvbuf, fifo_size);
			DBG_SKB(DBG_RPACKET, skb);
			B_L1L2(hscx, PH_DATA | INDICATION, skb);
		out:
			hscx->rcvidx = 0;
		}
	}
}

void hscx_irq(struct hscx *hscx_a)
{
	struct hscx *hscx_b = hscx_a + 1;
	unsigned char val;

	val = hscx_b->read_hscx(hscx_b, HSCX_ISTA);
	DBG(DBG_IRQ, "ISTA B %#x", val);

	if (val & HSCX_ISTA_EXB) {
		DBG(DBG_IRQ, "EXI B");
		hscx_exi_interrupt(hscx_b);
	}
	if (val & HSCX_ISTA_EXA) {
		DBG(DBG_IRQ, "EXI A");
		hscx_exi_interrupt(hscx_a);
	}
	if (val & 0xf8) {
		hscx_reg_interrupt(hscx_b, val);
	}
	if (val & HSCX_ISTA_ICA) {
		val = hscx_a->read_hscx(hscx_a, HSCX_ISTA);
		DBG(DBG_IRQ, "ISTA A %#x", val);
		hscx_reg_interrupt(hscx_a, val);
		hscx_a->write_hscx(hscx_a, HSCX_MASK, 0xff);
		hscx_a->write_hscx(hscx_a, HSCX_MASK, 0x00);
	}
	hscx_b->write_hscx(hscx_b, HSCX_MASK, 0xff);
	hscx_b->write_hscx(hscx_b, HSCX_MASK, 0x00);
}

static void modehscx(struct hscx *hscx, int mode)
{
	int bc = hscx->channel;

	DBG(0x40, "hscx %c mode %d --> %d",
	    'A' + hscx->channel, hscx->mode, mode);

	hscx->mode = mode;
	hscx->write_hscx(hscx, HSCX_XAD1, 0xFF);
	hscx->write_hscx(hscx, HSCX_XAD2, 0xFF);
	hscx->write_hscx(hscx, HSCX_RAH2, 0xFF);
	hscx->write_hscx(hscx, HSCX_XBCH, 0x0);
	hscx->write_hscx(hscx, HSCX_RLCR, 0x0);
	hscx->write_hscx(hscx, HSCX_CCR1, 
			 test_bit(HSCX_IPAC, &hscx->flags) ? 0x82 : 0x85);
	hscx->write_hscx(hscx, HSCX_CCR2, 0x30);
	hscx->write_hscx(hscx, HSCX_XCCR, 7);
	hscx->write_hscx(hscx, HSCX_RCCR, 7);

	/* Switch IOM 1 SSI */
	if (test_bit(HSCX_IOM1, &hscx->flags))
		bc = 1;

	hscx->write_hscx(hscx, HSCX_TSAX, hscx->tsaxr);
	hscx->write_hscx(hscx, HSCX_TSAR, hscx->tsaxr);

	switch (mode) {
		case (L1_MODE_NULL):
			hscx->write_hscx(hscx, HSCX_TSAX, 0x1f);
			hscx->write_hscx(hscx, HSCX_TSAR, 0x1f);
			hscx->write_hscx(hscx, HSCX_MODE, 0x84);
			break;
		case (L1_MODE_TRANS):
			hscx->write_hscx(hscx, HSCX_MODE, 0xe4);
			break;
		case (L1_MODE_HDLC):
			hscx->write_hscx(hscx, HSCX_CCR1, test_bit(HSCX_IPAC, &hscx->flags) ? 0x8a : 0x8d);
			hscx->write_hscx(hscx, HSCX_MODE, 0x8c);
			break;
	}
	if (mode)
		hscx->write_hscx(hscx, HSCX_CMDR, 0x41);

	hscx->write_hscx(hscx, HSCX_ISTA, 0x00);
}

void hscx_init(struct hscx *hscx)
{
	if (hscx->channel)
		hscx->tsaxr = 0x03;
	else
		hscx->tsaxr = 0x2f;
}

void hscx_setup(struct hscx *hscx)
{
	hscx_version(hscx);
	hscx->mode = -1;
	modehscx(hscx, L1_MODE_NULL);
}

void hscx_b_l2l1(struct hisax_if *ifc, int pr, void *arg)
{
	struct hscx *hscx = ifc->priv;
	struct sk_buff *skb = arg;
	int mode;

	DBG(0x10, "pr %#x", pr);

	switch (pr) {
	case PH_DATA | REQUEST:
		if (hscx->tx_skb)
			BUG();
		
		hscx->tx_skb = skb;
		DBG_SKB(1, skb);
		hscx_fill_fifo(hscx);
		break;
	case PH_ACTIVATE | REQUEST:
		mode = (int) arg;
		DBG(4,"B%d,PH_ACTIVATE_REQUEST %d", hscx->channel + 1, mode);
		modehscx(hscx, mode);
		B_L1L2(hscx, PH_ACTIVATE | INDICATION, NULL);
		break;
	case PH_DEACTIVATE | REQUEST:
		DBG(4,"B%d,PH_DEACTIVATE_REQUEST", hscx->channel + 1);
		modehscx(hscx, L1_MODE_NULL);
		B_L1L2(hscx, PH_DEACTIVATE | INDICATION, NULL);
		break;
	}
}

static int __init hisax_hscx_init(void)
{
	printk(KERN_INFO "hisax_hscx: HSCX ISDN driver v0.1.0\n");
	return 0;
}

static void __exit hisax_hscx_exit(void)
{
}

EXPORT_SYMBOL(hscx_init);
EXPORT_SYMBOL(hscx_b_l2l1);

EXPORT_SYMBOL(hscx_setup);
EXPORT_SYMBOL(hscx_irq);

module_init(hisax_hscx_init);
module_exit(hisax_hscx_exit);
