/* $Id: avm_pci.c,v 1.22.6.6 2001/09/23 22:24:46 kai Exp $
 *
 * low level stuff for AVM Fritz!PCI and ISA PnP isdn cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to AVM, Berlin for information
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/isapnp.h>
#include <linux/interrupt.h>

extern const char *CardType[];
static const char *avm_pci_rev = "$Revision: 1.22.6.6 $";
static spinlock_t avm_pci_lock = SPIN_LOCK_UNLOCKED;

#define  AVM_FRITZ_PCI		1
#define  AVM_FRITZ_PNP		2

#define  HDLC_FIFO		0x0
#define  HDLC_STATUS		0x4

#define	 AVM_HDLC_1		0x00
#define	 AVM_HDLC_2		0x01
#define	 AVM_ISAC_FIFO		0x02
#define	 AVM_ISAC_REG_LOW	0x04
#define	 AVM_ISAC_REG_HIGH	0x06

#define  AVM_STATUS0_IRQ_ISAC	0x01
#define  AVM_STATUS0_IRQ_HDLC	0x02
#define  AVM_STATUS0_IRQ_TIMER	0x04
#define  AVM_STATUS0_IRQ_MASK	0x07

#define  AVM_STATUS0_RESET	0x01
#define  AVM_STATUS0_DIS_TIMER	0x02
#define  AVM_STATUS0_RES_TIMER	0x04
#define  AVM_STATUS0_ENA_IRQ	0x08
#define  AVM_STATUS0_TESTBIT	0x10

#define  AVM_STATUS1_INT_SEL	0x0f
#define  AVM_STATUS1_ENA_IOM	0x80

#define  HDLC_MODE_ITF_FLG	0x01
#define  HDLC_MODE_TRANS	0x02
#define  HDLC_MODE_CCR_7	0x04
#define  HDLC_MODE_CCR_16	0x08
#define  HDLC_MODE_TESTLOOP	0x80

#define  HDLC_INT_XPR		0x80
#define  HDLC_INT_XDU		0x40
#define  HDLC_INT_RPR		0x20
#define  HDLC_INT_MASK		0xE0

#define  HDLC_STAT_RME		0x01
#define  HDLC_STAT_RDO		0x10
#define  HDLC_STAT_CRCVFRRAB	0x0E
#define  HDLC_STAT_CRCVFR	0x06
#define  HDLC_STAT_RML_MASK	0x3f00

#define  HDLC_CMD_XRS		0x80
#define  HDLC_CMD_XME		0x01
#define  HDLC_CMD_RRS		0x20
#define  HDLC_CMD_XML_MASK	0x3f00


/* Interface functions */

static u8
ReadISAC(struct IsdnCardState *cs, u8 offset)
{
	u8 idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	u8 val;
	unsigned long flags;

	spin_lock_irqsave(&avm_pci_lock, flags);
	outb(idx, cs->hw.avm.cfg_reg + 4);
	val = inb(cs->hw.avm.isac + (offset & 0xf));
	spin_unlock_irqrestore(&avm_pci_lock, flags);
	return (val);
}

