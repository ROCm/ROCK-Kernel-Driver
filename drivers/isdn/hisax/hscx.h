/* $Id: hscx.h,v 1.6.6.2 2001/09/23 22:24:48 kai Exp $
 *
 * HSCX specific defines
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/interrupt.h>

/* All Registers original Siemens Spec  */

#define HSCX_ISTA 0x20
#define HSCX_CCR1 0x2f
#define HSCX_CCR2 0x2c
#define HSCX_TSAR 0x31
#define HSCX_TSAX 0x30
#define HSCX_XCCR 0x32
#define HSCX_RCCR 0x33
#define HSCX_MODE 0x22
#define HSCX_CMDR 0x21
#define HSCX_EXIR 0x24
#define HSCX_XAD1 0x24
#define HSCX_XAD2 0x25
#define HSCX_RAH2 0x27
#define HSCX_RSTA 0x27
#define HSCX_TIMR 0x23
#define HSCX_STAR 0x21
#define HSCX_RBCL 0x25
#define HSCX_XBCH 0x2d
#define HSCX_VSTR 0x2e
#define HSCX_RLCR 0x2e
#define HSCX_MASK 0x20

extern void modehscx(struct BCState *bcs, int mode, int bc);
extern void inithscxisac(struct IsdnCardState *cs);
extern void hscx_int_main(struct IsdnCardState *cs, u8 val);
extern irqreturn_t hscxisac_irq(int intno, void *dev_id, struct pt_regs *regs);
extern int  hscxisac_setup(struct IsdnCardState *cs,
			   struct dc_hw_ops *isac_ops,
			   struct bc_hw_ops *hscx_ops);
