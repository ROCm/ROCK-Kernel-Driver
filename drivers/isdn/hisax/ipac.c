#include "hisax.h"
#include "isdnl1.h"
#include "ipac.h"
#include "hscx.h"
#include "isac.h"

static inline u8
ipac_dc_read(struct IsdnCardState *cs, u8 addr)
{
	return cs->dc_hw_ops->read_reg(cs, addr);
}

static inline void
ipac_dc_write(struct IsdnCardState *cs, u8 addr, u8 val)
{
	cs->dc_hw_ops->write_reg(cs, addr, val);
}

static inline u8
ipac_bc_read(struct IsdnCardState *cs, int hscx, u8 addr)
{
	return cs->bc_hw_ops->read_reg(cs, hscx, addr);
}

static inline void
ipac_bc_write(struct IsdnCardState *cs, int hscx, u8 addr, u8 val)
{
	cs->bc_hw_ops->write_reg(cs, hscx, addr, val);
}

static inline u8
ipac_read(struct IsdnCardState *cs, u8 offset)
{
	return ipac_dc_read(cs, offset - 0x80);
}

static inline void
ipac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	ipac_dc_write(cs, offset - 0x80, value);
}

void
ipac_init(struct IsdnCardState *cs)
{
	set_bit(HW_IPAC, &cs->HW_Flags);
	inithscxisac(cs);
}

irqreturn_t
ipac_irq(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 ista, val, icnt = 5;

	spin_lock(&cs->lock);
	ista = ipac_read(cs, IPAC_ISTA);
Start_IPAC:
	if (cs->debug & L1_DEB_IPAC)
		debugl1(cs, "IPAC ISTA %02X", ista);
	if (ista & 0x0f) {
		val = ipac_bc_read(cs, 1, HSCX_ISTA);
		if (ista & 0x01)
			val |= 0x01;
		if (ista & 0x04)
			val |= 0x02;
		if (ista & 0x08)
			val |= 0x04;
		if (val)
			hscx_int_main(cs, val);
	}
	if (ista & 0x20) {
		val = ipac_dc_read(cs, ISAC_ISTA) & 0xfe;
		if (val) {
			isac_interrupt(cs, val);
		}
	}
	if (ista & 0x10) {
		val = 0x01;
		isac_interrupt(cs, val);
	}
	ista  = ipac_read(cs, IPAC_ISTA);
	if ((ista & 0x3f) && icnt) {
		icnt--;
		goto Start_IPAC;
	}
	if (!icnt)
		printk(KERN_WARNING "IRQ LOOP\n");

	ipac_write(cs, IPAC_MASK, 0xFF);
	ipac_write(cs, IPAC_MASK, 0xC0);
	spin_unlock(&cs->lock);
	return IRQ_HANDLED;
}

int
ipac_setup(struct IsdnCardState *cs, struct dc_hw_ops *ipac_dc_ops,
	   struct bc_hw_ops *ipac_bc_ops)
{
	u8 val;

	cs->dc_hw_ops = ipac_dc_ops;
	cs->bc_hw_ops = ipac_bc_ops;
	val = ipac_read(cs, IPAC_ID);
	printk(KERN_INFO "HiSax: IPAC version %#x\n", val);
	return 0;
}
