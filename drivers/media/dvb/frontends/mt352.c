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
 *
 *  Copyright (C) 2004 Antonio Mancuso <antonio.mancuso@digitaltelevision.it>
 *                     Amauri  Celani  <acelani@essegi.net>
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
static int force_card = -1;
static int card_type = -1;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");
//FIXME: Should be an array.
module_param(force_card, int, 0444);
MODULE_PARM_DESC(force_card, "Force card type.\n\t(0 == AVDVBT771, 1 == TUA6034, "
		 "2 == TDTC9251DH01C).\n\tDefault is that AVDVBT771 is attempted "
		 "to be autodetected,\n\tif you do not have this card, you must "
		 "specify the card type here.");


struct mt352_state {
	struct i2c_adapter *i2c;
	struct dvb_adapter *dvb;
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

// FIXME:
static struct _tuner_info tuner_info [] = {
	//AVERMEDIA 771 board
	{
	  .fe_frequency_min = 174000000,
	  .fe_frequency_max = 862000000,
	  .fe_frequency_stepsize = 83333,
	  .coderate_hp_shift = 7,
	  .coderate_lp_shift = 4,
	  .constellation_shift = 13,
	  .tx_mode_shift = 0,
	  .guard_interval_shift = 2,
	  .hierarchy_shift = 10,
	  .read_reg_flag = I2C_M_NOSTART,
	  .mt352_init = mt352_init_AVERMEDIA771,
	  .mt352_charge_pump = mt352_cp_AVERMEDIA771,
	  .mt352_band_select = mt352_bs_AVERMEDIA771
	},
	//TUA6034 tuner
	{
	  .fe_frequency_min = 174000000,
	  .fe_frequency_max = 862000000,
	  .fe_frequency_stepsize = 166667,
	  .coderate_hp_shift = 9,
	  .coderate_lp_shift = 6,
	  .constellation_shift = 14,
	  .tx_mode_shift = 1,
	  .guard_interval_shift = 3,
	  .hierarchy_shift = 12,
	  .read_reg_flag = I2C_M_NOSTART,
	  .mt352_init = mt352_init_TUA6034,
	  .mt352_charge_pump = mt352_cp_TUA6034,
	  .mt352_band_select = mt352_bs_TUA6034
	},
	//TDTC9251DH01C tuner
	{
	  .fe_frequency_min = 474000000,
	  .fe_frequency_max = 858000000,
	  .fe_frequency_stepsize = 166667,
	  .coderate_hp_shift = 9,
	  .coderate_lp_shift = 6,
	  .constellation_shift = 4,
	  .tx_mode_shift = 1,
	  .guard_interval_shift = 3,
	  .hierarchy_shift = 12,
	  .read_reg_flag = 0,
	  .mt352_init = mt352_init_TDTC9251DH01C,
	  .mt352_charge_pump = mt352_cp_TDTC9251DH01C,
	  .mt352_band_select = mt352_bs_TDTC9251DH01C
	}
};


static struct dvb_frontend_info mt352_info = {
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

static int mt352_init_TUA6034(struct i2c_adapter *i2c)
{
	static u8 mt352_reset [] = { RESET, 0x80 };
	static u8 mt352_clock_config [] = { CLOCK_CTL, 0x38, 0x2d };
	static u8 mt352_adc_ctl_1_cfg [] = { ADC_CTL_1, 0x40 };
	static u8 mt352_agc_cfg [] = { AGC_TARGET, 0x19, 0xa0 };
	static u8 mt352_acq_ctl [] = { ACQ_CTL, 0x50 };

	mt352_write(mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(mt352_reset, sizeof(mt352_reset));
	mt352_write(mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));
	mt352_write(mt352_acq_ctl, sizeof(mt352_acq_ctl));

	mt352_write(mt352_agc_cfg, sizeof(mt352_agc_cfg));

	return 0;
}

static int mt352_init_AVERMEDIA771(struct i2c_adapter *i2c)
{
	static u8 mt352_reset [] = { RESET, 0x80 };
	static u8 mt352_clock_config [] = { CLOCK_CTL, 0x38, 0x2d };
	static u8 mt352_adc_ctl_1_cfg [] = { ADC_CTL_1, 0x40 };
	static u8 mt352_agc_cfg [] = { AGC_TARGET, 0x10, 0x23, 0x00, 0xFF, 0xFF,
				       0x00, 0xFF, 0x00, 0x40, 0x40 };
	static u8 mt352_acq_ctl [] = { ACQ_CTL, 0x50 };
	static u8 mt352_av771_extra[] = { 0xB5, 0x7A };

	mt352_write(mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(mt352_reset, sizeof(mt352_reset));
	mt352_write(mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));
	mt352_write(mt352_acq_ctl, sizeof(mt352_acq_ctl));

	mt352_write(mt352_agc_cfg,sizeof(mt352_agc_cfg));
	udelay(2000);
	mt352_write(mt352_av771_extra,sizeof(mt352_av771_extra));

	return 0;
}

static int mt352_init_TDTC9251DH01C(struct i2c_adapter *i2c)
{
	static u8 mt352_reset [] = { RESET, 0x80 };
	static u8 mt352_clock_config [] = { CLOCK_CTL, 0x10, 0x2d };
	static u8 mt352_adc_ctl_1_cfg [] = { ADC_CTL_1, 0x40 };
	static u8 mt352_agc_cfg [] = { AGC_TARGET, 0x28, 0xa1 };
	static u8 mt352_acq_ctl [] = { ACQ_CTL, 0x50 };

	mt352_write(mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(mt352_reset, sizeof(mt352_reset));
	mt352_write(mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));
	mt352_write(mt352_acq_ctl, sizeof(mt352_acq_ctl));

	mt352_write(mt352_agc_cfg, sizeof(mt352_agc_cfg));

	return 0;
}

static unsigned char mt352_cp_TUA6034(u32 freq)
{
	unsigned char cp = 0;

	if (freq < 542)
		cp = 0xbe;
	else if (freq < 830)
		cp = 0xf6;
	else
		cp = 0xfe;

	return cp;
}

static unsigned char mt352_cp_AVERMEDIA771(u32 freq)
{
	unsigned char cp = 0;

	if (freq < 150)
		cp = 0xB4;
	else if (freq < 173)
		cp = 0xBC;
	else if (freq < 250)
		cp = 0xB4;
	else if (freq < 400)
		cp = 0xBC;
	else if (freq < 420)
		cp = 0xF4;
	else if (freq < 470)
		cp = 0xFC;
	else if (freq < 600)
		cp = 0xBC;
	else if (freq < 730)
		cp = 0xF4;
	else
		cp = 0xFC;

	return cp;
}

static unsigned char mt352_cp_TDTC9251DH01C(u32 freq)
{
	return(0xcc);
}

static unsigned char mt352_bs_TUA6034(u32 freq)
{
	unsigned char bs = 0;

	if (freq < 250)
		bs = 0x01;
	else
		bs = 0x08;

	return bs;
}

static unsigned char mt352_bs_AVERMEDIA771(u32 freq)
{
	unsigned char bs = 0;

	if (freq < 150)
		bs = 0x01;
	else if (freq < 173)
		bs = 0x01;
	else if (freq < 250)
		bs = 0x02;
	else if (freq < 400)
		bs = 0x02;
	else if (freq < 420)
		bs = 0x02;
	else if (freq < 470)
		bs = 0x02;
	else if (freq < 600)
		bs = 0x08;
	else if (freq < 730)
		bs = 0x08;
	else
		bs = 0x08;

	return bs;
}

static unsigned char mt352_bs_TDTC9251DH01C(u32 freq)
{
	unsigned char bs = 0;

	if ((freq >= 48) && (freq <= 154))      /* low band */
		bs = 0x09;

	if ((freq >= 161) && (freq <= 439))     /* medium band */
		bs = 0x0a;

	if ((freq >= 447) && (freq <= 863))     /* high band */
		bs = 0x08;

	return bs;
}


static int mt352_detect_avermedia_771(struct i2c_adapter *i2c)
{
	int i;
	u8 reg;
	u8 id[4];
	const u8 pciid[4] = { 0x07, 0x71, 0x14, 0x61 };
	struct i2c_msg msg[2] =
	{
		{
			.addr = 0x50,
			.flags = I2C_M_NOSTART,
			.buf = &reg,
			.len = 1
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 1
		}
	};

	for (i = 0; i < 4; i++)
	{
		reg = i + 0xFC;
		msg[1].buf = id + i;
		if (i2c_transfer(i2c,msg,2) != 2)
		{
			return 0;
		}
	}

	return *((u32 *) id) == *((u32 *) pciid);
}

static int mt352_init(struct i2c_adapter *i2c)
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
	static u8 mt352_softdown[] = { 0x89, 0x20, 0x08 };

