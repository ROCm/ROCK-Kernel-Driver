/*
 * ip22-setup.c: SGI specific setup, including init of the feature struct.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 1998 Ralf Baechle (ralf@gnu.org)
 */
#include <linux/config.h>
#include <linux/ds1286.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/sched.h>
#include <linux/tty.h>

#include <asm/addrspace.h>
#include <asm/bcache.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/gdb-stub.h>
#include <asm/io.h>
#include <asm/traps.h>
#include <asm/sgialib.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>

#ifdef CONFIG_KGDB
extern void rs_kgdb_hook(int);
extern void breakpoint(void);
static int remote_debug = 0;
#endif

unsigned long sgi_gfxaddr;

/*
 * Stop-A is originally a Sun thing that isn't standard on IP22 so to avoid
 * accidents it's disabled by default on IP22.
 *
 * FIXME: provide a mechanism to change the value of stop_a_enabled.
 */
int serial_console;
int stop_a_enabled;

void ip22_do_break(void)
{
	if (!stop_a_enabled)
		return;

	printk("\n");
	ArcEnterInteractiveMode();
}

EXPORT_SYMBOL(ip22_do_break);

extern void ip22_be_init(void) __init;
extern void ip22_time_init(void) __init;

static int __init ip22_setup(void)
{
	char *ctype;
#ifdef CONFIG_KGDB
	char *kgdb_ttyd;
#endif

	board_be_init = ip22_be_init;
	ip22_time_init();

	/* Init the INDY HPC I/O controller.  Need to call this before
	 * fucking with the memory controller because it needs to know the
	 * boardID and whether this is a Guiness or a FullHouse machine.
	 */
	sgihpc_init();

	/* Init INDY memory controller. */
	sgimc_init();

#ifdef CONFIG_BOARD_SCACHE
	/* Now enable boardcaches, if any. */
	indy_sc_init();
#endif

	/* Set EISA IO port base for Indigo2 */
	set_io_port_base(KSEG1ADDR(0x00080000));

	/* ARCS console environment variable is set to "g?" for
	 * graphics console, it is set to "d" for the first serial
	 * line and "d2" for the second serial line.
	 */
	ctype = ArcGetEnvironmentVariable("console");
	if (ctype && *ctype == 'd') {
		static char options[8];
		char *baud = ArcGetEnvironmentVariable("dbaud");
		if (baud)
			strcpy(options, baud);
		add_preferred_console("ttyS", *(ctype + 1) == '2' ? 1 : 0,
				      baud ? options : NULL);
	} else if (!ctype || *ctype != 'g') {
		/* Use ARC if we don't want serial ('d') or Newport ('g'). */
		prom_flags |= PROM_FLAG_USE_AS_CONSOLE;
		add_preferred_console("arc", 0, NULL);
	}

#ifdef CONFIG_KGDB
	kgdb_ttyd = prom_getcmdline();
	if ((kgdb_ttyd = strstr(kgdb_ttyd, "kgdb=ttyd")) != NULL) {
		int line;
		kgdb_ttyd += strlen("kgdb=ttyd");
		if (*kgdb_ttyd != '1' && *kgdb_ttyd != '2')
			printk(KERN_INFO "KGDB: Uknown serial line /dev/ttyd%c"
			       ", falling back to /dev/ttyd1\n", *kgdb_ttyd);
		line = *kgdb_ttyd == '2' ? 0 : 1;
		printk(KERN_INFO "KGDB: Using serial line /dev/ttyd%d for "
		       "session\n", line ? 1 : 2);
		rs_kgdb_hook(line);

		printk(KERN_INFO "KGDB: Using serial line /dev/ttyd%d for "
		       "session, please connect your debugger\n", line ? 1:2);

		remote_debug = 1;
		/* Breakpoints and stuff are in sgi_irq_setup() */
	}
#endif

#ifdef CONFIG_VT
#ifdef CONFIG_SGI_NEWPORT_CONSOLE
	if (ctype && *ctype == 'g'){
		ULONG *gfxinfo;
		ULONG * (*__vec)(void) = (void *) (long)
			*((_PULONG *)(long)((PROMBLOCK)->pvector + 0x20));

		gfxinfo = __vec();
		sgi_gfxaddr = ((gfxinfo[1] >= 0xa0000000
			       && gfxinfo[1] <= 0xc0000000)
			       ? gfxinfo[1] - 0xa0000000 : 0);

		/* newport addresses? */
		if (sgi_gfxaddr == 0x1f0f0000 || sgi_gfxaddr == 0x1f4f0000) {
			conswitchp = &newport_con;
		}
	}
#endif
#endif

	return 0;
}

early_initcall(ip22_setup);
