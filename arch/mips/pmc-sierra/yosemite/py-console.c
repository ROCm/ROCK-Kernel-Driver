/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001, 2002, 2004 Ralf Baechle
 */
#include <linux/init.h>
#include <linux/console.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/termios.h>
#include <linux/sched.h>
#include <linux/tty.h>

#include <linux/serial.h>
#include <linux/serial_core.h>
#include <asm/serial.h>
#include <asm/io.h>

/* SUPERIO uart register map */
struct yo_uartregs {
	union {
		volatile u8	rbr;	/* read only, DLAB == 0 */
		volatile u8	thr;	/* write only, DLAB == 0 */
		volatile u8	dll;	/* DLAB == 1 */
	} u1;
	union {
		volatile u8	ier;	/* DLAB == 0 */
		volatile u8	dlm;	/* DLAB == 1 */
	} u2;
	union {
		volatile u8	iir;	/* read only */
		volatile u8	fcr;	/* write only */
	} u3;
	volatile u8	iu_lcr;
	volatile u8	iu_mcr;
	volatile u8	iu_lsr;
	volatile u8	iu_msr;
	volatile u8	iu_scr;
} yo_uregs_t;

#define iu_rbr u1.rbr
#define iu_thr u1.thr
#define iu_dll u1.dll
#define iu_ier u2.ier
#define iu_dlm u2.dlm
#define iu_iir u3.iir
#define iu_fcr u3.fcr

extern unsigned long uart_base;

#define IO_BASE_64	0x9000000000000000ULL

static unsigned char readb_outer_space(unsigned long phys)
{
	unsigned long long vaddr = IO_BASE_64 | phys;
	unsigned char res;
	unsigned int sr;

	sr = read_c0_status();
	write_c0_status((sr | ST0_KX) & ~ ST0_IE);
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");

	__asm__ __volatile__ (
	"	.set	mips3		\n"
	"	ld	%0, (%0)	\n"
	"	lbu	%0, (%0)	\n"
	"	.set	mips0		\n"
	: "=r" (res)
	: "0" (&vaddr));

	write_c0_status(sr);
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");

	return res;
}

static void writeb_outer_space(unsigned long phys, unsigned char c)
{
	unsigned long long vaddr = IO_BASE_64 | phys;
	unsigned long tmp;
	unsigned int sr;

	sr = read_c0_status();
	write_c0_status((sr | ST0_KX) & ~ ST0_IE);
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");

	__asm__ __volatile__ (
	"	.set	mips3		\n"
	"	ld	%0, (%1)	\n"
	"	sb	%2, (%0)	\n"
	"	.set	mips0		\n"
	: "=r" (tmp)
	: "r" (&vaddr), "r" (c));

	write_c0_status(sr);
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");
	__asm__("sll	$0, $0, 2\n");
}

static inline struct yo_uartregs *console_uart(void)
{
	return (struct yo_uartregs *) (uart_base + 8);
}

void prom_putchar(char c)
{
	unsigned long lsr = 0xfd000008UL + offsetof(struct yo_uartregs, iu_lsr);
	unsigned long thr = 0xfd000008UL + offsetof(struct yo_uartregs, iu_thr);

	while ((readb_outer_space(lsr) & 0x20) == 0);
	writeb_outer_space(thr, c);
}

char __init prom_getchar(void)
{
	return 0;
}