static void
WriteISAC(struct IsdnCardState *cs, u8 offset, u8 value)
{
	u8 idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	unsigned long flags;

	spin_lock_irqsave(&avm_pci_lock, flags);
	outb(idx, cs->hw.avm.cfg_reg + 4);
	outb(value, cs->hw.avm.isac + (offset & 0xf));
	spin_unlock_irqrestore(&avm_pci_lock, flags);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u8 * data, int size)
{
	outb(AVM_ISAC_FIFO, cs->hw.avm.cfg_reg + 4);
	insb(cs->hw.avm.isac, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u8 * data, int size)
{
	outb(AVM_ISAC_FIFO, cs->hw.avm.cfg_reg + 4);
	outsb(cs->hw.avm.isac, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = ReadISAC,
	.write_reg  = WriteISAC,
	.read_fifo  = ReadISACfifo,
	.write_fifo = WriteISACfifo,
};

static inline u_int
ReadHDLCPCI(struct IsdnCardState *cs, int chan, u8 offset)
{
	u_int idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	u_int val;
	unsigned long flags;

	spin_lock_irqsave(&avm_pci_lock, flags);
	outl(idx, cs->hw.avm.cfg_reg + 4);
	val = inl(cs->hw.avm.isac + offset);
	spin_unlock_irqrestore(&avm_pci_lock, flags);
	return (val);
}

static inline void
WriteHDLCPCI(struct IsdnCardState *cs, int chan, u8 offset, u_int value)
{
	u_int idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	unsigned long flags;

	spin_lock_irqsave(&avm_pci_lock, flags);
	outl(idx, cs->hw.avm.cfg_reg + 4);
	outl(value, cs->hw.avm.isac + offset);
	spin_unlock_irqrestore(&avm_pci_lock, flags);
}

static inline u8
ReadHDLCPnP(struct IsdnCardState *cs, int chan, u8 offset)
{
	u8 idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	u8 val;
	unsigned long flags;

	spin_lock_irqsave(&avm_pci_lock, flags);
	outb(idx, cs->hw.avm.cfg_reg + 4);
	val = inb(cs->hw.avm.isac + offset);
	spin_unlock_irqrestore(&avm_pci_lock, flags);
	return (val);
}

static inline void
WriteHDLCPnP(struct IsdnCardState *cs, int chan, u8 offset, u8 value)
{
	u8 idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	unsigned long flags;

	spin_lock_irqsave(&avm_pci_lock, flags);
	outb(idx, cs->hw.avm.cfg_reg + 4);
	outb(value, cs->hw.avm.isac + offset);
	spin_unlock_irqrestore(&avm_pci_lock, flags);
}

static void
hdlc_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int len)
{
	u8 idx = hscx ? AVM_HDLC_2 : AVM_HDLC_1;
	int i;

	if (cs->subtyp == AVM_FRITZ_PCI) {
		u32 *ptr = (u32 *) data;

		outl(idx, cs->hw.avm.cfg_reg + 4);
		for (i = 0; i < len; i += 4) {
#ifdef __powerpc__
#ifdef CONFIG_APUS
			*ptr++ = in_le32((u32 *)(cs->hw.avm.isac +_IO_BASE));
#else
			*ptr++ = in_be32((u32 *)(cs->hw.avm.isac +_IO_BASE));
#endif /* CONFIG_APUS */
#else
			*ptr++ = inl(cs->hw.avm.isac);
#endif /* __powerpc__ */
		}
	} else {
		outb(idx, cs->hw.avm.cfg_reg + 4);
		for (i = 0; i < len; i++) {
			*data++ = inb(cs->hw.avm.isac);
		}
	}
}

static struct bc_hw_ops hdlc_hw_ops = {
	.read_fifo  = hdlc_read_fifo,
};

static inline
struct BCState *Sel_BCS(struct IsdnCardState *cs, int channel)
{
	if (cs->bcs[0].mode && (cs->bcs[0].channel == channel))
		return(&cs->bcs[0]);
	else if (cs->bcs[1].mode && (cs->bcs[1].channel == channel))
		return(&cs->bcs[1]);
	else
		return(NULL);
}

void
write_ctrl(struct BCState *bcs, int which) {

	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs, "hdlc %c wr%x ctrl %x",
			'A' + bcs->channel, which, bcs->hw.hdlc.ctrl.ctrl);
	if (bcs->cs->subtyp == AVM_FRITZ_PCI) {
		WriteHDLCPCI(bcs->cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl.ctrl);
	} else {
		if (which & 4)
			WriteHDLCPnP(bcs->cs, bcs->channel, HDLC_STATUS + 2,
				bcs->hw.hdlc.ctrl.sr.mode);
		if (which & 2)
			WriteHDLCPnP(bcs->cs, bcs->channel, HDLC_STATUS + 1,
				bcs->hw.hdlc.ctrl.sr.xml);
		if (which & 1)
			WriteHDLCPnP(bcs->cs, bcs->channel, HDLC_STATUS,
				bcs->hw.hdlc.ctrl.sr.cmd);
	}
}

