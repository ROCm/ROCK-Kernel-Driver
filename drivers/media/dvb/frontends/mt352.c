/*
 *  Driver for Zarlink DVB-T MT352 demodulator
 *
 *  Written by Holger Waechtler <holger@qanu.de>
 *	 and Daniel Mack <daniel@qanu.de>
 *
 *  AVerMedia AVerTV DVB-T 771 support by
 *       Wolfram Joost <dbox2@frokaschwei.de>
 *
 *  Support for Samsung TDTC9251DH01C(M) tuner
 *  Copyright (C) 2004 Antonio Mancuso <antonio.mancuso@digitaltelevision.it>
 *                     Amauri  Celani  <acelani@essegi.net>
 *
 *  DVICO FusionHDTV DVB-T1 and DVICO FusionHDTV DVB-T Lite support by
 *       Christopher Pascoe <c.pascoe@itee.uq.edu.au>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>

#include "dvb_frontend.h"
#include "mt352.h"

#define FRONTEND_NAME "dvbfe_mt352"

#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG FRONTEND_NAME ": " args); \
	} while (0)

static int debug;
#define MAX_CARDS	4
static int force_card[MAX_CARDS] = { -1, -1, -1, -1 };
static int force_card_count = 0;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");
module_param_array(force_card, int, &force_card_count, 0444);
MODULE_PARM_DESC(force_card, "Forces the type of each attached mt352 frontend.\n\t"
		 "If your card is not autodetected, then you must specify its type here.\n\t"
		 "Valid card types are: 0 == AVDVBT771, 1 == TUA6034, 2 == TDTC9251DH01C,\n\t"
		 "3 == DVICO FusionHDTV DVB-T1, 4 == DVICO FusionHDTV DVB-T Lite.");

struct mt352_state {
	struct i2c_adapter *i2c;
	struct dvb_adapter *dvb;
	struct dvb_frontend_info fe_info;
	int card_type;
};

#define mt352_write(ibuf, ilen)						\
do {									\
	struct i2c_msg msg = { .addr = I2C_MT352_ADDR, .flags = 0,	\
			       .buf = ibuf, .len = ilen };		\
	int err = i2c_transfer(i2c, &msg, 1);				\
	if (err != 1) {							\
		printk(KERN_WARNING					\
		       "mt352_write() failed (err = %d)!\n", err);	\
		return err;						\
	}								\
} while (0)

static struct _tuner_info tuner_info [] = {
	{
	  .fe_name = "AverMedia DVB-T 771",
	  .fe_frequency_min = 174000000,
	  .fe_frequency_max = 862000000,
	  .fe_frequency_stepsize = 166667,
	  .pll_i2c_addr = 0xc2,
	  .mt352_init = mt352_init_AVERMEDIA771,
	  .mt352_charge_pump = mt352_cp_AVERMEDIA771,
	  .mt352_band_select = mt352_bs_AVERMEDIA771
	},
	{
	  .fe_name = "Zarlink MT352 + TUA6034 DVB-T",
	  .fe_frequency_min = 174000000,
	  .fe_frequency_max = 862000000,
	  .fe_frequency_stepsize = 166667,
	  .pll_i2c_addr = 0xc2,
	  .mt352_init = mt352_init_TUA6034,
	  .mt352_charge_pump = mt352_cp_TUA6034,
	  .mt352_band_select = mt352_bs_TUA6034
	},
	{
	  .fe_name = "Zarlink MT352 + Samsung TDTC9251DH01C DVB-T",
	  .fe_frequency_min = 474000000,
	  .fe_frequency_max = 858000000,
	  .fe_frequency_stepsize = 166667,
	  .pll_i2c_addr = 0xc2,
	  .mt352_init = mt352_init_TDTC9251DH01C,
	  .mt352_charge_pump = mt352_cp_TDTC9251DH01C,
	  .mt352_band_select = mt352_bs_TDTC9251DH01C
	},
	{
	  .fe_name = "DVICO FusionHDTV DVB-T1",
	  .fe_frequency_min = 174000000,
	  .fe_frequency_max = 862000000,
	  .fe_frequency_stepsize = 166667,
	  .pll_i2c_addr = 0xc2,
	  .mt352_init = mt352_init_DVICODVBT1,
	  .mt352_charge_pump = mt352_cp_DVICODVBT1,
	  .mt352_band_select = mt352_bs_DVICODVBT1,
	},
	{
	  .fe_name = "DVICO FusionHDTV DVB-T Lite",
	  .fe_frequency_min = 174000000,
	  .fe_frequency_max = 862000000,
	  .fe_frequency_stepsize = 166667,
	  .pll_i2c_addr = 0xc0,
	  .mt352_init = mt352_init_DVICODVBTLITE,
	  .mt352_charge_pump = mt352_cp_DVICODVBTLITE,
	  .mt352_band_select = mt352_bs_DVICODVBTLITE,
	}
};

static struct dvb_frontend_info mt352_info_template = {
	.name			= "DVB-T Zarlink MT352 demodulator driver",
	.type			= FE_OFDM,
/*
	.frequency_min		= 0,
	.frequency_max		= 0,
	.frequency_stepsize	= 0,
	.frequency_tolerance	= 0,
	.symbol_rate_min	= 1000000,
	.symbol_rate_max	= 45000000,
	.symbol_rate_tolerance	= ???,
*/
	.notifier_delay		 = 0,
	.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
		FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
		FE_CAN_FEC_AUTO |
		FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
		FE_CAN_HIERARCHY_AUTO | FE_CAN_RECOVER |
		FE_CAN_MUTE_TS
};

