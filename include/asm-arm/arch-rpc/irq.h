/*
 *  linux/include/asm-arm/arch-rpc/irq.h
 *
 *  Copyright (C) 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   10-10-1996	RMK	Brought up to date with arch-sa110eval
 *   22-08-1998	RMK	Restructured IRQ routines
 */
#include <asm/hardware/iomd.h>

#define fixup_irq(x) (x)

static void rpc_mask_irq_ack_a(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2]\n"
"	strb	%1, [%3]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOMD_IRQMASKA)),
	  "r" (ioaddr(IOMD_IRQCLRA)));
}

static void rpc_mask_irq_a(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOMD_IRQMASKA)));
}

static void rpc_unmask_irq_a(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	orr	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOMD_IRQMASKA)));
}

static void rpc_mask_irq_b(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOMD_IRQMASKB)));
}

static void rpc_unmask_irq_b(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	orr	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOMD_IRQMASKB)));
}

static void rpc_mask_irq_dma(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOMD_DMAMASK)));
}

static void rpc_unmask_irq_dma(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	orr	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOMD_DMAMASK)));
}

static void rpc_mask_irq_fiq(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOMD_FIQMASK)));
}

static void rpc_unmask_irq_fiq(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	orr	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOMD_FIQMASK)));
}

static __inline__ void irq_init_irq(void)
{
	extern void ecard_disableirq(unsigned int irq);
	extern void ecard_enableirq(unsigned int irq);
	int irq;

	outb(0, IOMD_IRQMASKA);
	outb(0, IOMD_IRQMASKB);
	outb(0, IOMD_FIQMASK);
	outb(0, IOMD_DMAMASK);

	for (irq = 0; irq < NR_IRQS; irq++) {
		switch (irq) {
		case 0 ... 6:
			irq_desc[irq].probe_ok = 1;
		case 7:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = rpc_mask_irq_ack_a;
			irq_desc[irq].mask     = rpc_mask_irq_a;
			irq_desc[irq].unmask   = rpc_unmask_irq_a;
			break;

		case 9 ... 15:
			irq_desc[irq].probe_ok = 1;
		case 8:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = rpc_mask_irq_b;
			irq_desc[irq].mask     = rpc_mask_irq_b;
			irq_desc[irq].unmask   = rpc_unmask_irq_b;
			break;

		case 16 ... 21:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].noautoenable = 1;
			irq_desc[irq].mask_ack = rpc_mask_irq_dma;
			irq_desc[irq].mask     = rpc_mask_irq_dma;
			irq_desc[irq].unmask   = rpc_unmask_irq_dma;
			break;

		case 32 ... 39:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = ecard_disableirq;
			irq_desc[irq].mask     = ecard_disableirq;
			irq_desc[irq].unmask   = ecard_enableirq;
			break;

		case 64 ... 71:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = rpc_mask_irq_fiq;
			irq_desc[irq].mask     = rpc_mask_irq_fiq;
			irq_desc[irq].unmask   = rpc_unmask_irq_fiq;
			break;
		}
	}

	irq_desc[IRQ_KEYBOARDTX].noautoenable = 1;

	init_FIQ();
}
