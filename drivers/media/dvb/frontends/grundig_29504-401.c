/* 
    driver for Grundig 29504-401 DVB-T Frontends based on
    LSI L64781 COFDM demodulator as used in Technotrend DVB-T cards

    Copyright (C) 2001 Holger Waechtler <holger@convergence.de>
                       for Convergence Integrated Media GmbH
                       Marko Kohtala <marko.kohtala@nokia.com>

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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "dvb_functions.h"

static int debug = 0;

#define dprintk	if (debug) printk


struct dvb_frontend_info grundig_29504_401_info = {
	.name = "Grundig 29504-401",
	.type = FE_OFDM,
/*	.frequency_min = ???,*/
/*	.frequency_max = ???,*/
	.frequency_stepsize = 166666,
/*      .frequency_tolerance = ???,*/
/*      .symbol_rate_tolerance = ???,*/
	.notifier_delay = 0,
	.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
	      FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
	      FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
	      FE_CAN_MUTE_TS /*| FE_CAN_CLEAN_SETUP*/
};


static int l64781_writereg (struct dvb_i2c_bus *i2c, u8 reg, u8 data)
{
	int ret;
	u8 buf [] = { reg, data };
	struct i2c_msg msg = { .addr = 0x55, .flags = 0, .buf = buf, .len = 2 };

	if ((ret = i2c->xfer (i2c, &msg, 1)) != 1)
		dprintk ("%s: write_reg error (reg == %02x) = %02x!\n",
			 __FUNCTION__, reg, ret);

	return (ret != 1) ? -1 : 0;
}


static u8 l64781_readreg (struct dvb_i2c_bus *i2c, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = 0x55, .flags = 0, .buf = b0, .len = 1 },
			   { .addr = 0x55, .flags = I2C_M_RD, .buf = b1, .len = 1 } };

	ret = i2c->xfer (i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];
}


static int tsa5060_write (struct dvb_i2c_bus *i2c, u8 data [4])
{
	int ret;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = 4 };

	if ((ret = i2c->xfer (i2c, &msg, 1)) != 1)
		dprintk ("%s: write_reg error == %02x!\n", __FUNCTION__, ret);

	return (ret != 1) ? -1 : 0;
}


/**
 *   set up the downconverter frequency divisor for a
 *   reference clock comparision frequency of 166666 Hz.
 *   frequency offset is 36125000 Hz.
 */
static int tsa5060_set_tv_freq (struct dvb_i2c_bus *i2c, u32 freq)
{
	u32 div;
	u8 buf [4];
	u8 cfg, cpump, band_select;

	div = (36125000 + freq) / 166666;
	cfg = 0x88;

	cpump = freq < 175000000 ? 2 : freq < 390000000 ? 1 :
		freq < 470000000 ? 2 : freq < 750000000 ? 1 : 3;

	band_select = freq < 175000000 ? 0x0e : freq < 470000000 ? 0x05 : 0x03;

	buf [0] = (div >> 8) & 0x7f;
	buf [1] = div & 0xff;
	buf [2] = ((div >> 10) & 0x60) | cfg;
	buf [3] = (cpump << 6) | band_select;

	return tsa5060_write (i2c, buf);
}


static void apply_tps (struct dvb_i2c_bus *i2c)
{
	l64781_writereg (i2c, 0x2a, 0x00);
	l64781_writereg (i2c, 0x2a, 0x01);

	/* This here is a little bit questionable because it enables
	   the automatic update of TPS registers. I think we'd need to
	   handle the IRQ from FE to update some other registers as
	   well, or at least implement some magic to tuning to correct
	   to the TPS received from transmission. */
	l64781_writereg (i2c, 0x2a, 0x02);
}


static void reset_afc (struct dvb_i2c_bus *i2c)
{
	/* Set AFC stall for the AFC_INIT_FRQ setting, TIM_STALL for
	   timing offset */
	l64781_writereg (i2c, 0x07, 0x9e); /* stall AFC */
	l64781_writereg (i2c, 0x08, 0);    /* AFC INIT FREQ */
	l64781_writereg (i2c, 0x09, 0);
	l64781_writereg (i2c, 0x0a, 0);
	l64781_writereg (i2c, 0x07, 0x8e);
	l64781_writereg (i2c, 0x0e, 0);    /* AGC gain to zero in beginning */
	l64781_writereg (i2c, 0x11, 0x80); /* stall TIM */
	l64781_writereg (i2c, 0x10, 0);    /* TIM_OFFSET_LSB */
	l64781_writereg (i2c, 0x12, 0);
	l64781_writereg (i2c, 0x13, 0);
	l64781_writereg (i2c, 0x11, 0x00);
}


