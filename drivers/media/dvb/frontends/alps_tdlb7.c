/*
    Driver for Alps TDLB7 Frontend

    Copyright (C) 1999 Juergen Peitz <peitz@snafu.de>

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/


/* 
    
    Wrote this code mainly to get my own card running. It's working for me, but I
    hope somebody who knows more about linux programming and the DVB driver can 
    improve it.
    
    Reused a lot from the existing driver and tuner code.
    Thanks to everybody who worked on it!
    
    This driver needs a copy of the microcode file 'Sc_main.mc' from the Haupauge 
    windows driver in the 'usr/lib/DVB/driver/frontends' directory.  
    You can also pass the complete file name with the module parameter 'mcfile'.
    
    The code only needs to be loaded once after a power on. Because loading the 
    microcode to the card takes some time, you can use the 'loadcode=0' module 
    parameter, if you only want to reload the dvb driver.      
    
    Juergen Peitz
    
*/  



#define __KERNEL_SYSCALLS__
#include <linux/module.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/unistd.h>

#include "compat.h"
#include "dvb_frontend.h"

static int debug = 0;

static int loadcode = 1;

static char * mcfile = "/usr/lib/DVB/driver/frontends/Sc_main.mc";

#define dprintk	if (debug) printk

/* microcode size for sp8870 */
#define SP8870_CODE_SIZE 16384

/* starting point for microcode in file 'Sc_main.mc' */
#define SP8870_CODE_OFFSET 0x0A


static int errno;

static
struct dvb_frontend_info tdlb7_info = {
	name: "Alps TDLB7",
	type: FE_OFDM,
	frequency_min: 470000000,
	frequency_max: 860000000,
	frequency_stepsize: 166666,
#if 0
    	frequency_tolerance: ???,
	symbol_rate_min: ???,
	symbol_rate_max: ???,
	symbol_rate_tolerance: ???,
	notifier_delay: 0,
#endif
	caps: FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
	      FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
	      FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64
};


static
int sp8870_writereg (struct dvb_i2c_bus *i2c, u16 reg, u16 data)
{
        u8 buf [] = { reg >> 8, reg & 0xff, data >> 8, data & 0xff };
	struct i2c_msg msg = { addr: 0x71, flags: 0, buf: buf, len: 4 };
	int err;

        if ((err = i2c->xfer (i2c, &msg, 1)) != 1) {
		dprintk ("%s: writereg error (err == %i, reg == 0x%02x, data == 0x%02x)\n", __FUNCTION__, err, reg, data);
		return -EREMOTEIO;
	}

        return 0;
}