	mt352_write(mt352_softdown, sizeof(mt352_softdown));

	return 0;
}

static int mt352_set_parameters(struct i2c_adapter *i2c,
				struct dvb_frontend_parameters *param)
{
	unsigned char buf[14];
	unsigned int tps = 0;
	struct dvb_ofdm_parameters *op = &param->u.ofdm;
	u32 freq = param->frequency / 1000000;
	uint16_t tmp;

	switch (op->code_rate_HP) {
		case FEC_2_3:
			tps |= (1 << CODERATE_HP_SHIFT);
			break;
		case FEC_3_4:
			tps |= (2 << CODERATE_HP_SHIFT);
			break;
		case FEC_5_6:
			tps |= (3 << CODERATE_HP_SHIFT);
			break;
		case FEC_7_8:
			tps |= (4 << CODERATE_HP_SHIFT);
			break;
		case FEC_1_2:
		case FEC_AUTO:
			break;
		default:
			return -EINVAL;
	}

	switch (op->code_rate_LP) {
		case FEC_2_3:
			tps |= (1 <<  CODERATE_LP_SHIFT);
			break;
		case FEC_3_4:
			tps |= (2 <<  CODERATE_LP_SHIFT);
			break;
		case FEC_5_6:
			tps |= (3 <<  CODERATE_LP_SHIFT);
			break;
		case FEC_7_8:
			tps |= (4 <<  CODERATE_LP_SHIFT);
			break;
		case FEC_1_2:
		case FEC_AUTO:
			break;
		default:
			return -EINVAL;
	}

