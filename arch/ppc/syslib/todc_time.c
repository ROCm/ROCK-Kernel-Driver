/*
 * arch/ppc/syslib/todc_time.c
 *
 * Time of Day Clock support for the M48T35, M48T37, M48T59, and MC146818
 * Real Time Clocks/Timekeepers.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/bcd.h>

#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/time.h>
#include <asm/todc.h>

/*
 * Depending on the hardware on your board and your board design, the
 * RTC/NVRAM may be accessed either directly (like normal memory) or via
 * address/data registers.  If your board uses the direct method, set
 * 'nvram_data' to the base address of your nvram and leave 'nvram_as0' and
 * 'nvram_as1' NULL.  If your board uses address/data regs to access nvram,
 * set 'nvram_as0' to the address of the lower byte, set 'nvram_as1' to the
 * address of the upper byte (leave NULL if using mv146818), and set
 * 'nvram_data' to the address of the 8-bit data register.
 *
 * You also need to set 'ppc_md.nvram_read_val' and 'ppc_md.nvram_write_val' to
 * the proper routines.  There are standard ones defined further down in
 * this file that you can use.
 *
 * There is a built in assumption that the RTC and NVRAM are accessed by the
 * same mechanism (i.e., ppc_md.nvram_read_val, etc works for both).
 *
 * Note: Even though the documentation for the various RTC chips say that it
 * 	 take up to a second before it starts updating once the 'R' bit is
 * 	 cleared, they always seem to update even though we bang on it many
 * 	 times a second.  This is true, except for the Dallas Semi 1746/1747
 * 	 (possibly others).  Those chips seem to have a real problem whenever
 * 	 we set the 'R' bit before reading them, they basically stop counting.
 * 	 					--MAG
 */

/*
 * 'todc_info' should be initialized in your *_setup.c file to
 * point to a fully initialized 'todc_info_t' structure.
 * This structure holds all the register offsets for your particular
 * TODC/RTC chip.
 * TODC_ALLOC()/TODC_INIT() will allocate and initialize this table for you.
 */

#ifdef	RTC_FREQ_SELECT
#undef	RTC_FREQ_SELECT
#define	RTC_FREQ_SELECT		control_b	/* Register A */
#endif

#ifdef	RTC_CONTROL
#undef	RTC_CONTROL
#define	RTC_CONTROL		control_a	/* Register B */
#endif

#ifdef	RTC_INTR_FLAGS
#undef	RTC_INTR_FLAGS
#define	RTC_INTR_FLAGS		watchdog	/* Register C */
#endif

#ifdef	RTC_VALID
#undef	RTC_VALID
#define	RTC_VALID		interrupts	/* Register D */
#endif

/* Access routines when RTC accessed directly (like normal memory) */
u_char
todc_direct_read_val(int addr)
{
	return readb(todc_info->nvram_data + addr);
}

void
todc_direct_write_val(int addr, unsigned char val)
{
	writeb(val, todc_info->nvram_data + addr);
	return;
}

/* Access routines for accessing m48txx type chips via addr/data regs */
u_char
todc_m48txx_read_val(int addr)
{
	outb(addr, todc_info->nvram_as0);
	outb(addr>>todc_info->as0_bits, todc_info->nvram_as1);
	return inb(todc_info->nvram_data);
}

void
todc_m48txx_write_val(int addr, unsigned char val)
{
	outb(addr, todc_info->nvram_as0);
	outb(addr>>todc_info->as0_bits, todc_info->nvram_as1);
   	outb(val, todc_info->nvram_data);
	return;
}

/* Access routines for accessing mc146818 type chips via addr/data regs */
u_char
todc_mc146818_read_val(int addr)
{
	outb(addr, todc_info->nvram_as0);
	return inb(todc_info->nvram_data);
}

void
todc_mc146818_write_val(int addr, unsigned char val)
{
	outb(addr, todc_info->nvram_as0);
   	outb(val, todc_info->nvram_data);
	return;
}

/*
 * TODC routines
 *
 * There is some ugly stuff in that there are assumptions for the mc146818.
 *
 * Assumptions:
 *	- todc_info->control_a has the offset as mc146818 Register B reg
 *	- todc_info->control_b has the offset as mc146818 Register A reg
 *	- m48txx control reg's write enable or 'W' bit is same as
 *	  mc146818 Register B 'SET' bit (i.e., 0x80)
 *
 * These assumptions were made to make the code simpler.
 */
