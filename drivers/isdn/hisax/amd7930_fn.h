/* 2001/10/02
 *
 * gerdes_amd7930.h     Header-file included by
 *                      gerdes_amd7930.c
 *
 * Author               Christoph Ersfeld <info@formula-n.de>
 *                      Formula-n Europe AG (www.formula-n.com)
 *                      previously Gerdes AG
 *
 *
 *                      This file is (c) under GNU PUBLIC LICENSE
 */


#define AMD_CR		0x00
#define AMD_DR		0x01


#define DBUSY_TIMER_VALUE 80

void Amd7930_interrupt(struct IsdnCardState *cs, unsigned char irflags);
void Amd7930_init(struct IsdnCardState *cs);
int  amd7930_setup(struct IsdnCardState *cs, struct dc_hw_ops *amd7930_ops,
		   void (*set_irq_mask)(struct IsdnCardState *, u8 val));