static int apply_frontend_param (struct dvb_i2c_bus *i2c,
			  struct dvb_frontend_parameters *param)
{
	/* The coderates for FEC_NONE, FEC_4_5 and FEC_FEC_6_7 are arbitrary */
	static const u8 fec_tab[] = { 7, 0, 1, 2, 9, 3, 10, 4 };
	/* QPSK, QAM_16, QAM_64 */
	static const u8 qam_tab [] = { 2, 4, 0, 6 };
	static const u8 bw_tab [] = { 8, 7, 6 };  /* 8Mhz, 7MHz, 6MHz */
	static const u8 guard_tab [] = { 1, 2, 4, 8 };
	/* The Grundig 29504-401.04 Tuner comes with 18.432MHz crystal. */
	static const u32 ppm = 8000;
	struct dvb_ofdm_parameters *p = &param->u.ofdm;
	u32 ddfs_offset_fixed;
/*	u32 ddfs_offset_variable = 0x6000-((1000000UL+ppm)/ */
/*			bw_tab[p->bandWidth]<<10)/15625; */
	u32 init_freq;
	u32 spi_bias;
	u8 val0x04;
	u8 val0x05;
	u8 val0x06;
	int bw = p->bandwidth - BANDWIDTH_8_MHZ;

	if (param->inversion != INVERSION_ON &&
	    param->inversion != INVERSION_OFF)
		return -EINVAL;

	if (bw < 0 || bw > 2)
		return -EINVAL;
	
	if (p->code_rate_HP != FEC_1_2 && p->code_rate_HP != FEC_2_3 &&
	    p->code_rate_HP != FEC_3_4 && p->code_rate_HP != FEC_5_6 &&
	    p->code_rate_HP != FEC_7_8)
		return -EINVAL;

	if (p->hierarchy_information != HIERARCHY_NONE &&
	    (p->code_rate_LP != FEC_1_2 && p->code_rate_LP != FEC_2_3 &&
	     p->code_rate_LP != FEC_3_4 && p->code_rate_LP != FEC_5_6 &&
	     p->code_rate_LP != FEC_7_8))
		return -EINVAL;

	if (p->constellation != QPSK && p->constellation != QAM_16 &&
	    p->constellation != QAM_64)
		return -EINVAL;

	if (p->transmission_mode != TRANSMISSION_MODE_2K &&
	    p->transmission_mode != TRANSMISSION_MODE_8K)
		return -EINVAL;

	if (p->guard_interval < GUARD_INTERVAL_1_32 ||
	    p->guard_interval > GUARD_INTERVAL_1_4)
		return -EINVAL;

	if (p->hierarchy_information < HIERARCHY_NONE ||
	    p->hierarchy_information > HIERARCHY_4)
		return -EINVAL;

	ddfs_offset_fixed = 0x4000-(ppm<<16)/bw_tab[p->bandwidth]/1000000;

	/* This works up to 20000 ppm, it overflows if too large ppm! */
	init_freq = (((8UL<<25) + (8UL<<19) / 25*ppm / (15625/25)) /
			bw_tab[p->bandwidth] & 0xFFFFFF);

	/* SPI bias calculation is slightly modified to fit in 32bit */
	/* will work for high ppm only... */
	spi_bias = 378 * (1 << 10);
	spi_bias *= 16;
	spi_bias *= bw_tab[p->bandwidth];
	spi_bias *= qam_tab[p->constellation];
	spi_bias /= p->code_rate_HP + 1;
	spi_bias /= (guard_tab[p->guard_interval] + 32);
	spi_bias *= 1000ULL;
	spi_bias /= 1000ULL + ppm/1000;
	spi_bias *= p->code_rate_HP;

	val0x04 = (p->transmission_mode << 2) | p->guard_interval;
	val0x05 = fec_tab[p->code_rate_HP];

	if (p->hierarchy_information != HIERARCHY_NONE)
		val0x05 |= (p->code_rate_LP - FEC_1_2) << 3;

	val0x06 = (p->hierarchy_information << 2) | p->constellation;