long __init
todc_time_init(void)
{
	static u_char	not_initialized = 1;

	/* Make sure clocks are running */
	if (not_initialized) {
		u_char	cntl_b;

		cntl_b = ppc_md.nvram_read_val(todc_info->control_b);

		if (todc_info->rtc_type == TODC_TYPE_MC146818) {
			if ((cntl_b & 0x70) != 0x20) {
				printk(KERN_INFO "TODC %s %s\n",
					"real-time-clock was stopped.",
					"Now starting...");
				cntl_b &= ~0x70;
				cntl_b |= 0x20;
			}

			ppc_md.nvram_write_val(todc_info->control_b, cntl_b);
		}
		else if (todc_info->rtc_type == TODC_TYPE_DS1501) {
			u_char	month;

			todc_info->enable_read = TODC_DS1501_CNTL_B_TE;
			todc_info->enable_write = TODC_DS1501_CNTL_B_TE;

			month = ppc_md.nvram_read_val(todc_info->month);

			if ((month & 0x80) == 0x80) {
				printk(KERN_INFO "TODC %s %s\n",
					"real-time-clock was stopped.",
					"Now starting...");
				month &= ~0x80;
				ppc_md.nvram_write_val(todc_info->month, month);
			}

			cntl_b &= ~TODC_DS1501_CNTL_B_TE;
			ppc_md.nvram_write_val(todc_info->control_b, cntl_b);
		}
		else { /* must be a m48txx type */
			u_char	cntl_a;

			todc_info->enable_read = TODC_MK48TXX_CNTL_A_R;
			todc_info->enable_write = TODC_MK48TXX_CNTL_A_W;

			cntl_a = ppc_md.nvram_read_val(todc_info->control_a);

			/* Check & clear STOP bit in control B register */
			if (cntl_b & TODC_MK48TXX_DAY_CB) {
				printk(KERN_INFO "TODC %s %s\n",
					"real-time-clock was stopped.",
					"Now starting...");

				cntl_a |= todc_info->enable_write;
				cntl_b &= ~TODC_MK48TXX_DAY_CB;/* Start Oscil */

				ppc_md.nvram_write_val(todc_info->control_a,
						       cntl_a);
				ppc_md.nvram_write_val(todc_info->control_b,
						       cntl_b);
			}

			/* Make sure READ & WRITE bits are cleared. */
			cntl_a &= ~(todc_info->enable_write |
				    todc_info->enable_read);
			ppc_md.nvram_write_val(todc_info->control_a, cntl_a);
		}

		not_initialized = 0;
	}


	return 0;
}

/*
 * There is some ugly stuff in that there are assumptions that for a mc146818,
 * the todc_info->control_a has the offset of the mc146818 Register B reg and
 * that the register'ss 'SET' bit is the same as the m48txx's write enable
 * bit in the control register of the m48txx (i.e., 0x80).
 *
 * It was done to make the code look simpler.
 */
ulong
todc_get_rtc_time(void)
{
	uint	year, mon, day, hour, min, sec;
	uint	limit, i;
	u_char	save_control, uip;

	save_control = ppc_md.nvram_read_val(todc_info->control_a);

	if (todc_info->rtc_type != TODC_TYPE_MC146818) {
		limit = 1;

		switch (todc_info->rtc_type) {
			case TODC_TYPE_DS1557:
			case TODC_TYPE_DS1743:
			case TODC_TYPE_DS1746:	/* XXXX BAD HACK -> FIX */
			case TODC_TYPE_DS1747:
				break;
			default:
				ppc_md.nvram_write_val(todc_info->control_a,
				       (save_control | todc_info->enable_read));
		}
	}
	else {
		limit = 100000000;
	}

	for (i=0; i<limit; i++) {
		if (todc_info->rtc_type == TODC_TYPE_MC146818) {
			uip = ppc_md.nvram_read_val(todc_info->RTC_FREQ_SELECT);
		}

		sec = ppc_md.nvram_read_val(todc_info->seconds) & 0x7f;
		min = ppc_md.nvram_read_val(todc_info->minutes) & 0x7f;
		hour = ppc_md.nvram_read_val(todc_info->hours) & 0x3f;
		day = ppc_md.nvram_read_val(todc_info->day_of_month) & 0x3f;
		mon = ppc_md.nvram_read_val(todc_info->month) & 0x1f;
		year = ppc_md.nvram_read_val(todc_info->year) & 0xff;

		if (todc_info->rtc_type == TODC_TYPE_MC146818) {
			uip |= ppc_md.nvram_read_val(
					todc_info->RTC_FREQ_SELECT);
			if ((uip & RTC_UIP) == 0) break;
		}
	}

	if (todc_info->rtc_type != TODC_TYPE_MC146818) {
		switch (todc_info->rtc_type) {
			case TODC_TYPE_DS1557:
			case TODC_TYPE_DS1743:
			case TODC_TYPE_DS1746:	/* XXXX BAD HACK -> FIX */
			case TODC_TYPE_DS1747:
				break;
			default:
				save_control &= ~(todc_info->enable_read);
				ppc_md.nvram_write_val(todc_info->control_a,
						       save_control);
		}
	}

	if ((todc_info->rtc_type != TODC_TYPE_MC146818) ||
	    ((save_control & RTC_DM_BINARY) == 0) ||
	    RTC_ALWAYS_BCD) {

		BCD_TO_BIN(sec);
		BCD_TO_BIN(min);
		BCD_TO_BIN(hour);
		BCD_TO_BIN(day);
		BCD_TO_BIN(mon);
		BCD_TO_BIN(year);
	}

	year = year + 1900;
	if (year < 1970) {
		year += 100;
	}

	return mktime(year, mon, day, hour, min, sec);
}

