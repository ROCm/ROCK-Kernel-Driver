/* 

	NxtWave Communications - NXT6000 demodulator driver
	
	This driver currently supports:
	
	Alps TDME7 (Tuner: MITEL SP5659)
	Alps TDED4 (Tuner: TI ALP510, external Nxt6000)
	Comtech DVBT-6k07 (PLL IC: SP5730)

    Copyright (C) 2002-2003 Florian Schirmer <jolt@tuxbox.org>
    Copyright (C) 2003 Paul Andreassen <paul@andreassen.com.au>

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
#include "nxt6000.h"

static int debug = 0;

MODULE_DESCRIPTION("NxtWave NXT6000 DVB demodulator driver");
MODULE_AUTHOR("Florian Schirmer");
MODULE_LICENSE("GPL");
MODULE_PARM(debug, "i");

static struct dvb_frontend_info nxt6000_info = {

	.name = "NxtWave NXT6000",
	.type = FE_OFDM,
	.frequency_min = 0,
	.frequency_max = 863250000,
	.frequency_stepsize = 62500,
	/*.frequency_tolerance = */	/* FIXME: 12% of SR */
	.symbol_rate_min = 0,		/* FIXME */
	.symbol_rate_max = 9360000,	/* FIXME */
	.symbol_rate_tolerance = 4000,
	.notifier_delay = 0,
	.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 | 
                FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 | 
                FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO | 
                FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO | 
                FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | 
                FE_CAN_HIERARCHY_AUTO,
};

struct nxt6000_config {
	u8 demod_addr;
	u8 tuner_addr;
	u8 tuner_type;
	u8 clock_inversion;
};

#define TUNER_TYPE_ALP510	0
#define TUNER_TYPE_SP5659	1
#define TUNER_TYPE_SP5730	2

#define FE2NXT(fe) ((struct nxt6000_config *)((fe)->data))
#define FREQ2DIV(freq) ((freq + 36166667) / 166667)

#define dprintk if (debug) printk

static int nxt6000_write(struct dvb_i2c_bus *i2c, u8 addr, u8 reg, u8 data)
{

	u8 buf[] = {reg, data};
	struct i2c_msg msg = {.addr = addr >> 1, .flags = 0, .buf = buf, .len = 2};
	int ret;
	
	if ((ret = i2c->xfer(i2c, &msg, 1)) != 1)
		dprintk("nxt6000: nxt6000_write error (.addr = 0x%02X, reg: 0x%02X, data: 0x%02X, ret: %d)\n", addr, reg, data, ret);

	return (ret != 1) ? -EFAULT : 0;
	
}

static u8 nxt6000_writereg(struct dvb_frontend *fe, u8 reg, u8 data)
{

	struct nxt6000_config *nxt = FE2NXT(fe);

	return nxt6000_write(fe->i2c, nxt->demod_addr, reg, data);

}

static u8 nxt6000_read(struct dvb_i2c_bus *i2c, u8 addr, u8 reg)
{

	int ret;
	u8 b0[] = {reg};
	u8 b1[] = {0};
	struct i2c_msg msgs[] = {
		{.addr = addr >> 1,.flags = 0,.buf = b0,.len = 1},
		{.addr = addr >> 1,.flags = I2C_M_RD,.buf = b1,.len = 1}
	};

	ret = i2c->xfer(i2c, msgs, 2);
	
	if (ret != 2)
		dprintk("nxt6000: nxt6000_read error (.addr = 0x%02X, reg: 0x%02X, ret: %d)\n", addr, reg, ret);
	
	return b1[0];

}

static u8 nxt6000_readreg(struct dvb_frontend *fe, u8 reg)
{

	struct nxt6000_config *nxt = FE2NXT(fe);

	return nxt6000_read(fe->i2c, nxt->demod_addr, reg);
}

