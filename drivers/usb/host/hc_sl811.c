/*-------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------*
 * SL811HS USB HCD for Linux Version 0.1 (10/28/2001)
 * 
 * requires (includes) hc_simple.[hc] simple generic HCD frontend
 *  
 * COPYRIGHT(C) 2001 by CYPRESS SEMICONDUCTOR INC.
 *
 *-------------------------------------------------------------------------*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *-------------------------------------------------------------------------*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/list.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/usb.h>
#include "../core/hcd.h"

#undef HC_URB_TIMEOUT
#undef HC_SWITCH_INT
#undef HC_ENABLE_ISOC

#define SL811_DEBUG_ERR

#ifdef SL811_DEBUG_ERR
#define DBGERR(fmt, args...) printk(fmt,## args)
#else
#define DBGERR(fmt, args...)
#endif

#ifdef SL811_DEBUG
#define DBG(fmt, args...) printk(fmt,## args)
#else
#define DBG(fmt, args...)
#endif

#ifdef SL811_DEBUG_FUNC
#define DBGFUNC(fmt, args...) printk(fmt,## args)
#else
#define DBGFUNC(fmt, args...)
#endif

#ifdef SL811_DEBUG_DATA
#define DBGDATAR(fmt, args...) printk(fmt,## args)
#define DBGDATAW(fmt, args...) printk(fmt,## args)
#else
#define DBGDATAR(fmt, args...)
#define DBGDATAW(fmt, args...)
#endif

#ifdef SL811_DEBUG_VERBOSE
#define DBGVERBOSE(fmt, args...) printk(fmt,## args)
#else
#define DBGVERBOSE(fmt, args...)
#endif

#define TRUE 1
#define FALSE 0

#define HC_SWITCH_INT
#include "hc_sl811.h"
#include "hc_simple.h"

static int urb_debug = 0;

#include "hc_simple.c"
#include "hc_sl811_rh.c"

/* The base_addr, data_reg_addr, and irq number are board specific.
 * The current values are design to run on the Accelent SA1110 IDP
 * NOTE: values need to modify for different development boards 
 */

static int base_addr = 0xd3800000;
static int data_reg_addr = 0xd3810000;
static int irq = 34;

/* forware declaration */

int SL11StartXaction (hci_t * hci, __u8 addr, __u8 epaddr, int pid, int len,
		      int toggle, int slow, int urb_state);

static int sofWaitCnt = 0;

MODULE_PARM (urb_debug, "i");
MODULE_PARM_DESC (urb_debug, "debug urb messages, default is 0 (no)");

MODULE_PARM (base_addr, "i");
MODULE_PARM_DESC (base_addr, "sl811 base address 0xd3800000");
MODULE_PARM (data_reg_addr, "i");
MODULE_PARM_DESC (data_reg_addr, "sl811 data register address 0xd3810000");
MODULE_PARM (irq, "i");
MODULE_PARM_DESC (irq, "IRQ 34 (default)");

static int hc_reset (hci_t * hci);

/***************************************************************************
 * Function Name : SL811Read
 *
 * Read a byte of data from the SL811H/SL11H
 *
 * Input:  hci = data structure for the host controller
 *         offset = address of SL811/SL11H register or memory
 *
 * Return: data 
 **************************************************************************/
char SL811Read (hci_t * hci, char offset)
{
	hcipriv_t *hp = &hci->hp;
	char data;
	writeb (offset, hp->hcport);
	wmb ();
	data = readb (hp->hcport2);
	rmb ();
	return (data);
}

/***************************************************************************
 * Function Name : SL811Write
 *
 * Write a byte of data to the SL811H/SL11H
 *
 * Input:  hci = data structure for the host controller
 *         offset = address of SL811/SL11H register or memory
 *         data  = the data going to write to SL811H
 *
 * Return: none 
 **************************************************************************/
void SL811Write (hci_t * hci, char offset, char data)
{
	hcipriv_t *hp = &hci->hp;
	writeb (offset, hp->hcport);
	writeb (data, hp->hcport2);
	wmb ();
}

/***************************************************************************
 * Function Name : SL811BufRead
 *
 * Read consecutive bytes of data from the SL811H/SL11H buffer
 *
 * Input:  hci = data structure for the host controller
 *         offset = SL811/SL11H register offset
 *         buf = the buffer where the data will store
 *         size = number of bytes to read
 *
 * Return: none 
 **************************************************************************/
void SL811BufRead (hci_t * hci, short offset, char *buf, short size)
{
	hcipriv_t *hp = &hci->hp;
	if (size <= 0)
		return;
	writeb ((char) offset, hp->hcport);
	wmb ();
	DBGDATAR ("SL811BufRead: offset = 0x%x, data = ", offset);
	while (size--) {
		*buf++ = (char) readb (hp->hcport2);
		DBGDATAR ("0x%x ", *(buf - 1));
		rmb ();
	}
	DBGDATAR ("\n");
}

