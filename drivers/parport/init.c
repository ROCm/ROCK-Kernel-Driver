/* Parallel-port initialisation code.
 * 
 * Authors: David Campbell <campbell@torque.net>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell <Philip.Blundell@pobox.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/string.h>
#include <linux/parport.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>

#ifndef MODULE
static int io[PARPORT_MAX+1] __initdata = { [0 ... PARPORT_MAX] = 0 };
#ifdef CONFIG_PARPORT_PC
static int io_hi[PARPORT_MAX+1] __initdata =
	{ [0 ... PARPORT_MAX] = PARPORT_IOHI_AUTO };
#endif
static int irq[PARPORT_MAX] __initdata = { [0 ... PARPORT_MAX-1] = PARPORT_IRQ_PROBEONLY };
static int dma[PARPORT_MAX] __initdata = { [0 ... PARPORT_MAX-1] = PARPORT_DMA_NONE };

extern int parport_pc_init(int *io, int *io_hi, int *irq, int *dma);

static int parport_setup_ptr __initdata = 0;

/*
 * Acceptable parameters:
 *
 * parport=0
 * parport=auto
 * parport=0xBASE[,IRQ[,DMA]]
 *
 * IRQ/DMA may be numeric or 'auto' or 'none'
 */
static int __init parport_setup (char *str)
{
	char *endptr;
	char *sep;
	int val;

	if (!str || !*str || (*str == '0' && !*(str+1))) {
		/* Disable parport if "parport=0" in cmdline */
		io[0] = PARPORT_DISABLE;
		return 1;
	}

	if (!strncmp (str, "auto", 4)) {
		irq[0] = PARPORT_IRQ_AUTO;
		dma[0] = PARPORT_DMA_AUTO;
		return 1;
	}

	val = simple_strtoul (str, &endptr, 0);
	if (endptr == str) {
		printk (KERN_WARNING "parport=%s not understood\n", str);
		return 1;
	}

	if (parport_setup_ptr == PARPORT_MAX) {
		printk(KERN_ERR "parport=%s ignored, too many ports\n", str);
		return 1;
	}
	
	io[parport_setup_ptr] = val;
	irq[parport_setup_ptr] = PARPORT_IRQ_NONE;
	dma[parport_setup_ptr] = PARPORT_DMA_NONE;

	sep = strchr (str, ',');
	if (sep++) {
		if (!strncmp (sep, "auto", 4))
			irq[parport_setup_ptr] = PARPORT_IRQ_AUTO;
		else if (strncmp (sep, "none", 4)) {
			val = simple_strtoul (sep, &endptr, 0);
			if (endptr == sep) {
				printk (KERN_WARNING
					"parport=%s: irq not understood\n",
					str);
				return 1;
			}
			irq[parport_setup_ptr] = val;
		}
	}

	sep = strchr (sep, ',');
	if (sep++) {
		if (!strncmp (sep, "auto", 4))
			dma[parport_setup_ptr] = PARPORT_DMA_AUTO;
		else if (!strncmp (sep, "nofifo", 6))
			dma[parport_setup_ptr] = PARPORT_DMA_NOFIFO;
		else if (strncmp (sep, "none", 4)) {
			val = simple_strtoul (sep, &endptr, 0);
			if (endptr == sep) {
				printk (KERN_WARNING
					"parport=%s: dma not understood\n",
					str);
				return 1;
			}
			dma[parport_setup_ptr] = val;
		}
	}

	parport_setup_ptr++;
	return 1;
}

__setup ("parport=", parport_setup);

#endif

#ifdef MODULE
int init_module(void)
{
#ifdef CONFIG_SYSCTL
	parport_default_proc_register ();
#endif
	return 0;
}

void cleanup_module(void)
{
#ifdef CONFIG_SYSCTL
	parport_default_proc_unregister ();
#endif
}

#else

int __init parport_init (void)
{
	if (io[0] == PARPORT_DISABLE) 
		return 1;

#ifdef CONFIG_SYSCTL
	parport_default_proc_register ();
#endif

#ifdef CONFIG_PARPORT_PC
	parport_pc_init(io, io_hi, irq, dma);
#endif
	return 0;
}

__initcall(parport_init);

#endif
