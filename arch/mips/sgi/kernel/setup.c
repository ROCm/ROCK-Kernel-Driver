/*
 * setup.c: SGI specific setup, including init of the feature struct.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 1998 Ralf Baechle (ralf@gnu.org)
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/console.h>
#include <linux/sched.h>
#include <linux/mc146818rtc.h>
#include <linux/pc_keyb.h>

#include <asm/addrspace.h>
#include <asm/bcache.h>
#include <asm/keyboard.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/sgialib.h>
#include <asm/sgi/sgimc.h>
#include <asm/sgi/sgihpc.h>
#include <asm/sgi/sgint23.h>
#include <asm/gdb-stub.h>

#ifdef CONFIG_REMOTE_DEBUG
extern void rs_kgdb_hook(int);
extern void breakpoint(void);
static int remote_debug = 0;
#endif

#if defined(CONFIG_SERIAL_CONSOLE) || defined(CONFIG_SGI_PROM_CONSOLE)
extern void console_setup(char *);
#endif

extern unsigned long r4k_interval; /* Cycle counter ticks per 1/HZ seconds */

extern struct rtc_ops indy_rtc_ops;
void indy_reboot_setup(void);
void sgi_volume_set(unsigned char);

#define sgi_kh ((struct hpc_keyb *) (KSEG1 + 0x1fbd9800 + 64))

#define KBD_STAT_IBF		0x02	/* Keyboard input buffer full */

static void sgi_request_region(void)
{
	/* No I/O ports are being used on the Indy.  */
}

static int sgi_request_irq(void (*handler)(int, void *, struct pt_regs *))
{
	/* Dirty hack, this get's called as a callback from the keyboard
	   driver.  We piggyback the initialization of the front panel
	   button handling on it even though they're technically not
	   related with the keyboard driver in any way.  Doing it from
	   indy_setup wouldn't work since kmalloc isn't initialized yet.  */
	indy_reboot_setup();

	return request_irq(SGI_KEYBOARD_IRQ, handler, 0, "keyboard", NULL);
}

static int sgi_aux_request_irq(void (*handler)(int, void *, struct pt_regs *))
{
	/* Nothing to do, interrupt is shared with the keyboard hw  */
	return 0;
}

static void sgi_aux_free_irq(void)
{
	/* Nothing to do, interrupt is shared with the keyboard hw  */
}

static unsigned char sgi_read_input(void)
{
	return sgi_kh->data;
}

static void sgi_write_output(unsigned char val)
{
	int status;

	do {
		status = sgi_kh->command;
	} while (status & KBD_STAT_IBF);
	sgi_kh->data = val;
}

static void sgi_write_command(unsigned char val)
{
	int status;

	do {
		status = sgi_kh->command;
	} while (status & KBD_STAT_IBF);
	sgi_kh->command = val;
}

static unsigned char sgi_read_status(void)
{
	return sgi_kh->command;
}

struct kbd_ops sgi_kbd_ops = {
	sgi_request_region,
	sgi_request_irq,

	sgi_aux_request_irq,
	sgi_aux_free_irq,

	sgi_read_input,
	sgi_write_output,
	sgi_write_command,
	sgi_read_status
};

static void __init sgi_irq_setup(void)
{
	sgint_init();

#ifdef CONFIG_REMOTE_DEBUG
	if (remote_debug)
		set_debug_traps();
	breakpoint(); /* you may move this line to whereever you want :-) */
#endif
}

int __init page_is_ram(unsigned long pagenr)
{
	if ((pagenr<<PAGE_SHIFT) < 0x2000UL)
		return 1;
	if ((pagenr<<PAGE_SHIFT) > 0x08002000)
		return 1;
	return 0;
}

void (*board_time_init)(struct irqaction *irq);

static unsigned long dosample(volatile unsigned char *tcwp,
                              volatile unsigned char *tc2p)
{
        unsigned long ct0, ct1;
        unsigned char msb, lsb;

        /* Start the counter. */
        *tcwp = (SGINT_TCWORD_CNT2 | SGINT_TCWORD_CALL | SGINT_TCWORD_MRGEN);
        *tc2p = (SGINT_TCSAMP_COUNTER & 0xff);
        *tc2p = (SGINT_TCSAMP_COUNTER >> 8);

        /* Get initial counter invariant */
        ct0 = read_32bit_cp0_register(CP0_COUNT);

        /* Latch and spin until top byte of counter2 is zero */
        do {
                *tcwp = (SGINT_TCWORD_CNT2 | SGINT_TCWORD_CLAT);
                lsb = *tc2p;
                msb = *tc2p;
                ct1 = read_32bit_cp0_register(CP0_COUNT);
        } while(msb);

        /* Stop the counter. */
        *tcwp = (SGINT_TCWORD_CNT2 | SGINT_TCWORD_CALL | SGINT_TCWORD_MSWST);

        /* Return the difference, this is how far the r4k counter increments
         * for every 1/HZ seconds. We round off the the nearest 1 MHz of
	 * master clock (= 1000000 / 100 / 2 = 5000 count).
         */
        return ((ct1 - ct0) / 5000) * 5000;
}

#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5)