static int pll_test(struct dvb_i2c_bus *i2c, u8 demod_addr, u8 tuner_addr)
{
	u8 buf [1];
	struct i2c_msg msg = {.addr = tuner_addr >> 1,.flags = I2C_M_RD,.buf = buf,.len = 1 };
	int ret;

	nxt6000_write(i2c, demod_addr, ENABLE_TUNER_IIC, 0x01);	/* open i2c bus switch */
	ret = i2c->xfer(i2c, &msg, 1);
	nxt6000_write(i2c, demod_addr, ENABLE_TUNER_IIC, 0x00);	/* close i2c bus switch */

	return (ret != 1) ? -EFAULT : 0;
}

static int pll_write(struct dvb_i2c_bus *i2c, u8 demod_addr, u8 tuner_addr, u8 *buf, u8 len)
{

	struct i2c_msg msg = {.addr = tuner_addr >> 1, .flags = 0, .buf = buf, .len = len};
	int ret;
				
	nxt6000_write(i2c, demod_addr, ENABLE_TUNER_IIC, 0x01);		/* open i2c bus switch */
	ret = i2c->xfer(i2c, &msg, 1);
	nxt6000_write(i2c, demod_addr, ENABLE_TUNER_IIC, 0x00);		/* close i2c bus switch */
										
	if (ret != 1)
		dprintk("nxt6000: pll_write error %d\n", ret);
																
	return (ret != 1) ? -EFAULT : 0;

}

static int sp5659_set_tv_freq(struct dvb_frontend *fe, u32 freq)
{

	u8 buf[4];
	struct nxt6000_config *nxt = FE2NXT(fe);

	buf[0] = (FREQ2DIV(freq) >> 8) & 0x7F;
	buf[1] = FREQ2DIV(freq) & 0xFF;
	buf[2] = (((FREQ2DIV(freq) >> 15) & 0x03) << 5) | 0x85;

	if ((freq >= 174000000) && (freq < 230000000))
		buf[3] = 0x82;
	else if ((freq >= 470000000) && (freq < 782000000))
		buf[3] = 0x85;
	else if ((freq >= 782000000) && (freq < 863000000))
		buf[3] = 0xC5;
	else
		return -EINVAL;

	return pll_write(fe->i2c, nxt->demod_addr, nxt->tuner_addr, buf, 4);
	
}

static int alp510_set_tv_freq(struct dvb_frontend *fe, u32 freq)
{

	u8 buf[4];
	struct nxt6000_config *nxt = FE2NXT(fe);

	buf[0] = (FREQ2DIV(freq) >> 8) & 0x7F;
	buf[1] = FREQ2DIV(freq) & 0xFF;
	buf[2] = 0x85;

#if 0
	if ((freq >= 47000000) && (freq < 153000000))
		buf[3] = 0x01;
	else if ((freq >= 153000000) && (freq < 430000000))
		buf[3] = 0x02;
	else if ((freq >= 430000000) && (freq < 824000000))
		buf[3] = 0x08;
	else if ((freq >= 824000000) && (freq < 863000000))
		buf[3] = 0x88;
	else
		return -EINVAL;
#else
	if ((freq >= 47000000) && (freq < 153000000))
		buf[3] = 0x01;
	else if ((freq >= 153000000) && (freq < 430000000))
		buf[3] = 0x02;
	else if ((freq >= 430000000) && (freq < 824000000))
		buf[3] = 0x0C;
	else if ((freq >= 824000000) && (freq < 863000000))
		buf[3] = 0x8C;
	else
		return -EINVAL;
#endif

	return pll_write(fe->i2c, nxt->demod_addr, nxt->tuner_addr, buf, 4);
	
}

static int sp5730_set_tv_freq(struct dvb_frontend *fe, u32 freq)
{

	u8 buf[4];
	struct nxt6000_config *nxt = FE2NXT(fe);

	buf[0] = (FREQ2DIV(freq) >> 8) & 0x7F;
	buf[1] = FREQ2DIV(freq) & 0xFF;
	buf[2] = 0x93;

	if ((freq >= 51000000) && (freq < 132100000))
		buf[3] = 0x05;
	else if ((freq >= 132100000) && (freq < 143000000))
		buf[3] = 0x45;
	else if ((freq >= 146000000) && (freq < 349100000))
		buf[3] = 0x06;
	else if ((freq >= 349100000) && (freq < 397100000))
		buf[3] = 0x46;
	else if ((freq >= 397100000) && (freq < 426000000))
		buf[3] = 0x86;
	else if ((freq >= 430000000) && (freq < 659100000))
		buf[3] = 0x03;
	else if ((freq >= 659100000) && (freq < 759100000))
		buf[3] = 0x43;
	else if ((freq >= 759100000) && (freq < 858000000))
		buf[3] = 0x83;
	else
		return -EINVAL;

	return pll_write(fe->i2c, nxt->demod_addr, nxt->tuner_addr, buf, 4);
	
}