int
todc_set_rtc_time(unsigned long nowtime)
{
	struct rtc_time	tm;
	u_char		save_control, save_freq_select;

	to_tm(nowtime, &tm);

	save_control = ppc_md.nvram_read_val(todc_info->control_a);

	/* Assuming MK48T59_RTC_CA_WRITE & RTC_SET are equal */
	ppc_md.nvram_write_val(todc_info->control_a,
			       (save_control | todc_info->enable_write));
	save_control &= ~(todc_info->enable_write); /* in case it was set */

	if (todc_info->rtc_type == TODC_TYPE_MC146818) {
		save_freq_select =
			ppc_md.nvram_read_val(todc_info->RTC_FREQ_SELECT);
		ppc_md.nvram_write_val(todc_info->RTC_FREQ_SELECT,
				       save_freq_select | RTC_DIV_RESET2);
	}


        tm.tm_year = (tm.tm_year - 1900) % 100;

	if ((todc_info->rtc_type != TODC_TYPE_MC146818) ||
	    ((save_control & RTC_DM_BINARY) == 0) ||
	    RTC_ALWAYS_BCD) {

		BIN_TO_BCD(tm.tm_sec);
		BIN_TO_BCD(tm.tm_min);
		BIN_TO_BCD(tm.tm_hour);
		BIN_TO_BCD(tm.tm_mon);
		BIN_TO_BCD(tm.tm_mday);
		BIN_TO_BCD(tm.tm_year);
	}

	ppc_md.nvram_write_val(todc_info->seconds,      tm.tm_sec);
	ppc_md.nvram_write_val(todc_info->minutes,      tm.tm_min);
	ppc_md.nvram_write_val(todc_info->hours,        tm.tm_hour);
	ppc_md.nvram_write_val(todc_info->month,        tm.tm_mon);
	ppc_md.nvram_write_val(todc_info->day_of_month, tm.tm_mday);
	ppc_md.nvram_write_val(todc_info->year,         tm.tm_year);

	ppc_md.nvram_write_val(todc_info->control_a, save_control);

	if (todc_info->rtc_type == TODC_TYPE_MC146818) {
		ppc_md.nvram_write_val(todc_info->RTC_FREQ_SELECT,
				       save_freq_select);
	}

	return 0;
}

/*
 * Manipulates read bit to reliably read seconds at a high rate.
 */
static unsigned char __init todc_read_timereg(int addr)
{
	unsigned char save_control, val;

	switch (todc_info->rtc_type) {
		case TODC_TYPE_DS1557:
		case TODC_TYPE_DS1743:
		case TODC_TYPE_DS1746:	/* XXXX BAD HACK -> FIX */
		case TODC_TYPE_DS1747:
		case TODC_TYPE_MC146818:
			break;
		default:
			save_control =
				ppc_md.nvram_read_val(todc_info->control_a);
			ppc_md.nvram_write_val(todc_info->control_a,
				       (save_control | todc_info->enable_read));
	}
	val = ppc_md.nvram_read_val(addr);

	switch (todc_info->rtc_type) {
		case TODC_TYPE_DS1557:
		case TODC_TYPE_DS1743:
		case TODC_TYPE_DS1746:	/* XXXX BAD HACK -> FIX */
		case TODC_TYPE_DS1747:
		case TODC_TYPE_MC146818:
			break;
		default:
			save_control &= ~(todc_info->enable_read);
			ppc_md.nvram_write_val(todc_info->control_a,
					       save_control);
	}

	return val;
}

/*
 * This was taken from prep_setup.c
 * Use the NVRAM RTC to time a second to calibrate the decrementer.
 */
void __init
todc_calibrate_decr(void)
{
	ulong	freq;
	ulong	tbl, tbu;
        long	i, loop_count;
        u_char	sec;

	todc_time_init();

	/*
	 * Actually this is bad for precision, we should have a loop in
	 * which we only read the seconds counter. nvram_read_val writes
	 * the address bytes on every call and this takes a lot of time.
	 * Perhaps an nvram_wait_change method returning a time
	 * stamp with a loop count as parameter would be the solution.
	 */
	/*
	 * Need to make sure the tbl doesn't roll over so if tbu increments
	 * during this test, we need to do it again.
	 */
	loop_count = 0;

	sec = todc_read_timereg(todc_info->seconds) & 0x7f;

	do {
		tbu = get_tbu();

		for (i = 0 ; i < 10000000 ; i++) {/* may take up to 1 second */
		   tbl = get_tbl();

		   if ((todc_read_timereg(todc_info->seconds) & 0x7f) != sec) {
		      break;
		   }
		}

		sec = todc_read_timereg(todc_info->seconds) & 0x7f;

		for (i = 0 ; i < 10000000 ; i++) { /* Should take 1 second */
		   freq = get_tbl();

		   if ((todc_read_timereg(todc_info->seconds) & 0x7f) != sec) {
		      break;
		   }
		}

		freq -= tbl;
	} while ((get_tbu() != tbu) && (++loop_count < 2));

	printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq/1000000, freq%1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	return;
}
