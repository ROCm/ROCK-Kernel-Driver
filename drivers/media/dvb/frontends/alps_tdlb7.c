/*
    Driver for Alps TDLB7 Frontend

    Copyright (C) 1999 Juergen Peitz

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
    This driver needs a copy of the firmware file 'Sc_main.mc' from the Haupauge
    windows driver in the '/usr/lib/DVB/driver/frontends' directory.
    You can also pass the complete file name with the module parameter 'firmware_file'.
    
*/  

#include <linux/module.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/delay.h>
#include <linux/syscalls.h>

#include "dvb_frontend.h"
#include "dvb_functions.h"

#ifndef CONFIG_ALPS_TDLB7_FIRMWARE_LOCATION
#define CONFIG_ALPS_TDLB7_FIRMWARE_LOCATION "/usr/lib/DVB/driver/frontends/Sc_main.mc"
#endif

static char * firmware_file = CONFIG_ALPS_TDLB7_FIRMWARE_LOCATION;
static int debug = 0;

#define dprintk	if (debug) printk

/* firmware size for sp8870 */
#define SP8870_FIRMWARE_SIZE 16382

/* starting point for firmware in file 'Sc_main.mc' */
#define SP8870_FIRMWARE_OFFSET 0x0A


static int errno;

static struct dvb_frontend_info tdlb7_info = {
	.name			= "Alps TDLB7",
	.type			= FE_OFDM,
	.frequency_min		= 470000000,
	.frequency_max		= 860000000,
	.frequency_stepsize	= 166666,
	.caps			= FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
				  FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 |
				  FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
				  FE_CAN_QPSK | FE_CAN_QAM_16 |
				  FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
				  FE_CAN_HIERARCHY_AUTO |  FE_CAN_RECOVER
};


static int sp8870_writereg (struct dvb_i2c_bus *i2c, u16 reg, u16 data)
{
        u8 buf [] = { reg >> 8, reg & 0xff, data >> 8, data & 0xff };
	struct i2c_msg msg = { .addr = 0x71, .flags = 0, .buf = buf, .len = 4 };
	int err;

        if ((err = i2c->xfer (i2c, &msg, 1)) != 1) {
		dprintk ("%s: writereg error (err == %i, reg == 0x%02x, data == 0x%02x)\n", __FUNCTION__, err, reg, data);
		return -EREMOTEIO;
	}

        return 0;
}


static u16 sp8870_readreg (struct dvb_i2c_bus *i2c, u16 reg)
{
	int ret;
	u8 b0 [] = { reg >> 8 , reg & 0xff };
	u8 b1 [] = { 0, 0 };
	struct i2c_msg msg [] = { { .addr = 0x71, .flags = 0, .buf = b0, .len = 2 },
			   { .addr = 0x71, .flags = I2C_M_RD, .buf = b1, .len = 2 } };

	ret = i2c->xfer (i2c, msg, 2);

	if (ret != 2) {
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);
		return -1;
	}

	return (b1[0] << 8 | b1[1]);
}


static int sp5659_write (struct dvb_i2c_bus *i2c, u8 data [4])
{
        int ret;
        struct i2c_msg msg = { .addr = 0x60, .flags = 0, .buf = data, .len = 4 };

        ret = i2c->xfer (i2c, &msg, 1);

        if (ret != 1)
                printk("%s: i/o error (ret == %i)\n", __FUNCTION__, ret);

        return (ret != 1) ? -1 : 0;
}


static void sp5659_set_tv_freq (struct dvb_i2c_bus *i2c, u32 freq)
{
        u32 div = (freq + 36200000) / 166666;
        u8 buf [4];
	int pwr;

	if (freq <= 782000000)
		pwr = 1;
	else 
		pwr = 2;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x85;
	buf[3] = pwr << 6;

	/* open i2c gate for PLL message transmission... */
	sp8870_writereg(i2c, 0x206, 0x001);
	sp5659_write (i2c, buf);
	sp8870_writereg(i2c, 0x206, 0x000);
}


