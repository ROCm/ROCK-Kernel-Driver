/* Serialport functions for debugging
 *
 * Copyright (c) 2000 Axis Communications AB
 *
 * Authors:  Bjorn Wesen
 *
 * Exports:
 *    console_print_etrax(char *buf)
 *    int getDebugChar()
 *    putDebugChar(int)
 *    enableDebugIRQ()
 *    init_etrax_debug()
 *
 * $Log: debugport.c,v $
 * Revision 1.14  2004/05/17 13:11:29  starvik
 * Disable DMA until real serial driver is up
 *
 * Revision 1.13  2004/05/14 07:58:01  starvik
 * Merge of changes from 2.4
 *
 * Revision 1.12  2003/09/11 07:29:49  starvik
 * Merge of Linux 2.6.0-test5
 *
 * Revision 1.11  2003/07/07 09:53:36  starvik
 * Revert all the 2.5.74 merge changes to make the console work again
 *
 * Revision 1.9  2003/02/17 17:07:23  starvik
 * Solved the problem with corrupted debug output (from Linux 2.4)
 *   * Wait until DMA, FIFO and pipe is empty before and after transmissions
 *   * Buffer data until a FIFO flush can be triggered.
 *
 * Revision 1.8  2003/01/22 06:48:36  starvik
 * Fixed warnings issued by GCC 3.2.1
 *
 * Revision 1.7  2002/12/12 08:26:32  starvik
 * Don't use C-comments inside CVS comments
 *
 * Revision 1.6  2002/12/11 15:42:02  starvik
 * Extracted v10 (ETRAX 100LX) specific stuff from arch/cris/kernel/
 *
 * Revision 1.5  2002/11/20 06:58:03  starvik
 * Compiles with kgdb
 *
 * Revision 1.4  2002/11/19 14:35:24  starvik
 * Changes from linux 2.4
 * Changed struct initializer syntax to the currently prefered notation
 *
 * Revision 1.3  2002/11/06 09:47:03  starvik
 * Modified for new interrupt macros
 *
 * Revision 1.2  2002/01/21 15:21:50  bjornw
 * Update for kdev_t changes
 *
 * Revision 1.6  2001/04/17 13:58:39  orjanf
 * * Renamed CONFIG_KGDB to CONFIG_ETRAX_KGDB.
 *
 * Revision 1.5  2001/03/26 14:22:05  bjornw
 * Namechange of some config options
 *
 * Revision 1.4  2000/10/06 12:37:26  bjornw
 * Use physical addresses when talking to DMA
 *
 *
 */

#include <linux/config.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <asm/system.h>
#include <asm/arch/svinto.h>
#include <asm/io.h>             /* Get SIMCOUT. */

/* Which serial-port is our debug port ? */

#if defined(CONFIG_ETRAX_DEBUG_PORT0) || defined(CONFIG_ETRAX_DEBUG_PORT_NULL)
#define DEBUG_PORT_IDX 0
#define DEBUG_OCMD R_DMA_CH6_CMD
#define DEBUG_FIRST R_DMA_CH6_FIRST
#define DEBUG_OCLRINT R_DMA_CH6_CLR_INTR
#define DEBUG_STATUS R_DMA_CH6_STATUS
#define DEBUG_READ R_SERIAL0_READ
#define DEBUG_WRITE R_SERIAL0_TR_DATA
#define DEBUG_TR_CTRL R_SERIAL0_TR_CTRL
#define DEBUG_REC_CTRL R_SERIAL0_REC_CTRL
#define DEBUG_IRQ IO_STATE(R_IRQ_MASK1_SET, ser0_data, set)
#define DEBUG_DMA_IRQ_CLR IO_STATE(R_IRQ_MASK2_CLR, dma6_descr, clr)
#endif

#ifdef CONFIG_ETRAX_DEBUG_PORT1
#define DEBUG_PORT_IDX 1
#define DEBUG_OCMD R_DMA_CH8_CMD
#define DEBUG_FIRST R_DMA_CH8_FIRST
#define DEBUG_OCLRINT R_DMA_CH8_CLR_INTR
#define DEBUG_STATUS R_DMA_CH8_STATUS
#define DEBUG_READ R_SERIAL1_READ
#define DEBUG_WRITE R_SERIAL1_TR_DATA
#define DEBUG_TR_CTRL R_SERIAL1_TR_CTRL
#define DEBUG_REC_CTRL R_SERIAL1_REC_CTRL
#define DEBUG_IRQ IO_STATE(R_IRQ_MASK1_SET, ser1_data, set)
#define DEBUG_DMA_IRQ_CLR IO_STATE(R_IRQ_MASK2_CLR, dma8_descr, clr)
#endif

#ifdef CONFIG_ETRAX_DEBUG_PORT2
#define DEBUG_PORT_IDX 2
#define DEBUG_OCMD R_DMA_CH2_CMD
#define DEBUG_FIRST R_DMA_CH2_FIRST
#define DEBUG_OCLRINT R_DMA_CH2_CLR_INTR
#define DEBUG_STATUS R_DMA_CH2_STATUS
#define DEBUG_READ R_SERIAL2_READ
#define DEBUG_WRITE R_SERIAL2_TR_DATA
#define DEBUG_TR_CTRL R_SERIAL2_TR_CTRL
#define DEBUG_REC_CTRL R_SERIAL2_REC_CTRL
#define DEBUG_IRQ IO_STATE(R_IRQ_MASK1_SET, ser2_data, set)
#define DEBUG_DMA_IRQ_CLR IO_STATE(R_IRQ_MASK2_CLR, dma2_descr, clr)
#endif