static void nxt6000_reset(struct dvb_frontend *fe)
{

	u8 val;

	val = nxt6000_readreg(fe, OFDM_COR_CTL);
	
	nxt6000_writereg(fe, OFDM_COR_CTL, val & ~COREACT);
	nxt6000_writereg(fe, OFDM_COR_CTL, val | COREACT);
	
}

static int nxt6000_set_bandwidth(struct dvb_frontend *fe, fe_bandwidth_t bandwidth)
{

	u16 nominal_rate;
	int result;

	switch(bandwidth) {
	
		case BANDWIDTH_6_MHZ:
		
			nominal_rate = 0x55B7;
			
			break;

		case BANDWIDTH_7_MHZ:

			nominal_rate = 0x6400;
			
			break;

		case BANDWIDTH_8_MHZ:

			nominal_rate = 0x7249;
			
			break;

		default:
			
			return -EINVAL;
			
	}

	if ((result = nxt6000_writereg(fe, OFDM_TRL_NOMINALRATE_1, nominal_rate & 0xFF)) < 0)
		return result;
		
	return nxt6000_writereg(fe, OFDM_TRL_NOMINALRATE_2, (nominal_rate >> 8) & 0xFF);
		
}

static int nxt6000_set_guard_interval(struct dvb_frontend *fe, fe_guard_interval_t guard_interval)
{

	switch(guard_interval) {
	
		case GUARD_INTERVAL_1_32:

			return nxt6000_writereg(fe, OFDM_COR_MODEGUARD, 0x00 | (nxt6000_readreg(fe, OFDM_COR_MODEGUARD) & ~0x03));

		case GUARD_INTERVAL_1_16:

			return nxt6000_writereg(fe, OFDM_COR_MODEGUARD, 0x01 | (nxt6000_readreg(fe, OFDM_COR_MODEGUARD) & ~0x03));

		case GUARD_INTERVAL_AUTO:
		case GUARD_INTERVAL_1_8:

			return nxt6000_writereg(fe, OFDM_COR_MODEGUARD, 0x02 | (nxt6000_readreg(fe, OFDM_COR_MODEGUARD) & ~0x03));

		case GUARD_INTERVAL_1_4:

			return nxt6000_writereg(fe, OFDM_COR_MODEGUARD, 0x03 | (nxt6000_readreg(fe, OFDM_COR_MODEGUARD) & ~0x03));
			
		default:
		
			return -EINVAL;

	}

}

static int nxt6000_set_inversion(struct dvb_frontend *fe, fe_spectral_inversion_t inversion)
{

	switch(inversion) {
	
		case INVERSION_OFF:
		
			return nxt6000_writereg(fe, OFDM_ITB_CTL, 0x00);
			
		case INVERSION_ON:
		
			return nxt6000_writereg(fe, OFDM_ITB_CTL, ITBINV);

		default:
		
			return -EINVAL;	
	
	}

}

static int nxt6000_set_transmission_mode(struct dvb_frontend *fe, fe_transmit_mode_t transmission_mode)
{

	int result;

	switch(transmission_mode) {

		case TRANSMISSION_MODE_2K:	

			if ((result = nxt6000_writereg(fe, EN_DMD_RACQ, 0x00 | (nxt6000_readreg(fe, EN_DMD_RACQ) & ~0x03))) < 0)
				return result;
				
			return nxt6000_writereg(fe, OFDM_COR_MODEGUARD, (0x00 << 2) | (nxt6000_readreg(fe, OFDM_COR_MODEGUARD) & ~0x04));

		case TRANSMISSION_MODE_8K:	
		case TRANSMISSION_MODE_AUTO:	

			if ((result = nxt6000_writereg(fe, EN_DMD_RACQ, 0x02 | (nxt6000_readreg(fe, EN_DMD_RACQ) & ~0x03))) < 0)
				return result;

			return nxt6000_writereg(fe, OFDM_COR_MODEGUARD, (0x01 << 2) | (nxt6000_readreg(fe, OFDM_COR_MODEGUARD) & ~0x04));

		default:
			
			return -EINVAL;
	
	}

}

