
/* ------------------------------------------------------------------------- */
/* i2c-algo-pcf.c i2c driver algorithms for PCF8584 adapters		     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-1997 Simon G. Vogl
                   1998-2000 Hans Berglund

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi> and 
   Frodo Looijaard <frodol@dds.nl> ,and also from Martin Bailey
   <mbailey@littlefeet-inc.com> */

/* $Id: i2c-algo-pcf.c,v 1.25 2000/11/10 13:43:32 frodo Exp $ */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/version.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/sched.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-pcf.h>
#include "i2c-pcf8584.h"

/* ----- global defines ----------------------------------------------- */
#define DEB(x) if (i2c_debug>=1) x
#define DEB2(x) if (i2c_debug>=2) x
#define DEB3(x) if (i2c_debug>=3) x /* print several statistical values*/
#define DEBPROTO(x) if (i2c_debug>=9) x;
 	/* debug the protocol by showing transferred bits */
#define DEF_TIMEOUT 16

/* debugging - slow down transfer to have a look at the data .. 	*/
/* I use this with two leds&resistors, each one connected to sda,scl 	*/
/* respectively. This makes sure that the algorithm works. Some chips   */
/* might not like this, as they have an internal timeout of some mils	*/
/*
#define SLO_IO      jif=jiffies;while(jiffies<=jif+i2c_table[minor].veryslow)\
                        if (need_resched) schedule();
*/


/* ----- global variables ---------------------------------------------	*/

#ifdef SLO_IO
	int jif;
#endif

/* module parameters:
 */
static int i2c_debug=1;
static int pcf_test=0;	/* see if the line-setting functions work	*/
static int pcf_scan=0;	/* have a look at what's hanging 'round		*/

/* --- setting states on the bus with the right timing: ---------------	*/

#define set_pcf(adap, ctl, val) adap->setpcf(adap->data, ctl, val)
#define get_pcf(adap, ctl) adap->getpcf(adap->data, ctl)
#define get_own(adap) adap->getown(adap->data)
#define get_clock(adap) adap->getclock(adap->data)
#define i2c_outb(adap, val) adap->setpcf(adap->data, 0, val)
#define i2c_inb(adap) adap->getpcf(adap->data, 0)


/* --- other auxiliary functions --------------------------------------	*/

static void i2c_start(struct i2c_algo_pcf_data *adap) 
{
	DEBPROTO(printk("S "));
	set_pcf(adap, 1, I2C_PCF_START);
}

static void i2c_repstart(struct i2c_algo_pcf_data *adap) 
{
	DEBPROTO(printk(" Sr "));
	set_pcf(adap, 1, I2C_PCF_REPSTART);
}


static void i2c_stop(struct i2c_algo_pcf_data *adap) 
{
	DEBPROTO(printk("P\n"));
	set_pcf(adap, 1, I2C_PCF_STOP);
}


static int wait_for_bb(struct i2c_algo_pcf_data *adap) {

	int timeout = DEF_TIMEOUT;
	int status;

	status = get_pcf(adap, 1);
#ifndef STUB_I2C
	while (timeout-- && !(status & I2C_PCF_BB)) {
		udelay(1000); /* How much is this? */
		status = get_pcf(adap, 1);
	}
#endif
	if (timeout<=0)
		printk("Timeout waiting for Bus Busy\n");
	/*
	set_pcf(adap, 1, I2C_PCF_STOP);
	*/
	return(timeout<=0);
}


static inline void pcf_sleep(unsigned long timeout)
{
	schedule_timeout( timeout * HZ);
}


static int wait_for_pin(struct i2c_algo_pcf_data *adap, int *status) {

	int timeout = DEF_TIMEOUT;

	*status = get_pcf(adap, 1);
#ifndef STUB_I2C
	while (timeout-- && (*status & I2C_PCF_PIN)) {
		adap->waitforpin();
		*status = get_pcf(adap, 1);
	}
#endif
	if (timeout <= 0)
		return(-1);
	else
		return(0);
}