/***************************************************************************
 * Function Name : SL811BufWrite
 *
 * Write consecutive bytes of data to the SL811H/SL11H buffer
 *
 * Input:  hci = data structure for the host controller
 *         offset = SL811/SL11H register offset
 *         buf = the data buffer 
 *         size = number of bytes to write
 *
 * Return: none 
 **************************************************************************/
void SL811BufWrite (hci_t * hci, short offset, char *buf, short size)
{
	hcipriv_t *hp = &hci->hp;
	if (size <= 0)
		return;
	writeb ((char) offset, hp->hcport);
	wmb ();
	DBGDATAW ("SL811BufWrite: offset = 0x%x, data = ", offset);
	while (size--) {
		DBGDATAW ("0x%x ", *buf);
		writeb (*buf, hp->hcport2);
		wmb ();
		buf++;
	}
	DBGDATAW ("\n");
}

/***************************************************************************
 * Function Name : regTest
 *
 * This routine test the Read/Write functionality of SL811HS registers  
 *
 * 1) Store original register value into a buffer
 * 2) Write to registers with a RAMP pattern. (10, 11, 12, ..., 255)
 * 3) Read from register
 * 4) Compare the written value with the read value and make sure they are 
 *    equivalent
 * 5) Restore the original register value 
 *
 * Input:  hci = data structure for the host controller
 *   
 *
 * Return: TRUE = passed; FALSE = failed 
 **************************************************************************/
int regTest (hci_t * hci)
{
	int i, data, result = TRUE;
	char buf[256];

	DBGFUNC ("Enter regTest\n");
	for (i = 0x10; i < 256; i++) {
		/* save the original buffer */
		buf[i] = (char) SL811Read (hci, i);

		/* Write the new data to the buffer */
		SL811Write (hci, i, i);
	}

	/* compare the written data */
	for (i = 0x10; i < 256; i++) {
		data = SL811Read (hci, i);
		if (data != i) {
			DBGERR ("Pattern test failed!! value = 0x%x, s/b 0x%x\n",
				data, i);
			result = FALSE;
		}
	}

	/* restore the data */
	for (i = 0x10; i < 256; i++) {
		SL811Write (hci, i, buf[i]);
	}

	return (result);
}

/***************************************************************************
 * Function Name : regShow
 *
 * Display all SL811HS register values
 *
 * Input:  hci = data structure for the host controller
 *
 * Return: none 
 **************************************************************************/
void regShow (hci_t * hci)
{
	int i;
	for (i = 0; i < 256; i++) {
		printk ("offset %d: 0x%x\n", i, SL811Read (hci, i));
	}
}

/************************************************************************
 * Function Name : USBReset
 *  
 * This function resets SL811HS controller and detects the speed of
 * the connecting device				  
 *
 * Input:  hci = data structure for the host controller
 *                
 * Return: 0 = no device attached; 1 = USB device attached
 *                
 ***********************************************************************/
static int USBReset (hci_t * hci)
{
	int status;
	hcipriv_t *hp = &hci->hp;

	DBGFUNC ("enter USBReset\n");

	SL811Write (hci, SL11H_CTLREG2, 0xae);

	// setup master and full speed

	SL811Write (hci, SL11H_CTLREG1, 0x08);	// reset USB
	mdelay (20);		// 20ms                             
	SL811Write (hci, SL11H_CTLREG1, 0);	// remove SE0        

	for (status = 0; status < 100; status++)
		SL811Write (hci, SL11H_INTSTATREG, 0xff);	// clear all interrupt bits

	status = SL811Read (hci, SL11H_INTSTATREG);

	if (status & 0x40)	// Check if device is removed
	{
		DBG ("USBReset: Device removed\n");
		SL811Write (hci, SL11H_INTENBLREG,
			    SL11H_INTMASK_XFERDONE | SL11H_INTMASK_SOFINTR |
			    SL11H_INTMASK_INSRMV);
		hp->RHportStatus->portStatus &=
		    ~(PORT_CONNECT_STAT | PORT_ENABLE_STAT);

		return 0;
	}

	SL811Write (hci, SL11H_BUFLNTHREG_B, 0);	//zero lenth
	SL811Write (hci, SL11H_PIDEPREG_B, 0x50);	//send SOF to EP0       
	SL811Write (hci, SL11H_DEVADDRREG_B, 0x01);	//address0
	SL811Write (hci, SL11H_SOFLOWREG, 0xe0);

	if (!(status & 0x80)) {
		/* slow speed device connect directly to root-hub */

		DBG ("USBReset: low speed Device attached\n");
		SL811Write (hci, SL11H_CTLREG1, 0x8);
		mdelay (20);
		SL811Write (hci, SL11H_SOFTMRREG, 0xee);
		SL811Write (hci, SL11H_CTLREG1, 0x21);

		/* start the SOF or EOP */

		SL811Write (hci, SL11H_HOSTCTLREG_B, 0x01);
		hp->RHportStatus->portStatus |=
		    (PORT_CONNECT_STAT | PORT_LOW_SPEED_DEV_ATTACH_STAT);

		/* clear all interrupt bits */

		for (status = 0; status < 20; status++)
			SL811Write (hci, SL11H_INTSTATREG, 0xff);
	} else {
		/* full speed device connect directly to root hub */

		DBG ("USBReset: full speed Device attached\n");
		SL811Write (hci, SL11H_CTLREG1, 0x8);
		mdelay (20);
		SL811Write (hci, SL11H_SOFTMRREG, 0xae);
		SL811Write (hci, SL11H_CTLREG1, 0x01);

		/* start the SOF or EOP */

		SL811Write (hci, SL11H_HOSTCTLREG_B, 0x01);
		hp->RHportStatus->portStatus |= (PORT_CONNECT_STAT);
		hp->RHportStatus->portStatus &= ~PORT_LOW_SPEED_DEV_ATTACH_STAT;

		/* clear all interrupt bits */

		SL811Write (hci, SL11H_INTSTATREG, 0xff);

	}

	/* enable all interrupts */
	SL811Write (hci, SL11H_INTENBLREG,
		    SL11H_INTMASK_XFERDONE | SL11H_INTMASK_SOFINTR |
		    SL11H_INTMASK_INSRMV);

	return 1;
}

