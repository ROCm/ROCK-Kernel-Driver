/* $Id: time.c,v 1.32 2000/09/22 23:02:13 davem Exp $
 * time.c: UltraSparc timer and TOD clock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Eddie C. Dost   (ecd@skynet.be)
 *
 * Based largely on code which is:
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/oplib.h>
#include <asm/mostek.h>
#include <asm/timer.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/fhc.h>
#include <asm/pbm.h>
#include <asm/ebus.h>
#include <asm/starfire.h>

extern rwlock_t xtime_lock;

unsigned long mstk48t02_regs = 0UL;
static unsigned long mstk48t08_regs = 0UL;
static unsigned long mstk48t59_regs = 0UL;

static int set_rtc_mmss(unsigned long);

/* timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 *
 * NOTE: On SUN5 systems the ticker interrupt comes in using 2
 *       interrupts, one at level14 and one with softint bit 0.
 */
unsigned long timer_tick_offset;
unsigned long timer_tick_compare;
unsigned long timer_ticks_per_usec_quotient;

static __inline__ void timer_check_rtc(void)
{
	/* last time the cmos clock got updated */
	static long last_rtc_update=0;

	/* Determine when to update the Mostek clock. */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec >= 500000 - ((unsigned) tick) / 2 &&
	    xtime.tv_usec <= 500000 + ((unsigned) tick) / 2) {
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600;
			/* do it again in 60 s */
	}
}

void sparc64_do_profile(unsigned long pc, unsigned long o7)
{
	if (prof_buffer && current->pid) {
		extern int _stext;
		extern int rwlock_impl_begin, rwlock_impl_end;
		extern int atomic_impl_begin, atomic_impl_end;
		extern int __memcpy_begin, __memcpy_end;
		extern int __bitops_begin, __bitops_end;

		if ((pc >= (unsigned long) &atomic_impl_begin &&
		     pc < (unsigned long) &atomic_impl_end) ||
		    (pc >= (unsigned long) &rwlock_impl_begin &&
		     pc < (unsigned long) &rwlock_impl_end) ||
		    (pc >= (unsigned long) &__memcpy_begin &&
		     pc < (unsigned long) &__memcpy_end) ||
		    (pc >= (unsigned long) &__bitops_begin &&
		     pc < (unsigned long) &__bitops_end))
			pc = o7;

		pc -= (unsigned long) &_stext;
		pc >>= prof_shift;

		if(pc >= prof_len)
			pc = prof_len - 1;
		atomic_inc((atomic_t *)&prof_buffer[pc]);
	}
}

static void timer_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	unsigned long ticks, pstate;

	write_lock(&xtime_lock);

	do {
#ifndef CONFIG_SMP
		if ((regs->tstate & TSTATE_PRIV) != 0)
			sparc64_do_profile(regs->tpc, regs->u_regs[UREG_RETPC]);
#endif
		do_timer(regs);

		/* Guarentee that the following sequences execute
		 * uninterrupted.
		 */
		__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
				     "wrpr	%0, %1, %%pstate"
				     : "=r" (pstate)
				     : "i" (PSTATE_IE));

		/* Workaround for Spitfire Errata (#54 I think??), I discovered
		 * this via Sun BugID 4008234, mentioned in Solaris-2.5.1 patch
		 * number 103640.
		 *
		 * On Blackbird writes to %tick_cmpr can fail, the
		 * workaround seems to be to execute the wr instruction
		 * at the start of an I-cache line, and perform a dummy
		 * read back from %tick_cmpr right after writing to it. -DaveM
		 *
		 * Just to be anal we add a workaround for Spitfire
		 * Errata 50 by preventing pipeline bypasses on the
		 * final read of the %tick register into a compare
		 * instruction.  The Errata 50 description states
		 * that %tick is not prone to this bug, but I am not
		 * taking any chances.
		 */
		__asm__ __volatile__("
			rd	%%tick_cmpr, %0
			ba,pt	%%xcc, 1f
			 add	%0, %2, %0
			.align	64
		     1: wr	%0, 0, %%tick_cmpr
		        rd	%%tick_cmpr, %%g0
			rd	%%tick, %1
			mov	%1, %1"
			: "=&r" (timer_tick_compare), "=r" (ticks)
			: "r" (timer_tick_offset));

		/* Restore PSTATE_IE. */
		__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
				     : /* no outputs */
				     : "r" (pstate));
	} while (ticks >= timer_tick_compare);

	timer_check_rtc();

	write_unlock(&xtime_lock);
}

