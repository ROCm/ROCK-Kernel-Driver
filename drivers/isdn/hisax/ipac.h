/* $Id: ipac.h,v 1.5.6.2 2001/09/23 22:24:49 kai Exp $
 *
 * IPAC specific defines
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

#define IPAC_CONF	0xC0
#define IPAC_MASK	0xC1
#define IPAC_ISTA	0xC1
#define IPAC_ID		0xC2
#define IPAC_ACFG	0xC3
#define IPAC_AOE	0xC4
#define IPAC_ARX	0xC5
#define IPAC_ATX	0xC5
#define IPAC_PITA1	0xC6
#define IPAC_PITA2	0xC7
#define IPAC_POTA1	0xC8
#define IPAC_POTA2	0xC9
#define IPAC_PCFG	0xCA
#define IPAC_SCFG	0xCB
#define IPAC_TIMR2	0xCC

void ipac_init(struct IsdnCardState *cs);
irqreturn_t ipac_irq(int intno, void *dev_id, struct pt_regs *regs);
int  ipac_setup(struct IsdnCardState *cs, struct dc_hw_ops *ipac_dc_ops,
		struct bc_hw_ops *ipac_bc_ops);

/* Macro to build the needed D- and B-Channel access routines given
 * access functions for the IPAC */

#define BUILD_IPAC_OPS(ipac)                                                  \
                                                                              \
static u8                                                                     \
ipac ## _dc_read(struct IsdnCardState *cs, u8 offset)                         \
{                                                                             \
	return ipac ## _read(cs, offset+0x80);                                \
}                                                                             \
                                                                              \
static void                                                                   \
ipac ## _dc_write(struct IsdnCardState *cs, u8 offset, u8 value)              \
{                                                                             \
	ipac ## _write(cs, offset+0x80, value);                               \
}                                                                             \
                                                                              \
static void                                                                   \
ipac ## _dc_read_fifo(struct IsdnCardState *cs, u8 * data, int size)          \
{                                                                             \
	ipac ## _readfifo(cs, 0x80, data, size);                              \
}                                                                             \
                                                                              \
static void                                                                   \
ipac ## _dc_write_fifo(struct IsdnCardState *cs, u8 * data, int size)         \
{                                                                             \
	ipac ## _writefifo(cs, 0x80, data, size);                             \
}                                                                             \
                                                                              \
static struct dc_hw_ops ipac ## _dc_ops = {                                   \
	.read_reg   = ipac ## _dc_read,                                       \
	.write_reg  = ipac ## _dc_write,                                      \
	.read_fifo  = ipac ## _dc_read_fifo,                                  \
	.write_fifo = ipac ## _dc_write_fifo,                                 \
};                                                                            \
                                                                              \
static u8                                                                     \
ipac ## _bc_read(struct IsdnCardState *cs, int hscx, u8 offset)               \
{                                                                             \
	return ipac ## _read(cs, offset + (hscx ? 0x40 : 0));                 \
}                                                                             \
                                                                              \
static void                                                                   \
ipac ## _bc_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)    \
{                                                                             \
	ipac ## _write(cs, offset + (hscx ? 0x40 : 0), value);                \
}                                                                             \
                                                                              \
static void                                                                   \
ipac ## _bc_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size) \
{                                                                             \
	ipac ## _readfifo(cs, hscx ? 0x40 : 0, data, size);                   \
}                                                                             \
                                                                              \
static void                                                                   \
ipac ## _bc_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)\
{                                                                             \
	ipac ## _writefifo(cs, hscx ? 0x40 : 0, data, size);                  \
}                                                                             \
                                                                              \
static struct bc_hw_ops ipac ## _bc_ops = {                                   \
	.read_reg   = ipac ## _bc_read,                                       \
	.write_reg  = ipac ## _bc_write,                                      \
	.read_fifo  = ipac ## _bc_read_fifo,                                  \
	.write_fifo = ipac ## _bc_write_fifo,                                 \
}