/*-------------------------------------------------------------------------*/
/* tl functions */
static inline void hc_mark_last_trans (hci_t * hci)
{
	hcipriv_t *hp = &hci->hp;
	__u8 *ptd = hp->tl;

	dbg ("enter hc_mark_last_trans\n");
	if (ptd == NULL) {
		printk ("hc_mark_last_trans: ptd = null\n");
		return;
	}
	if (hp->xferPktLen > 0)
		*(ptd + hp->tl_last) |= (1 << 3);
}

static inline void hc_flush_data_cache (hci_t * hci, void *data, int len)
{
}

/************************************************************************
 * Function Name : hc_add_trans
 *  
 * This function sets up the SL811HS register and transmit the USB packets.
 * 
 * 1) Determine if enough time within the current frame to send the packet
 * 2) Load the data into the SL811HS register
 * 3) Set the appropriate command to the register and trigger the transmit
 *
 * Input:  hci = data structure for the host controller
 *         len = data length
 *         data = transmitting data
 *         toggle = USB toggle bit, either 0 or 1
 *         maxps = maximum packet size for this endpoint
 *         slow = speed of the device
 *         endpoint = endpoint number
 *         address = USB address of the device
 *         pid = packet ID
 *         format = 
 *         urb_state = the current stage of USB transaction
 *       
 * Return: 0 = no time left to schedule the transfer
 *         1 = success 
 *                
 ***********************************************************************/
static inline int hc_add_trans (hci_t * hci, int len, void *data, int toggle,
				int maxps, int slow, int endpoint, int address,
				int pid, int format, int urb_state)
{
	hcipriv_t *hp = &hci->hp;
	__u16 speed;
	int ii, jj, kk;

	DBGFUNC ("enter hc_addr_trans: len =0x%x, toggle:0x%x, endpoing:0x%x,"
		 " addr:0x%x, pid:0x%x,format:0x%x\n", len, toggle, endpoint,
		 i address, pid, format);

	if (len > maxps) {
		len = maxps;
	}

	speed = hp->RHportStatus->portStatus;
	if (speed & PORT_LOW_SPEED_DEV_ATTACH_STAT) {
//      ii = (8*7*8 + 6*3) * len + 800; 
		ii = 8 * 8 * len + 1024;
	} else {
		if (slow) {
//          ii = (8*7*8 + 6*3) * len + 800; 
			ii = 8 * 8 * len + 2048;
		} else
//          ii = (8*7 + 6*3)*len + 110;
			ii = 8 * len + 256;
	}

	ii += 2 * 10 * len;

	jj = SL811Read (hci, SL11H_SOFTMRREG);
	kk = (jj & 0xFF) * 64 - ii;

	if (kk < 0) {
		DBGVERBOSE
		    ("hc_add_trans: no bandwidth for schedule, ii = 0x%x,"
		     "jj = 0x%x, len =0x%x, active_trans = 0x%x\n", ii, jj, len,
		     hci->active_trans);
		return (-1);
	}

	if (pid != PID_IN) {
		/* Load data into hc */

		SL811BufWrite (hci, SL11H_DATA_START, (__u8 *) data, len);
	}

	/* transmit */

	SL11StartXaction (hci, (__u8) address, (__u8) endpoint, (__u8) pid, len,
			  toggle, slow, urb_state);

	return len;
}

/************************************************************************
 * Function Name : hc_parse_trans
 *  
 * This function checks the status of the transmitted or received packet
 * and copy the data from the SL811HS register into a buffer.
 *
 * 1) Check the status of the packet 
 * 2) If successful, and IN packet then copy the data from the SL811HS register
 *    into a buffer
 *
 * Input:  hci = data structure for the host controller
 *         actbytes = pointer to actual number of bytes
 *         data = data buffer
 *         cc = packet status
 *         length = the urb transmit length
 *         pid = packet ID
 *         urb_state = the current stage of USB transaction
 *       
 * Return: 0 
 ***********************************************************************/
