#ifndef __EARLY_PRINTK_H_
#define __EARLY_PRINTK_H_

#ifdef CONFIG_EARLY_PRINTK
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <asm/io.h>
#include <asm/early_printk.h>

/* Simple VGA output */

#define MAX_YPOS	25
#define MAX_XPOS	80

/* Simple serial port output */

#define DEFAULT_BAUD	57600
#define XMTRDY		0x20

#define DLAB		0x80

#define TXR		0	/*  Transmit register (WRITE) */
#define RXR		0	/*  Receive register  (READ)  */
#define IER		1	/*  Interrupt Enable	  	*/
#define IIR		2	/*  Interrupt ID		*/
#define FCR		2	/*  FIFO control		*/
#define LCR		3	/*  Line control		*/
#define MCR		4	/*  Modem control		*/
#define LSR		5	/*  Line Status			*/
#define MSR		6	/*  Modem Status		*/
#define DLL		0	/*  Divisor Latch Low	 	*/
#define DLH		1	/*  Divisor latch High		*/


void early_printk(const char *fmt, ...);
int __init setup_early_printk(void); 

#else

#define early_printk(...) do {} while(0)
#define setup_early_printk() do {} while(0)

#endif

#endif