void sgi_time_init (struct irqaction *irq) {
	/* Here we need to calibrate the cycle counter to at least be close.
	 * We don't need to actually register the irq handler because that's
	 * all done in indyIRQ.S.
	 */
        struct sgi_ioc_timers *p;
        volatile unsigned char *tcwp, *tc2p;
	unsigned long r4k_ticks[3] = { 0, 0, 0 };
	unsigned long r4k_next;

        /* Figure out the r4k offset, the algorithm is very simple
         * and works in _all_ cases as long as the 8254 counter
         * register itself works ok (as an interrupt driving timer
         * it does not because of bug, this is why we are using
         * the onchip r4k counter/compare register to serve this
         * purpose, but for r4k_offset calculation it will work
         * ok for us).  There are other very complicated ways
         * of performing this calculation but this one works just
         * fine so I am not going to futz around. ;-)
         */
        p = ioc_timers;
        tcwp = &p->tcword;
        tc2p = &p->tcnt2;

        printk("Calibrating system timer... ");
        dosample(tcwp, tc2p);                   /* Prime cache. */
        dosample(tcwp, tc2p);                   /* Prime cache. */
	/* Zero is NOT an option. */
	while (!r4k_ticks[0])
		r4k_ticks[0] = dosample (tcwp, tc2p);
	while (!r4k_ticks[1])
		r4k_ticks[1] = dosample (tcwp, tc2p);

	if (r4k_ticks[0] != r4k_ticks[1]) {
		printk ("warning: timer counts differ, retrying...");
		r4k_ticks[2] = dosample (tcwp, tc2p);
		if (r4k_ticks[2] == r4k_ticks[0] 
		    || r4k_ticks[2] == r4k_ticks[1])
			r4k_interval = r4k_ticks[2];
		else {
			printk ("disagreement, using average...");
			r4k_interval = (r4k_ticks[0] + r4k_ticks[1] 
					+ r4k_ticks[2]) / 3;
		}
	} else
		r4k_interval = r4k_ticks[0];

        printk("%d [%d.%02d MHz CPU]\n", (int) r4k_interval, 
		(int) (r4k_interval / 5000), (int) (r4k_interval % 5000) / 50);

	/* Set ourselves up for future interrupts */
        r4k_next = (read_32bit_cp0_register(CP0_COUNT) + r4k_interval);
        write_32bit_cp0_register(CP0_COMPARE, r4k_next);
        set_cp0_status(ST0_IM, ALLINTS);
	sti ();
}

void __init sgi_setup(void)
{
#ifdef CONFIG_SERIAL_CONSOLE
	char *ctype;
#endif
#ifdef CONFIG_REMOTE_DEBUG
	char *kgdb_ttyd;
#endif


	irq_setup = sgi_irq_setup;
	board_time_init = sgi_time_init;

	/* Init the INDY HPC I/O controller.  Need to call this before
	 * fucking with the memory controller because it needs to know the
	 * boardID and whether this is a Guiness or a FullHouse machine.
	 */
	sgihpc_init();

	/* Init INDY memory controller. */
	sgimc_init();

	/* Now enable boardcaches, if any. */
	indy_sc_init();

#ifdef CONFIG_SERIAL_CONSOLE
	/* ARCS console environment variable is set to "g?" for
	 * graphics console, it is set to "d" for the first serial
	 * line and "d2" for the second serial line.
	 */
	ctype = ArcGetEnvironmentVariable("console");
	if(*ctype == 'd') {
		if(*(ctype+1)=='2')
			console_setup ("ttyS1");
		else
			console_setup ("ttyS0");
	}
#endif

#ifdef CONFIG_REMOTE_DEBUG
	kgdb_ttyd = prom_getcmdline();
	if ((kgdb_ttyd = strstr(kgdb_ttyd, "kgdb=ttyd")) != NULL) {
		int line;
		kgdb_ttyd += strlen("kgdb=ttyd");
		if (*kgdb_ttyd != '1' && *kgdb_ttyd != '2')
			printk("KGDB: Uknown serial line /dev/ttyd%c, "
			       "falling back to /dev/ttyd1\n", *kgdb_ttyd);
		line = *kgdb_ttyd == '2' ? 0 : 1;
		printk("KGDB: Using serial line /dev/ttyd%d for session\n",
		       line ? 1 : 2);
		rs_kgdb_hook(line);

		prom_printf("KGDB: Using serial line /dev/ttyd%d for session, "
			    "please connect your debugger\n", line ? 1 : 2);

		remote_debug = 1;
		/* Breakpoints and stuff are in sgi_irq_setup() */
	}
#endif

#ifdef CONFIG_SGI_PROM_CONSOLE
	console_setup("ttyS0");
#endif
 
	sgi_volume_set(simple_strtoul(ArcGetEnvironmentVariable("volume"), NULL, 10));

#ifdef CONFIG_VT
#ifdef CONFIG_SGI_NEWPORT_CONSOLE
	conswitchp = &newport_con;

	screen_info = (struct screen_info) {
		0, 0,		/* orig-x, orig-y */
		0,		/* unused */
		0,		/* orig_video_page */
		0,		/* orig_video_mode */
		160,		/* orig_video_cols */
		0, 0, 0,	/* unused, ega_bx, unused */
		64,		/* orig_video_lines */
		0,		/* orig_video_isVGA */
		16		/* orig_video_points */
	};
#else
	conswitchp = &dummy_con;
#endif
#endif

	rtc_ops = &indy_rtc_ops;
	kbd_ops = &sgi_kbd_ops;
#ifdef CONFIG_PSMOUSE
	aux_device_present = 0xaa;
#endif
#ifdef CONFIG_VIDEO_VINO
	init_vino();
#endif

}