#ifdef CONFIG_ETRAX_DEBUG_PORT3
#define DEBUG_PORT_IDX 3
#define DEBUG_OCMD R_DMA_CH4_CMD
#define DEBUG_FIRST R_DMA_CH4_FIRST
#define DEBUG_OCLRINT R_DMA_CH4_CLR_INTR
#define DEBUG_STATUS R_DMA_CH4_STATUS
#define DEBUG_READ R_SERIAL3_READ
#define DEBUG_WRITE R_SERIAL3_TR_DATA
#define DEBUG_TR_CTRL R_SERIAL3_TR_CTRL
#define DEBUG_REC_CTRL R_SERIAL3_REC_CTRL
#define DEBUG_IRQ IO_STATE(R_IRQ_MASK1_SET, ser3_data, set)
#define DEBUG_DMA_IRQ_CLR IO_STATE(R_IRQ_MASK2_CLR, dma4_descr, clr)
#endif

#define MIN_SIZE 32 /* Size that triggers the FIFO to flush characters to interface */

static struct tty_driver *serial_driver;

typedef int (*debugport_write_function)(int i, const char *buf, unsigned int len);

debugport_write_function debug_write_function = NULL;

static void
console_write_direct(struct console *co, const char *buf, unsigned int len)
{
	int i;
	/* Send data */
	for (i = 0; i < len; i++) {
		/* Wait until transmitter is ready and send.*/
		while(!(*DEBUG_READ & IO_MASK(R_SERIAL0_READ, tr_ready)));
                *DEBUG_WRITE = buf[i];
	}
}

static void 
console_write(struct console *co, const char *buf, unsigned int len)
{
	unsigned long flags;
#ifdef CONFIG_ETRAX_DEBUG_PORT_NULL
        /* no debug printout at all */
        return;
#endif

#ifdef CONFIG_SVINTO_SIM
	/* no use to simulate the serial debug output */
	SIMCOUT(buf,len);
	return;
#endif

#ifdef CONFIG_ETRAX_KGDB
	/* kgdb needs to output debug info using the gdb protocol */
	putDebugString(buf, len);
	return;
#endif

	local_irq_save(flags);
	if (debug_write_function)
		if (debug_write_function(co->index, buf, len))
			return;
	console_write_direct(co, buf, len);
	local_irq_restore(flags);
}

/* legacy function */

void
console_print_etrax(const char *buf)
{
	console_write(NULL, buf, strlen(buf));
}

/* Use polling to get a single character FROM the debug port */

int
getDebugChar(void)
{
	unsigned long readval;
	
	do {
		readval = *DEBUG_READ;
	} while(!(readval & IO_MASK(R_SERIAL0_READ, data_avail)));

	return (readval & IO_MASK(R_SERIAL0_READ, data_in));
}

/* Use polling to put a single character to the debug port */

void
putDebugChar(int val)
{
	while(!(*DEBUG_READ & IO_MASK(R_SERIAL0_READ, tr_ready))) ;
;
	*DEBUG_WRITE = val;
}

/* Enable irq for receiving chars on the debug port, used by kgdb */

void
enableDebugIRQ(void)
{
	*R_IRQ_MASK1_SET = DEBUG_IRQ;
	/* use R_VECT_MASK directly, since we really bypass Linux normal
	 * IRQ handling in kgdb anyway, we don't need to use enable_irq
	 */
	*R_VECT_MASK_SET = IO_STATE(R_VECT_MASK_SET, serial, set);

	*DEBUG_REC_CTRL = IO_STATE(R_SERIAL0_REC_CTRL, rec_enable, enable);
}

static struct tty_driver*
console_device(struct console *c, int *index)
{
	*index = c->index;
	return serial_driver;
}

static int __init 
console_setup(struct console *co, char *options)
{
        return 0;
}

static struct console sercons = {
	.name    = "ttyS",
	.write   = console_write,
	.read    = NULL,
	.device  = console_device,
	.unblank = NULL,
	.setup   = console_setup,
	.flags   = CON_PRINTBUFFER,
	.index   = DEBUG_PORT_IDX,
	.cflag   = 0,
	.next    = NULL
};

/*
 *      Register console (for printk's etc)
 */

void __init 
init_etrax_debug(void)
{
#if CONFIG_ETRAX_DEBUG_PORT_NULL
	return;
#endif

#if DEBUG_PORT_IDX == 0
	genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma6);
	genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma6, unused);
#elif DEBUG_PORT_IDX == 1
	genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma8);
	genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma8, usb);
#elif DEBUG_PORT_IDX == 2
	genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma2);
	genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma2, par0);
#elif DEBUG_PORT_IDX == 3
	genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma4);
	genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma4, par1);
#endif
	*R_GEN_CONFIG = genconfig_shadow;

	register_console(&sercons);
}

int __init
init_console(void)
{
	serial_driver = alloc_tty_driver(1);
	if (!serial_driver)
		return -ENOMEM;
	return 0;
}