static u8 mt352_reset [] = { RESET, 0x80 };
static u8 mt352_adc_ctl_1_cfg [] = { ADC_CTL_1, 0x40 };
static u8 mt352_capt_range_cfg[] = { CAPT_RANGE, 0x32 };

static int mt352_init_TUA6034(struct i2c_adapter *i2c)
{
	static u8 mt352_clock_config [] = { CLOCK_CTL, 0x38, 0x2d };
	static u8 mt352_agc_cfg [] = { AGC_TARGET, 0x19, 0xa0 };

	mt352_write(mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(mt352_reset, sizeof(mt352_reset));
	mt352_write(mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(mt352_agc_cfg, sizeof(mt352_agc_cfg));
	mt352_write(mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int mt352_init_AVERMEDIA771(struct i2c_adapter *i2c)
{
	static u8 mt352_clock_config [] = { CLOCK_CTL, 0x38, 0x2d };
	static u8 mt352_agc_cfg [] = { AGC_TARGET, 0x10, 0x23, 0x00, 0xFF, 0xFF,
				       0x00, 0xFF, 0x00, 0x40, 0x40 };
	static u8 mt352_av771_extra[] = { 0xB5, 0x7A };
	static u8 mt352_capt_range_cfg[] = { CAPT_RANGE, 0x32 };

	mt352_write(mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(mt352_reset, sizeof(mt352_reset));
	mt352_write(mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(mt352_agc_cfg,sizeof(mt352_agc_cfg));
	udelay(2000);
	mt352_write(mt352_av771_extra,sizeof(mt352_av771_extra));
	mt352_write(mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int mt352_init_TDTC9251DH01C(struct i2c_adapter *i2c)
{
	static u8 mt352_clock_config [] = { CLOCK_CTL, 0x10, 0x2d };
	static u8 mt352_agc_cfg [] = { AGC_TARGET, 0x28, 0xa1 };

	mt352_write(mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(mt352_reset, sizeof(mt352_reset));
	mt352_write(mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(mt352_agc_cfg, sizeof(mt352_agc_cfg));
	mt352_write(mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int mt352_init_DVICODVBT1(struct i2c_adapter *i2c)
{
	static u8 mt352_clock_config [] = { CLOCK_CTL, 0x38, 0x39 };
	static u8 mt352_agc_cfg [] = { AGC_TARGET, 0x24, 0x20 };
	static u8 mt352_gpp_ctl_cfg [] = { GPP_CTL, 0x33 };

	mt352_write(mt352_clock_config, sizeof(mt352_clock_config));
	udelay(200);
	mt352_write(mt352_reset, sizeof(mt352_reset));
	mt352_write(mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(mt352_agc_cfg, sizeof(mt352_agc_cfg));
	mt352_write(mt352_gpp_ctl_cfg, sizeof(mt352_gpp_ctl_cfg));
	mt352_write(mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int mt352_init_DVICODVBTLITE(struct i2c_adapter *i2c)
{
	static u8 mt352_clock_config [] = { CLOCK_CTL, 0x38, 0x38 };
	static u8 mt352_agc_cfg [] = { AGC_TARGET, 0x28, 0x20 };
	static u8 mt352_gpp_ctl_cfg [] = { GPP_CTL, 0x33 };

	mt352_write(mt352_clock_config, sizeof(mt352_clock_config));
	udelay(200);
	mt352_write(mt352_reset, sizeof(mt352_reset));
	mt352_write(mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(mt352_agc_cfg, sizeof(mt352_agc_cfg));
	mt352_write(mt352_gpp_ctl_cfg, sizeof(mt352_gpp_ctl_cfg));
	mt352_write(mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static unsigned char mt352_cp_TUA6034(u32 freq)
{
	unsigned char cp = 0;

	if (freq < 542000000)
		cp = 0xbe;
	else if (freq < 830000000)
		cp = 0xf6;
	else
		cp = 0xfe;

	return cp;
}

static unsigned char mt352_cp_AVERMEDIA771(u32 freq)
{
	unsigned char cp = 0;

	if (freq < 150000000)
		cp = 0xB4;
	else if (freq < 173000000)
		cp = 0xBC;
	else if (freq < 250000000)
		cp = 0xB4;
	else if (freq < 400000000)
		cp = 0xBC;
	else if (freq < 420000000)
		cp = 0xF4;
	else if (freq < 470000000)
		cp = 0xFC;
	else if (freq < 600000000)
		cp = 0xBC;
	else if (freq < 730000000)
		cp = 0xF4;
	else
		cp = 0xFC;

	return cp;
}

static unsigned char mt352_cp_TDTC9251DH01C(u32 freq)
{
	return(0xcc);
}

static unsigned char mt352_cp_DVICODVBT1(u32 freq)
{
	unsigned char cp = 0;

	if (freq < 542000000)
		cp = 0xbc;
	else if (freq < 830000000)
		cp = 0xf4;
	else
		cp = 0xfc;

	return cp;
}

static unsigned char mt352_cp_DVICODVBTLITE(u32 freq)
{
	unsigned char cp = 0;

	if (freq < 542000000)
		cp = 0xb4;
	else if (freq < 771000000)
		cp = 0xbc;
	else 
		cp = 0xf4;

	return cp;
}

static unsigned char mt352_bs_TUA6034(u32 freq)
{
	unsigned char bs = 0;

	if (freq < 250000000)
		bs = 0x01;
	else
		bs = 0x08;

	return bs;
}

static unsigned char mt352_bs_AVERMEDIA771(u32 freq)
{
	unsigned char bs = 0;

	if (freq < 150000000)
		bs = 0x01;
	else if (freq < 173000000)
		bs = 0x01;
	else if (freq < 250000000)
		bs = 0x02;
	else if (freq < 400000000)
		bs = 0x02;
	else if (freq < 420000000)
		bs = 0x02;
	else if (freq < 470000000)
		bs = 0x02;
	else if (freq < 600000000)
		bs = 0x08;
	else if (freq < 730000000)
		bs = 0x08;
	else
		bs = 0x08;

	return bs;
}

static unsigned char mt352_bs_TDTC9251DH01C(u32 freq)
{
	unsigned char bs = 0;

	if (freq >= 48000000 && freq <= 154000000)      /* low band */
		bs = 0x09;

	if (freq >= 161000000 && freq <= 439000000)     /* medium band */
		bs = 0x0a;

	if (freq >= 447000000 && freq <= 863000000)     /* high band */
		bs = 0x08;

	return bs;
}

static unsigned char mt352_bs_DVICODVBT1(u32 freq)
{
	unsigned char bs = 0;

	if (freq == 0)			/* power down PLL */
		bs = 0x03;
	else if (freq < 157500000)	/* low band */
		bs = 0x01;
	else if (freq < 443250000)	/* mid band */
		bs = 0x02;
	else				/* high band */
		bs = 0x04;

	return bs;
}

static unsigned char mt352_bs_DVICODVBTLITE(u32 freq)
{
	unsigned char bs = 0;

	if (freq == 0)			/* power down PLL */
		bs = 0x03;
	else if (freq < 443250000)	/* mid band */
		bs = 0x02;
	else				/* high band */
		bs = 0x08;

	return bs;
}

static u32 mt352_read_eeprom_dword(struct i2c_adapter *i2c, int dword_base)
	{
	int i;
	u32 dword = 0;
	u8 reg, val;
	struct i2c_msg msg[2] = {
		{
			.addr = 0x50,
			.flags = 0,
			.buf = &reg,
			.len = 1
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.buf = &val,
			.len = 1
		}
	};

	for (i = 0; i < 4; i++) {
		reg = dword_base + i;
		if (i2c_transfer(i2c,msg,2) != 2)
			return 0;
		dword = (dword << 8) | val;
	}

	return dword;
}

static int mt352_init(struct i2c_adapter *i2c, int card_type)
{
	/**
	 *  all register write sequence have the register address of the
	 *  first register in the first byte, thenafter the value to write
	 *  into this and the following registers.
	 *
	 *
	 *  We only write non-default settings, all default settings are
	 *  restored by the full mt352_reset sequence.
	 *
	 *
	 *  The optimal AGC target value and slope might vary from tuner
	 *  type to tuner type, so check whether you need to adjust this one...
	 **/

	return(MT352_INIT(i2c));
}

static int mt352_sleep(struct i2c_adapter *i2c)
{
	static u8 mt352_softdown[] = { CLOCK_CTL, 0x20, 0x08 };

	mt352_write(mt352_softdown, sizeof(mt352_softdown));

	return 0;
}

static int mt352_set_parameters(struct i2c_adapter *i2c,
				struct dvb_frontend_parameters *param,
				int card_type)
{
	unsigned char buf[14];
	unsigned int tps = 0;
	struct dvb_ofdm_parameters *op = &param->u.ofdm;
	uint16_t tmp;
	int i;

	switch (op->code_rate_HP) {
		case FEC_2_3:
			tps |= (1 << 7);
			break;
		case FEC_3_4:
			tps |= (2 << 7);
			break;
		case FEC_5_6:
			tps |= (3 << 7);
			break;
		case FEC_7_8:
			tps |= (4 << 7);
			break;
		case FEC_1_2:
		case FEC_AUTO:
			break;
		default:
			return -EINVAL;
	}

	switch (op->code_rate_LP) {
		case FEC_2_3:
			tps |= (1 << 4);
			break;
		case FEC_3_4:
			tps |= (2 << 4);
			break;
		case FEC_5_6:
			tps |= (3 << 4);
			break;
		case FEC_7_8:
			tps |= (4 << 4);
			break;
		case FEC_1_2:
		case FEC_AUTO:
			break;
		case FEC_NONE:
			if (op->hierarchy_information == HIERARCHY_AUTO ||
			    op->hierarchy_information == HIERARCHY_NONE)
				break;
		default:
			return -EINVAL;
	}

	switch (op->constellation) {
		case QPSK:
			break;
		case QAM_AUTO:
		case QAM_16:
			tps |= (1 << 13);
			break;
		case QAM_64:
			tps |= (2 << 13);
			break;
		default:
			return -EINVAL;
	}

	switch (op->transmission_mode) {
		case TRANSMISSION_MODE_2K:
		case TRANSMISSION_MODE_AUTO:
			break;
		case TRANSMISSION_MODE_8K:
			tps |= (1 << 0);
			break;
		default:
			return -EINVAL;
	}

	switch (op->guard_interval) {
		case GUARD_INTERVAL_1_32:
		case GUARD_INTERVAL_AUTO:
			break;
		case GUARD_INTERVAL_1_16:
			tps |= (1 << 2);
			break;
		case GUARD_INTERVAL_1_8:
			tps |= (2 << 2);
			break;
		case GUARD_INTERVAL_1_4:
			tps |= (3 << 2);
			break;
		default:
			return -EINVAL;
	}

	switch (op->hierarchy_information) {
		case HIERARCHY_AUTO:
		case HIERARCHY_NONE:
			break;
		case HIERARCHY_1:
			tps |= (1 << 10);
			break;
		case HIERARCHY_2:
			tps |= (2 << 10);
			break;
		case HIERARCHY_4:
			tps |= (3 << 10);
			break;
		default:
			return -EINVAL;
	}


	buf[0] = TPS_GIVEN_1; /* TPS_GIVEN_1 and following registers */

	buf[1] = msb(tps);      /* TPS_GIVEN_(1|0) */
	buf[2] = lsb(tps);

	buf[3] = 0x50;

	/**
	 *  these settings assume 20.48MHz f_ADC, for other tuners you might
	 *  need other values. See p. 33 in the MT352 Design Manual.
	 */
	if (op->bandwidth == BANDWIDTH_8_MHZ) {
		buf[4] = 0x72;  /* TRL_NOMINAL_RATE_(1|0) */
		buf[5] = 0x49;
	} else if (op->bandwidth == BANDWIDTH_7_MHZ) {
		buf[4] = 0x64;
		buf[5] = 0x00;
	} else {		/* 6MHz */
		buf[4] = 0x55;
		buf[5] = 0xb7;
	}

	buf[6] = 0x31;  /* INPUT_FREQ_(1|0), 20.48MHz clock, 36.166667MHz IF */
	buf[7] = 0x05;  /* see MT352 Design Manual page 32 for details */

	buf[8] = PLL_I2C_ADDR;

	/**
	 *  All the following settings are tuner module dependent,
	 *  check the datasheet...
	 */

	/* here we assume 1/6MHz == 166.66kHz stepsize */
	#define IF_FREQUENCYx6 217    /* 6 * 36.16666666667MHz */
	tmp = (((param->frequency + 83333) * 3) / 500000) + IF_FREQUENCYx6;

	buf[9] = msb(tmp);      /* CHAN_START_(1|0) */
	buf[10] = lsb(tmp);

	buf[11] = MT352_CHARGE_PUMP(param->frequency);
	buf[12] = MT352_BAND_SELECT(param->frequency);

	buf[13] = 0x01; /* TUNER_GO!! */

	/* Only send the tuning request if the tuner doesn't have the requested
	 * parameters already set.  Enhances tuning time and prevents stream
	 * breakup when retuning the same transponder. */
	for (i = 1; i < 13; i++)
		if (buf[i] != mt352_read_register(i2c, i + 0x50)) {
	mt352_write(buf, sizeof(buf));
			break;
		}

	return 0;
}

static u8 mt352_read_register(struct i2c_adapter *i2c, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = I2C_MT352_ADDR,
				    .flags = 0,
				    .buf = b0, .len = 1 },
				  { .addr = I2C_MT352_ADDR,
				    .flags = I2C_M_RD,
				    .buf = b1, .len = 1 } };

	ret = i2c_transfer(i2c, msg, 2);

	if (ret != 2)
		printk(KERN_WARNING
		       "%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];
}


static int mt352_get_parameters(struct i2c_adapter *i2c,
				struct dvb_frontend_parameters *param)
{
	u16 tps;
	u16 div;
	u8 trl;
	struct dvb_ofdm_parameters *op = &param->u.ofdm;
	static const u8 tps_fec_to_api[8] =
	{
		FEC_1_2,
		FEC_2_3,
		FEC_3_4,
		FEC_5_6,
		FEC_7_8,
		FEC_AUTO,
		FEC_AUTO,
		FEC_AUTO
	};

	if ( (mt352_read_register(i2c,0x00) & 0xC0) != 0xC0 )
	{
		return -EINVAL;
	}

	/* Use TPS_RECEIVED-registers, not the TPS_CURRENT-registers because
	 * the mt352 sometimes works with the wrong parameters
	 */
	tps = (mt352_read_register(i2c,	TPS_RECEIVED_1) << 8) | mt352_read_register(i2c, TPS_RECEIVED_0);
	div = (mt352_read_register(i2c, CHAN_START_1) << 8) | mt352_read_register(i2c, CHAN_START_0);
	trl = mt352_read_register(i2c, TRL_NOMINAL_RATE_1);

	op->code_rate_HP = tps_fec_to_api[(tps >> 7) & 7];
	op->code_rate_LP = tps_fec_to_api[(tps >> 4) & 7];

	switch ( (tps >> 13) & 3)
	{
		case 0:
			op->constellation = QPSK;
			break;
		case 1:
			op->constellation = QAM_16;
			break;
		case 2:
			op->constellation = QAM_64;
			break;
		default:
			op->constellation = QAM_AUTO;
			break;
	}

	op->transmission_mode = (tps & 0x01) ? TRANSMISSION_MODE_8K : TRANSMISSION_MODE_2K;

	switch ( (tps >> 2) & 3)
	{
		case 0:
			op->guard_interval = GUARD_INTERVAL_1_32;
			break;
		case 1:
			op->guard_interval = GUARD_INTERVAL_1_16;
			break;
		case 2:
			op->guard_interval = GUARD_INTERVAL_1_8;
			break;
		case 3:
			op->guard_interval = GUARD_INTERVAL_1_4;
			break;
		default:
			op->guard_interval = GUARD_INTERVAL_AUTO;
			break;
	}

	switch ( (tps >> 10) & 7)
	{
		case 0:
			op->hierarchy_information = HIERARCHY_NONE;
			break;
		case 1:
			op->hierarchy_information = HIERARCHY_1;
			break;
		case 2:
			op->hierarchy_information = HIERARCHY_2;
			break;
		case 3:
			op->hierarchy_information = HIERARCHY_4;
			break;
		default:
			op->hierarchy_information = HIERARCHY_AUTO;
			break;
	}

	param->frequency = ( 500 * (div - IF_FREQUENCYx6) ) / 3 * 1000;

	if (trl == 0x72)
	{
		op->bandwidth = BANDWIDTH_8_MHZ;
	}
	else if (trl == 0x64)
	{
		op->bandwidth = BANDWIDTH_7_MHZ;
	}
	else
	{
		op->bandwidth = BANDWIDTH_6_MHZ;
	}


	if (mt352_read_register(i2c, STATUS_2) & 0x02)
		param->inversion = INVERSION_OFF;
	else
		param->inversion = INVERSION_ON;

	return 0;
}


static int mt352_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct mt352_state *state = fe->data;
	struct i2c_adapter *i2c = state->i2c;
	int card_type = state->card_type;
	u8 r,snr;
	fe_status_t *status;
	u16 signal;
	struct dvb_frontend_tune_settings *fe_tune_settings;

	switch (cmd) {
	case FE_GET_INFO:
		memcpy(arg, &state->fe_info, sizeof(struct dvb_frontend_info));
		break;

	case FE_READ_STATUS:
		status = arg;
		*status = 0;
		r = mt352_read_register (i2c, STATUS_0);
		if (r & (1 << 4))
			*status = FE_HAS_CARRIER;
		if (r & (1 << 1))
			*status |= FE_HAS_VITERBI;
		if (r & (1 << 5))
			*status |= FE_HAS_LOCK;

		r = mt352_read_register (i2c, STATUS_1);
		if (r & (1 << 1))
			*status |= FE_HAS_SYNC;

		r = mt352_read_register (i2c, STATUS_3);
		if (r & (1 << 6))
			*status |= FE_HAS_SIGNAL;

		break;

	case FE_READ_BER:
		*((u32 *) arg) = (mt352_read_register (i2c, RS_ERR_CNT_2) << 16) |
		       (mt352_read_register (i2c, RS_ERR_CNT_1) << 8) |
		       (mt352_read_register (i2c, RS_ERR_CNT_0));
		break;

	case FE_READ_SIGNAL_STRENGTH:
		signal = (mt352_read_register (i2c, AGC_GAIN_3) << 8) |
			     (mt352_read_register (i2c, AGC_GAIN_2));
		*((u16*) arg) = ~signal;
		break;

	case FE_READ_SNR:
		snr = mt352_read_register (i2c, SNR);
		*((u16*) arg) = (snr << 8) | snr;
		break;

	case FE_READ_UNCORRECTED_BLOCKS:
		*(u32*) arg = (mt352_read_register (i2c,  RS_UBC_1) << 8) |
			      (mt352_read_register (i2c,  RS_UBC_0));
		break;

	case FE_SET_FRONTEND:
		return mt352_set_parameters (i2c,
				 (struct dvb_frontend_parameters *) arg,
				 card_type);

	case FE_GET_FRONTEND:
		return mt352_get_parameters (i2c,
				 (struct dvb_frontend_parameters *) arg);

	case FE_GET_TUNE_SETTINGS:
		fe_tune_settings = (struct dvb_frontend_tune_settings *) arg;
		fe_tune_settings->min_delay_ms = 800;
		fe_tune_settings->step_size = 0;
		fe_tune_settings->max_drift = 0;
		break;

	case FE_SLEEP:
		return mt352_sleep(i2c);

	case FE_INIT:
		/* Only send the initialisation command if the demodulator
		 * isn't already enabled.  Greatly enhances tuning time. */
		if ((mt352_read_register(i2c, CLOCK_CTL) & 0x10) == 0 ||
		    (mt352_read_register(i2c, CONFIG) & 0x20) == 0)
			return mt352_init(i2c, card_type);
		else
			return 0;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct i2c_client client_template;

static int mt352_attach_adapter(struct i2c_adapter *i2c)
{
	static int num_cards_probed;
	struct mt352_state *state;
	struct i2c_client *client;
	static u8 mt352_reset_attach [] = { RESET, 0xC0 };
	int ret;
	int card_type, forced_card = -1;

	dprintk("Trying to attach to adapter 0x%x:%s.\n",
		i2c->id, i2c->name);

	if (mt352_read_register(i2c, CHIP_ID) != ID_MT352)
		return -ENODEV;

	if ( !(state = kmalloc(sizeof(struct mt352_state), GFP_KERNEL)) )
		return -ENOMEM;

	memset(state, 0, sizeof(struct mt352_state));
	state->i2c = i2c;
	state->card_type = -1;
	memcpy(&state->fe_info, &mt352_info_template, sizeof(struct dvb_frontend_info));

	/* Attempt autodetection of card type based on PCI ID information
	 * stored in any on-board EEPROM. */
	switch (mt352_read_eeprom_dword(i2c, 0xFC)) {	/* BT878A chipset */
	case 0x07711461:
		state->card_type = CARD_AVDVBT771;
		break;
	case 0xdb1018ac:
		state->card_type = CARD_DVICODVBTLITE;
		break;
	default:
		break;
	}

	switch (mt352_read_eeprom_dword(i2c, 0x04)) {	/* CX2388x chipset */
	case 0xac1800db:
		state->card_type = CARD_DVICODVBT1;
		break;
	default:
		break;
	}

	if (num_cards_probed < force_card_count)
		forced_card = force_card[num_cards_probed++];

	if (state->card_type == -1 && forced_card < 0) {
		dprintk("Card type not automatically detected.  You "
			"must use the 'force_card' module parameter.\n");
		kfree(state);
		return -ENODEV;
	}

	if (forced_card >= 0) {
		if (state->card_type >= 0 && forced_card != state->card_type)
			printk(KERN_WARNING FRONTEND_NAME ": Warning, overriding"
					    " detected card type.\n");
		state->card_type = forced_card;
	}

	card_type = state->card_type;
	printk(KERN_INFO FRONTEND_NAME ": Setup for %s\n", FE_NAME);

	/* set the frontend name and card-specific frequency info */
	strlcpy(state->fe_info.name, FE_NAME, sizeof(state->fe_info.name));
	state->fe_info.frequency_min = FE_FREQ_MIN;
	state->fe_info.frequency_max = FE_FREQ_MAX;
	state->fe_info.frequency_stepsize = FE_FREQ_STEPSIZE;

	/* Do a "hard" reset */
	mt352_write(mt352_reset_attach, sizeof(mt352_reset_attach));

	/* Try to intiialise the device */
	if (mt352_init(i2c, card_type) != 0) {
		kfree(state);
		return -ENODEV;
	}

	if ( !(client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL)) ) {
		kfree(state);
		return -ENOMEM;
	}

	memcpy(client, &client_template, sizeof(struct i2c_client));
	client->adapter = i2c;
	client->addr = 0; // XXX
	i2c_set_clientdata(client, state);

	if ((ret = i2c_attach_client(client))) {
		kfree(client);
		kfree(state);
		return ret;
	}

	return 0;
}

static int mt352_detach_client(struct i2c_client *client)
{
	struct mt352_state *state = i2c_get_clientdata(client);

	if (state->dvb)
	dvb_unregister_frontend (mt352_ioctl, state->dvb);
	i2c_detach_client(client);
	kfree(client);
	kfree(state);
	return 0;
}

static int mt352_command (struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct mt352_state *state = i2c_get_clientdata(client);
	int ret;

	switch (cmd) {
	case FE_REGISTER:
		if (!state->dvb) {
			if ((ret = dvb_register_frontend(mt352_ioctl, arg,
							 state, &state->fe_info,
							 THIS_MODULE)))
				return ret;
		state->dvb = arg;
		}
		break;
	case FE_UNREGISTER:
		if (state->dvb == arg) {
			dvb_unregister_frontend(mt352_ioctl, state->dvb);
		state->dvb = NULL;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static struct i2c_driver driver = {
	.owner 		= THIS_MODULE,
	.name 		= FRONTEND_NAME,
	.id 		= I2C_DRIVERID_DVBFE_MT352,
	.flags 		= I2C_DF_NOTIFY,
	.attach_adapter = mt352_attach_adapter,
	.detach_client 	= mt352_detach_client,
	.command 	= mt352_command,
};

static struct i2c_client client_template = {
	.name		= FRONTEND_NAME,
	.flags 		= I2C_CLIENT_ALLOW_USE,
	.driver  	= &driver,
};

static int __init mt352_module_init(void)
{
	return i2c_add_driver(&driver);
}

static void __exit mt352_module_exit(void)
{
	if (i2c_del_driver(&driver))
		printk(KERN_ERR "mt352: driver deregistration failed.\n");
}

module_init(mt352_module_init);
module_exit(mt352_module_exit);

MODULE_DESCRIPTION("DVB-T MT352 Zarlink");
MODULE_AUTHOR("Holger Waechtler, Daniel Mack, Antonio Mancuso");
MODULE_LICENSE("GPL");