#ifdef CONFIG_SMP
void timer_tick_interrupt(struct pt_regs *regs)
{
	write_lock(&xtime_lock);

	do_timer(regs);

	/*
	 * Only keep timer_tick_offset uptodate, but don't set TICK_CMPR.
	 */
	__asm__ __volatile__("
		rd	%%tick_cmpr, %0
		add	%0, %1, %0"
		: "=&r" (timer_tick_compare)
		: "r" (timer_tick_offset));

	timer_check_rtc();

	write_unlock(&xtime_lock);
}
#endif

/* Kick start a stopped clock (procedure from the Sun NVRAM/hostid FAQ). */
static void __init kick_start_clock(void)
{
	unsigned long regs = mstk48t02_regs;
	u8 sec, tmp;
	int i, count;

	prom_printf("CLOCK: Clock was stopped. Kick start ");

	/* Turn on the kick start bit to start the oscillator. */
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp |= MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);
	tmp = mostek_read(regs + MOSTEK_SEC);
	tmp &= ~MSTK_STOP;
	mostek_write(regs + MOSTEK_SEC, tmp);
	tmp = mostek_read(regs + MOSTEK_HOUR);
	tmp |= MSTK_KICK_START;
	mostek_write(regs + MOSTEK_HOUR, tmp);
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);

	/* Delay to allow the clock oscillator to start. */
	sec = MSTK_REG_SEC(regs);
	for (i = 0; i < 3; i++) {
		while (sec == MSTK_REG_SEC(regs))
			for (count = 0; count < 100000; count++)
				/* nothing */ ;
		prom_printf(".");
		sec = MSTK_REG_SEC(regs);
	}
	prom_printf("\n");

	/* Turn off kick start and set a "valid" time and date. */
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp |= MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);
	tmp = mostek_read(regs + MOSTEK_HOUR);
	tmp &= ~MSTK_KICK_START;
	mostek_write(regs + MOSTEK_HOUR, tmp);
	MSTK_SET_REG_SEC(regs,0);
	MSTK_SET_REG_MIN(regs,0);
	MSTK_SET_REG_HOUR(regs,0);
	MSTK_SET_REG_DOW(regs,5);
	MSTK_SET_REG_DOM(regs,1);
	MSTK_SET_REG_MONTH(regs,8);
	MSTK_SET_REG_YEAR(regs,1996 - MSTK_YEAR_ZERO);
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);

	/* Ensure the kick start bit is off. If it isn't, turn it off. */
	while (mostek_read(regs + MOSTEK_HOUR) & MSTK_KICK_START) {
		prom_printf("CLOCK: Kick start still on!\n");
		tmp = mostek_read(regs + MOSTEK_CREG);
		tmp |= MSTK_CREG_WRITE;
		mostek_write(regs + MOSTEK_CREG, tmp);

		tmp = mostek_read(regs + MOSTEK_HOUR);
		tmp &= ~MSTK_KICK_START;
		mostek_write(regs + MOSTEK_HOUR, tmp);

		tmp = mostek_read(regs + MOSTEK_CREG);
		tmp &= ~MSTK_CREG_WRITE;
		mostek_write(regs + MOSTEK_CREG, tmp);
	}

	prom_printf("CLOCK: Kick start procedure successful.\n");
}

/* Return nonzero if the clock chip battery is low. */
static int __init has_low_battery(void)
{
	unsigned long regs = mstk48t02_regs;
	u8 data1, data2;

	data1 = mostek_read(regs + MOSTEK_EEPROM);	/* Read some data. */
	mostek_write(regs + MOSTEK_EEPROM, ~data1);	/* Write back the complement. */
	data2 = mostek_read(regs + MOSTEK_EEPROM);	/* Read back the complement. */
	mostek_write(regs + MOSTEK_EEPROM, data1);	/* Restore original value. */

	return (data1 == data2);	/* Was the write blocked? */
}