static void nxt6000_setup(struct dvb_frontend *fe)
{

	struct nxt6000_config *nxt = FE2NXT(fe);

	nxt6000_writereg(fe, RS_COR_SYNC_PARAM, SYNC_PARAM);
	nxt6000_writereg(fe, BER_CTRL, /*(1 << 2) |*/ (0x01 << 1) | 0x01);
	nxt6000_writereg(fe, VIT_COR_CTL, VIT_COR_RESYNC);
	nxt6000_writereg(fe, OFDM_COR_CTL, (0x01 << 5) | (nxt6000_readreg(fe, OFDM_COR_CTL) & 0x0F));
	nxt6000_writereg(fe, OFDM_COR_MODEGUARD, FORCEMODE8K | 0x02);
	nxt6000_writereg(fe, OFDM_AGC_CTL, AGCLAST | INITIAL_AGC_BW);
	nxt6000_writereg(fe, OFDM_ITB_FREQ_1, 0x06);
	nxt6000_writereg(fe, OFDM_ITB_FREQ_2, 0x31);
	nxt6000_writereg(fe, OFDM_CAS_CTL, (0x01 << 7) | (0x02 << 3) | 0x04);
	nxt6000_writereg(fe, CAS_FREQ, 0xBB);	/* CHECKME */
	nxt6000_writereg(fe, OFDM_SYR_CTL, 1 << 2);
	nxt6000_writereg(fe, OFDM_PPM_CTL_1, PPM256);
	nxt6000_writereg(fe, OFDM_TRL_NOMINALRATE_1, 0x49);
	nxt6000_writereg(fe, OFDM_TRL_NOMINALRATE_2, 0x72);
	nxt6000_writereg(fe, ANALOG_CONTROL_0, 1 << 5);
	nxt6000_writereg(fe, EN_DMD_RACQ, (1 << 7) | (3 << 4) | 2);
	nxt6000_writereg(fe, DIAG_CONFIG, TB_SET);
	
	if (nxt->clock_inversion)
		nxt6000_writereg(fe, SUB_DIAG_MODE_SEL, CLKINVERSION);
	else
		nxt6000_writereg(fe, SUB_DIAG_MODE_SEL, 0);
		
	nxt6000_writereg(fe, TS_FORMAT, 0);

}