static int sp8870_read_firmware_file (const char *fn, char **fp)
{
        int fd;
	loff_t filesize;
	char *dp;

	fd = sys_open(fn, 0, 0);
	if (fd == -1) {
                printk("%s: unable to open '%s'.\n", __FUNCTION__, fn);
		return -EIO;
	}

	filesize = sys_lseek(fd, 0L, 2);
	if (filesize <= 0 || filesize < SP8870_FIRMWARE_OFFSET + SP8870_FIRMWARE_SIZE) {
	        printk("%s: firmware filesize to small '%s'\n", __FUNCTION__, fn);
		sys_close(fd);
		return -EIO;
	}

	*fp= dp = vmalloc(SP8870_FIRMWARE_SIZE);
	if (dp == NULL)	{
		printk("%s: out of memory loading '%s'.\n", __FUNCTION__, fn);
		sys_close(fd);
		return -EIO;
	}

	sys_lseek(fd, SP8870_FIRMWARE_OFFSET, 0);
	if (sys_read(fd, dp, SP8870_FIRMWARE_SIZE) != SP8870_FIRMWARE_SIZE) {
		printk("%s: failed to read '%s'.\n",__FUNCTION__, fn);
		vfree(dp);
		sys_close(fd);
		return -EIO;
	}

	sys_close(fd);
	*fp = dp;

	return 0;
}


static int sp8870_firmware_upload (struct dvb_i2c_bus *i2c)
{
	struct i2c_msg msg;
	char *fw_buf = NULL;
	int fw_pos;
	u8 tx_buf[255];
	int tx_len;
	int err = 0;
	mm_segment_t fs = get_fs();

	dprintk ("%s: ...\n", __FUNCTION__);

	// system controller stop 
	sp8870_writereg(i2c,0x0F00,0x0000);

	// instruction RAM register hiword
	sp8870_writereg(i2c, 0x8F08, ((SP8870_FIRMWARE_SIZE / 2) & 0xFFFF));

	// instruction RAM MWR
	sp8870_writereg(i2c, 0x8F0A, ((SP8870_FIRMWARE_SIZE / 2) >> 16));

	// reading firmware file to buffer
	set_fs(get_ds());
        err = sp8870_read_firmware_file(firmware_file, (char**) &fw_buf);
	set_fs(fs);
	if (err != 0) {
		printk("%s: reading firmware file failed!\n", __FUNCTION__);
		return err;
	}

	// do firmware upload
	fw_pos = 0;
	while (fw_pos < SP8870_FIRMWARE_SIZE){
		tx_len = (fw_pos <= SP8870_FIRMWARE_SIZE - 252) ? 252 : SP8870_FIRMWARE_SIZE - fw_pos;
		// write register 0xCF0A
		tx_buf[0] = 0xCF;
		tx_buf[1] = 0x0A;
		memcpy(&tx_buf[2], fw_buf + fw_pos, tx_len);
		msg.addr=0x71;
		msg.flags=0;
		msg.buf = tx_buf;
		msg.len = tx_len + 2;
        	if ((err = i2c->xfer (i2c, &msg, 1)) != 1) {
			printk("%s: firmware upload failed!\n", __FUNCTION__);
			printk ("%s: i2c error (err == %i)\n", __FUNCTION__, err);
        		vfree(fw_buf);
			return err;
		}
		fw_pos += tx_len;
	}

	vfree(fw_buf);

	dprintk ("%s: done!\n", __FUNCTION__);
	return 0;
};


static void sp8870_microcontroller_stop (struct dvb_i2c_bus *i2c)
{
	sp8870_writereg(i2c, 0x0F08, 0x000);
	sp8870_writereg(i2c, 0x0F09, 0x000);

	// microcontroller STOP
	sp8870_writereg(i2c, 0x0F00, 0x000);
}


static void sp8870_microcontroller_start (struct dvb_i2c_bus *i2c)
{
	sp8870_writereg(i2c, 0x0F08, 0x000);
	sp8870_writereg(i2c, 0x0F09, 0x000);

	// microcontroller START
	sp8870_writereg(i2c, 0x0F00, 0x001);
	// not documented but if we don't read 0x0D01 out here
	// we don't get a correct data valid signal
	sp8870_readreg(i2c, 0x0D01);
}