static inline int hc_parse_trans (hci_t * hci, int *actbytes, __u8 * data,
				  int *cc, int *toggle, int length, int pid,
				  int urb_state)
{
	__u8 addr;
	__u8 len;

	DBGFUNC ("enter hc_parse_trans\n");

	/* get packet status; convert ack rcvd to ack-not-rcvd */

	*cc = (int) SL811Read (hci, SL11H_PKTSTATREG);

	if (*cc &
	    (SL11H_STATMASK_ERROR | SL11H_STATMASK_TMOUT | SL11H_STATMASK_OVF |
	     SL11H_STATMASK_NAK | SL11H_STATMASK_STALL)) {
		if (*cc & SL11H_STATMASK_OVF)
			DBGERR ("parse trans: error recv ack, cc = 0x%x, TX_BASE_Len = "
				"0x%x, TX_count=0x%x\n", *cc,
				SL811Read (hci, SL11H_BUFLNTHREG),
				SL811Read (hci, SL11H_XFERCNTREG));

	} else {
		DBGVERBOSE ("parse trans: recv ack, cc = 0x%x, len = 0x%x, \n",
			    *cc, length);

		/* Successful data */
		if ((pid == PID_IN) && (urb_state != US_CTRL_SETUP)) {

			/* Find the base address */
			addr = SL811Read (hci, SL11H_BUFADDRREG);

			/* Find the Transmit Length */
			len = SL811Read (hci, SL11H_BUFLNTHREG);

			/* The actual data length = xmit length reg - xfer count reg */
			*actbytes = len - SL811Read (hci, SL11H_XFERCNTREG);

			if ((data != NULL) && (*actbytes > 0)) {
				SL811BufRead (hci, addr, data, *actbytes);

			} else if ((data == NULL) && (*actbytes <= 0)) {
				DBGERR ("hc_parse_trans: data = NULL or actbyte = 0x%x\n",
					*actbytes);
				return 0;
			}
		} else if (pid == PID_OUT) {
			*actbytes = length;
		} else {
			// printk ("ERR:parse_trans, pid != IN or OUT, pid = 0x%x\n", pid);
		}
		*toggle = !*toggle;
	}

	return 0;
}

/************************************************************************
 * Function Name : hc_start_int
 *  
 * This function enables SL811HS interrupts
 *
 * Input:  hci = data structure for the host controller
 *       
 * Return: none 
 ***********************************************************************/
static void hc_start_int (hci_t * hci)
{
#ifdef HC_SWITCH_INT
	int mask =
	    SL11H_INTMASK_XFERDONE | SL11H_INTMASK_SOFINTR |
	    SL11H_INTMASK_INSRMV | SL11H_INTMASK_USBRESET;
	SL811Write (hci, IntEna, mask);
#endif
}

/************************************************************************
 * Function Name : hc_stop_int
 *  
 * This function disables SL811HS interrupts
 *
 * Input:  hci = data structure for the host controller
 *       
 * Return: none 
 ***********************************************************************/
static void hc_stop_int (hci_t * hci)
{
#ifdef HC_SWITCH_INT
	SL811Write (hci, SL11H_INTSTATREG, 0xff);
//  SL811Write(hci, SL11H_INTENBLREG, SL11H_INTMASK_INSRMV);

#endif
}

/************************************************************************
 * Function Name : handleInsRmvIntr
 *  
 * This function handles the insertion or removal of device on  SL811HS. 
 * It resets the controller and updates the port status
 *
 * Input:  hci = data structure for the host controller
 *       
 * Return: none 
 ***********************************************************************/
void handleInsRmvIntr (hci_t * hci)
{
	hcipriv_t *hp = &hci->hp;

	USBReset (hci);

	/* Changes in connection status */

	hp->RHportStatus->portChange |= PORT_CONNECT_CHANGE;

	/* Port Enable or Disable */

	if (hp->RHportStatus->portStatus & PORT_CONNECT_STAT) {
		/* device is connected to the port:
		 *    1) Enable port 
		 *    2) Resume ?? 
		 */
//               hp->RHportStatus->portChange |= PORT_ENABLE_CHANGE;

		/* Over Current is not supported by the SL811 HW ?? */

		/* How about the Port Power ?? */

	} else {
		/* Device has disconnect:
		 *    1) Disable port
		 */

		hp->RHportStatus->portStatus &= ~(PORT_ENABLE_STAT);
		hp->RHportStatus->portChange |= PORT_ENABLE_CHANGE;

	}
}

/*****************************************************************
 *
 * Function Name: SL11StartXaction
 *  
 * This functions load the registers with appropriate value and 
 * transmit the packet.				  
 *
 * Input:  hci = data structure for the host controller
 *         addr = USB address of the device
 *         epaddr = endpoint number
 *         pid = packet ID
 *         len = data length
 *         toggle = USB toggle bit, either 0 or 1
 *         slow = speed of the device
 *         urb_state = the current stage of USB transaction
 *
 * Return: 0 = error; 1 = successful
 *                
 *****************************************************************/