/* 
 * This should perform the 'PCF8584 initialization sequence' as described
 * in the Philips IC12 data book (1995, Aug 29).
 * There should be a 30 clock cycle wait after reset, I assume this
 * has been fulfilled.
 * There should be a delay at the end equal to the longest I2C message
 * to synchronize the BB-bit (in multimaster systems). How long is
 * this? I assume 1 second is always long enough.
 */
static int pcf_init_8584 (struct i2c_algo_pcf_data *adap)
{

	/* S1=0x80: S0 selected, serial interface off			*/
	set_pcf(adap, 1, I2C_PCF_PIN);

	/* load own address in S0, effective address is (own << 1)	*/
	i2c_outb(adap, get_own(adap));

	/* S1=0xA0, next byte in S2					*/
	set_pcf(adap, 1, I2C_PCF_PIN | I2C_PCF_ES1);

	/* load clock register S2					*/
	i2c_outb(adap, get_clock(adap));

	/* Enable serial interface, idle, S0 selected			*/
	set_pcf(adap, 1, I2C_PCF_IDLE);

	DEB2(printk("i2c-algo-pcf.o: irq: Initialized 8584.\n"));
	return 0;
}


/*
 * Sanity check for the adapter hardware - check the reaction of
 * the bus lines only if it seems to be idle.
 */
static int test_bus(struct i2c_algo_pcf_data *adap, char *name) {
#if 0
	int scl,sda;
	sda=getsda(adap);
	if (adap->getscl==NULL) {
		printk("i2c-algo-pcf.o: Warning: Adapter can't read from clock line - skipping test.\n");
		return 0;		
	}
	scl=getscl(adap);
	printk("i2c-algo-pcf.o: Adapter: %s scl: %d  sda: %d -- testing...\n",
	name,getscl(adap),getsda(adap));
	if (!scl || !sda ) {
		printk("i2c-algo-pcf.o: %s seems to be busy.\n",adap->name);
		goto bailout;
	}
	sdalo(adap);
	printk("i2c-algo-pcf.o:1 scl: %d  sda: %d \n",getscl(adap),
	       getsda(adap));
	if ( 0 != getsda(adap) ) {
		printk("i2c-algo-pcf.o: %s SDA stuck high!\n",name);
		sdahi(adap);
		goto bailout;
	}
	if ( 0 == getscl(adap) ) {
		printk("i2c-algo-pcf.o: %s SCL unexpected low while pulling SDA low!\n",
			name);
		goto bailout;
	}		
	sdahi(adap);
	printk("i2c-algo-pcf.o:2 scl: %d  sda: %d \n",getscl(adap),
	       getsda(adap));
	if ( 0 == getsda(adap) ) {
		printk("i2c-algo-pcf.o: %s SDA stuck low!\n",name);
		sdahi(adap);
		goto bailout;
	}
	if ( 0 == getscl(adap) ) {
		printk("i2c-algo-pcf.o: %s SCL unexpected low while SDA high!\n",
		       adap->name);
	goto bailout;
	}
	scllo(adap);
	printk("i2c-algo-pcf.o:3 scl: %d  sda: %d \n",getscl(adap),
	       getsda(adap));
	if ( 0 != getscl(adap) ) {
		printk("i2c-algo-pcf.o: %s SCL stuck high!\n",name);
		sclhi(adap);
		goto bailout;
	}
	if ( 0 == getsda(adap) ) {
		printk("i2c-algo-pcf.o: %s SDA unexpected low while pulling SCL low!\n",
			name);
		goto bailout;
	}
	sclhi(adap);
	printk("i2c-algo-pcf.o:4 scl: %d  sda: %d \n",getscl(adap),
	       getsda(adap));
	if ( 0 == getscl(adap) ) {
		printk("i2c-algo-pcf.o: %s SCL stuck low!\n",name);
		sclhi(adap);
		goto bailout;
	}
	if ( 0 == getsda(adap) ) {
		printk("i2c-algo-pcf.o: %s SDA unexpected low while SCL high!\n",
			name);
		goto bailout;
	}
	printk("i2c-algo-pcf.o: %s passed test.\n",name);
	return 0;
bailout:
	sdahi(adap);
	sclhi(adap);
	return -ENODEV;
#endif
	return (0);
}

