/* 
    Alps TDMB7 DVB OFDM frontend driver

    Copyright (C) 2001-2002 Convergence Integrated Media GmbH
	Holger Waechtler <holger@convergence.de>

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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "dvb_functions.h"


static int debug = 0;
#define dprintk	if (debug) printk


static struct dvb_frontend_info tdmb7_info = {
	.name 			= "Alps TDMB7",
	.type 			= FE_OFDM,
	.frequency_min 		= 470000000,
	.frequency_max 		= 860000000,
	.frequency_stepsize 	= 166667,
#if 0
    	.frequency_tolerance 	= ???,
	.symbol_rate_min 	= ???,
	.symbol_rate_max 	= ???,
	.symbol_rate_tolerance	= 500,  /* ppm */
	.notifier_delay 	= 0,
#endif
	.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
	      FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
	      FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | 
              FE_CAN_RECOVER
};


static u8 init_tab [] = {
	0x04, 0x10,
	0x05, 0x09,
	0x06, 0x00,
	0x08, 0x04,
	0x09, 0x00,
	0x0a, 0x01,
	0x15, 0x40,
	0x16, 0x10,
	0x17, 0x87,
	0x18, 0x17,
	0x1a, 0x10,
	0x25, 0x04,
	0x2e, 0x00,
	0x39, 0x00,
	0x3a, 0x04,
	0x45, 0x08,
	0x46, 0x02,
	0x47, 0x05,
};


static int cx22700_writereg (struct dvb_i2c_bus *i2c, u8 reg, u8 data)
{
	int ret;
	u8 buf [] = { reg, data };
	struct i2c_msg msg = { .addr = 0x43, .flags = 0, .buf = buf, .len = 2 };

	dprintk ("%s\n", __FUNCTION__);

	ret = i2c->xfer (i2c, &msg, 1);

	if (ret != 1) 
		printk("%s: writereg error (reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			__FUNCTION__, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}


static u8 cx22700_readreg (struct dvb_i2c_bus *i2c, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = 0x43, .flags = 0, .buf = b0, .len = 1 },
			   { .addr = 0x43, .flags = I2C_M_RD, .buf = b1, .len = 1 } };
        
	dprintk ("%s\n", __FUNCTION__);

	ret = i2c->xfer (i2c, msg, 2);
        
	if (ret != 2) 
		printk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];
}


static int pll_write (struct dvb_i2c_bus *i2c, u8 data [4])
{
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = 4 };
	int ret;

	cx22700_writereg (i2c, 0x0a, 0x00);  /* open i2c bus switch */
	ret = i2c->xfer (i2c, &msg, 1);
	cx22700_writereg (i2c, 0x0a, 0x01);  /* close i2c bus switch */

	if (ret != 1)
		printk("%s: i/o error (addr == 0x%02x, ret == %i)\n", __FUNCTION__, msg.addr, ret);

	return (ret != 1) ? -1 : 0;
}


/**
 *   set up the downconverter frequency divisor for a 
 *   reference clock comparision frequency of 125 kHz.
 */
static int pll_set_tv_freq (struct dvb_i2c_bus *i2c, u32 freq)
{
	u32 div = (freq + 36166667) / 166667;
#if 1 //ALPS_SETTINGS
	u8 buf [4] = { (div >> 8) & 0x7f, div & 0xff, ((div >> 10) & 0x60) | 0x85,
		       freq < 592000000 ? 0x40 : 0x80 };
#else
	u8 buf [4] = { (div >> 8) & 0x7f, div & 0xff, ((div >> 10) & 0x60) | 0x85,
		       freq < 470000000 ? 0x42 : freq < 862000000 ? 0x41 : 0x81 };
#endif

	dprintk ("%s: freq == %i, div == %i\n", __FUNCTION__, (int) freq, (int) div);

	return pll_write (i2c, buf);
}


static int cx22700_init (struct dvb_i2c_bus *i2c)
{
	int i;

	dprintk("cx22700_init: init chip\n");

	cx22700_writereg (i2c, 0x00, 0x02);   /*  soft reset */
	cx22700_writereg (i2c, 0x00, 0x00);

	dvb_delay(10);
	
	for (i=0; i<sizeof(init_tab); i+=2)
		cx22700_writereg (i2c, init_tab[i], init_tab[i+1]);

	cx22700_writereg (i2c, 0x00, 0x01);
	
	return 0;
}


