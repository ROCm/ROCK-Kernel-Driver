#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/system.h>
#include <asm/pdc.h>	/* for iodc_call() proto and friends */
#include <asm/real.h>

static int __attribute__((aligned(8)))   iodc_retbuf[32];
static char __attribute__((aligned(64))) iodc_dbuf[4096];

/*
 * pdc_putc:
 * Console character print using IODC.
 *
 * Note that only these special chars are architected for console IODC io:
 * BEL, BS, CR, and LF. Others are passed through.
 * Since the HP console requires CR+LF to perform a 'newline', we translate
 * "\n" to "\r\n".
 */

static int posx;	/* for simple TAB-Simulation... */

/* XXX Should we spinlock posx usage */

void pdc_putc(unsigned char c)
{
	unsigned int n;
	unsigned long flags;

	switch (c) {
	case '\n':
		iodc_dbuf[0] = '\r'; 
		iodc_dbuf[1] = '\n';
               	n = 2;
               	posx = 0;
		break;
	case '\t':
		pdc_putc(' ');
		while (posx & 7) 	/* expand TAB */
			pdc_putc(' ');
		return;		/* return since IODC can't handle this */
	case '\b':
		posx-=2;		/* BS */
	default:
		iodc_dbuf[0] = c;
		n = 1;
		posx++;
		break;
	}
	{
		real32_call(PAGE0->mem_cons.iodc_io,
			(unsigned long)PAGE0->mem_cons.hpa, ENTRY_IO_COUT,
			PAGE0->mem_cons.spa, __pa(PAGE0->mem_cons.dp.layers),
			__pa(iodc_retbuf), 0, __pa(iodc_dbuf), n, 0);
	}
}

static void pdc_console_write(struct console *co, const char *s, unsigned count)
{
	while(count--)
		pdc_putc(*s++);
}

int pdc_console_wait_key(struct console *co)
{
	int ch = 'X';
	int status;

	/* Bail if no console input device. */
	if (!PAGE0->mem_kbd.iodc_io)
		return 0;
	
	/* wait for a keyboard (rs232)-input */
	do {
		unsigned long flags;

		save_flags(flags);
		cli();
		status = real32_call(PAGE0->mem_kbd.iodc_io,
			(unsigned long)PAGE0->mem_kbd.hpa, ENTRY_IO_CIN,
			PAGE0->mem_kbd.spa, __pa(PAGE0->mem_kbd.dp.layers),
			__pa(iodc_retbuf), 0, __pa(iodc_dbuf), 1, 0);
		restore_flags(flags);
		ch = *iodc_dbuf;	/* save the character directly to ch */
	} while (*iodc_retbuf == 0);	/* wait for a key */
	return ch;
}

int pdc_getc(void)
{
	return pdc_console_wait_key(NULL);
}

static int pdc_console_setup(struct console *co, char *options)
{
	return 0;
}

static struct console pdc_cons = {
	name:		"ttyB",
	write:		pdc_console_write,
	read:		NULL,
	device:		NULL, 
	wait_key:	pdc_console_wait_key,
	unblank:	NULL,
	setup:		pdc_console_setup,
	flags:		CON_PRINTBUFFER|CON_ENABLED,  // |CON_CONSDEV,
	index:		-1,
};

static int pdc_console_initialized;

void pdc_console_init(void)
{
	if (pdc_console_initialized)
		return;
	++pdc_console_initialized;
	
	/* If the console is duplex then copy the COUT parameters to CIN. */
	if (PAGE0->mem_cons.cl_class == CL_DUPLEX)
		memcpy(&PAGE0->mem_kbd, &PAGE0->mem_cons, sizeof(PAGE0->mem_cons));

	pdc_console_write(0, "PDC Console Initialized\n", 24);
	/* register the pdc console */
	register_console(&pdc_cons);
}


/* Unregister the pdc console with the printk console layer */
void pdc_console_die(void)
{
	printk("Switching from PDC console\n");
	if (!pdc_console_initialized)
		return;
	--pdc_console_initialized;
	
#ifdef CONFIG_VT_CONSOLE
	{
	    /* fixme (needed?): Wait for console-tasklet to finish !*/
	    extern struct tasklet_struct console_tasklet;
    	    tasklet_schedule(&console_tasklet);
	}
#endif

	unregister_console(&pdc_cons);
}


/*
 * Used for emergencies. Currently only used if an HPMC occurs. If an
 * HPMC occurs, it is possible that the current console may not be
 * properly initialed after the PDC IO reset. This routine unregisters all
 * of the current consoles, reinitializes the pdc console and
 * registers it.
 */

void pdc_console_restart(void)
{
	struct console *console;
	extern int log_size;

	if (pdc_console_initialized)
		return;

	while ((console = console_drivers) != (struct console *)0)
		unregister_console(console_drivers);

	log_size = 0;
	pdc_console_init();
	printk("Switched to PDC console\n");
	return;
}