int SL11StartXaction (hci_t * hci, __u8 addr, __u8 epaddr, int pid, int len,
		      int toggle, int slow, int urb_state)
{

	hcipriv_t *hp = &hci->hp;
	__u8 cmd = 0;
	__u8 setup_data[4];
	__u16 speed;

	speed = hp->RHportStatus->portStatus;
	if (!(speed & PORT_LOW_SPEED_DEV_ATTACH_STAT) && slow) {
		cmd |= SL11H_HCTLMASK_PREAMBLE;
	}
	switch (pid) {
	case PID_SETUP:
		cmd &= SL11H_HCTLMASK_PREAMBLE;
		cmd |=
		    (SL11H_HCTLMASK_ARM | SL11H_HCTLMASK_ENBLEP |
		     SL11H_HCTLMASK_WRITE);
		break;

	case PID_OUT:
		cmd &= (SL11H_HCTLMASK_SEQ | SL11H_HCTLMASK_PREAMBLE);
		cmd |=
		    (SL11H_HCTLMASK_ARM | SL11H_HCTLMASK_ENBLEP |
		     SL11H_HCTLMASK_WRITE);
		if (toggle) {
			cmd |= SL11H_HCTLMASK_SEQ;
		}
		break;

	case PID_IN:
		cmd &= (SL11H_HCTLMASK_SEQ | SL11H_HCTLMASK_PREAMBLE);
		cmd |= (SL11H_HCTLMASK_ARM | SL11H_HCTLMASK_ENBLEP);
		break;

	default:
		DBGERR ("ERR: SL11StartXaction: unknow pid = 0x%x\n", pid);
		return 0;
	}
	setup_data[0] = SL11H_DATA_START;
	setup_data[1] = len;
	setup_data[2] = (((pid & 0x0F) << 4) | (epaddr & 0xF));
	setup_data[3] = addr & 0x7F;

	SL811BufWrite (hci, SL11H_BUFADDRREG, (__u8 *) & setup_data[0], 4);

	SL811Write (hci, SL11H_HOSTCTLREG, cmd);

#if 0
	/* The SL811 has a hardware flaw when hub devices sends out
	 * SE0 between packets. It has been found in a TI chipset and
	 * cypress hub chipset. It causes the SL811 to hang
	 * The workaround is to re-issue the preample again.
	 */

	if ((cmd & SL11H_HCTLMASK_PREAMBLE)) {
		SL811Write (hci, SL11H_PIDEPREG_B, 0xc0);
		SL811Write (hci, SL11H_HOSTCTLREG_B, 0x1);	// send the premable
	}
#endif
	return 1;
}

/*****************************************************************
 *
 * Function Name: hc_interrupt
 *
 * Interrupt service routine. 
 *
 * 1) determine the causes of interrupt
 * 2) clears all interrupts
 * 3) calls appropriate function to service the interrupt
 *
 * Input:  irq = interrupt line associated with the controller 
 *         hci = data structure for the host controller
 *         r = holds the snapshot of the processor's context before 
 *             the processor entered interrupt code. (not used here) 
 *
 * Return value  : None.
 *                
 *****************************************************************/
static void hc_interrupt (int irq, void *__hci, struct pt_regs *r)
{
	char ii;
	hci_t *hci = __hci;
	int isExcessNak = 0;
	int urb_state = 0;
	char tmpIrq = 0;

	/* Get value from interrupt status register */

	ii = SL811Read (hci, SL11H_INTSTATREG);

	if (ii & SL11H_INTMASK_INSRMV) {
		/* Device insertion or removal detected for the USB port */

		SL811Write (hci, SL11H_INTENBLREG, 0);
		SL811Write (hci, SL11H_CTLREG1, 0);
		mdelay (100);	// wait for device stable 
		handleInsRmvIntr (hci);
		return;
	}

	/* Clear all interrupts */

	SL811Write (hci, SL11H_INTSTATREG, 0xff);

	if (ii & SL11H_INTMASK_XFERDONE) {
		/* USB Done interrupt occurred */

		urb_state = sh_done_list (hci, &isExcessNak);
#ifdef WARNING
		if (hci->td_array->len > 0)
			printk ("WARNING: IRQ, td_array->len = 0x%x, s/b:0\n",
				hci->td_array->len);
#endif
		if (hci->td_array->len == 0 && !isExcessNak
		    && !(ii & SL11H_INTMASK_SOFINTR) && (urb_state == 0)) {
			if (urb_state == 0) {
				/* All urb_state has not been finished yet! 
				 * continue with the current urb transaction 
				 */

				if (hci->last_packet_nak == 0) {
					if (!usb_pipecontrol
					    (hci->td_array->td[0].urb->pipe))
						sh_add_packet (hci, hci->td_array-> td[0].urb);
				}
			} else {
				/* The last transaction has completed:
				 * schedule the next transaction 
				 */

				sh_schedule_trans (hci, 0);
			}
		}
		SL811Write (hci, SL11H_INTSTATREG, 0xff);
		return;
	}

	if (ii & SL11H_INTMASK_SOFINTR) {
		hci->frame_number = (hci->frame_number + 1) % 2048;
		if (hci->td_array->len == 0)
			sh_schedule_trans (hci, 1);
		else {
			if (sofWaitCnt++ > 100) {
				/* The last transaction has not completed.
				 * Need to retire the current td, and let
				 * it transmit again later on.
				 * (THIS NEEDS TO BE WORK ON MORE, IT SHOULD NEVER 
				 *  GET TO THIS POINT)
				 */

				DBGERR ("SOF interrupt: td_array->len = 0x%x, s/b: 0\n",
					hci->td_array->len);
				urb_print (hci->td_array->td[hci->td_array->len - 1].urb,
					   "INTERRUPT", 0);
				sh_done_list (hci, &isExcessNak);
				SL811Write (hci, SL11H_INTSTATREG, 0xff);
				hci->td_array->len = 0;
				sofWaitCnt = 0;
			}
		}
		tmpIrq = SL811Read (hci, SL11H_INTSTATREG) & SL811Read (hci, SL11H_INTENBLREG);
		if (tmpIrq) {
			DBG ("IRQ occurred while service SOF: irq = 0x%x\n",
			     tmpIrq);

			/* If we receive a DONE IRQ after schedule, need to 
			 * handle DONE IRQ again 
			 */

			if (tmpIrq & SL11H_INTMASK_XFERDONE) {
				DBGERR ("IRQ occurred while service SOF: irq = 0x%x\n",
					tmpIrq);
				urb_state = sh_done_list (hci, &isExcessNak);
			}
			SL811Write (hci, SL11H_INTSTATREG, 0xff);
		}
	} else {
		DBG ("SL811 ISR: unknown, int = 0x%x \n", ii);
	}

	SL811Write (hci, SL11H_INTSTATREG, 0xff);
	return;
}