static void nxt6000_dump_status(struct dvb_frontend *fe)
{
	u8 val;

/*
	printk("RS_COR_STAT: 0x%02X\n", nxt6000_readreg(fe, RS_COR_STAT));
	printk("VIT_SYNC_STATUS: 0x%02X\n", nxt6000_readreg(fe, VIT_SYNC_STATUS));
	printk("OFDM_COR_STAT: 0x%02X\n", nxt6000_readreg(fe, OFDM_COR_STAT));
	printk("OFDM_SYR_STAT: 0x%02X\n", nxt6000_readreg(fe, OFDM_SYR_STAT));
	printk("OFDM_TPS_RCVD_1: 0x%02X\n", nxt6000_readreg(fe, OFDM_TPS_RCVD_1));
	printk("OFDM_TPS_RCVD_2: 0x%02X\n", nxt6000_readreg(fe, OFDM_TPS_RCVD_2));
	printk("OFDM_TPS_RCVD_3: 0x%02X\n", nxt6000_readreg(fe, OFDM_TPS_RCVD_3));
	printk("OFDM_TPS_RCVD_4: 0x%02X\n", nxt6000_readreg(fe, OFDM_TPS_RCVD_4));
	printk("OFDM_TPS_RESERVED_1: 0x%02X\n", nxt6000_readreg(fe, OFDM_TPS_RESERVED_1));
	printk("OFDM_TPS_RESERVED_2: 0x%02X\n", nxt6000_readreg(fe, OFDM_TPS_RESERVED_2));
*/
	printk("NXT6000 status:");

	val = nxt6000_readreg(fe, RS_COR_STAT);
	
	printk(" DATA DESCR LOCK: %d,", val & 0x01);
	printk(" DATA SYNC LOCK: %d,", (val >> 1) & 0x01);

	val = nxt6000_readreg(fe, VIT_SYNC_STATUS);

	printk(" VITERBI LOCK: %d,", (val >> 7) & 0x01);

	switch((val >> 4) & 0x07) {
	
		case 0x00: 
		
			printk(" VITERBI CODERATE: 1/2,");
			
			break;
	
		case 0x01: 
		
			printk(" VITERBI CODERATE: 2/3,");
			
			break;
	
		case 0x02: 
		
			printk(" VITERBI CODERATE: 3/4,");
			
			break;
	
		case 0x03: 
			printk(" VITERBI CODERATE: 5/6,");
		break;

		case 0x04: 
			printk(" VITERBI CODERATE: 7/8,");
			break;

		default: 
		
			printk(" VITERBI CODERATE: Reserved,");
			
	}

	val = nxt6000_readreg(fe, OFDM_COR_STAT);
	
	printk(" CHCTrack: %d,", (val >> 7) & 0x01);
	printk(" TPSLock: %d,", (val >> 6) & 0x01);
	printk(" SYRLock: %d,", (val >> 5) & 0x01);
	printk(" AGCLock: %d,", (val >> 4) & 0x01);

	switch(val & 0x0F) {
	
		case 0x00:
		
			printk(" CoreState: IDLE,");
			
			break;
	
		case 0x02:
		
			printk(" CoreState: WAIT_AGC,");
			
			break;
	
		case 0x03:
		
			printk(" CoreState: WAIT_SYR,");
			
			break;
	
		case 0x04:
			printk(" CoreState: WAIT_PPM,");
		break;

		case 0x01:
			printk(" CoreState: WAIT_TRL,");
			break;

		case 0x05:
		
			printk(" CoreState: WAIT_TPS,");
			
			break;

		case 0x06:
		
			printk(" CoreState: MONITOR_TPS,");
			
			break;

		default: 
		
			printk(" CoreState: Reserved,");
			
	}

	val = nxt6000_readreg(fe, OFDM_SYR_STAT);

	printk(" SYRLock: %d,", (val >> 4) & 0x01);
	printk(" SYRMode: %s,", (val >> 2) & 0x01 ? "8K" : "2K");

	switch((val >> 4) & 0x03) {
	
		case 0x00: 
		
			printk(" SYRGuard: 1/32,");
			
			break;
	
		case 0x01: 
		
			printk(" SYRGuard: 1/16,");
			
			break;
	
		case 0x02: 
		
			printk(" SYRGuard: 1/8,");
			
			break;
	
		case 0x03: 
		
			printk(" SYRGuard: 1/4,");
			
			break;
			
	}

	val = nxt6000_readreg(fe, OFDM_TPS_RCVD_3);
	
	switch((val >> 4) & 0x07) {
	
		case 0x00: 
		
			printk(" TPSLP: 1/2,");
			
			break;
	
		case 0x01: 
		
			printk(" TPSLP: 2/3,");
			
			break;
	
		case 0x02: 
		
			printk(" TPSLP: 3/4,");
			
			break;
	
		case 0x03: 
			printk(" TPSLP: 5/6,");
		break;

		case 0x04: 
			printk(" TPSLP: 7/8,");
			break;

		default: 
		
			printk(" TPSLP: Reserved,");
			
	}

	switch(val & 0x07) {
	
		case 0x00: 
		
			printk(" TPSHP: 1/2,");
			
			break;
	
		case 0x01: 
		
			printk(" TPSHP: 2/3,");
			
			break;
	
		case 0x02: 
		
			printk(" TPSHP: 3/4,");
			
			break;
	
		case 0x03: 
			printk(" TPSHP: 5/6,");
		break;

		case 0x04: 
			printk(" TPSHP: 7/8,");
			break;

		default: 
		
			printk(" TPSHP: Reserved,");
			
	}

	val = nxt6000_readreg(fe, OFDM_TPS_RCVD_4);
	
	printk(" TPSMode: %s,", val & 0x01 ? "8K" : "2K");
	
	switch((val >> 4) & 0x03) {
	
		case 0x00: 
		
			printk(" TPSGuard: 1/32,");
			
			break;
	
		case 0x01: 
		
			printk(" TPSGuard: 1/16,");
			
			break;
	
		case 0x02: 
		
			printk(" TPSGuard: 1/8,");
			
			break;
	
		case 0x03: 
		
			printk(" TPSGuard: 1/4,");
			
			break;
			
	}
	
	/* Strange magic required to gain access to RF_AGC_STATUS */
	nxt6000_readreg(fe, RF_AGC_VAL_1);
	val = nxt6000_readreg(fe, RF_AGC_STATUS);
	val = nxt6000_readreg(fe, RF_AGC_STATUS);

	printk(" RF AGC LOCK: %d,", (val >> 4) & 0x01);

	printk("\n");
	
}