static int cx22700_set_inversion (struct dvb_i2c_bus *i2c, int inversion)
{
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	switch (inversion) {
	case INVERSION_AUTO:
		return -EOPNOTSUPP;
	case INVERSION_ON:
		val = cx22700_readreg (i2c, 0x09);
		return cx22700_writereg (i2c, 0x09, val | 0x01);
	case INVERSION_OFF:
		val = cx22700_readreg (i2c, 0x09);
		return cx22700_writereg (i2c, 0x09, val & 0xfe);
	default:
		return -EINVAL;
	}
}


static int cx22700_set_tps (struct dvb_i2c_bus *i2c, struct dvb_ofdm_parameters *p)
{
	static const u8 qam_tab [4] = { 0, 1, 0, 2 };
	static const u8 fec_tab [6] = { 0, 1, 2, 0, 3, 4 };
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	if (p->code_rate_HP < FEC_1_2 || p->code_rate_HP > FEC_7_8)
		return -EINVAL;

	if (p->code_rate_LP < FEC_1_2 || p->code_rate_LP > FEC_7_8)

	if (p->code_rate_HP == FEC_4_5 || p->code_rate_LP == FEC_4_5)
		return -EINVAL;

	if (p->guard_interval < GUARD_INTERVAL_1_32 ||
	    p->guard_interval > GUARD_INTERVAL_1_4)
		return -EINVAL;

	if (p->transmission_mode != TRANSMISSION_MODE_2K &&
	    p->transmission_mode != TRANSMISSION_MODE_8K)
		return -EINVAL;

	if (p->constellation != QPSK &&
	    p->constellation != QAM_16 &&
	    p->constellation != QAM_64)
		return -EINVAL;

	if (p->hierarchy_information < HIERARCHY_NONE ||
	    p->hierarchy_information > HIERARCHY_4)
		return -EINVAL;

	if (p->bandwidth < BANDWIDTH_8_MHZ && p->bandwidth > BANDWIDTH_6_MHZ)
		return -EINVAL;

	if (p->bandwidth == BANDWIDTH_7_MHZ)
		cx22700_writereg (i2c, 0x09, cx22700_readreg (i2c, 0x09 | 0x10));
	else
		cx22700_writereg (i2c, 0x09, cx22700_readreg (i2c, 0x09 & ~0x10));

	val = qam_tab[p->constellation - QPSK];
	val |= p->hierarchy_information - HIERARCHY_NONE;

	cx22700_writereg (i2c, 0x04, val);

	val = fec_tab[p->code_rate_HP - FEC_1_2] << 3;
	val |= fec_tab[p->code_rate_LP - FEC_1_2];

	cx22700_writereg (i2c, 0x05, val);

	val = (p->guard_interval - GUARD_INTERVAL_1_32) << 2;
	val |= p->transmission_mode - TRANSMISSION_MODE_2K;

	cx22700_writereg (i2c, 0x06, val);

	cx22700_writereg (i2c, 0x08, 0x04 | 0x02);  /* use user tps parameters */
	cx22700_writereg (i2c, 0x08, 0x04);         /* restart aquisition */

	return 0;
}


static int cx22700_get_tps (struct dvb_i2c_bus *i2c, struct dvb_ofdm_parameters *p)
{
	static const fe_modulation_t qam_tab [3] = { QPSK, QAM_16, QAM_64 };
	static const fe_code_rate_t fec_tab [5] = { FEC_1_2, FEC_2_3, FEC_3_4,
						    FEC_5_6, FEC_7_8 };
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	if (!(cx22700_readreg(i2c, 0x07) & 0x20))  /*  tps valid? */
		return -EAGAIN;

	val = cx22700_readreg (i2c, 0x01);

	if ((val & 0x7) > 4)
		p->hierarchy_information = HIERARCHY_AUTO;
	else
		p->hierarchy_information = HIERARCHY_NONE + (val & 0x7);

	if (((val >> 3) & 0x3) > 2)
		p->constellation = QAM_AUTO;
	else
		p->constellation = qam_tab[(val >> 3) & 0x3];


	val = cx22700_readreg (i2c, 0x02);

	if (((val >> 3) & 0x07) > 4)
		p->code_rate_HP = FEC_AUTO;
	else
		p->code_rate_HP = fec_tab[(val >> 3) & 0x07];

	if ((val & 0x07) > 4)
		p->code_rate_LP = FEC_AUTO;
	else
		p->code_rate_LP = fec_tab[val & 0x07];


	val = cx22700_readreg (i2c, 0x03);

	p->guard_interval = GUARD_INTERVAL_1_32 + ((val >> 6) & 0x3);
	p->transmission_mode = TRANSMISSION_MODE_2K + ((val >> 5) & 0x1);

	return 0;
}


