/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997, 2000-2003 Silicon Graphics, Inc.  All rights reserved.
 */

/* In general, this file is organized in a hierarchy from lower-level
 * to higher-level layers, as follows:
 *
 *	UART routines
 *	Bedrock/L1 "PPP-like" protocol implementation
 *	System controller "message" interface (allows multiplexing
 *		of various kinds of requests and responses with
 *		console I/O)
 *	Console interface:
 *	  "l1_cons", the glue that allows the L1 to act
 *		as the system console for the stdio libraries
 *
 * Routines making use of the system controller "message"-style interface
 * can be found in l1_command.c.
 */


#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/router.h>
#include <asm/sn/module.h>
#include <asm/sn/ksys/l1.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/uart16550.h>
#include <asm/sn/simulator.h>


#define UART_BAUD_RATE          57600

static int L1_connected;	/* non-zero when interrupts are enabled */


int 
get_L1_baud(void)
{
    return UART_BAUD_RATE;
}



/* Return the current interrupt level */
int
l1_get_intr_value( void )
{
	cpuid_t intr_cpuid;
	nasid_t console_nasid;
	int major, minor;
	extern nasid_t get_console_nasid(void);

	/* if it is an old prom, run in poll mode */

	major = sn_sal_rev_major();
	minor = sn_sal_rev_minor();
	if ( (major < 1) || ((major == 1) && (minor < 10)) ) {
		/* before version 1.10 doesn't work */
		return (0);
	}

	console_nasid = get_console_nasid();
	intr_cpuid = NODEPDA(NASID_TO_COMPACT_NODEID(console_nasid))->node_first_cpu;
	return CPU_VECTOR_TO_IRQ(intr_cpuid, SGI_UART_VECTOR);
}

/* Disconnect the callup functions - throw away interrupts */

void
l1_unconnect_intr(void)
{
}

/* Set up uart interrupt handling for this node's uart */

int
l1_connect_intr(void *intr_func, void *arg, struct pt_regs *ep)
{
	cpuid_t intr_cpuid;
	nasid_t console_nasid;
	unsigned int console_irq;
	int result;
	extern int intr_connect_level(cpuid_t, int, ilvl_t, intr_func_t);
	extern nasid_t get_console_nasid(void);


	/* don't call to connect multiple times - we DON'T support changing the handler */

	if ( !L1_connected ) {
		L1_connected++;
		console_nasid = get_console_nasid();
		intr_cpuid = NODEPDA(NASID_TO_COMPACT_NODEID(console_nasid))->node_first_cpu;
		console_irq = CPU_VECTOR_TO_IRQ(intr_cpuid, SGI_UART_VECTOR);
		result = intr_connect_level(intr_cpuid, SGI_UART_VECTOR,
                                	0 /*not used*/, 0 /*not used*/);
		if (result != SGI_UART_VECTOR) {
			if (result < 0)
				printk(KERN_WARNING "L1 console driver : intr_connect_level failed %d\n", result);
        		else
				printk(KERN_WARNING "L1 console driver : intr_connect_level returns wrong bit %d\n", result);
			return (-1);
		}

		result = request_irq(console_irq, intr_func, SA_INTERRUPT,
					"SGI L1 console driver", (void *)arg);
		if (result < 0) {
			printk(KERN_WARNING "L1 console driver : request_irq failed %d\n", result);
			return (-1);
		}

		/* ask SAL to turn on interrupts in the UART itself */
		ia64_sn_console_intr_enable(SAL_CONSOLE_INTR_RECV);
	}
	return (0);
}


/* These are functions to use from serial_in/out when in protocol
 * mode to send and receive uart control regs. These are external
 * interfaces into the protocol driver.
 */

void
l1_control_out(int offset, int value)
{
	/* quietly ignore unless simulator */
	if ( IS_RUNNING_ON_SIMULATOR() ) {
		extern u64 master_node_bedrock_address;
		if ( master_node_bedrock_address != (u64)0 ) {
			writeb(value, (unsigned long)master_node_bedrock_address +
				(offset<< 3));
		}
		return;
	}
}