static int nxt6000_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{

	switch (cmd) {

		case FE_GET_INFO:

			memcpy(arg, &nxt6000_info, sizeof (struct dvb_frontend_info));

			return 0;

		case FE_READ_STATUS:
		{
			fe_status_t *status = (fe_status_t *)arg;

			u8 core_status;

			*status = 0;
			
			core_status = nxt6000_readreg(fe, OFDM_COR_STAT);

			if (core_status & AGCLOCKED)
				*status |= FE_HAS_SIGNAL;

			if (nxt6000_readreg(fe, OFDM_SYR_STAT) & GI14_SYR_LOCK)
				*status |= FE_HAS_CARRIER;

			if (nxt6000_readreg(fe, VIT_SYNC_STATUS) & VITINSYNC)
				*status |= FE_HAS_VITERBI;

			if (nxt6000_readreg(fe, RS_COR_STAT) & RSCORESTATUS)
				*status |= FE_HAS_SYNC;
				
			if ((core_status & TPSLOCKED) && (*status == (FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC)))
				*status |= FE_HAS_LOCK;
				
			if (debug)
				nxt6000_dump_status(fe);

			return 0;
			
		}
	
		case FE_READ_BER:
		{
			u32 *ber = (u32 *)arg;

			*ber=0;

			return 0;
			
		}
	
		case FE_READ_SIGNAL_STRENGTH:
		{
			s16 *signal = (s16 *) arg;
/*
			*signal=(((signed char)readreg(client, 0x16))+128)<<8;
*/
			*signal = 0;
			return 0;
			
		}
	
		case FE_READ_SNR:
		{
			s16 *snr = (s16 *) arg;
/*
			*snr=readreg(client, 0x24)<<8;
			*snr|=readreg(client, 0x25);
*/
			*snr = 0;
			break;
		}
	
		case FE_READ_UNCORRECTED_BLOCKS: 
		{
			u32 *ublocks = (u32 *)arg;

			*ublocks = 0;

			break;
		}
	
		case FE_INIT:
			nxt6000_reset(fe);
			nxt6000_setup(fe);
		break;

		case FE_SET_FRONTEND:
		{
			struct nxt6000_config *nxt = FE2NXT(fe);
			struct dvb_frontend_parameters *param = (struct dvb_frontend_parameters *)arg;
			int result;

			switch(nxt->tuner_type) {
			
				case TUNER_TYPE_ALP510:

					if ((result = alp510_set_tv_freq(fe, param->frequency)) < 0)
						return result;
						
					break;

				case TUNER_TYPE_SP5659:

					if ((result = sp5659_set_tv_freq(fe, param->frequency)) < 0)
						return result;
						
					break;
					
				case TUNER_TYPE_SP5730:

					if ((result = sp5730_set_tv_freq(fe, param->frequency)) < 0)
						return result;

					break;

				default:
				
					return -EFAULT;
					
			}

			if ((result = nxt6000_set_bandwidth(fe, param->u.ofdm.bandwidth)) < 0)
				return result;
			if ((result = nxt6000_set_guard_interval(fe, param->u.ofdm.guard_interval)) < 0)
				return result;
			if ((result = nxt6000_set_transmission_mode(fe, param->u.ofdm.transmission_mode)) < 0)
				return result;
			if ((result = nxt6000_set_inversion(fe, param->inversion)) < 0)
				return result;
			
			break;
		}

		default:

			return -EOPNOTSUPP;

	}

	return 0;
	
} 