static int sp8870_init (struct dvb_i2c_bus *i2c)
{
	dprintk ("%s\n", __FUNCTION__);

	/* enable TS output and interface pins */
	sp8870_writereg(i2c, 0xc18, 0x00d);

	// system controller stop 
	sp8870_microcontroller_stop(i2c);

	// ADC mode
	sp8870_writereg(i2c,0x0301,0x0003);

	// Reed Solomon parity bytes passed to output
	sp8870_writereg(i2c,0x0C13,0x0001);

	// MPEG clock is suppressed if no valid data
	sp8870_writereg(i2c,0x0C14,0x0001);

	/* bit 0x010: enable data valid signal */
	sp8870_writereg(i2c, 0x0D00, 0x010);
	sp8870_writereg(i2c, 0x0D01, 0x000);

	return 0;
}


static int sp8870_read_status (struct dvb_i2c_bus *i2c,  fe_status_t * fe_status)
{
	int status;
	int signal;

	*fe_status = 0;

	status = sp8870_readreg (i2c, 0x0200);
	if (status < 0)
		return -EIO;

	signal = sp8870_readreg (i2c, 0x0303);
	if (signal < 0)
		return -EIO;

	if (signal > 0x0F)
		*fe_status |= FE_HAS_SIGNAL;
	if (status & 0x08)
		*fe_status |= FE_HAS_SYNC;
	if (status & 0x04)
		*fe_status |= FE_HAS_LOCK | FE_HAS_CARRIER | FE_HAS_VITERBI;

	return 0;
}


static int sp8870_read_ber (struct dvb_i2c_bus *i2c, u32 * ber)
{
	int ret;
	u32 tmp;

	*ber = 0;

	ret = sp8870_readreg(i2c, 0xC08);
	if (ret < 0)
		return -EIO;

	tmp = ret & 0x3F;

	ret = sp8870_readreg(i2c, 0xC07);
	if (ret < 0)
		return -EIO;

	 tmp = ret << 6;

	if (tmp >= 0x3FFF0)
		tmp = ~0;

	*ber = tmp;

	return 0;
	}


static int sp8870_read_signal_strength (struct dvb_i2c_bus *i2c,  u16 * signal)
	{
	int ret;
	u16 tmp;

	*signal = 0;

	ret = sp8870_readreg (i2c, 0x306);
	if (ret < 0)
		return -EIO;

	tmp = ret << 8;

	ret = sp8870_readreg (i2c, 0x303);
	if (ret < 0)
		return -EIO;

	tmp |= ret;

	if (tmp)
		*signal = 0xFFFF - tmp;

	return 0;
	}


static int sp8870_read_snr(struct dvb_i2c_bus *i2c, u32* snr)
	{
                *snr=0;  
		return -EOPNOTSUPP;
	}


static int sp8870_read_uncorrected_blocks (struct dvb_i2c_bus *i2c, u32* ublocks)
	{
		int ret;

		*ublocks=0;  

		ret = sp8870_readreg(i2c, 0xC0C);
		if (ret < 0)
			return -EIO;

		if (ret == 0xFFFF)
			ret = ~0;

		*ublocks = ret;

		return 0;
	}


static int sp8870_read_data_valid_signal(struct dvb_i2c_bus *i2c)
{
	return (sp8870_readreg(i2c, 0x0D02) > 0);
}


static
int configure_reg0xc05 (struct dvb_frontend_parameters *p, u16 *reg0xc05)
{
	int known_parameters = 1;

	*reg0xc05 = 0x000;

	switch (p->u.ofdm.constellation) {
	case QPSK:
		break;
	case QAM_16:
		*reg0xc05 |= (1 << 10);
		break;
	case QAM_64:
		*reg0xc05 |= (2 << 10);
		break;
	case QAM_AUTO:
		known_parameters = 0;
		break;
	default:
		return -EINVAL;
	};

	switch (p->u.ofdm.hierarchy_information) {
	case HIERARCHY_NONE:
		break;
	case HIERARCHY_1:
		*reg0xc05 |= (1 << 7);
		break;
	case HIERARCHY_2:
		*reg0xc05 |= (2 << 7);
		break;
	case HIERARCHY_4:
		*reg0xc05 |= (3 << 7);
		break;
	case HIERARCHY_AUTO:
		known_parameters = 0;
		break;
	default:
		return -EINVAL;
	};

	switch (p->u.ofdm.code_rate_HP) {
	case FEC_1_2:
		break;
	case FEC_2_3:
		*reg0xc05 |= (1 << 3);
		break;
	case FEC_3_4:
		*reg0xc05 |= (2 << 3);
		break;
	case FEC_5_6:
		*reg0xc05 |= (3 << 3);
		break;
	case FEC_7_8:
		*reg0xc05 |= (4 << 3);
		break;
	case FEC_AUTO:
		known_parameters = 0;
		break;
	default:
		return -EINVAL;
	};

	if (known_parameters)
		*reg0xc05 |= (2 << 1);	/* use specified parameters */
	else
		*reg0xc05 |= (1 << 1);	/* enable autoprobing */

	return 0;
}