/*****************************************************************
 *
 * Function Name: hc_reset
 *
 * This function does register test and resets the SL811HS 
 * controller.
 *
 * Input:  hci = data structure for the host controller
 *
 * Return value  : 0
 *                
 *****************************************************************/
static int hc_reset (hci_t * hci)
{
	int attachFlag = 0;

	DBGFUNC ("Enter hc_reset\n");
	regTest (hci);
	attachFlag = USBReset (hci);
	if (attachFlag) {
		setPortChange (hci, PORT_CONNECT_CHANGE);
	}
	return (0);
}

/*****************************************************************
 *
 * Function Name: hc_alloc_trans_buffer
 *
 * This function allocates all transfer buffer  
 *
 * Input:  hci = data structure for the host controller
 *
 * Return value  : 0
 *                
 *****************************************************************/
static int hc_alloc_trans_buffer (hci_t * hci)
{
	hcipriv_t *hp = &hci->hp;
	int maxlen;

	hp->itl0_len = 0;
	hp->itl1_len = 0;
	hp->atl_len = 0;

	hp->itl_buffer_len = 1024;
	hp->atl_buffer_len = 4096 - 2 * hp->itl_buffer_len;	/* 2048 */

	maxlen = (hp->itl_buffer_len > hp->atl_buffer_len) ? hp->itl_buffer_len : hp->atl_buffer_len;

	hp->tl = kmalloc (maxlen, GFP_KERNEL);

	if (!hp->tl)
		return -ENOMEM;

	memset (hp->tl, 0, maxlen);
	return 0;
}

/*****************************************************************
 *
 * Function Name: getPortStatusAndChange
 *
 * This function gets the ports status from SL811 and format it 
 * to a USB request format
 *
 * Input:  hci = data structure for the host controller
 *
 * Return value  : port status and change
 *                
 *****************************************************************/
static __u32 getPortStatusAndChange (hci_t * hci)
{
	hcipriv_t *hp = &hci->hp;
	__u32 portstatus;

	DBGFUNC ("enter getPorStatusAndChange\n");

	portstatus = hp->RHportStatus->portChange << 16 | hp->RHportStatus->portStatus;

	return (portstatus);
}

/*****************************************************************
 *
 * Function Name: setPortChange
 *
 * This function set the bit position of portChange.
 *
 * Input:  hci = data structure for the host controller
 *         bitPos = the bit position
 *
 * Return value  : none 
 *                
 *****************************************************************/
static void setPortChange (hci_t * hci, __u16 bitPos)
{
	hcipriv_t *hp = &hci->hp;

	switch (bitPos) {
	case PORT_CONNECT_STAT:
		hp->RHportStatus->portChange |= bitPos;
		break;

	case PORT_ENABLE_STAT:
		hp->RHportStatus->portChange |= bitPos;
		break;

	case PORT_RESET_STAT:
		hp->RHportStatus->portChange |= bitPos;
		break;

	case PORT_POWER_STAT:
		hp->RHportStatus->portChange |= bitPos;
		break;

	case PORT_SUSPEND_STAT:
		hp->RHportStatus->portChange |= bitPos;
		break;

	case PORT_OVER_CURRENT_STAT:
		hp->RHportStatus->portChange |= bitPos;
		break;
	}
}

/*****************************************************************
 *
 * Function Name: clrPortChange
 *
 * This function clear the bit position of portChange.
 *
 * Input:  hci = data structure for the host controller
 *         bitPos = the bit position
 *
 * Return value  : none 
 *                
 *****************************************************************/