static u8 demod_addr_tbl[] = {0x14, 0x18, 0x24, 0x28};

static int nxt6000_attach(struct dvb_i2c_bus *i2c, void **data)
{
	u8 addr_nr;
	u8 fe_count = 0;
	struct nxt6000_config *pnxt;

	dprintk("nxt6000: attach\n");
	
	pnxt = kmalloc(sizeof(demod_addr_tbl)*sizeof(struct nxt6000_config), GFP_KERNEL);
	if (NULL == pnxt) {
		dprintk("nxt6000: no memory for private data.\n");
		return -ENOMEM;
	}
	*data = pnxt;

	for (addr_nr = 0; addr_nr < sizeof(demod_addr_tbl); addr_nr++) {
		struct nxt6000_config *nxt = &pnxt[addr_nr];
	
		if (nxt6000_read(i2c, demod_addr_tbl[addr_nr], OFDM_MSC_REV) != NXT6000ASICDEVICE)
			continue;

		if (pll_test(i2c, demod_addr_tbl[addr_nr], 0xC0) == 0) {
			nxt->tuner_addr = 0xC0;
			nxt->tuner_type = TUNER_TYPE_ALP510;
			nxt->clock_inversion = 1;
	
			dprintk("nxt6000: detected TI ALP510 tuner at 0x%02X\n", nxt->tuner_addr);
		
		} else if (pll_test(i2c, demod_addr_tbl[addr_nr], 0xC2) == 0) {
			nxt->tuner_addr = 0xC2;
			nxt->tuner_type = TUNER_TYPE_SP5659;
			nxt->clock_inversion = 0;

			dprintk("nxt6000: detected MITEL SP5659 tuner at 0x%02X\n", nxt->tuner_addr);
		
		} else if (pll_test(i2c, demod_addr_tbl[addr_nr], 0xC0) == 0) {
			nxt->tuner_addr = 0xC0;
			nxt->tuner_type = TUNER_TYPE_SP5730;
			nxt->clock_inversion = 0;

			dprintk("nxt6000: detected SP5730 tuner at 0x%02X\n", nxt->tuner_addr);
		
		} else {
			printk("nxt6000: unable to detect tuner\n");
			continue;	
		}
		
		nxt->demod_addr = demod_addr_tbl[addr_nr];
	  
		dprintk("nxt6000: attached at %d:%d\n", i2c->adapter->num, i2c->id);
	
		dvb_register_frontend(nxt6000_ioctl, i2c, (void *)nxt, &nxt6000_info);
		
		fe_count++;
	}
	
	if (fe_count == 0) {
		kfree(pnxt);
		return -ENODEV;
	}
	
	return 0;
}

static void nxt6000_detach(struct dvb_i2c_bus *i2c, void *data)
{
	struct nxt6000_config *pnxt = (struct nxt6000_config *)data;
	dprintk("nxt6000: detach\n");
	dvb_unregister_frontend(nxt6000_ioctl, i2c);
	kfree(pnxt);
}

static __init int nxt6000_init(void)
{

	dprintk("nxt6000: init\n");
	
	return dvb_register_i2c_device(THIS_MODULE, nxt6000_attach, nxt6000_detach);
	
}

static __exit void nxt6000_exit(void)
{

	dprintk("nxt6000: cleanup\n");

	dvb_unregister_i2c_device(nxt6000_attach);

}

module_init(nxt6000_init);
module_exit(nxt6000_exit);