void
modehdlc(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int hdlc = bcs->channel;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hdlc %c mode %d --> %d ichan %d --> %d",
			'A' + hdlc, bcs->mode, mode, hdlc, bc);
	bcs->hw.hdlc.ctrl.ctrl = 0;
	switch (mode) {
		case (-1): /* used for init */
			bcs->mode = 1;
			bcs->channel = bc;
			bc = 0;
		case (L1_MODE_NULL):
			if (bcs->mode == L1_MODE_NULL)
				return;
			bcs->hw.hdlc.ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			bcs->hw.hdlc.ctrl.sr.mode = HDLC_MODE_TRANS;
			write_ctrl(bcs, 5);
			bcs->mode = L1_MODE_NULL;
			bcs->channel = bc;
			break;
		case (L1_MODE_TRANS):
			bcs->mode = mode;
			bcs->channel = bc;
			bcs->hw.hdlc.ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			bcs->hw.hdlc.ctrl.sr.mode = HDLC_MODE_TRANS;
			write_ctrl(bcs, 5);
			bcs->hw.hdlc.ctrl.sr.cmd = HDLC_CMD_XRS;
			write_ctrl(bcs, 1);
			bcs->hw.hdlc.ctrl.sr.cmd = 0;
			sched_b_event(bcs, B_XMTBUFREADY);
			break;
		case (L1_MODE_HDLC):
			bcs->mode = mode;
			bcs->channel = bc;
			bcs->hw.hdlc.ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			bcs->hw.hdlc.ctrl.sr.mode = HDLC_MODE_ITF_FLG;
			write_ctrl(bcs, 5);
			bcs->hw.hdlc.ctrl.sr.cmd = HDLC_CMD_XRS;
			write_ctrl(bcs, 1);
			bcs->hw.hdlc.ctrl.sr.cmd = 0;
			sched_b_event(bcs, B_XMTBUFREADY);
			break;
	}
}

static inline void
hdlc_empty_fifo(struct BCState *bcs, int count)
{
	recv_empty_fifo_b(bcs, count);
}

static void
hdlc_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int count, more, cnt =0;
	int fifo_size = 32;
	unsigned char *p;
	unsigned int *ptr;

	p = xmit_fill_fifo_b(bcs, fifo_size, &count, &more);
	if (!p)
		return;

	if (more)
		bcs->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_XME;
	else
		bcs->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_XME;

	bcs->hw.hdlc.ctrl.sr.xml = ((count == fifo_size) ? 0 : count);
	write_ctrl(bcs, 3);  /* sets the correct index too */
	if (cs->subtyp == AVM_FRITZ_PCI) {
		ptr = (unsigned int *) p;
		while (cnt<count) {
#ifdef __powerpc__
#ifdef CONFIG_APUS
			out_le32((unsigned *)(cs->hw.avm.isac +_IO_BASE), *ptr++);
#else
			out_be32((unsigned *)(cs->hw.avm.isac +_IO_BASE), *ptr++);
#endif /* CONFIG_APUS */
#else
			outl(*ptr++, cs->hw.avm.isac);
#endif /* __powerpc__ */
			cnt += 4;
		}
	} else {
		while (cnt<count) {
			outb(*p++, cs->hw.avm.isac);
			cnt++;
		}
	}
}

static void
reset_xmit(struct BCState *bcs)
{
	bcs->hw.hdlc.ctrl.sr.xml = 0;
	bcs->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_XRS;
	write_ctrl(bcs, 1);
	bcs->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_XRS;
	write_ctrl(bcs, 1);
	hdlc_fill_fifo(bcs);
}