/* Probe for the real time clock chip. */
static void __init set_system_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned long mregs = mstk48t02_regs;
	u8 tmp;

	do_get_fast_time = do_gettimeofday;

	if(!mregs) {
		prom_printf("Something wrong, clock regs not mapped yet.\n");
		prom_halt();
	}		

	tmp = mostek_read(mregs + MOSTEK_CREG);
	tmp |= MSTK_CREG_READ;
	mostek_write(mregs + MOSTEK_CREG, tmp);

	sec = MSTK_REG_SEC(mregs);
	min = MSTK_REG_MIN(mregs);
	hour = MSTK_REG_HOUR(mregs);
	day = MSTK_REG_DOM(mregs);
	mon = MSTK_REG_MONTH(mregs);
	year = MSTK_CVT_YEAR( MSTK_REG_YEAR(mregs) );
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_usec = 0;

	tmp = mostek_read(mregs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_READ;
	mostek_write(mregs + MOSTEK_CREG, tmp);
}

void __init clock_probe(void)
{
	struct linux_prom_registers clk_reg[2];
	char model[128];
	int node, busnd = -1, err;
	unsigned long flags;
#ifdef CONFIG_PCI
	struct linux_ebus *ebus = NULL;
#endif


	if (this_is_starfire) {
		/* davem suggests we keep this within the 4M locked kernel image */
		static char obp_gettod[256];
		static u32 unix_tod;

		sprintf(obp_gettod, "h# %08x unix-gettod",
			(unsigned int) (long) &unix_tod);
		prom_feval(obp_gettod);
		xtime.tv_sec = unix_tod;
		xtime.tv_usec = 0;
		return;
	}

	__save_and_cli(flags);

	if(central_bus != NULL) {
		busnd = central_bus->child->prom_node;
	}
#ifdef CONFIG_PCI
	else if (ebus_chain != NULL) {
		ebus = ebus_chain;
		busnd = ebus->prom_node;
	}
#endif
	else if (sbus_root != NULL) {
		busnd = sbus_root->prom_node;
	}

	if(busnd == -1) {
		prom_printf("clock_probe: problem, cannot find bus to search.\n");
		prom_halt();
	}

	node = prom_getchild(busnd);

	while(1) {
		if (!node)
			model[0] = 0;
		else
			prom_getstring(node, "model", model, sizeof(model));
		if(strcmp(model, "mk48t02") &&
		   strcmp(model, "mk48t08") &&
		   strcmp(model, "mk48t59")) {
		   	if (node)
				node = prom_getsibling(node);
#ifdef CONFIG_PCI
			while ((node == 0) && ebus != NULL) {
				ebus = ebus->next;
				if (ebus != NULL) {
					busnd = ebus->prom_node;
					node = prom_getchild(busnd);
				}
			}
#endif
			if(node == 0) {
				prom_printf("clock_probe: Cannot find timer chip\n");
				prom_halt();
			}
			continue;
		}

		err = prom_getproperty(node, "reg", (char *)clk_reg,
				       sizeof(clk_reg));
		if(err == -1) {
			prom_printf("clock_probe: Cannot get Mostek reg property\n");
			prom_halt();
		}

		if(central_bus) {
			apply_fhc_ranges(central_bus->child, clk_reg, 1);
			apply_central_ranges(central_bus, clk_reg, 1);
		}
#ifdef CONFIG_PCI
		else if (ebus_chain != NULL) {
			struct linux_ebus_device *edev;

			for_each_ebusdev(edev, ebus)
				if (edev->prom_node == node)
					break;
			if (edev == NULL) {
				prom_printf("%s: Mostek not probed by EBUS\n",
					    __FUNCTION__);
				prom_halt();
			}

			mstk48t59_regs = edev->resource[0].start;
			mstk48t02_regs = mstk48t59_regs + MOSTEK_48T59_48T02;
			break;
		}
#endif
		else {
			if (sbus_root->num_sbus_ranges) {
				int nranges = sbus_root->num_sbus_ranges;
				int rngc;

				for (rngc = 0; rngc < nranges; rngc++)
					if (clk_reg[0].which_io ==
					    sbus_root->sbus_ranges[rngc].ot_child_space)
						break;
				if (rngc == nranges) {
					prom_printf("clock_probe: Cannot find ranges for "
						    "clock regs.\n");
					prom_halt();
				}
				clk_reg[0].which_io =
					sbus_root->sbus_ranges[rngc].ot_parent_space;
				clk_reg[0].phys_addr +=
					sbus_root->sbus_ranges[rngc].ot_parent_base;
			}
		}

		if(model[5] == '0' && model[6] == '2') {
			mstk48t02_regs = (((u64)clk_reg[0].phys_addr) |
					  (((u64)clk_reg[0].which_io)<<32UL));
		} else if(model[5] == '0' && model[6] == '8') {
			mstk48t08_regs = (((u64)clk_reg[0].phys_addr) |
					  (((u64)clk_reg[0].which_io)<<32UL));
			mstk48t02_regs = mstk48t08_regs + MOSTEK_48T08_48T02;
		} else {
			mstk48t59_regs = (((u64)clk_reg[0].phys_addr) |
					  (((u64)clk_reg[0].which_io)<<32UL));
			mstk48t02_regs = mstk48t59_regs + MOSTEK_48T59_48T02;
		}
		break;
	}

	/* Report a low battery voltage condition. */
	if (has_low_battery())
		prom_printf("NVRAM: Low battery voltage!\n");

	/* Kick start the clock if it is completely stopped. */
	if (mostek_read(mstk48t02_regs + MOSTEK_SEC) & MSTK_STOP)
		kick_start_clock();

	set_system_time();
	
	__restore_flags(flags);
}

