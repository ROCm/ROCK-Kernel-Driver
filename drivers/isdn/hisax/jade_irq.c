/* $Id: jade_irq.c,v 1.5.6.2 2001/09/23 22:24:49 kai Exp $
 *
 * Low level JADE IRQ stuff (derived from original hscx_irq.c)
 *
 * Author       Roland Klabunde
 * Copyright    by Roland Klabunde   <R.Klabunde@Berkom.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

static inline void
waitforCEC(struct IsdnCardState *cs, int jade, int reg)
{
  	int to = 50;
  	int mask = (reg == jade_HDLC_XCMD ? jadeSTAR_XCEC : jadeSTAR_RCEC);
  	while ((jade_read_reg(cs, jade, jade_HDLC_STAR) & mask) && to) {
  		udelay(1);
  		to--;
  	}
  	if (!to)
  		printk(KERN_WARNING "HiSax: waitforCEC (jade) timeout\n");
}


static inline void
waitforXFW(struct BCState *bcs)
{
}

static inline void
WriteJADECMDR(struct BCState *bcs, int reg, u8 data)
{
	int jade = bcs->unit;

	waitforCEC(bcs->cs, jade, reg);
	jade_write_reg(bcs->cs, jade, reg, data);
}



static void
jade_empty_fifo(struct BCState *bcs, int count)
{
	recv_empty_fifo_b(bcs, count);
	WriteJADECMDR(bcs, jade_HDLC_RCMD, jadeRCMD_RMC);
}

static void
jade_fill_fifo(struct BCState *bcs)
{
	int more, count;
	int fifo_size = 32;
	unsigned char *p;

	p = xmit_fill_fifo_b(bcs, fifo_size, &count, &more);
	if (!p)
		return;

	waitforXFW(bcs);
	jade_write_fifo(bcs, p, count);
	WriteJADECMDR(bcs, jade_HDLC_XCMD,
		      more ? jadeXCMD_XF : (jadeXCMD_XF|jadeXCMD_XME));
}


static inline void
jade_interrupt(struct IsdnCardState *cs, u8 val, u8 jade)
{
	u8 r;
	struct BCState *bcs = cs->bcs + jade;
	int fifo_size = 32;
	int count;
	int i_jade = (int) jade; /* To satisfy the compiler */
	
	if (!test_bit(BC_FLG_INIT, &bcs->Flag))
		return;

	if (val & 0x80) {	/* RME */
		r = jade_read_reg(cs, i_jade, jade_HDLC_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!(r & 0x80))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "JADE %s invalid frame", (jade ? "B":"A"));
			if ((r & 0x40) && bcs->mode)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "JADE %c RDO mode=%d", 'A'+jade, bcs->mode);
			if (!(r & 0x20))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "JADE %c CRC error", 'A'+jade);
			WriteJADECMDR(bcs, jade_HDLC_RCMD, jadeRCMD_RMC);
			bcs->rcvidx = 0;
		} else {
			count = jade_read_reg(cs, i_jade, jade_HDLC_RBCL) & 0x1F;
			if (count == 0)
				count = fifo_size;

			jade_empty_fifo(bcs, count);
			recv_rme_b(bcs);
		}
	}
	if (val & 0x40) {	/* RPF */
		jade_empty_fifo(bcs, fifo_size);
		recv_rpf_b(bcs);
	}
	if (val & 0x10) {	/* XPR */
		xmit_xpr_b(bcs);
	}
}

static void
reset_xmit(struct BCState *bcs)
{
	WriteJADECMDR(bcs, jade_HDLC_XCMD, jadeXCMD_XRES);
}

void
jade_int_main(struct IsdnCardState *cs, u8 val, int jade)
{
	struct BCState *bcs;
	bcs = cs->bcs + jade;
	
	if (val & jadeISR_RFO) {
		/* handled with RDO */
		val &= ~jadeISR_RFO;
	}
	if (val & jadeISR_XDU) {
		xmit_xdu_b(bcs, reset_xmit);
	}
	if (val & (jadeISR_RME|jadeISR_RPF|jadeISR_XPR)) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "JADE %c interrupt %x", 'A'+jade, val);
		jade_interrupt(cs, val, jade);
	}
}