static inline void
HDLC_irq(struct BCState *bcs, u_int stat)
{
	int len;

	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs, "ch%d stat %#x", bcs->channel, stat);

	if (stat & HDLC_INT_RPR) {
		if (stat & HDLC_STAT_RDO) {
			if (bcs->cs->debug & L1_DEB_HSCX)
				debugl1(bcs->cs, "RDO");
			else
				debugl1(bcs->cs, "ch%d stat %#x", bcs->channel, stat);
			bcs->hw.hdlc.ctrl.sr.xml = 0;
			bcs->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_RRS;
			write_ctrl(bcs, 1);
			bcs->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_RRS;
			write_ctrl(bcs, 1);
			bcs->rcvidx = 0;
		} else {
			if (!(len = (stat & HDLC_STAT_RML_MASK)>>8))
				len = 32;
			hdlc_empty_fifo(bcs, len);
			if ((stat & HDLC_STAT_RME) || (bcs->mode == L1_MODE_TRANS)) {
				if (((stat & HDLC_STAT_CRCVFRRAB)==HDLC_STAT_CRCVFR) ||
					(bcs->mode == L1_MODE_TRANS)) {
					recv_rme_b(bcs);
				} else {
					if (bcs->cs->debug & L1_DEB_HSCX)
						debugl1(bcs->cs, "invalid frame");
					else
						debugl1(bcs->cs, "ch%d invalid frame %#x", bcs->channel, stat);
					bcs->rcvidx = 0;
				}
			}
		}
	}
	if (stat & HDLC_INT_XDU) {
		xmit_xdu_b(bcs, reset_xmit);
	} else if (stat & HDLC_INT_XPR) {
		xmit_xpr_b(bcs);
	}
}

inline void
HDLC_irq_main(struct IsdnCardState *cs)
{
	u_int stat;
	struct BCState *bcs;

	spin_lock(&cs->lock);
	if (cs->subtyp == AVM_FRITZ_PCI) {
		stat = ReadHDLCPCI(cs, 0, HDLC_STATUS);
	} else {
		stat = ReadHDLCPnP(cs, 0, HDLC_STATUS);
		if (stat & HDLC_INT_RPR)
			stat |= (ReadHDLCPnP(cs, 0, HDLC_STATUS+1))<<8;
	}
	if (stat & HDLC_INT_MASK) {
		if (!(bcs = Sel_BCS(cs, 0))) {
			if (cs->debug)
				debugl1(cs, "hdlc spurious channel 0 IRQ");
		} else
			HDLC_irq(bcs, stat);
	}
	if (cs->subtyp == AVM_FRITZ_PCI) {
		stat = ReadHDLCPCI(cs, 1, HDLC_STATUS);
	} else {
		stat = ReadHDLCPnP(cs, 1, HDLC_STATUS);
		if (stat & HDLC_INT_RPR)
			stat |= (ReadHDLCPnP(cs, 1, HDLC_STATUS+1))<<8;
	}
	if (stat & HDLC_INT_MASK) {
		if (!(bcs = Sel_BCS(cs, 1))) {
			if (cs->debug)
				debugl1(cs, "hdlc spurious channel 1 IRQ");
		} else
			HDLC_irq(bcs, stat);
	}
	spin_unlock(&cs->lock);
}

void
hdlc_l2l1(struct PStack *st, int pr, void *arg)
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
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			modehdlc(st->l1.bcs, st->l1.mode, st->l1.bc);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			modehdlc(st->l1.bcs, 0, st->l1.bc);
			L1L2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

void
close_hdlcstate(struct BCState *bcs)
{
	modehdlc(bcs, 0, 0);
	bc_close(bcs);
}

int
open_hdlcstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	return bc_open(bcs);
}