static void clrPortChange (hci_t * hci, __u16 bitPos)
{
	hcipriv_t *hp = &hci->hp;
	switch (bitPos) {
	case PORT_CONNECT_CHANGE:
		hp->RHportStatus->portChange &= ~bitPos;
		break;

	case PORT_ENABLE_CHANGE:
		hp->RHportStatus->portChange &= ~bitPos;
		break;

	case PORT_RESET_CHANGE:
		hp->RHportStatus->portChange &= ~bitPos;
		break;

	case PORT_SUSPEND_CHANGE:
		hp->RHportStatus->portChange &= ~bitPos;
		break;

	case PORT_OVER_CURRENT_CHANGE:
		hp->RHportStatus->portChange &= ~bitPos;
		break;
	}
}

/*****************************************************************
 *
 * Function Name: clrPortStatus
 *
 * This function clear the bit position of portStatus.
 *
 * Input:  hci = data structure for the host controller
 *         bitPos = the bit position
 *
 * Return value  : none 
 *                
 *****************************************************************/
static void clrPortStatus (hci_t * hci, __u16 bitPos)
{
	hcipriv_t *hp = &hci->hp;
	switch (bitPos) {
	case PORT_ENABLE_STAT:
		hp->RHportStatus->portStatus &= ~bitPos;
		break;

	case PORT_RESET_STAT:
		hp->RHportStatus->portStatus &= ~bitPos;
		break;

	case PORT_POWER_STAT:
		hp->RHportStatus->portStatus &= ~bitPos;
		break;

	case PORT_SUSPEND_STAT:
		hp->RHportStatus->portStatus &= ~bitPos;
		break;
	}
}

/*****************************************************************
 *
 * Function Name: setPortStatus
 *
 * This function set the bit position of portStatus.
 *
 * Input:  hci = data structure for the host controller
 *         bitPos = the bit position
 *
 * Return value  : none 
 *                
 *****************************************************************/
static void setPortStatus (hci_t * hci, __u16 bitPos)
{
	hcipriv_t *hp = &hci->hp;
	switch (bitPos) {
	case PORT_ENABLE_STAT:
		hp->RHportStatus->portStatus |= bitPos;
		break;

	case PORT_RESET_STAT:
		hp->RHportStatus->portStatus |= bitPos;
		break;

	case PORT_POWER_STAT:
		hp->RHportStatus->portStatus |= bitPos;
		break;

	case PORT_SUSPEND_STAT:
		hp->RHportStatus->portStatus |= bitPos;
		break;
	}
}

/*****************************************************************
 *
 * Function Name: hc_start
 *
 * This function starts the root hub functionality. 
 *
 * Input:  hci = data structure for the host controller
 *
 * Return value  : 0 
 *                
 *****************************************************************/
static int hc_start (hci_t * hci)
{
	DBGFUNC ("Enter hc_start\n");

	rh_connect_rh (hci);

	return 0;
}

/*****************************************************************
 *
 * Function Name: hc_alloc_hci
 *
 * This function allocates all data structure and store in the 
 * private data structure. 
 *
 * Input:  hci = data structure for the host controller
 *
 * Return value  : 0 
 *                
 *****************************************************************/
static hci_t *__devinit hc_alloc_hci (void)
{
	hci_t *hci;
	hcipriv_t *hp;
	portstat_t *ps;
	struct usb_bus *bus;

	DBGFUNC ("Enter hc_alloc_hci\n");
	hci = (hci_t *) kmalloc (sizeof (hci_t), GFP_KERNEL);
	if (!hci)
		return NULL;

	memset (hci, 0, sizeof (hci_t));

	hp = &hci->hp;

	hp->irq = -1;
	hp->hcport = -1;

	/* setup root hub port status */

	ps = (portstat_t *) kmalloc (sizeof (portstat_t), GFP_KERNEL);

	if (!ps)
		return NULL;
	ps->portStatus = PORT_STAT_DEFAULT;
	ps->portChange = PORT_CHANGE_DEFAULT;
	hp->RHportStatus = ps;

	hci->nakCnt = 0;
	hci->last_packet_nak = 0;

	hci->a_td_array.len = 0;
	hci->i_td_array[0].len = 0;
	hci->i_td_array[1].len = 0;
	hci->td_array = &hci->a_td_array;
	hci->active_urbs = 0;
	hci->active_trans = 0;
	INIT_LIST_HEAD (&hci->hci_hcd_list);
	list_add (&hci->hci_hcd_list, &hci_hcd_list);
	init_waitqueue_head (&hci->waitq);

	INIT_LIST_HEAD (&hci->ctrl_list);
	INIT_LIST_HEAD (&hci->bulk_list);
	INIT_LIST_HEAD (&hci->iso_list);
	INIT_LIST_HEAD (&hci->intr_list);
	INIT_LIST_HEAD (&hci->del_list);

	bus = usb_alloc_bus (&hci_device_operations);
	if (!bus) {
		kfree (hci);
		kfree (ps);
		return NULL;
	}

	hci->bus = bus;
	bus->hcpriv = (void *) hci;

	return hci;
}