static
u16 sp8870_readreg (struct dvb_i2c_bus *i2c, u16 reg)
{
	int ret;
	u8 b0 [] = { reg >> 8 , reg & 0xff };
	u8 b1 [] = { 0, 0 };
	struct i2c_msg msg [] = { { addr: 0x71, flags: 0, buf: b0, len: 2 },
			   { addr: 0x71, flags: I2C_M_RD, buf: b1, len: 2 } };

	ret = i2c->xfer (i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return (b1[0] << 8 | b1[1]);
}


static
int sp5659_write (struct dvb_i2c_bus *i2c, u8 data [4])
{
        int ret;
        struct i2c_msg msg = { addr: 0x60, flags: 0, buf: data, len: 4 };

        ret = i2c->xfer (i2c, &msg, 1);

        if (ret != 1)
                printk("%s: i/o error (ret == %i)\n", __FUNCTION__, ret);

        return (ret != 1) ? -1 : 0;
}


static
int sp5659_set_tv_freq (struct dvb_i2c_bus *i2c, u32 freq, u8 pwr)
{
        u32 div = (freq + 36200000) / 166666;
        u8 buf [4] = { (div >> 8) & 0x7f, div & 0xff, 0x85, (pwr << 5) | 0x30 };

	return sp5659_write (i2c, buf);
}


static
int sp8870_read_code(const char *fn, char **fp)
{
        int fd;
	loff_t l;
	char *dp;

	fd = open(fn, 0, 0);
	if (fd == -1) {
                printk(KERN_INFO "%s: Unable to load '%s'.\n", __FUNCTION__, fn);
		return -1;
	}
	l = lseek(fd, 0L, 2);
	if (l <= 0 || l < SP8870_CODE_OFFSET+SP8870_CODE_SIZE) {
	        printk(KERN_INFO "%s: code file too small '%s'\n", __FUNCTION__, fn);
		sys_close(fd);
		return -1;
	}
	lseek(fd, SP8870_CODE_OFFSET, 0);
	*fp= dp = vmalloc(SP8870_CODE_SIZE);
	if (dp == NULL)	{
		printk(KERN_INFO "%s: Out of memory loading '%s'.\n", __FUNCTION__, fn);
		sys_close(fd);
		return -1;
	}
	if (read(fd, dp, SP8870_CODE_SIZE) != SP8870_CODE_SIZE) {
		printk(KERN_INFO "%s: Failed to read '%s'.\n",__FUNCTION__, fn);
		vfree(dp);
		sys_close(fd);
		return -1;
	}
	sys_close(fd);
	*fp = dp;
	return 0;
}


static 
int sp8870_load_code(struct dvb_i2c_bus *i2c)
{
	/* this takes a long time. is there a way to do it faster? */
	char *lcode;
	struct i2c_msg msg;
	unsigned char buf[255];
	int err;
	int p=0;
	int c;
	mm_segment_t fs = get_fs();

	sp8870_writereg(i2c,0x8F08,0x1FFF);
	sp8870_writereg(i2c,0x8F0A,0x0000);

	set_fs(get_ds());
        if (sp8870_read_code(mcfile,(char**) &lcode)<0) return -1;
	set_fs(fs);
	while (p<SP8870_CODE_SIZE){
		c = (p<=SP8870_CODE_SIZE-252) ? 252 : SP8870_CODE_SIZE-p;
		buf[0]=0xCF;
		buf[1]=0x0A;
		memcpy(&buf[2],lcode+p,c);
		c+=2;
		msg.addr=0x71;
		msg.flags=0;
		msg.buf=buf;
		msg.len=c;
        	if ((err = i2c->xfer (i2c, &msg, 1)) != 1) {
			dprintk ("%s: i2c error (err == %i)\n", __FUNCTION__, err);
        		vfree(lcode);
			return -EREMOTEIO;
		}

		p+=252;
	}
        vfree(lcode);
	return 0;
};


static
int sp8870_init (struct dvb_i2c_bus *i2c)
{

	dprintk ("%s\n", __FUNCTION__);

	sp8870_readreg(i2c,0x200);
     	sp8870_readreg(i2c,0x200);
	sp8870_readreg(i2c,0x0F00);	/* system controller stop */
	sp8870_readreg(i2c,0x0301);	/* ???????? */
	sp8870_readreg(i2c,0x0309);	/* integer carrier offset */
	sp8870_readreg(i2c,0x030A);	/* fractional carrier offset */
	sp8870_readreg(i2c,0x0311);	/* filter for 8 Mhz channel */
	sp8870_readreg(i2c,0x0319);	/* sample rate correction bit [23..17] */
	sp8870_readreg(i2c,0x031A);	/* sample rate correction bit [16..0] */
	sp8870_readreg(i2c,0x0338);	/* ???????? */
	sp8870_readreg(i2c,0x0F00);
	sp8870_readreg(i2c,0x0200);

	if (loadcode) {
		dprintk("%s: loading mcfile '%s' !\n", __FUNCTION__, mcfile);
		if (sp8870_load_code(i2c)==0)
		    dprintk("%s: microcode loaded!\n", __FUNCTION__);
	}else{
		dprintk("%s: without loading mcfile!\n", __FUNCTION__);
	}

	return 0;
}


static
int sp8870_reset (struct dvb_i2c_bus *i2c)
{
	dprintk("%s\n", __FUNCTION__);
	sp8870_writereg(i2c,0x0F00,0x0000);	/* system controller stop */
	sp8870_writereg(i2c,0x0301,0x0003);	/* ???????? */
	sp8870_writereg(i2c,0x0309,0x0400);	/* integer carrier offset */
	sp8870_writereg(i2c,0x030A,0x0000);	/* fractional carrier offset */
	sp8870_writereg(i2c,0x0311,0x0000);	/* filter for 8 Mhz channel */
	sp8870_writereg(i2c,0x0319,0x000A);	/* sample rate correction bit [23..17] */
	sp8870_writereg(i2c,0x031A,0x0AAB);	/* sample rate correction bit [16..0] */
	sp8870_writereg(i2c,0x0338,0x0000);	/* ???????? */
	sp8870_writereg(i2c,0x0201,0x0000);	/* interrupts for change of lock or tuner adjustment disabled */
	sp8870_writereg(i2c,0x0F00,0x0001);	/* system controller start */

        return 0;
}

static
int tdlb7_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct dvb_i2c_bus *i2c = fe->i2c;

        switch (cmd) {
        case FE_GET_INFO:
		memcpy (arg, &tdlb7_info, sizeof(struct dvb_frontend_info));
		break;

        case FE_READ_STATUS:
	{
		fe_status_t *status = arg;
		int sync = sp8870_readreg (i2c, 0x0200);

		*status=0;

		if (sync&0x04) // FIXME: find criteria for having signal
			*status |= FE_HAS_SIGNAL;

		if (sync&0x04) // FIXME: find criteria
			*status |= FE_HAS_CARRIER;

		if (sync&0x04) // FIXME
			*status |= FE_HAS_VITERBI;

		if (sync&0x08) // FIXME
			*status |= FE_HAS_SYNC;

		if (sync&0x04)
			*status |= FE_HAS_LOCK;
		break;

	}

        case FE_READ_BER:
	{
		u32 *ber=(u32 *) arg;
		*ber=sp8870_readreg(i2c,0x0C07);  // bit error rate before Viterbi
		break;

	}

        case FE_READ_SIGNAL_STRENGTH:		// not supported by hardware?
	{
		s32 *signal=(s32 *) arg;
                *signal=0;
		break;
	}

        case FE_READ_SNR:			// not supported by hardware?
	{
		s32 *snr=(s32 *) arg;
                *snr=0;  
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS: 	// not supported by hardware?
	{
		u32 *ublocks=(u32 *) arg;
		*ublocks=0;  
		break;
	}

        case FE_SET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;
		sp5659_set_tv_freq (i2c, p->frequency, 0);
		 			// all other parameters are set by the on card
					// system controller. Don't know how to pass 
					// distinct values to the card.
		break;			
        }

	case FE_GET_FRONTEND: 		// how to do this?
	{
		break;
	}

        case FE_SLEEP:			// is this supported by hardware?
		return -EOPNOTSUPP;

        case FE_INIT:
		return sp8870_init (i2c);

	case FE_RESET:
		return sp8870_reset (i2c);

	default:
		return -EOPNOTSUPP;
        };

        return 0;
}