	switch (op->constellation) {
		case QPSK:
			break;
		case QAM_AUTO:
		case QAM_16:
			tps |= (1 << CONSTELLATION_SHIFT);
			break;
		case QAM_64:
			tps |= (2 << CONSTELLATION_SHIFT);
			break;
		default:
			return -EINVAL;
	}

	switch (op->transmission_mode) {
		case TRANSMISSION_MODE_2K:
		case TRANSMISSION_MODE_AUTO:
			break;
		case TRANSMISSION_MODE_8K:
			tps |= (1 << TX_MODE_SHIFT);
			break;
		default:
			return -EINVAL;
	}

	switch (op->guard_interval) {
		case GUARD_INTERVAL_1_32:
		case GUARD_INTERVAL_AUTO:
			break;
		case GUARD_INTERVAL_1_16:
			tps |= (1 << GUARD_INTERVAL_SHIFT);
			break;
		case GUARD_INTERVAL_1_8:
			tps |= (2 << GUARD_INTERVAL_SHIFT);
			break;
		case GUARD_INTERVAL_1_4:
			tps |= (3 << GUARD_INTERVAL_SHIFT);
			break;
		default:
			return -EINVAL;
	}

	switch (op->hierarchy_information) {
		case HIERARCHY_AUTO:
		case HIERARCHY_NONE:
			break;
		case HIERARCHY_1:
			tps |= (1 << HIERARCHY_SHIFT);
			break;
		case HIERARCHY_2:
			tps |= (2 << HIERARCHY_SHIFT);
			break;
		case HIERARCHY_4:
			tps |= (3 << HIERARCHY_SHIFT);
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

	buf[8] = I2C_TUNER_ADDR;

	/**
	 *  All the following settings are tuner module dependent,
	 *  check the datasheet...
	 */

	/* here we assume 1/6MHz == 166.66kHz stepsize */
	#define IF_FREQUENCYx6 217    /* 6 * 36.16666666667MHz */
	tmp = ((param->frequency*3)/500000) + IF_FREQUENCYx6;

	buf[9] = msb(tmp);      /* CHAN_START_(1|0) */
	buf[10] = lsb(tmp);

	buf[11] = MT352_CHARGE_PUMP(freq);
	buf[12] = MT352_BAND_SELECT(freq);

	buf[13] = 0x01; /* TUNER_GO!! */

	mt352_write(buf, sizeof(buf));

	return 0;
}

static u8 mt352_read_register(struct i2c_adapter *i2c, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = I2C_MT352_ADDR,
				    .flags =  READ_REG_FLAG,
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
	u8 r,snr;
	fe_status_t *status;
	u16 signal;
	struct dvb_frontend_tune_settings *fe_tune_settings;

	switch (cmd) {
	case FE_GET_INFO:
		memcpy (arg, &mt352_info, sizeof(struct dvb_frontend_info));
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
				 (struct dvb_frontend_parameters *) arg);

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
		return mt352_init(i2c);

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct i2c_client client_template;

static int mt352_attach_adapter(struct i2c_adapter *i2c)
{
	struct mt352_state *state;
	struct i2c_client *client;
	static u8 mt352_reset_attach [] = { 0x50, 0xC0 };
	int ret;

	dprintk("Trying to attach to adapter 0x%x:%s.\n",
		i2c->id, i2c->name);

	/* set the proper MT352 frequency range */
	mt352_info.frequency_min =  FE_FREQ_MIN;
	mt352_info.frequency_max =  FE_FREQ_MAX;
	mt352_info.frequency_stepsize =  FE_FREQ_STEPSIZE;

	if ( !(state = kmalloc(sizeof(struct mt352_state), GFP_KERNEL)) )
		return -ENOMEM;

	memset(state, 0, sizeof(struct mt352_state));
	state->i2c = i2c;

	if (mt352_detect_avermedia_771(i2c)) {
		card_type = CARD_AVDVBT771;
	} else if (force_card < 0) {
		dprintk("Avermedia 771 not detected, maybe you should try the "
			"'force_card' module parameter?.\n");
		kfree(state);
		return -ENODEV;
	}

	if (force_card > 0) {
		if (card_type >= 0 && force_card != card_type)
			printk(KERN_WARNING "dvbfe_mt352: Warning, overriding"
					    " detected card.\n");
		card_type = force_card;
	}

	if (mt352_read_register(i2c, CHIP_ID) != ID_MT352) {
		kfree(state);
		return -ENODEV;
	}

	if (card_type == CARD_AVDVBT771)
		printk(KERN_INFO FRONTEND_NAME ": Setup for Avermedia 771.\n");
	else if (card_type == CARD_TUA6034)
		printk(KERN_INFO FRONTEND_NAME ": Setup for TUA6034.\n");
	else if (card_type == CARD_TDTC9251DH01C)
		printk(KERN_INFO FRONTEND_NAME ": Setup for TDTC9251DH01C.\n");

	/* Do a "hard" reset */
	mt352_write(mt352_reset_attach, sizeof(mt352_reset_attach));

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

	BUG_ON(!state->dvb);

	if ((ret = dvb_register_frontend(mt352_ioctl, state->dvb, state,
					     &mt352_info, THIS_MODULE))) {
		i2c_detach_client(client);
		kfree(client);
		kfree(state);
		return ret;
	}

	return 0;
}

static int mt352_detach_client(struct i2c_client *client)
{
	struct mt352_state *state = i2c_get_clientdata(client);

	dvb_unregister_frontend (mt352_ioctl, state->dvb);
	i2c_detach_client(client);
	BUG_ON(state->dvb);
	kfree(client);
	kfree(state);
	return 0;
}

static int mt352_command (struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct mt352_state *state = i2c_get_clientdata(client);

	switch (cmd) {
	case FE_REGISTER:
		state->dvb = arg;
		break;
	case FE_UNREGISTER:
		state->dvb = NULL;
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