int
setstack_hdlc(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	bcs->unit = bcs->channel;
	if (open_hdlcstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l1.l2l1 = hdlc_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

static struct bc_l1_ops hdlc_l1_ops = {
	.fill_fifo = hdlc_fill_fifo,
	.open      = setstack_hdlc,
	.close     = close_hdlcstate,
};

static void __init
inithdlc(struct IsdnCardState *cs)
{
	u_int val;

	if (cs->subtyp == AVM_FRITZ_PCI) {
		val = ReadHDLCPCI(cs, 0, HDLC_STATUS);
		debugl1(cs, "HDLC 1 STA %x", val);
		val = ReadHDLCPCI(cs, 1, HDLC_STATUS);
		debugl1(cs, "HDLC 2 STA %x", val);
	} else {
		val = ReadHDLCPnP(cs, 0, HDLC_STATUS);
		debugl1(cs, "HDLC 1 STA %x", val);
		val = ReadHDLCPnP(cs, 0, HDLC_STATUS + 1);
		debugl1(cs, "HDLC 1 RML %x", val);
		val = ReadHDLCPnP(cs, 0, HDLC_STATUS + 2);
		debugl1(cs, "HDLC 1 MODE %x", val);
		val = ReadHDLCPnP(cs, 0, HDLC_STATUS + 3);
		debugl1(cs, "HDLC 1 VIN %x", val);
		val = ReadHDLCPnP(cs, 1, HDLC_STATUS);
		debugl1(cs, "HDLC 2 STA %x", val);
		val = ReadHDLCPnP(cs, 1, HDLC_STATUS + 1);
		debugl1(cs, "HDLC 2 RML %x", val);
		val = ReadHDLCPnP(cs, 1, HDLC_STATUS + 2);
		debugl1(cs, "HDLC 2 MODE %x", val);
		val = ReadHDLCPnP(cs, 1, HDLC_STATUS + 3);
		debugl1(cs, "HDLC 2 VIN %x", val);
	}

	modehdlc(cs->bcs, -1, 0);
	modehdlc(cs->bcs + 1, -1, 1);
}

static irqreturn_t
avm_pcipnp_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;
	u8 sval;

	sval = inb(cs->hw.avm.cfg_reg + 2);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK)
		/* possible a shared  IRQ reqest */
		return IRQ_NONE;
	if (!(sval & AVM_STATUS0_IRQ_ISAC)) {
		val = ReadISAC(cs, ISAC_ISTA);
		isac_interrupt(cs, val);
	}
	if (!(sval & AVM_STATUS0_IRQ_HDLC)) {
		HDLC_irq_main(cs);
	}
	WriteISAC(cs, ISAC_MASK, 0xFF);
	WriteISAC(cs, ISAC_MASK, 0x0);
	return IRQ_HANDLED;
}

static int
avm_pcipnp_reset(struct IsdnCardState *cs)
{
	printk(KERN_INFO "AVM PCI/PnP: reset\n");
	outb(AVM_STATUS0_RESET | AVM_STATUS0_DIS_TIMER, cs->hw.avm.cfg_reg + 2);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
	outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER | AVM_STATUS0_ENA_IRQ, cs->hw.avm.cfg_reg + 2);
	outb(AVM_STATUS1_ENA_IOM | cs->irq, cs->hw.avm.cfg_reg + 3);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
	printk(KERN_INFO "AVM PCI/PnP: S1 %x\n", inb(cs->hw.avm.cfg_reg + 3));
	return 0;
}

static void
avm_pcipnp_init(struct IsdnCardState *cs)
{
	initisac(cs);
	inithdlc(cs);
	outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER,
	     cs->hw.avm.cfg_reg + 2);
	outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER |
	     AVM_STATUS0_ENA_IRQ, cs->hw.avm.cfg_reg + 2);
}

static void
avm_pcipnp_release(struct IsdnCardState *cs)
{
	outb(0, cs->hw.avm.cfg_reg + 2);
	hisax_release_resources(cs);
}

static struct card_ops avm_pci_ops = {
	.init     = avm_pcipnp_init,
	.reset    = avm_pcipnp_reset,
	.release  = avm_pcipnp_release,
	.irq_func = avm_pcipnp_interrupt,
};

static int __init
avm_pcipnp_hw_init(struct IsdnCardState *cs)
{
	cs->bc_hw_ops = &hdlc_hw_ops;
	cs->bc_l1_ops = &hdlc_l1_ops;
	cs->card_ops = &avm_pci_ops;
	avm_pcipnp_reset(cs);
	return isac_setup(cs, &isac_ops);
}

static int __init
avm_pci_probe(struct IsdnCardState *cs, struct pci_dev *pdev)
{
	int rc;
	u32 val;

	printk(KERN_INFO "AVM PCI: defined at %#lx IRQ %u\n",
	       pci_resource_start(pdev, 1), pdev->irq);
	
	rc = -EBUSY;
	if (pci_enable_device(pdev))
		goto err;
			
	cs->subtyp = AVM_FRITZ_PCI;
	cs->irq = pdev->irq;
	cs->irq_flags |= SA_SHIRQ;
	cs->hw.avm.cfg_reg = pci_resource_start(pdev, 1);
	cs->hw.avm.isac = cs->hw.avm.cfg_reg + 0x10;
	if (!request_io(&cs->rs, cs->hw.avm.cfg_reg, 32, "avm PCI"))
		goto err;

	val = inl(cs->hw.avm.cfg_reg);
	printk(KERN_INFO "AVM PCI: stat %#x\n", val);
	printk(KERN_INFO "AVM PCI: Class %X Rev %d\n",
	       val & 0xff, (val>>8) & 0xff);

	if (avm_pcipnp_hw_init(cs))
		goto err;

	return 0;
 err:
	hisax_release_resources(cs);
	return rc;
}