static
int tdlb7_attach (struct dvb_i2c_bus *i2c)
{

	struct i2c_msg msg = { addr: 0x71, flags: 0, buf: NULL, len: 0 };

	dprintk ("%s\n", __FUNCTION__);

	if (i2c->xfer (i2c, &msg, 1) != 1)
                return -ENODEV;

	dvb_register_frontend (tdlb7_ioctl, i2c, NULL, &tdlb7_info);

	return 0;
}


static
void tdlb7_detach (struct dvb_i2c_bus *i2c)
{
	dprintk ("%s\n", __FUNCTION__);

	dvb_unregister_frontend (tdlb7_ioctl, i2c);
}


static
int __init init_tdlb7 (void)
{
	dprintk ("%s\n", __FUNCTION__);

	return dvb_register_i2c_device (THIS_MODULE, tdlb7_attach, tdlb7_detach);
}


static
void __exit exit_tdlb7 (void)
{
	dprintk ("%s\n", __FUNCTION__);

	dvb_unregister_i2c_device (tdlb7_attach);
}


module_init(init_tdlb7);
module_exit(exit_tdlb7);


MODULE_PARM(debug,"i");
MODULE_PARM_DESC(debug, "enable verbose debug messages");

MODULE_PARM(loadcode,"i");
MODULE_PARM_DESC(loadcode, "load tuner microcode");

MODULE_PARM(mcfile,"s");
MODULE_PARM_DESC(mcfile, "where to find the microcode file");

MODULE_DESCRIPTION("TDLB7 DVB-T Frontend");
MODULE_AUTHOR("Juergen Peitz");
MODULE_LICENSE("GPL");