/*****************************************************************
 *
 * Function Name: hc_release_hci
 *
 * This function De-allocate all resources  
 *
 * Input:  hci = data structure for the host controller
 *
 * Return value  : 0 
 *                
 *****************************************************************/
static void hc_release_hci (hci_t * hci)
{
	hcipriv_t *hp = &hci->hp;

	DBGFUNC ("Enter hc_release_hci\n");

	/* disconnect all devices */
	if (hci->bus->root_hub)
		usb_disconnect (&hci->bus->root_hub);

	hc_reset (hci);

	if (hp->tl)
		kfree (hp->tl);

	if (hp->hcport > 0) {
		release_region (hp->hcport, 2);
		hp->hcport = 0;
	}

	if (hp->irq >= 0) {
		free_irq (hp->irq, hci);
		hp->irq = -1;
	}

	usb_deregister_bus (hci->bus);
	usb_put_bus (hci->bus);

	list_del_init (&hci->hci_hcd_list);

	kfree (hci);
}

/*****************************************************************
 *
 * Function Name: init_irq
 *
 * This function is board specific.  It sets up the interrupt to 
 * be an edge trigger and trigger on the rising edge  
 *
 * Input: none 
 *
 * Return value  : none 
 *                
 *****************************************************************/
void init_irq (void)
{
	GPDR &= ~(1 << 13);
	set_GPIO_IRQ_edge (1 << 13, GPIO_RISING_EDGE);
}

/*****************************************************************
 *
 * Function Name: hc_found_hci
 *
 * This function request IO memory regions, request IRQ, and
 * allocate all other resources. 
 *
 * Input: addr = first IO address
 *        addr2 = second IO address
 *        irq = interrupt number 
 *
 * Return: 0 = success or error condition 
 *                
 *****************************************************************/
static int __devinit hc_found_hci (int addr, int addr2, int irq)
{
	hci_t *hci;
	hcipriv_t *hp;

	DBGFUNC ("Enter hc_found_hci\n");
	hci = hc_alloc_hci ();
	if (!hci) {
		return -ENOMEM;
	}

	init_irq ();
	hp = &hci->hp;

	if (!request_region (addr, 256, "SL811 USB HOST")) {
		DBGERR ("request address %d failed", addr);
		hc_release_hci (hci);
		return -EBUSY;
	}
	hp->hcport = addr;
	if (!hp->hcport) {
		DBGERR ("Error mapping SL811 Memory 0x%x", hp->hcport);
	}

	if (!request_region (addr2, 256, "SL811 USB HOST")) {
		DBGERR ("request address %d failed", addr2);
		hc_release_hci (hci);
		return -EBUSY;
	}
	hp->hcport2 = addr2;
	if (!hp->hcport2) {
		DBGERR ("Error mapping SL811 Memory 0x%x", hp->hcport2);
	}

	if (hc_alloc_trans_buffer (hci)) {
		hc_release_hci (hci);
		return -ENOMEM;
	}

	usb_register_bus (hci->bus);

	if (request_irq (irq, hc_interrupt, 0, "SL811", hci) != 0) {
		DBGERR ("request interrupt %d failed", irq);
		hc_release_hci (hci);
		return -EBUSY;
	}
	hp->irq = irq;

	printk (KERN_INFO __FILE__ ": USB SL811 at %x, addr2 = %x, IRQ %d\n",
		addr, addr2, irq);
	hc_reset (hci);

	if (hc_start (hci) < 0) {
		DBGERR ("can't start usb-%x", addr);
		hc_release_hci (hci);
		return -EBUSY;
	}

	return 0;
}

/*****************************************************************
 *
 * Function Name: hci_hcd_init
 *
 * This is an init function, and it is the first function being called
 *
 * Input: none 
 *
 * Return: 0 = success or error condition 
 *                
 *****************************************************************/
static int __init hci_hcd_init (void)
{
	int ret;

	DBGFUNC ("Enter hci_hcd_init\n");
	if (usb_disabled())
		return -ENODEV;

	ret = hc_found_hci (base_addr, data_reg_addr, irq);

	return ret;
}

/*****************************************************************
 *
 * Function Name: hci_hcd_cleanup
 *
 * This is a cleanup function, and it is called when module is 
 * unloaded. 
 *
 * Input: none 
 *
 * Return: none 
 *                
 *****************************************************************/
static void __exit hci_hcd_cleanup (void)
{
	struct list_head *hci_l;
	hci_t *hci;

	DBGFUNC ("Enter hci_hcd_cleanup\n");
	for (hci_l = hci_hcd_list.next; hci_l != &hci_hcd_list;) {
		hci = list_entry (hci_l, hci_t, hci_hcd_list);
		hci_l = hci_l->next;
		hc_release_hci (hci);
	}
}

module_init (hci_hcd_init);
module_exit (hci_hcd_cleanup);

MODULE_AUTHOR ("Pei Liu <pbl@cypress.com>");
MODULE_DESCRIPTION ("USB SL811HS Host Controller Driver");