static int __init
avm_pnp_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	int rc;
	u8 val, ver;

	printk(KERN_INFO "AVM PnP: defined at %#lx IRQ %lu\n",
	       card->para[1], card->para[0]);

	cs->subtyp = AVM_FRITZ_PNP;
	cs->irq = card->para[0];
	cs->hw.avm.cfg_reg = card->para[1];
	cs->hw.avm.isac = cs->hw.avm.cfg_reg + 0x10;

	rc = -EBUSY;
	if (!request_io(&cs->rs, cs->hw.avm.cfg_reg, 32, "avm PnP"))
		goto err;
	
	val = inb(cs->hw.avm.cfg_reg);
	ver = inb(cs->hw.avm.cfg_reg + 1);
	printk(KERN_INFO "AVM PnP: Class %X Rev %d\n", val, ver);

	if (avm_pcipnp_hw_init(cs))
		goto err;

	return 0;
 err:
	hisax_release_resources(cs);
	return rc;
}

static struct pci_dev *dev_avm __initdata = NULL;
#ifdef __ISAPNP__
static struct pnp_card *card_avm __initdata = NULL;
static struct pnp_dev *pnp_avm __initdata = NULL;
#endif

int __init
setup_avm_pcipnp(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, avm_pci_rev);
	printk(KERN_INFO "HiSax: AVM PCI driver Rev. %s\n", HiSax_getrev(tmp));
	if (card->para[1]) {
		/* old manual method */
		if (avm_pnp_probe(card->cs, card))
			return 0;
		return 1;
	} else {
#ifdef __ISAPNP__
		if (isapnp_present()) {
			struct pnp_card *ba;
			if ((ba = pnp_find_card(
				ISAPNP_VENDOR('A', 'V', 'M'),
				ISAPNP_FUNCTION(0x0900), card_avm))) {
				card_avm = ba;
				pnp_avm = NULL;
				if ((pnp_avm = pnp_find_dev(card_avm,
					ISAPNP_VENDOR('A', 'V', 'M'),
					ISAPNP_FUNCTION(0x0900), pnp_avm))) {
					if (pnp_device_attach(pnp_avm) < 0) {
						printk(KERN_ERR "FritzPnP: attach failed\n");
						return 0;
					}
					if (pnp_activate_dev(pnp_avm) < 0) {
						printk(KERN_ERR "FritzPnP: activate failed\n");
						pnp_device_detach(pnp_avm);
						return 0;
					}
					if (!pnp_irq_valid(pnp_avm, 0)) {
						printk(KERN_ERR "FritzPnP:No IRQ\n");
						pnp_device_detach(pnp_avm);
						return(0);
					}
					if (!pnp_port_valid(pnp_avm, 0)) {
						printk(KERN_ERR "FritzPnP:No IO address\n");
						pnp_device_detach(pnp_avm);
						return(0);
					}
					card->para[1] = pnp_port_start(pnp_avm, 0);
					card->para[0] = pnp_irq(pnp_avm, 0);
					if (avm_pnp_probe(card->cs, card))
						return 0;
					return 1;
				}
			}
		}
#endif
#ifdef CONFIG_PCI
		if ((dev_avm = pci_find_device(PCI_VENDOR_ID_AVM,
			PCI_DEVICE_ID_AVM_A1,  dev_avm))) {
			if (avm_pci_probe(card->cs, dev_avm))
				return 0;
			return 1;
		}
#endif /* CONFIG_PCI */
	}
	printk(KERN_WARNING "FritzPCI: No card found\n");
	return 0;
}
