/*
 *  Copyright (C) 2003 PMC-Sierra Inc.
 *  Author: Manish Lachwani (lachwani@pmc-sierra.com)
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/bcd.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/swap.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/timex.h>

#include <asm/time.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/pci_channel.h>
#include <asm/serial.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <asm/titan_dep.h>

#include "setup.h"

unsigned char titan_ge_mac_addr_base[6] = {
	0x00, 0x03, 0xcc, 0x1d, 0x22, 0x00
};

unsigned long cpu_clock;
unsigned long yosemite_base;

void __init bus_error_init(void)
{
	/* Do nothing */
}

unsigned long m48t37y_get_time(void)
{
	//unsigned char *rtc_base = (unsigned char *) YOSEMITE_RTC_BASE;
	unsigned char *rtc_base = (unsigned char *) 0xfc000000UL;
	unsigned int year, month, day, hour, min, sec;
return;

	/* Stop the update to the time */
	rtc_base[0x7ff8] = 0x40;

	year = BCD2BIN(rtc_base[0x7fff]);
	year += BCD2BIN(rtc_base[0x7fff1]) * 100;

	month = BCD2BIN(rtc_base[0x7ffe]);
	day = BCD2BIN(rtc_base[0x7ffd]);
	hour = BCD2BIN(rtc_base[0x7ffb]);
	min = BCD2BIN(rtc_base[0x7ffa]);
	sec = BCD2BIN(rtc_base[0x7ff9]);

	/* Start the update to the time again */
	rtc_base[0x7ff8] = 0x00;

	return mktime(year, month, day, hour, min, sec);
}

int m48t37y_set_time(unsigned long sec)
{
	unsigned char *rtc_base = (unsigned char *) YOSEMITE_RTC_BASE;
	struct rtc_time tm;
return;

	/* convert to a more useful format -- note months count from 0 */
	to_tm(sec, &tm);
	tm.tm_mon += 1;

	/* enable writing */
	rtc_base[0x7ff8] = 0x80;

	/* year */
	rtc_base[0x7fff] = BIN2BCD(tm.tm_year % 100);
	rtc_base[0x7ff1] = BIN2BCD(tm.tm_year / 100);

	/* month */
	rtc_base[0x7ffe] = BIN2BCD(tm.tm_mon);

	/* day */
	rtc_base[0x7ffd] = BIN2BCD(tm.tm_mday);

	/* hour/min/sec */
	rtc_base[0x7ffb] = BIN2BCD(tm.tm_hour);
	rtc_base[0x7ffa] = BIN2BCD(tm.tm_min);
	rtc_base[0x7ff9] = BIN2BCD(tm.tm_sec);

	/* day of week -- not really used, but let's keep it up-to-date */
	rtc_base[0x7ffc] = BIN2BCD(tm.tm_wday + 1);

	/* disable writing */
	rtc_base[0x7ff8] = 0x00;

	return 0;
}

void yosemite_timer_setup(struct irqaction *irq)
{
	setup_irq(7, irq);
}

void yosemite_time_init(void)
{
	board_timer_setup = yosemite_timer_setup;
	mips_hpt_frequency = cpu_clock / 2;

	rtc_get_time = m48t37y_get_time;
	rtc_set_time = m48t37y_set_time;
}

unsigned long uart_base = 0xfd000000L;

/* No other usable initialization hook than this ...  */
extern void (*late_time_init)(void);

unsigned long ocd_base;

EXPORT_SYMBOL(ocd_base);

/*
 * Common setup before any secondaries are started
 */

#define TITAN_UART_CLK		3686400
#define TITAN_SERIAL_BASE_BAUD	(TITAN_UART_CLK / 16)
#define TITAN_SERIAL_IRQ	4
#define TITAN_SERIAL_BASE	0xfd000008UL

static void __init py_map_ocd(void)
{
        struct uart_port up;

	/*
	 * Not specifically interrupt stuff but in case of SMP core_send_ipi
	 * needs this first so I'm mapping it here ...
	 */
	ocd_base = (unsigned long) ioremap(OCD_BASE, OCD_SIZE);
	if (!ocd_base)
		panic("Mapping OCD failed - game over.  Your score is 0.");

	/*
	 * Register to interrupt zero because we share the interrupt with
	 * the serial driver which we don't properly support yet.
	 */
	memset(&up, 0, sizeof(up));
	up.membase      = (unsigned char *) ioremap(TITAN_SERIAL_BASE, 8);
	up.irq          = TITAN_SERIAL_IRQ;
	up.uartclk      = TITAN_UART_CLK;
	up.regshift     = 0;
	up.iotype       = UPIO_MEM;
	up.flags        = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST;
	up.line         = 0;

	if (early_serial_setup(&up))
		printk(KERN_ERR "Early serial init of port 0 failed\n");
}

static int __init pmc_yosemite_setup(void)
{
	extern void pmon_smp_bootstrap(void);

	board_time_init = yosemite_time_init;
	late_time_init = py_map_ocd;

	/* Add memory regions */
	add_memory_region(0x00000000, 0x10000000, BOOT_MEM_RAM);

#if 0 /* XXX Crash ...  */
	OCD_WRITE(RM9000x2_OCD_HTSC,
	          OCD_READ(RM9000x2_OCD_HTSC) | HYPERTRANSPORT_ENABLE);

	/* Set the BAR. Shifted mode */
	OCD_WRITE(RM9000x2_OCD_HTBAR0, HYPERTRANSPORT_BAR0_ADDR);
	OCD_WRITE(RM9000x2_OCD_HTMASK0, HYPERTRANSPORT_SIZE0);
#endif

	return 0;
}

early_initcall(pmc_yosemite_setup);