static int tdmb7_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct dvb_i2c_bus *i2c = fe->i2c;

	dprintk ("%s\n", __FUNCTION__);

	switch (cmd) {
	case FE_GET_INFO:
		memcpy (arg, &tdmb7_info, sizeof(struct dvb_frontend_info));
		break;

	case FE_READ_STATUS:
	{
		fe_status_t *status = (fe_status_t *) arg;
		u16 rs_ber = (cx22700_readreg (i2c, 0x0d) << 9)
			   | (cx22700_readreg (i2c, 0x0e) << 1);
		u8 sync = cx22700_readreg (i2c, 0x07);

		*status = 0;

		if (rs_ber < 0xff00)
			*status |= FE_HAS_SIGNAL;

		if (sync & 0x20)
			*status |= FE_HAS_CARRIER;

		if (sync & 0x10)
			*status |= FE_HAS_VITERBI;

		if (sync & 0x10)
			*status |= FE_HAS_SYNC;

		if (*status == 0x0f)
			*status |= FE_HAS_LOCK;

		break;
	}

        case FE_READ_BER:
		*((u32*) arg) = cx22700_readreg (i2c, 0x0c) & 0x7f;
		cx22700_writereg (i2c, 0x0c, 0x00);
		break;

	case FE_READ_SIGNAL_STRENGTH:
	{
		u16 rs_ber = (cx22700_readreg (i2c, 0x0d) << 9)
			   | (cx22700_readreg (i2c, 0x0e) << 1);
		*((u16*) arg) = ~rs_ber;
		break;
	}
        case FE_READ_SNR:
	{
		u16 rs_ber = (cx22700_readreg (i2c, 0x0d) << 9)
			   | (cx22700_readreg (i2c, 0x0e) << 1);
		*((u16*) arg) = ~rs_ber;
		break;
	}
	case FE_READ_UNCORRECTED_BLOCKS: 
		*((u32*) arg) = cx22700_readreg (i2c, 0x0f);
		cx22700_writereg (i2c, 0x0f, 0x00);
		break;

        case FE_SET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;

		cx22700_writereg (i2c, 0x00, 0x02); /* XXX CHECKME: soft reset*/
		cx22700_writereg (i2c, 0x00, 0x00);

		pll_set_tv_freq (i2c, p->frequency);
		cx22700_set_inversion (i2c, p->inversion);
                cx22700_set_tps (i2c, &p->u.ofdm);
		cx22700_writereg (i2c, 0x37, 0x01);  /* PAL loop filter off */
		cx22700_writereg (i2c, 0x00, 0x01);  /* restart acquire */
                break;
        }

        case FE_GET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;
		u8 reg09 = cx22700_readreg (i2c, 0x09);
		
		p->inversion = reg09 & 0x1 ? INVERSION_ON : INVERSION_OFF;
		return cx22700_get_tps (i2c, &p->u.ofdm);
        }

        case FE_INIT:
		return cx22700_init (i2c);

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



static int tdmb7_attach (struct dvb_i2c_bus *i2c, void **data)
{
        u8 b0 [] = { 0x7 };
        u8 b1 [] = { 0 };
        struct i2c_msg msg [] = { { .addr = 0x43, .flags = 0, .buf = b0, .len = 1 },
                                  { .addr = 0x43, .flags = I2C_M_RD, .buf = b1, .len = 1 } };

	dprintk ("%s\n", __FUNCTION__);

        if (i2c->xfer (i2c, msg, 2) != 2)
                return -ENODEV;

	return dvb_register_frontend (tdmb7_ioctl, i2c, NULL, &tdmb7_info);
}


static void tdmb7_detach (struct dvb_i2c_bus *i2c, void *data)
{
	dprintk ("%s\n", __FUNCTION__);

	dvb_unregister_frontend (tdmb7_ioctl, i2c);
}


static int __init init_tdmb7 (void)
{
	dprintk ("%s\n", __FUNCTION__);

	return dvb_register_i2c_device (THIS_MODULE, tdmb7_attach, tdmb7_detach);
}


static void __exit exit_tdmb7 (void)
{
	dprintk ("%s\n", __FUNCTION__);

	dvb_unregister_i2c_device (tdmb7_attach);
}

module_init (init_tdmb7);
module_exit (exit_tdmb7);

MODULE_PARM(debug,"i");
MODULE_PARM_DESC(debug, "enable verbose debug messages");
MODULE_DESCRIPTION("TDMB7 DVB Frontend driver");
MODULE_AUTHOR("Holger Waechtler");
MODULE_LICENSE("GPL");