#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) (((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((((val)/10)<<4) + (val)%10)
#endif

extern void init_timers(void (*func)(int, void *, struct pt_regs *),
			unsigned long *);

void __init time_init(void)
{
	/* clock_probe() is now done at end of [se]bus_init on sparc64
	 * so that sbus, fhc and ebus bus information is probed and
	 * available.
	 */
	unsigned long clock;

	init_timers(timer_interrupt, &clock);
	timer_tick_offset = clock / HZ;
	timer_ticks_per_usec_quotient = ((1UL<<32) / (clock / 1000020));
}

static __inline__ unsigned long do_gettimeoffset(void)
{
	unsigned long ticks;

	__asm__ __volatile__("
		rd	%%tick, %%g1
		add	%1, %%g1, %0
		sub	%0, %2, %0
"
		: "=r" (ticks)
		: "r" (timer_tick_offset), "r" (timer_tick_compare)
		: "g1", "g2");

	return (ticks * timer_ticks_per_usec_quotient) >> 32UL;
}

void do_settimeofday(struct timeval *tv)
{
	if (this_is_starfire)
		return;

	write_lock_irq(&xtime_lock);

	tv->tv_usec -= do_gettimeoffset();
	if(tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;

	write_unlock_irq(&xtime_lock);
}

static int set_rtc_mmss(unsigned long nowtime)
{
	int real_seconds, real_minutes, mostek_minutes;
	unsigned long regs = mstk48t02_regs;
	u8 tmp;

	/* 
	 * Not having a register set can lead to trouble.
	 * Also starfire doesnt have a tod clock.
	 */
	if (!regs) 
		return -1;

	/* Read the current RTC minutes. */
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp |= MSTK_CREG_READ;
	mostek_write(regs + MOSTEK_CREG, tmp);

	mostek_minutes = MSTK_REG_MIN(regs);

	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_READ;
	mostek_write(regs + MOSTEK_CREG, tmp);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - mostek_minutes) + 15)/30) & 1)
		real_minutes += 30;	/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - mostek_minutes) < 30) {
		tmp = mostek_read(regs + MOSTEK_CREG);
		tmp |= MSTK_CREG_WRITE;
		mostek_write(regs + MOSTEK_CREG, tmp);

		MSTK_SET_REG_SEC(regs,real_seconds);
		MSTK_SET_REG_MIN(regs,real_minutes);

		tmp = mostek_read(regs + MOSTEK_CREG);
		tmp &= ~MSTK_CREG_WRITE;
		mostek_write(regs + MOSTEK_CREG, tmp);
	} else
		return -1;

	return 0;
}