/* ----- Utility functions
 */

static inline int try_address(struct i2c_algo_pcf_data *adap,
		       unsigned char addr, int retries)
{
	int i, status, ret = -1;
	for (i=0;i<retries;i++) {
		i2c_outb(adap, addr);
		i2c_start(adap);
		status = get_pcf(adap, 1);
		if (wait_for_pin(adap, &status) >= 0) {
			if ((status & I2C_PCF_LRB) == 0) { 
				i2c_stop(adap);
				break;	/* success! */
			}
		}
		i2c_stop(adap);
		udelay(adap->udelay);
	}
	DEB2(if (i) printk("i2c-algo-pcf.o: needed %d retries for %d\n",i,
	                   addr));
	return ret;
}


static int pcf_sendbytes(struct i2c_adapter *i2c_adap,const char *buf,
                         int count)
{
	struct i2c_algo_pcf_data *adap = i2c_adap->algo_data;
	int wrcount, status, timeout;
    
	for (wrcount=0; wrcount<count; ++wrcount) {
		DEB2(printk("i2c-algo-pcf.o: %s i2c_write: writing %2.2X\n",
		      i2c_adap->name, buf[wrcount]&0xff));
		i2c_outb(adap, buf[wrcount]);
		timeout = wait_for_pin(adap, &status);
		if (timeout) {
			i2c_stop(adap);
			printk("i2c-algo-pcf.o: %s i2c_write: "
			       "error - timeout.\n", i2c_adap->name);
			return -EREMOTEIO; /* got a better one ?? */
		}
#ifndef STUB_I2C
		if (status & I2C_PCF_LRB) {
			i2c_stop(adap);
			printk("i2c-algo-pcf.o: %s i2c_write: "
			       "error - no ack.\n", i2c_adap->name);
			return -EREMOTEIO; /* got a better one ?? */
		}
#endif
	}
	i2c_stop(adap);
	return (wrcount);
}


static int pcf_readbytes(struct i2c_adapter *i2c_adap, char *buf, int count)
{
	int rdcount=0, i, status, timeout, dummy=1;
	struct i2c_algo_pcf_data *adap = i2c_adap->algo_data;
    
	for (i=0; i<count; ++i) {
		buf[rdcount] = i2c_inb(adap);
		if (dummy) {
			dummy = 0;
		} else {
			rdcount++;
		}
		timeout = wait_for_pin(adap, &status);
		if (timeout) {
			i2c_stop(adap);
			printk("i2c-algo-pcf.o: i2c_read: "
			       "i2c_inb timed out.\n");
			return (-1);
		}
#ifndef STUB_I2C
		if (status & I2C_PCF_LRB) {
			i2c_stop(adap);
			printk("i2c-algo-pcf.o: i2c_read: i2c_inb, No ack.\n");
			return (-1);
		}
#endif
	}
	set_pcf(adap, 1, I2C_PCF_ESO);
	buf[rdcount] = i2c_inb(adap);
	if (dummy) {
		dummy = 0;
	} else {
		rdcount++;
	}
	timeout = wait_for_pin(adap, &status);
	if (timeout) {
		i2c_stop(adap);
		printk("i2c-algo-pcf.o: i2c_read: i2c_inb timed out.\n");
		return (-1);
	}
    
	i2c_stop(adap);

	/* Read final byte from S0 register */
	buf[rdcount++] = i2c_inb(adap);

	return (rdcount);
}


