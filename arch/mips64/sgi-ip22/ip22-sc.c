/* $Id: ip22-sc.c,v 1.2 1999/12/04 03:59:01 ralf Exp $
 *
 * indy_sc.c: Indy cache managment functions.
 *
 * Copyright (C) 1997 Ralf Baechle (ralf@gnu.org),
 * derived from r4xx0.c by David S. Miller (dm@engr.sgi.com).
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/bcache.h>
#include <asm/sgi/sgimc.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/sgialib.h>
#include <asm/mmu_context.h>

/* Secondary cache size in bytes, if present.  */
static unsigned long scache_size;

#undef DEBUG_CACHE

#define SC_SIZE 0x00080000
#define SC_LINE 32
#define CI_MASK (SC_SIZE - SC_LINE)
#define SC_INDEX(n) ((n) & CI_MASK)

static inline void indy_sc_wipe(unsigned long first, unsigned long last)
{
	__asm__ __volatile__("
		.set	noreorder
		or	%0, %4		# first line to flush
		or	%1, %4		# last line to flush
1:		sw	$0, 0(%0)
		bne	%0, %1, 1b
		daddu	%0, 32
		.set reorder"
		: "=r" (first), "=r" (last)
		: "0" (first), "1" (last), "r" (0x9000000080000000)
		: "$1");
}

static void indy_sc_wback_invalidate(unsigned long addr, unsigned long size)
{
	unsigned long first_line, last_line;
	unsigned int flags;

#ifdef DEBUG_CACHE
	printk("indy_sc_wback_invalidate[%08lx,%08lx]", addr, size);
#endif

	if (!size)
		return;

	/* Which lines to flush?  */
	first_line = SC_INDEX(addr);
	last_line = SC_INDEX(addr + size - 1);

	__save_and_cli(flags);
	if (first_line <= last_line) {
		indy_sc_wipe(first_line, last_line);
		goto out;
	}

	indy_sc_wipe(first_line, SC_SIZE - SC_LINE);
	indy_sc_wipe(0, last_line);
out:
	__restore_flags(flags);
}

static void inline indy_sc_enable(void)
{
#ifdef DEBUG_CACHE
	printk("Enabling R4600 SCACHE\n");
#endif
	*(volatile unsigned char *) 0x9000000080000000 = 0;
}

static void indy_sc_disable(void)
{
#ifdef DEBUG_CACHE
	printk("Disabling R4600 SCACHE\n");
#endif
	*(volatile unsigned short *) 0x9000000080000000 = 0;
}

static inline __init int indy_sc_probe(void)
{
	volatile u32 *cpu_control;
	unsigned short cmd = 0xc220;
	unsigned long data = 0;
	int i, n;

#ifdef __MIPSEB__
	cpu_control = (volatile u32 *) KSEG1ADDR(0x1fa00034);
#else
	cpu_control = (volatile u32 *) KSEG1ADDR(0x1fa00030);
#endif
#define DEASSERT(bit) (*(cpu_control) &= (~(bit)))
#define ASSERT(bit) (*(cpu_control) |= (bit))
#define DELAY  for(n = 0; n < 100000; n++) __asm__ __volatile__("")
	DEASSERT(SGIMC_EEPROM_PRE);
	DEASSERT(SGIMC_EEPROM_SDATAO);
	DEASSERT(SGIMC_EEPROM_SECLOCK);
	DEASSERT(SGIMC_EEPROM_PRE);
	DELAY;
	ASSERT(SGIMC_EEPROM_CSEL); ASSERT(SGIMC_EEPROM_SECLOCK);
	for(i = 0; i < 11; i++) {
		if(cmd & (1<<15))
			ASSERT(SGIMC_EEPROM_SDATAO);
		else
			DEASSERT(SGIMC_EEPROM_SDATAO);
		DEASSERT(SGIMC_EEPROM_SECLOCK);
		ASSERT(SGIMC_EEPROM_SECLOCK);
		cmd <<= 1;
	}
	DEASSERT(SGIMC_EEPROM_SDATAO);
	for(i = 0; i < (sizeof(unsigned short) * 8); i++) {
		unsigned int tmp;

		DEASSERT(SGIMC_EEPROM_SECLOCK);
		DELAY;
		ASSERT(SGIMC_EEPROM_SECLOCK);
		DELAY;
		data <<= 1;
		tmp = *cpu_control;
		if(tmp & SGIMC_EEPROM_SDATAI)
			data |= 1;
	}
	DEASSERT(SGIMC_EEPROM_SECLOCK);
	DEASSERT(SGIMC_EEPROM_CSEL);
	ASSERT(SGIMC_EEPROM_PRE);
	ASSERT(SGIMC_EEPROM_SECLOCK);

	data <<= PAGE_SHIFT;
	if (data == 0)
		return 0;

	scache_size = data;

	printk("R4600/R5000 SCACHE size %ldK, linesize 32 bytes.\n",
	       scache_size >> 10);

	return 1;
}

/* XXX Check with wje if the Indy caches can differenciate between
   writeback + invalidate and just invalidate.  */
static struct bcache_ops indy_sc_ops = {
	indy_sc_enable,
	indy_sc_disable,
	indy_sc_wback_invalidate,
	indy_sc_wback_invalidate
};

void __init indy_sc_init(void)
{
	if (indy_sc_probe()) {
		indy_sc_enable();
		bcops = &indy_sc_ops;
	}
}
