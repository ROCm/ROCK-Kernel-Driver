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
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/eeprom.h>
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

int 
get_L1_baud(void)
{
    return UART_BAUD_RATE;
}



/* Return the current interrupt level */
int
l1_get_intr_value( void )
{
	return(0);
}

/* Disconnect the callup functions - throw away interrupts */

void
l1_unconnect_intr(void)
{
}

/* Set up uart interrupt handling for this node's uart */

void
l1_connect_intr(void *rx_notify, void *tx_notify)
{
#if 0
	// Will need code here for sn2 - something like this
	console_nodepda = NODEPDA(NASID_TO_COMPACT_NODEID(get_master_nasid());
	intr_connect_level(console_nodepda->node_first_cpu,
                                SGI_UART_VECTOR, INTPEND0_MAXMASK,
                                dummy_intr_func);
	request_irq(SGI_UART_VECTOR | (console_nodepda->node_first_cpu << 8),
                                intr_func, SA_INTERRUPT | SA_SHIRQ,
                                "l1_protocol_driver", (void *)sc);
#endif
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
	int counter = len;

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
	if ( ia64_sn_console_putb(str, len) )
		return(0);

	return((counter <= 0) ? 0 : (len - counter));
}