static inline int pcf_doAddress(struct i2c_algo_pcf_data *adap,
                                struct i2c_msg *msg, int retries) 
{
	unsigned short flags = msg->flags;
	unsigned char addr;
	int ret;
	if ( (flags & I2C_M_TEN)  ) { 
		/* a ten bit address */
		addr = 0xf0 | (( msg->addr >> 7) & 0x03);
		DEB2(printk("addr0: %d\n",addr));
		/* try extended address code...*/
		ret = try_address(adap, addr, retries);
		if (ret!=1) {
			printk("died at extended address code.\n");
			return -EREMOTEIO;
		}
		/* the remaining 8 bit address */
		i2c_outb(adap,msg->addr & 0x7f);
/* Status check comes here */
		if (ret != 1) {
			printk("died at 2nd address code.\n");
			return -EREMOTEIO;
		}
		if ( flags & I2C_M_RD ) {
			i2c_repstart(adap);
			/* okay, now switch into reading mode */
			addr |= 0x01;
			ret = try_address(adap, addr, retries);
			if (ret!=1) {
				printk("died at extended address code.\n");
				return -EREMOTEIO;
			}
		}
	} else {		/* normal 7bit address	*/
		addr = ( msg->addr << 1 );
		if (flags & I2C_M_RD )
			addr |= 1;
		if (flags & I2C_M_REV_DIR_ADDR )
			addr ^= 1;
		i2c_outb(adap, addr);
	}
	return 0;
}

static int pcf_xfer(struct i2c_adapter *i2c_adap,
		    struct i2c_msg msgs[], 
		    int num)
{
	struct i2c_algo_pcf_data *adap = i2c_adap->algo_data;
	struct i2c_msg *pmsg;
	int i = 0;
	int ret, timeout, status;
    
	pmsg = &msgs[i];
    
	/* Send address here if Read */
	if (pmsg->flags & I2C_M_RD) {
		ret = pcf_doAddress(adap, pmsg, i2c_adap->retries);
	}
    
	/* Check for bus busy */
	timeout = wait_for_bb(adap);
	if (timeout) {
		DEB2(printk("i2c-algo-pcf.o: "
		            "Timeout waiting for BB in pcf_xfer\n");)
		return -EIO;
	}
    
	/* Send address here if Write */
	if (!(pmsg->flags & I2C_M_RD)) {
		ret = pcf_doAddress(adap, pmsg, i2c_adap->retries);
	}
	/* Send START */
	i2c_start(adap);
    
	/* Wait for PIN (pending interrupt NOT) */
	timeout = wait_for_pin(adap, &status);
	if (timeout) {
		i2c_stop(adap);
		DEB2(printk("i2c-algo-pcf.o: Timeout waiting "
		            "for PIN(1) in pcf_xfer\n");)
		return (-EREMOTEIO);
	}
    
#ifndef STUB_I2C
	/* Check LRB (last rcvd bit - slave ack) */
	if (status & I2C_PCF_LRB) {
		i2c_stop(adap);
		DEB2(printk("i2c-algo-pcf.o: No LRB(1) in pcf_xfer\n");)
		return (-EREMOTEIO);
	}
#endif
    
	DEB3(printk("i2c-algo-pcf.o: Msg %d, addr=0x%x, flags=0x%x, len=%d\n",
	            i, msgs[i].addr, msgs[i].flags, msgs[i].len);)
    
	/* Read */
	if (pmsg->flags & I2C_M_RD) {
        
		/* read bytes into buffer*/
		ret = pcf_readbytes(i2c_adap, pmsg->buf, pmsg->len);
        
		if (ret != pmsg->len) {
			DEB2(printk("i2c-algo-pcf.o: fail: "
			            "only read %d bytes.\n",ret));
		} else {
			DEB2(printk("i2c-algo-pcf.o: read %d bytes.\n",ret));
		}
	} else { /* Write */
        
        /* Write bytes from buffer */
		ret = pcf_sendbytes(i2c_adap, pmsg->buf, pmsg->len);
        
		if (ret != pmsg->len) {
			DEB2(printk("i2c-algo-pcf.o: fail: "
			            "only wrote %d bytes.\n",ret));
		} else {
			DEB2(printk("i2c-algo-pcf.o: wrote %d bytes.\n",ret));
		}
	}
	return (num);
}