/* Console input exported interface. Return a register value.  */

int
l1_control_in_polled(int offset)
{
	static int l1_control_in_local(int);

	return(l1_control_in_local(offset));
}

int
l1_control_in(int offset)
{
	static int l1_control_in_local(int);

	return(l1_control_in_local(offset));
}

static int
l1_control_in_local(int offset)
{
	int sal_call_status = 0, input;
	int ret = 0;

	if ( IS_RUNNING_ON_SIMULATOR() ) {
		extern u64 master_node_bedrock_address;
		ret = readb((unsigned long)master_node_bedrock_address +
				(offset<< 3));
		return(ret);
	}		
	if ( offset == REG_LSR ) {
		ret = (LSR_XHRE | LSR_XSRE);	/* can send anytime */
		sal_call_status = ia64_sn_console_check(&input);
		if ( !sal_call_status && input ) {
			/* input pending */
			ret |= LSR_RCA;
		}
	}
	return(ret);
}

/*
 * Console input exported interface. Return a character (if one is available)
 */

int
l1_serial_in_polled(void)
{
	static int l1_serial_in_local(void);

	return(l1_serial_in_local());
}

int
l1_serial_in(void)
{
	static int l1_serial_in_local(void);

	if ( IS_RUNNING_ON_SIMULATOR() ) {
		extern u64 master_node_bedrock_address;
		return(readb((unsigned long)master_node_bedrock_address + (REG_DAT<< 3)));
	}	
	return(l1_serial_in_local());
}

static int
l1_serial_in_local(void)
{
	int ch;

	if ( IS_RUNNING_ON_SIMULATOR() ) {
		extern u64 master_node_bedrock_address;
		return(readb((unsigned long)master_node_bedrock_address + (REG_DAT<< 3)));
	}		

	if ( !(ia64_sn_console_getc(&ch)) )
		return(ch);
	else
		return(0);
}

/* Console output exported interface. Write message to the console.  */

int
l1_serial_out( char *str, int len )
{
	int tmp;

	/* Ignore empty messages */
	if ( len == 0 )
		return(len);

#if defined(CONFIG_IA64_EARLY_PRINTK)
	/* Need to setup SAL calls so the PROM calls will work */
	{
		static int inited;
		void early_sn_setup(void);
		if(!inited) {
			inited=1;
			early_sn_setup();
		}
	}
#endif

	if ( IS_RUNNING_ON_SIMULATOR() ) {
		extern u64 master_node_bedrock_address;
		void early_sn_setup(void);
		int counter = len;

		if (!master_node_bedrock_address)
			early_sn_setup();
		if ( master_node_bedrock_address != (u64)0 ) {
#ifdef FLAG_DIRECT_CONSOLE_WRITES
			/* This is an easy way to pre-pend the output to know whether the output
			 * was done via sal or directly */
			writeb('[', (unsigned long)master_node_bedrock_address + (REG_DAT<< 3));
			writeb('+', (unsigned long)master_node_bedrock_address + (REG_DAT<< 3));
			writeb(']', (unsigned long)master_node_bedrock_address + (REG_DAT<< 3));
			writeb(' ', (unsigned long)master_node_bedrock_address + (REG_DAT<< 3));
#endif	/* FLAG_DIRECT_CONSOLE_WRITES */
			while ( counter > 0 ) {
				writeb(*str, (unsigned long)master_node_bedrock_address + (REG_DAT<< 3));
				counter--;
				str++;
			}
		}
		return(len);
	}

	/* Attempt to write things out thru the sal */
	if ( L1_connected )
		tmp = ia64_sn_console_xmit_chars(str, len);
	else
		tmp = ia64_sn_console_putb(str, len);
	return ((tmp < 0) ? 0 : tmp);
}