static int sp8870_set_frontend_parameters (struct dvb_i2c_bus *i2c,
				      struct dvb_frontend_parameters *p)
        {
	int  err;
	u16 reg0xc05;

	if ((err = configure_reg0xc05(p, &reg0xc05)))
		return err;

		// system controller stop 
	sp8870_microcontroller_stop(i2c);

	// set tuner parameters
		sp5659_set_tv_freq (i2c, p->frequency);

		// sample rate correction bit [23..17]
		sp8870_writereg(i2c,0x0319,0x000A);
		
		// sample rate correction bit [16..0]
		sp8870_writereg(i2c,0x031A,0x0AAB);

		// integer carrier offset 
		sp8870_writereg(i2c,0x0309,0x0400);

		// fractional carrier offset
		sp8870_writereg(i2c,0x030A,0x0000);

		// filter for 6/7/8 Mhz channel
		if (p->u.ofdm.bandwidth == BANDWIDTH_6_MHZ)
			sp8870_writereg(i2c,0x0311,0x0002);
		else if (p->u.ofdm.bandwidth == BANDWIDTH_7_MHZ)
			sp8870_writereg(i2c,0x0311,0x0001);
		else
			sp8870_writereg(i2c,0x0311,0x0000);

		// scan order: 2k first = 0x0000, 8k first = 0x0001 
		if (p->u.ofdm.transmission_mode == TRANSMISSION_MODE_2K)
			sp8870_writereg(i2c,0x0338,0x0000);
		else
			sp8870_writereg(i2c,0x0338,0x0001);

	sp8870_writereg(i2c, 0xc05, reg0xc05);

	// read status reg in order to clear pending irqs
	sp8870_readreg(i2c, 0x200);

		// system controller start
	sp8870_microcontroller_start(i2c);

	return 0;
        }


// number of trials to recover from lockup
#define MAXTRIALS 5
// maximum checks for data valid signal
#define MAXCHECKS 100

// only for debugging: counter for detected lockups
static int lockups = 0;
// only for debugging: counter for channel switches
static int switches = 0;

static int sp8870_set_frontend (struct dvb_i2c_bus *i2c, struct dvb_frontend_parameters *p)
	{
	/*
	    The firmware of the sp8870 sometimes locks up after setting frontend parameters.
	    We try to detect this by checking the data valid signal.
	    If it is not set after MAXCHECKS we try to recover the lockup by setting
	    the frontend parameters again.
	*/

	int err = 0;
	int valid = 0;
	int trials = 0;
	int check_count = 0;

	dprintk("%s: frequency = %i\n", __FUNCTION__, p->frequency);

	for (trials = 1; trials <= MAXTRIALS; trials++) {

		if ((err = sp8870_set_frontend_parameters(i2c, p)))
			return err;

		for (check_count = 0; check_count < MAXCHECKS; check_count++) {
//			valid = ((sp8870_readreg(i2c, 0x0200) & 4) == 0);
			valid = sp8870_read_data_valid_signal(i2c);
			if (valid) {
				dprintk("%s: delay = %i usec\n",
					__FUNCTION__, check_count * 10);
				break;
			}
			udelay(10);
		}
		if (valid)
		break;
	}

	if (!valid) {
		printk("%s: firmware crash!!!!!!\n", __FUNCTION__);
		return -EIO;
	}

	if (debug) {
		if (valid) {
			if (trials > 1) {
				printk("%s: firmware lockup!!!\n", __FUNCTION__);
				printk("%s: recovered after %i trial(s))\n",  __FUNCTION__, trials - 1);
				lockups++;
			}
		}
		switches++;
		printk("%s: switches = %i lockups = %i\n", __FUNCTION__, switches, lockups);
	}

	return 0;
}


