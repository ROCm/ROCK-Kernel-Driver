/* $Id: hscx_irq.c,v 1.16.6.2 2001/09/23 22:24:48 kai Exp $
 *
 * low level b-channel stuff for Siemens HSCX
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * This is an include file for fast inline IRQ stuff
 *
 */

static void
waitforCEC(struct BCState *bcs)
{
	int to = 50;

	while ((hscx_read(bcs, HSCX_STAR) & 0x04) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforCEC timeout\n");
}


static void
waitforXFW(struct BCState *bcs)
{
	int to = 50;

	while ((!(hscx_read(bcs, HSCX_STAR) & 0x44) == 0x40) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforXFW timeout\n");
}

static inline void
WriteHSCXCMDR(struct BCState *bcs, u8 data)
{
	waitforCEC(bcs);
	hscx_write(bcs, HSCX_CMDR, data);
}


static void
hscx_empty_fifo(struct BCState *bcs, int count)
{
	recv_empty_fifo_b(bcs, count);
	WriteHSCXCMDR(bcs, 0x80);
}

static void
hscx_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int more, count;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	u8 *p;

	p = xmit_fill_fifo_b(bcs, fifo_size, &count, &more);
	if (!p)
		return;

	waitforXFW(bcs);
	hscx_write_fifo(bcs, p, count);
	WriteHSCXCMDR(bcs, more ? 0x8 : 0xa);
}

static inline void
hscx_interrupt(struct IsdnCardState *cs, u8 val, u8 hscx)
{
	u8 r;
	struct BCState *bcs = cs->bcs + hscx;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	int count;

	if (!test_bit(BC_FLG_INIT, &bcs->Flag))
		return;

	if (val & 0x80) {	/* RME */
		r = hscx_read(bcs, HSCX_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!(r & 0x80)) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX invalid frame");
#ifdef ERROR_STATISTIC
				bcs->err_inv++;
#endif
			}
			if ((r & 0x40) && bcs->mode) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX RDO mode=%d",
						bcs->mode);
#ifdef ERROR_STATISTIC
				bcs->err_rdo++;
#endif
			}
			if (!(r & 0x20)) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX CRC error");
#ifdef ERROR_STATISTIC
				bcs->err_crc++;
#endif
			}
			WriteHSCXCMDR(bcs, 0x80);
			bcs->rcvidx = 0;
		} else {
			count = hscx_read(bcs, HSCX_RBCL) & (fifo_size-1);
			if (count == 0)
				count = fifo_size;

			hscx_empty_fifo(bcs, count);
			recv_rme_b(bcs);
		}
	}
	if (val & 0x40) {	/* RPF */
		hscx_empty_fifo(bcs, fifo_size);
		recv_rpf_b(bcs);
	}
	if (val & 0x10) {
		xmit_xpr_b(bcs);
	}
}

static void
reset_xmit(struct BCState *bcs)
{
	WriteHSCXCMDR(bcs, 0x01);
}

void
hscx_int_main(struct IsdnCardState *cs, u8 val)
{
	u8 exval;
	struct BCState *bcs;

	if (val & 0x01) {
		bcs = cs->bcs + 1;
		exval = hscx_read(bcs, HSCX_EXIR);
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX B EXIR %x", exval);
		if (exval & 0x40) {
			xmit_xdu_b(bcs, reset_xmit);
		}
	}
	if (val & 0xf8) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX B interrupt %x", val);
		hscx_interrupt(cs, val, 1);
	}
	if (val & 0x02) {
		bcs = cs->bcs;
		exval = hscx_read(bcs, HSCX_EXIR);
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX A EXIR %x", exval);
		if (exval & 0x40) {
			xmit_xdu_b(bcs, reset_xmit);
		}
	}
	if (val & 0x04) {
		bcs = cs->bcs;
		exval = hscx_read(bcs, HSCX_ISTA);
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX A interrupt %x", exval);
		hscx_interrupt(cs, exval, 0);
	}
}

/* ====================================================================== */

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

irqreturn_t
hscxisac_irq(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;
	int count = 0;

	spin_lock(&cs->lock);
	val = hscx_read(&cs->bcs[1], HSCX_ISTA);
      Start_HSCX:
	if (val)
		hscx_int_main(cs, val);
	val = isac_read(cs, ISAC_ISTA);
      Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	count++;
	val = hscx_read(&cs->bcs[1], HSCX_ISTA);
	if (val && count < 5) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = isac_read(cs, ISAC_ISTA);
	if (val && count < 5) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	hscx_write(&cs->bcs[0], HSCX_MASK, 0xFF);
	hscx_write(&cs->bcs[1], HSCX_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0x0);
	hscx_write(&cs->bcs[0], HSCX_MASK, 0x0);
	hscx_write(&cs->bcs[1], HSCX_MASK, 0x0);
	spin_unlock(&cs->lock);
	return IRQ_HANDLED;
}