	l64781_writereg (i2c, 0x04, val0x04);
	l64781_writereg (i2c, 0x05, val0x05);
	l64781_writereg (i2c, 0x06, val0x06);

	reset_afc (i2c);

	/* Technical manual section 2.6.1, TIM_IIR_GAIN optimal values */
	l64781_writereg (i2c, 0x15,
			 p->transmission_mode == TRANSMISSION_MODE_2K ? 1 : 3);
	l64781_writereg (i2c, 0x16, init_freq & 0xff);
	l64781_writereg (i2c, 0x17, (init_freq >> 8) & 0xff);
	l64781_writereg (i2c, 0x18, (init_freq >> 16) & 0xff);

	l64781_writereg (i2c, 0x1b, spi_bias & 0xff);
	l64781_writereg (i2c, 0x1c, (spi_bias >> 8) & 0xff);
	l64781_writereg (i2c, 0x1d, ((spi_bias >> 16) & 0x7f) |
		(param->inversion == INVERSION_ON ? 0x80 : 0x00));

	l64781_writereg (i2c, 0x22, ddfs_offset_fixed & 0xff);
	l64781_writereg (i2c, 0x23, (ddfs_offset_fixed >> 8) & 0x3f);

	l64781_readreg (i2c, 0x00);  /*  clear interrupt registers... */
	l64781_readreg (i2c, 0x01);  /*  dto. */

	apply_tps (i2c);

	return 0;
}


static int reset_and_configure (struct dvb_i2c_bus *i2c)
{
	u8 buf [] = { 0x06 };
	struct i2c_msg msg = { .addr = 0x00, .flags = 0, .buf = buf, .len = 1 };

	return (i2c->xfer (i2c, &msg, 1) == 1) ? 0 : -ENODEV;
}



static int init (struct dvb_i2c_bus *i2c)
{
        reset_and_configure (i2c);

	/* Power up */
	l64781_writereg (i2c, 0x3e, 0xa5);

	/* Reset hard */
	l64781_writereg (i2c, 0x2a, 0x04);
	l64781_writereg (i2c, 0x2a, 0x00);

	/* Set tuner specific things */
	/* AFC_POL, set also in reset_afc */
	l64781_writereg (i2c, 0x07, 0x8e);

	/* Use internal ADC */
	l64781_writereg (i2c, 0x0b, 0x81);

	/* AGC loop gain, and polarity is positive */
	l64781_writereg (i2c, 0x0c, 0x84);

	/* Internal ADC outputs two's complement */
	l64781_writereg (i2c, 0x0d, 0x8c);

	/* With ppm=8000, it seems the DTR_SENSITIVITY will result in
           value of 2 with all possible bandwidths and guard
           intervals, which is the initial value anyway. */
        /*l64781_writereg (i2c, 0x19, 0x92);*/

	/* Everything is two's complement, soft bit and CSI_OUT too */
	l64781_writereg (i2c, 0x1e, 0x09);

	return 0;
}