static int sp8870_sleep(struct dvb_i2c_bus *i2c)
{
	// tristate TS output and disable interface pins
	return sp8870_writereg(i2c, 0xC18, 0x000);
}


static int sp8870_wake_up(struct dvb_i2c_bus *i2c)
{
	// enable TS output and interface pins
	return sp8870_writereg(i2c, 0xC18, 0x00D);
}


static int tdlb7_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct dvb_i2c_bus *i2c = fe->i2c;

        switch (cmd) {
        case FE_GET_INFO:
		memcpy (arg, &tdlb7_info, sizeof(struct dvb_frontend_info));
		break;

        case FE_READ_STATUS:
		return sp8870_read_status(i2c, (fe_status_t *) arg);

        case FE_READ_BER:
		return sp8870_read_ber(i2c, (u32 *) arg);

        case FE_READ_SIGNAL_STRENGTH:
		return sp8870_read_signal_strength(i2c, (u16 *) arg);

        case FE_READ_SNR:				// not supported by hardware?
		return sp8870_read_snr(i2c, (u32 *) arg);

	case FE_READ_UNCORRECTED_BLOCKS:
		return sp8870_read_uncorrected_blocks(i2c, (u32 *) arg);

        case FE_SET_FRONTEND:
		return sp8870_set_frontend(i2c, (struct dvb_frontend_parameters*) arg);

	case FE_GET_FRONTEND:			 // FIXME: read known values back from Hardware...
		return -EOPNOTSUPP;

        case FE_SLEEP:
		return sp8870_sleep(i2c);

        case FE_INIT:
		sp8870_wake_up(i2c);
		if (fe->data == NULL) {		// first time initialisation...
			fe->data = (void*) ~0;
			sp8870_init (i2c);
		}
		break;

	case FE_GET_TUNE_SETTINGS:
	{
	        struct dvb_frontend_tune_settings* fesettings = (struct dvb_frontend_tune_settings*) arg;
	        fesettings->min_delay_ms = 150;
	        fesettings->step_size = 166667;
	        fesettings->max_drift = 166667*2;
	        return 0;
	}	    
	    
	default:
		return -EOPNOTSUPP;
        };

        return 0;
}


static int tdlb7_attach (struct dvb_i2c_bus *i2c, void **data)
{
        u8 b0 [] = { 0x02 , 0x00 };
        u8 b1 [] = { 0, 0 };
        struct i2c_msg msg [] = { { .addr = 0x71, .flags = 0, .buf = b0, .len = 2 },
                                  { .addr = 0x71, .flags = I2C_M_RD, .buf = b1, .len = 2 } };

	dprintk ("%s\n", __FUNCTION__);

        if (i2c->xfer (i2c, msg, 2) != 2)
                return -ENODEV;

	sp8870_firmware_upload(i2c);

	return dvb_register_frontend (tdlb7_ioctl, i2c, NULL, &tdlb7_info);
}


static void tdlb7_detach (struct dvb_i2c_bus *i2c, void *data)
{
	dprintk ("%s\n", __FUNCTION__);

	dvb_unregister_frontend (tdlb7_ioctl, i2c);
}


static int __init init_tdlb7 (void)
{
	dprintk ("%s\n", __FUNCTION__);

	return dvb_register_i2c_device (THIS_MODULE, tdlb7_attach, tdlb7_detach);
}


static void __exit exit_tdlb7 (void)
{
	dprintk ("%s\n", __FUNCTION__);

	dvb_unregister_i2c_device (tdlb7_attach);
}


module_init(init_tdlb7);
module_exit(exit_tdlb7);


MODULE_PARM(debug,"i");
MODULE_PARM_DESC(debug, "enable verbose debug messages");

MODULE_PARM(firmware_file,"s");
MODULE_PARM_DESC(firmware_file, "where to find the firmware file");

MODULE_DESCRIPTION("TDLB7 DVB-T Frontend");
MODULE_AUTHOR("Juergen Peitz");
MODULE_LICENSE("GPL");


