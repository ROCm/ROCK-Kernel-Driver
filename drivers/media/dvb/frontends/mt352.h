/*
 *  Driver for Zarlink DVB-T MT352 demodulator
 *
 *  Written by Holger Waechtler <holger@qanu.de>
 *	 and Daniel Mack <daniel@qanu.de>
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

#ifndef _MT352_
#define _MT352_

#define I2C_MT352_ADDR  0x0f
#define I2C_TUNER_ADDR  0xc2
#define ID_MT352        0x13

#define CARD_AVDVBT771	    0x00
#define CARD_TUA6034	    0x01
#define CARD_TDTC9251DH01C  0x02

#define msb(x) (((x) >> 8) & 0xff)
#define lsb(x) ((x) & 0xff)

enum mt352_reg_addr {
	STATUS_0           = 0x00,
	STATUS_1           = 0x01,
	STATUS_2           = 0x02,
	STATUS_3           = 0x03,
	STATUS_4           = 0x04,
	INTERRUPT_0        = 0x05,
	INTERRUPT_1        = 0x06,
	INTERRUPT_2        = 0x07,
	INTERRUPT_3        = 0x08,
	SNR                = 0x09,
	VIT_ERR_CNT_2      = 0x0A,
	VIT_ERR_CNT_1      = 0x0B,
	VIT_ERR_CNT_0      = 0x0C,
	RS_ERR_CNT_2       = 0x0D,
	RS_ERR_CNT_1       = 0x0E,
	RS_ERR_CNT_0       = 0x0F,
	RS_UBC_1           = 0x10,
	RS_UBC_0           = 0x11,
	AGC_GAIN_3         = 0x12,
	AGC_GAIN_2         = 0x13,
	AGC_GAIN_1         = 0x14,
	AGC_GAIN_0         = 0x15,
	FREQ_OFFSET_2      = 0x17,
	FREQ_OFFSET_1      = 0x18,
	FREQ_OFFSET_0      = 0x19,
	TIMING_OFFSET_1    = 0x1A,
	TIMING_OFFSET_0    = 0x1B,
	CHAN_FREQ_1        = 0x1C,
	CHAN_FREQ_0        = 0x1D,
	TPS_RECEIVED_1     = 0x1E,
	TPS_RECEIVED_0     = 0x1F,
	TPS_CURRENT_1      = 0x20,
	TPS_CURRENT_0      = 0x21,
	TPS_CELL_ID_1      = 0x22,
	TPS_CELL_ID_0      = 0x23,
	TPS_MISC_DATA_2    = 0x24,
	TPS_MISC_DATA_1    = 0x25,
	TPS_MISC_DATA_0    = 0x26,
	RESET              = 0x50,
	TPS_GIVEN_1        = 0x51,
	TPS_GIVEN_0        = 0x52,
	ACQ_CTL            = 0x53,
	TRL_NOMINAL_RATE_1 = 0x54,
	TRL_NOMINAL_RATE_0 = 0x55,
	INPUT_FREQ_1       = 0x56,
	INPUT_FREQ_0       = 0x57,
	TUNER_ADDR         = 0x58,
	CHAN_START_1       = 0x59,
	CHAN_START_0       = 0x5A,
	CONT_1             = 0x5B,
	CONT_0             = 0x5C,
	TUNER_GO           = 0x5D,
	STATUS_EN_0        = 0x5F,
	STATUS_EN_1        = 0x60,
	INTERRUPT_EN_0     = 0x61,
	INTERRUPT_EN_1     = 0x62,
	INTERRUPT_EN_2     = 0x63,
	INTERRUPT_EN_3     = 0x64,
	AGC_TARGET         = 0x67,
	AGC_CTL            = 0x68,
	CAPT_RANGE         = 0x75,
	SNR_SELECT_1       = 0x79,
	SNR_SELECT_0       = 0x7A,
	RS_ERR_PER_1       = 0x7C,
	RS_ERR_PER_0       = 0x7D,
	CHIP_ID            = 0x7F,
	CHAN_STOP_1        = 0x80,
	CHAN_STOP_0        = 0x81,
	CHAN_STEP_1        = 0x82,
	CHAN_STEP_0        = 0x83,
	FEC_LOCK_TIME      = 0x85,
	OFDM_LOCK_TIME     = 0x86,
	ACQ_DELAY          = 0x87,
	SCAN_CTL           = 0x88,
	CLOCK_CTL          = 0x89,
	CONFIG             = 0x8A,
	MCLK_RATIO         = 0x8B,
	GPP_CTL            = 0x8C,
	ADC_CTL_1          = 0x8E,
	ADC_CTL_0          = 0x8F
};

struct _tuner_info {
	__u32 fe_frequency_min;
#define FE_FREQ_MIN tuner_info[card_type].fe_frequency_min

	__u32 fe_frequency_max;
#define FE_FREQ_MAX tuner_info[card_type].fe_frequency_max

	__u32 fe_frequency_stepsize; //verificare se u32 e' corretto
#define FE_FREQ_STEPSIZE  tuner_info[card_type].fe_frequency_stepsize

	__u32 coderate_hp_shift; //verificare se u32 giusto
#define CODERATE_HP_SHIFT tuner_info[card_type].coderate_hp_shift

	__u32 coderate_lp_shift;
#define CODERATE_LP_SHIFT tuner_info[card_type].coderate_lp_shift

	int constellation_shift;
#define CONSTELLATION_SHIFT tuner_info[card_type].constellation_shift

	int tx_mode_shift;
#define TX_MODE_SHIFT tuner_info[card_type].tx_mode_shift

	int guard_interval_shift;
#define GUARD_INTERVAL_SHIFT tuner_info[card_type].guard_interval_shift

	int hierarchy_shift;
#define HIERARCHY_SHIFT tuner_info[card_type].hierarchy_shift

	int read_reg_flag;
#define READ_REG_FLAG tuner_info[card_type].read_reg_flag

	int (* mt352_init) (struct i2c_adapter *i2c);
#define MT352_INIT tuner_info[card_type].mt352_init

	unsigned char (* mt352_charge_pump) (u32 freq);
#define MT352_CHARGE_PUMP tuner_info[card_type].mt352_charge_pump

	unsigned char (* mt352_band_select) (u32 freq);
#define MT352_BAND_SELECT tuner_info[card_type].mt352_band_select
};

static int mt352_init_TUA6034(struct i2c_adapter *i2c);
static int mt352_init_AVERMEDIA771(struct i2c_adapter *i2c);
static int mt352_init_TDTC9251DH01C(struct i2c_adapter *i2c);
static unsigned char mt352_cp_TUA6034(u32 freq);
static unsigned char mt352_cp_AVERMEDIA771(u32 freq);
static unsigned char mt352_cp_TDTC9251DH01C(u32 freq);
static unsigned char mt352_bs_TUA6034(u32 freq);
static unsigned char mt352_bs_AVERMEDIA771(u32 freq);
static unsigned char mt352_bs_TDTC9251DH01C(u32 freq);
static int mt352_detect_avermedia_771(struct i2c_adapter *i2c);

#endif                          /* _MT352_ */