static 
int grundig_29504_401_ioctl (struct dvb_frontend *fe,
			     unsigned int cmd, void *arg)
{
	struct dvb_i2c_bus *i2c = fe->i2c;
        switch (cmd) {
        case FE_GET_INFO:
		memcpy (arg, &grundig_29504_401_info,
			sizeof(struct dvb_frontend_info));
                break;

	case FE_READ_STATUS:
	{
		fe_status_t *status = (fe_status_t *) arg;
		int sync = l64781_readreg (i2c, 0x32);
		int gain = l64781_readreg (i2c, 0x0e);

		l64781_readreg (i2c, 0x00);  /*  clear interrupt registers... */
		l64781_readreg (i2c, 0x01);  /*  dto. */

		*status = 0;

		if (gain > 5)
			*status |= FE_HAS_SIGNAL;

		if (sync & 0x02) /* VCXO locked, this criteria should be ok */
			*status |= FE_HAS_CARRIER;

		if (sync & 0x20)
			*status |= FE_HAS_VITERBI;

		if (sync & 0x40)
			*status |= FE_HAS_SYNC;

		if (sync == 0x7f)
			*status |= FE_HAS_LOCK;

		break;
	}

        case FE_READ_BER:
	{
		/*   XXX FIXME: set up counting period (reg 0x26...0x28)
		 */
		u32 *ber = (u32 *) arg;
		*ber = l64781_readreg (i2c, 0x39)
		    | (l64781_readreg (i2c, 0x3a) << 8);
		break;
	}

        case FE_READ_SIGNAL_STRENGTH:
	{
		u8 gain = l64781_readreg (i2c, 0x0e);
		*(u16 *) arg = (gain << 8) | gain;
		break;
	}

        case FE_READ_SNR:
	{
		u16 *snr = (u16 *) arg;
		u8 avg_quality = 0xff - l64781_readreg (i2c, 0x33);
		*snr = (avg_quality << 8) | avg_quality; /* not exact, but...*/ 
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS: 
	{
		u32 *ub = (u32 *) arg;
		*ub = l64781_readreg (i2c, 0x37)
		   | (l64781_readreg (i2c, 0x38) << 8);
		break;
	}

        case FE_SET_FRONTEND:
	{
		struct dvb_frontend_parameters *p = arg;

		tsa5060_set_tv_freq (i2c, p->frequency);
		return apply_frontend_param (i2c, p);
	}
        case FE_GET_FRONTEND:
		/*  we could correct the frequency here, but...
		 *  (...do you want to implement this?;)
		 */
		return 0;

	case FE_SLEEP:
		/* Power down */
		return l64781_writereg (i2c, 0x3e, 0x5a);

	case FE_INIT:
		return init (i2c);

        default:
		dprintk ("%s: unknown command !!!\n", __FUNCTION__);
		return -EINVAL;
        };
        
        return 0;
} 


static int l64781_attach (struct dvb_i2c_bus *i2c, void **data)
{
	u8 reg0x3e;
	u8 b0 [] = { 0x1a };
	u8 b1 [] = { 0x00 };
	struct i2c_msg msg [] = { { .addr = 0x55, .flags = 0, .buf = b0, .len = 1 },
			   { .addr = 0x55, .flags = I2C_M_RD, .buf = b1, .len = 1 } };

	/**
	 *  the L64781 won't show up before we send the reset_and_configure()
	 *  broadcast. If nothing responds there is no L64781 on the bus...
	 */
	if (reset_and_configure(i2c) < 0) {
		dprintk("no response on reset_and_configure() broadcast, bailing out...\n");
		return -ENODEV;
	}

	/* The chip always responds to reads */
	if (i2c->xfer(i2c, msg, 2) != 2) {  
	        dprintk("no response to read on I2C bus\n");
		return -ENODEV;
	}

	/* Save current register contents for bailout */
	reg0x3e = l64781_readreg(i2c, 0x3e);

	/* Reading the POWER_DOWN register always returns 0 */
	if (reg0x3e != 0) {
	        dprintk("Device doesn't look like L64781\n");
		return -ENODEV;
	}

	/* Turn the chip off */
	l64781_writereg (i2c, 0x3e, 0x5a);

	/* Responds to all reads with 0 */
	if (l64781_readreg(i2c, 0x1a) != 0) {
 	        dprintk("Read 1 returned unexpcted value\n");
	        goto bailout;
	}	  

	/* Turn the chip on */
	l64781_writereg (i2c, 0x3e, 0xa5);
	
	/* Responds with register default value */
	if (l64781_readreg(i2c, 0x1a) != 0xa1) { 
 	        dprintk("Read 2 returned unexpcted value\n");
	        goto bailout;
	}

	return dvb_register_frontend (grundig_29504_401_ioctl, i2c, NULL,
			       &grundig_29504_401_info);

 bailout:
	l64781_writereg (i2c, 0x3e, reg0x3e);  /* restore reg 0x3e */
	return -ENODEV;
}



static void l64781_detach (struct dvb_i2c_bus *i2c, void *data)
{
	dvb_unregister_frontend (grundig_29504_401_ioctl, i2c);
}


static int __init init_grundig_29504_401 (void)
{
	return dvb_register_i2c_device (THIS_MODULE,
					l64781_attach, l64781_detach);
}


static void __exit exit_grundig_29504_401 (void)
{
	dvb_unregister_i2c_device (l64781_attach);
}

module_init(init_grundig_29504_401);
module_exit(exit_grundig_29504_401);

MODULE_PARM(debug,"i");
MODULE_PARM_DESC(debug, "enable verbose debug messages");
MODULE_DESCRIPTION("Grundig 29504-401 DVB-T Frontend");
MODULE_AUTHOR("Holger Waechtler, Marko Kohtala");
MODULE_LICENSE("GPL");