static int algo_control(struct i2c_adapter *adapter, 
	unsigned int cmd, unsigned long arg)
{
	return 0;
}

static u32 pcf_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR | 
	       I2C_FUNC_PROTOCOL_MANGLING; 
}

/* -----exported algorithm data: -------------------------------------	*/

static struct i2c_algorithm pcf_algo = {
	"PCF8584 algorithm",
	I2C_ALGO_PCF,
	pcf_xfer,
	NULL,
	NULL,				/* slave_xmit		*/
	NULL,				/* slave_recv		*/
	algo_control,			/* ioctl		*/
	pcf_func,			/* functionality	*/
};

/* 
 * registering functions to load algorithms at runtime 
 */
int i2c_pcf_add_bus(struct i2c_adapter *adap)
{
	int i, status;
	struct i2c_algo_pcf_data *pcf_adap = adap->algo_data;

	if (pcf_test) {
		int ret = test_bus(pcf_adap, adap->name);
		if (ret<0)
			return -ENODEV;
	}

	DEB2(printk("i2c-algo-pcf.o: hw routines for %s registered.\n",
	            adap->name));

	/* register new adapter to i2c module... */

	adap->id |= pcf_algo.id;
	adap->algo = &pcf_algo;

	adap->timeout = 100;	/* default values, should	*/
	adap->retries = 3;		/* be replaced by defines	*/

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif

	i2c_add_adapter(adap);
	pcf_init_8584(pcf_adap);

	/* scan bus */
	if (pcf_scan) {
		printk(KERN_INFO " i2c-algo-pcf.o: scanning bus %s.\n",
		       adap->name);
		for (i = 0x00; i < 0xff; i+=2) {
			i2c_outb(pcf_adap, i);
			i2c_start(pcf_adap);
			if ((wait_for_pin(pcf_adap, &status) >= 0) && 
			    ((status & I2C_PCF_LRB) == 0)) { 
				printk("(%02x)",i>>1); 
			} else {
				printk("."); 
			}
			i2c_stop(pcf_adap);
			udelay(pcf_adap->udelay);
		}
		printk("\n");
	}
	return 0;
}


int i2c_pcf_del_bus(struct i2c_adapter *adap)
{
	int res;
	if ((res = i2c_del_adapter(adap)) < 0)
		return res;
	DEB2(printk("i2c-algo-pcf.o: adapter unregistered: %s\n",adap->name));

#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}

int __init i2c_algo_pcf_init (void)
{
	printk("i2c-algo-pcf.o: i2c pcf8584 algorithm module\n");
	return 0;
}


EXPORT_SYMBOL(i2c_pcf_add_bus);
EXPORT_SYMBOL(i2c_pcf_del_bus);

#ifdef MODULE
MODULE_AUTHOR("Hans Berglund <hb@spacetec.no>");
MODULE_DESCRIPTION("I2C-Bus PCF8584 algorithm");

MODULE_PARM(pcf_test, "i");
MODULE_PARM(pcf_scan, "i");
MODULE_PARM(i2c_debug,"i");

MODULE_PARM_DESC(pcf_test, "Test if the I2C bus is available");
MODULE_PARM_DESC(pcf_scan, "Scan for active chips on the bus");
MODULE_PARM_DESC(i2c_debug,
        "debug level - 0 off; 1 normal; 2,3 more verbose; 9 pcf-protocol");


int init_module(void) 
{
	return i2c_algo_pcf_init();
}

void cleanup_module(void) 
{
}
#endif
