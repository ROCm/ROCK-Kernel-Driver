/* 
    Driver for VES1893 and VES1993 QPSK Frontends

    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>
    Copyright (C) 2001 Ronny Strutz <3des@elitedvb.de>
    Copyright (C) 2002 Dennis Noermann <dennis.noermann@noernet.de>
    Copyright (C) 2002-2003 Andreas Oberritter <obi@linuxtv.org>

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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "dvb_frontend.h"

static int debug = 0;
#define dprintk	if (debug) printk

static int board_type = 0;
#define BOARD_SIEMENS_PCI	0
#define BOARD_NOKIA_DBOX2	1
#define BOARD_SAGEM_DBOX2	2

static int demod_type = 0;
#define DEMOD_VES1893		0
#define DEMOD_VES1993		1

static struct dvb_frontend_info ves1x93_info = {
	.name			= "VES1x93",
	.type			= FE_QPSK,
	.frequency_min		= 950000,
	.frequency_max		= 2150000,
	.frequency_stepsize	= 125,		 /* kHz for QPSK frontends */
	.frequency_tolerance	= 29500,
	.symbol_rate_min	= 1000000,
	.symbol_rate_max	= 45000000,
/*	.symbol_rate_tolerance	=	???,*/
	.notifier_delay		= 50,		     /* 1/20 s */
	.caps = FE_CAN_INVERSION_AUTO |
		FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QPSK
};


/**
 * nokia dbox2 (ves1893) and sagem dbox2 (ves1993)
 * need bit AGCR[PWMS] set to 1
 */

static u8 init_1893_tab [] = {
	0x01, 0xa4, 0x35, 0x80, 0x2a, 0x0b, 0x55, 0xc4,
	0x09, 0x69, 0x00, 0x86, 0x4c, 0x28, 0x7f, 0x00,
	0x00, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x80, 0x00, 0x21, 0xb0, 0x14, 0x00, 0xdc, 0x00,
	0x81, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x55, 0x00, 0x00, 0x7f, 0x00
};


static u8 init_1993_tab [] = {
	0x00, 0x9c, 0x35, 0x80, 0x6a, 0x09, 0x72, 0x8c,
	0x09, 0x6b, 0x00, 0x00, 0x4c, 0x08, 0x00, 0x00,
	0x00, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x80, 0x40, 0x21, 0xb0, 0x00, 0x00, 0x00, 0x10,
	0x81, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x55, 0x03, 0x00, 0x00, 0x00, 0x00, 0x03,
	0x00, 0x00, 0x0e, 0x80, 0x00
};


static u8 * init_1x93_tab;


static u8 init_1893_wtab[] =
{
	1,1,1,1,1,1,1,1, 1,1,0,0,1,1,0,0,
	0,1,0,0,0,0,0,0, 1,0,1,1,0,0,0,1,
	1,1,1,0,0,0,0,0, 0,0,1,1,0,0,0,0,
	1,1,1,0,1,1
};


static u8 init_1993_wtab[] =
{
	1,1,1,1,1,1,1,1, 1,1,0,0,1,1,0,0,
	0,1,0,0,0,0,0,0, 1,1,1,1,0,0,0,1,
	1,1,1,0,0,0,0,0, 0,0,1,1,0,0,0,0,
	1,1,1,0,1,1,1,1, 1,1,1,1,1
};

struct ves1x93_state {
	fe_spectral_inversion_t inversion;
	struct i2c_adapter *i2c;
	struct dvb_adapter *dvb;
};



static int ves1x93_writereg (struct i2c_adapter *i2c, u8 reg, u8 data)
{
	u8 buf [] = { 0x00, reg, data };
	struct i2c_msg msg = { .addr = 0x08, .flags = 0, .buf = buf, .len = 3 };
	int err;

	if ((err = i2c_transfer (i2c, &msg, 1)) != 1) {
		dprintk ("%s: writereg error (err == %i, reg == 0x%02x, data == 0x%02x)\n", __FUNCTION__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}


static u8 ves1x93_readreg (struct i2c_adapter *i2c, u8 reg)
{
	int ret;
	u8 b0 [] = { 0x00, reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = 0x08, .flags = 0, .buf = b0, .len = 2 },
			   { .addr = 0x08, .flags = I2C_M_RD, .buf = b1, .len = 1 } };

	ret = i2c_transfer (i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];
}


static int tuner_write (struct i2c_adapter *i2c, u8 *data, u8 len)
{
	int ret;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = len };

	ves1x93_writereg(i2c, 0x00, 0x11);
	ret = i2c_transfer (i2c, &msg, 1);
	ves1x93_writereg(i2c, 0x00, 0x01);

	if (ret != 1)
		printk("%s: i/o error (ret == %i)\n", __FUNCTION__, ret);

	return (ret != 1) ? -1 : 0;
}



/**
 *   set up the downconverter frequency divisor for a
 *   reference clock comparision frequency of 125 kHz.
 */
static int sp5659_set_tv_freq (struct i2c_adapter *i2c, u32 freq)
{
	u8 pwr = 0;
   	u8 buf[4];
	u32 div = (freq + 479500) / 125;
   
	if (freq > 2000000) pwr = 3;
	else if (freq > 1800000) pwr = 2;
	else if (freq > 1600000) pwr = 1;
	else if (freq > 1200000) pwr = 0;
	else if (freq >= 1100000) pwr = 1;
	else pwr = 2;

   	buf[0] = (div >> 8) & 0x7f;
   	buf[1] = div & 0xff;
   	buf[2] = ((div & 0x18000) >> 10) | 0x95;
   	buf[3] = (pwr << 6) | 0x30;
   	
   	// NOTE: since we're using a prescaler of 2, we set the
	// divisor frequency to 62.5kHz and divide by 125 above

	return tuner_write (i2c, buf, sizeof(buf));
}


static int tsa5059_set_tv_freq (struct i2c_adapter *i2c, u32 freq)
{
	int ret;
	u8 buf [2];

	freq /= 1000;

	buf[0] = (freq >> 8) & 0x7F;
	buf[1] = freq & 0xFF;

	ret = tuner_write(i2c, buf, sizeof(buf));

	return ret;
}


static int tuner_set_tv_freq (struct i2c_adapter *i2c, u32 freq)
{
	if ((demod_type == DEMOD_VES1893) && (board_type == BOARD_SIEMENS_PCI))
		return sp5659_set_tv_freq (i2c, freq);
	else if (demod_type == DEMOD_VES1993)
		return tsa5059_set_tv_freq (i2c, freq);

	return -EINVAL;
}


static int ves1x93_init (struct i2c_adapter *i2c)
{
	int i;
	int size;
	u8 *init_1x93_wtab;
 
	dprintk("%s: init chip\n", __FUNCTION__);

	switch (demod_type) {
	case DEMOD_VES1893:
		init_1x93_tab = init_1893_tab;
		init_1x93_wtab = init_1893_wtab;
		size = sizeof(init_1893_tab);
		if (board_type == BOARD_NOKIA_DBOX2)
			init_1x93_tab[0x05] |= 0x20; /* invert PWM */
		break;

	case DEMOD_VES1993:
		init_1x93_tab = init_1993_tab;
		init_1x93_wtab = init_1993_wtab;
		size = sizeof(init_1993_tab);
		if (board_type == BOARD_SAGEM_DBOX2)
			init_1x93_tab[0x05] |= 0x20; /* invert PWM */
		break;

	default:
		return -EINVAL;
	}

	for (i = 0; i < size; i++)
		if (init_1x93_wtab[i])
			ves1x93_writereg (i2c, i, init_1x93_tab[i]);

	if (demod_type == DEMOD_VES1993) {
		if (board_type == BOARD_NOKIA_DBOX2)
			tuner_write(i2c, "\x06\x5c\x83\x60", 4);
		else if (board_type == BOARD_SAGEM_DBOX2)
			tuner_write(i2c, "\x25\x70\x92\x40", 4);
	}

	return 0;
}


static int ves1x93_clr_bit (struct i2c_adapter *i2c)
{
	msleep(10);
	ves1x93_writereg (i2c, 0, init_1x93_tab[0] & 0xfe);
	ves1x93_writereg (i2c, 0, init_1x93_tab[0]);
	msleep(50);
	return 0;
}

static int ves1x93_set_inversion (struct i2c_adapter *i2c, fe_spectral_inversion_t inversion)
{
	u8 val;

	/*
	 * inversion on/off are interchanged because i and q seem to
	 * be swapped on the hardware
	 */

	switch (inversion) {
	case INVERSION_OFF:
		val = 0xc0;
		break;
	case INVERSION_ON:
		val = 0x80;
		break;
	case INVERSION_AUTO:
		val = 0x00;
		break;
	default:
		return -EINVAL;
	}

	return ves1x93_writereg (i2c, 0x0c, (init_1x93_tab[0x0c] & 0x3f) | val);
}


static int ves1x93_set_fec (struct i2c_adapter *i2c, fe_code_rate_t fec)
{
	if (fec == FEC_AUTO)
		return ves1x93_writereg (i2c, 0x0d, 0x08);
	else if (fec < FEC_1_2 || fec > FEC_8_9)
		return -EINVAL;
	else
		return ves1x93_writereg (i2c, 0x0d, fec - FEC_1_2);
}


static fe_code_rate_t ves1x93_get_fec (struct i2c_adapter *i2c)
{
	return FEC_1_2 + ((ves1x93_readreg (i2c, 0x0d) >> 4) & 0x7);
}


static int ves1x93_set_symbolrate (struct i2c_adapter *i2c, u32 srate)
{
	u32 BDR;
	u32 ratio;
	u8  ADCONF, FCONF, FNR;
	u32 BDRI;
	u32 tmp;
	u32 XIN, FIN;

	dprintk("%s: srate == %d\n", __FUNCTION__, (unsigned int) srate);

	switch (board_type) {
	case BOARD_SIEMENS_PCI:
		XIN = 90100000UL;
		break;
	case BOARD_NOKIA_DBOX2:
		if (demod_type == DEMOD_VES1893)
			XIN = 91000000UL;
		else if (demod_type == DEMOD_VES1993)
			XIN = 96000000UL;
		else
			return -EINVAL;
		break;
	case BOARD_SAGEM_DBOX2:
		XIN = 92160000UL;
		break;
	default:
		return -EINVAL;
	}

	if (srate > XIN/2)
		srate = XIN/2;

	if (srate < 500000)
		srate = 500000;

#define MUL (1UL<<26)

	FIN = (XIN + 6000) >> 4;

	tmp = srate << 6;
	ratio = tmp / FIN;

	tmp = (tmp % FIN) << 8;
	ratio = (ratio << 8) + tmp / FIN;

	tmp = (tmp % FIN) << 8;
	ratio = (ratio << 8) + tmp / FIN;

	FNR = 0xff;

	if (ratio < MUL/3)	     FNR = 0;
	if (ratio < (MUL*11)/50)     FNR = 1;
	if (ratio < MUL/6)	     FNR = 2;
	if (ratio < MUL/9)	     FNR = 3;
	if (ratio < MUL/12)	     FNR = 4;
	if (ratio < (MUL*11)/200)    FNR = 5;
	if (ratio < MUL/24)	     FNR = 6;
	if (ratio < (MUL*27)/1000)   FNR = 7;
	if (ratio < MUL/48)	     FNR = 8;
	if (ratio < (MUL*137)/10000) FNR = 9;

	if (FNR == 0xff) {
		ADCONF = 0x89;
		FCONF  = 0x80;
		FNR	= 0;
	} else {
		ADCONF = 0x81;
		FCONF  = 0x88 | (FNR >> 1) | ((FNR & 0x01) << 5);
		/*FCONF	 = 0x80 | ((FNR & 0x01) << 5) | (((FNR > 1) & 0x03) << 3) | ((FNR >> 1) & 0x07);*/
	}

	BDR = (( (ratio << (FNR >> 1)) >> 4) + 1) >> 1;
	BDRI = ( ((FIN << 8) / ((srate << (FNR >> 1)) >> 2)) + 1) >> 1;

	dprintk("FNR= %d\n", FNR);
	dprintk("ratio= %08x\n", (unsigned int) ratio);
	dprintk("BDR= %08x\n", (unsigned int) BDR);
	dprintk("BDRI= %02x\n", (unsigned int) BDRI);

	if (BDRI > 0xff)
		BDRI = 0xff;

	ves1x93_writereg (i2c, 0x06, 0xff & BDR);
	ves1x93_writereg (i2c, 0x07, 0xff & (BDR >> 8));
	ves1x93_writereg (i2c, 0x08, 0x0f & (BDR >> 16));

	ves1x93_writereg (i2c, 0x09, BDRI);
	ves1x93_writereg (i2c, 0x20, ADCONF);
	ves1x93_writereg (i2c, 0x21, FCONF);

	if (srate < 6000000) 
		ves1x93_writereg (i2c, 0x05, init_1x93_tab[0x05] | 0x80);
	else
		ves1x93_writereg (i2c, 0x05, init_1x93_tab[0x05] & 0x7f);

	/* ves1993 hates this, will lose lock */
	if (demod_type != DEMOD_VES1993)
		ves1x93_clr_bit (i2c);

	return 0;
}

static int ves1x93_set_voltage (struct i2c_adapter *i2c, fe_sec_voltage_t voltage)
{
	switch (voltage) {
	case SEC_VOLTAGE_13:
		return ves1x93_writereg (i2c, 0x1f, 0x20);
	case SEC_VOLTAGE_18:
		return ves1x93_writereg (i2c, 0x1f, 0x30);
	case SEC_VOLTAGE_OFF:
		return ves1x93_writereg (i2c, 0x1f, 0x00);
	default:
		return -EINVAL;
	}
}


static int ves1x93_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct ves1x93_state *state = (struct ves1x93_state *) fe->data;
	struct i2c_adapter *i2c = state->i2c;

	switch (cmd) {
	case FE_GET_INFO:
		memcpy (arg, &ves1x93_info, sizeof(struct dvb_frontend_info));
		break;

	case FE_READ_STATUS:
	{
		fe_status_t *status = arg;
		u8 sync = ves1x93_readreg (i2c, 0x0e);

		/*
		 * The ves1893 sometimes returns sync values that make no sense,
		 * because, e.g., the SIGNAL bit is 0, while some of the higher
		 * bits are 1 (and how can there be a CARRIER w/o a SIGNAL?).
		 * Tests showed that the the VITERBI and SYNC bits are returned
		 * reliably, while the SIGNAL and CARRIER bits ar sometimes wrong.
		 * If such a case occurs, we read the value again, until we get a
		 * valid value.
		 */
		int maxtry = 10; /* just for safety - let's not get stuck here */
		while ((sync & 0x03) != 0x03 && (sync & 0x0c) && maxtry--) {
			msleep(10);
			sync = ves1x93_readreg (i2c, 0x0e);
		}

		*status = 0;

		if (sync & 1)
			*status |= FE_HAS_SIGNAL;

		if (sync & 2)
			*status |= FE_HAS_CARRIER;

		if (sync & 4)
			*status |= FE_HAS_VITERBI;

		if (sync & 8)
			*status |= FE_HAS_SYNC;

		if ((sync & 0x1f) == 0x1f)
			*status |= FE_HAS_LOCK;

		break;
	}

	case FE_READ_BER:
	{
		u32 *ber = (u32 *) arg;

		*ber = ves1x93_readreg (i2c, 0x15);
		*ber |= (ves1x93_readreg (i2c, 0x16) << 8);
		*ber |= ((ves1x93_readreg (i2c, 0x17) & 0x0F) << 16);
		*ber *= 10;
		break;
	}

	case FE_READ_SIGNAL_STRENGTH:
	{
		u8 signal = ~ves1x93_readreg (i2c, 0x0b);
		*((u16*) arg) = (signal << 8) | signal;
		break;
	}

	case FE_READ_SNR:
	{
		u8 snr = ~ves1x93_readreg (i2c, 0x1c);
		*(u16*) arg = (snr << 8) | snr;
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS: 
	{
		*(u32*) arg = ves1x93_readreg (i2c, 0x18) & 0x7f;

		if (*(u32*) arg == 0x7f)
			*(u32*) arg = 0xffffffff;   /* counter overflow... */
		
		ves1x93_writereg (i2c, 0x18, 0x00);  /* reset the counter */
		ves1x93_writereg (i2c, 0x18, 0x80);  /* dto. */
		break;
	}

	case FE_SET_FRONTEND:
	{
		struct dvb_frontend_parameters *p = arg;

		tuner_set_tv_freq (i2c, p->frequency);
		ves1x93_set_inversion (i2c, p->inversion);
		ves1x93_set_fec (i2c, p->u.qpsk.fec_inner);
		ves1x93_set_symbolrate (i2c, p->u.qpsk.symbol_rate);
		state->inversion = p->inversion;
		break;
	}

	case FE_GET_FRONTEND:
	{
		struct dvb_frontend_parameters *p = arg;
		int afc;

		afc = ((int)((char)(ves1x93_readreg (i2c, 0x0a) << 1)))/2;
		afc = (afc * (int)(p->u.qpsk.symbol_rate/1000/8))/16;

		p->frequency -= afc;

		/*
		 * inversion indicator is only valid
		 * if auto inversion was used
		 */
		if (state->inversion == INVERSION_AUTO)
			p->inversion = (ves1x93_readreg (i2c, 0x0f) & 2) ? 
					INVERSION_OFF : INVERSION_ON;
		p->u.qpsk.fec_inner = ves1x93_get_fec (i2c);
	/*  XXX FIXME: timing offset !! */
		break;
	}

	case FE_SLEEP:
		if (board_type == BOARD_SIEMENS_PCI)
			ves1x93_writereg (i2c, 0x1f, 0x00);    /*  LNB power off  */
		return ves1x93_writereg (i2c, 0x00, 0x08);

	case FE_INIT:
		return ves1x93_init (i2c);

	case FE_SET_TONE:
		return -EOPNOTSUPP;  /* the ves1893 can generate the 22k */
				     /* let's implement this when we have */
				     /* a box that uses the 22K_0 pin... */

	case FE_SET_VOLTAGE:
		return ves1x93_set_voltage (i2c, (fe_sec_voltage_t) arg);

	default:
		return -EOPNOTSUPP;
	};
	
	return 0;
} 

static struct i2c_client client_template;

static int attach_adapter(struct i2c_adapter *adapter)
{
	struct i2c_client *client;
	struct ves1x93_state* state;
	u8 identity = ves1x93_readreg(adapter, 0x1e);
	int ret;
	
	switch (identity) {
	case 0xdc: /* VES1893A rev1 */
		printk("ves1x93: Detected ves1893a rev1\n");
		demod_type = DEMOD_VES1893;
		ves1x93_info.name[4] = '8';
		break;
	case 0xdd: /* VES1893A rev2 */
		printk("ves1x93: Detected ves1893a rev2\n");
		demod_type = DEMOD_VES1893;
		ves1x93_info.name[4] = '8';
		break;
	case 0xde: /* VES1993 */
		printk("ves1x93: Detected ves1993\n");
		demod_type = DEMOD_VES1993;
		ves1x93_info.name[4] = '9';
		break;
	default:
		dprintk("VES1x93 not found (identity %02x)\n", identity);
		return -ENODEV;
	}

	if ((state = kmalloc(sizeof(struct ves1x93_state), GFP_KERNEL)) == NULL) {
		return -ENOMEM;
	}

	if (NULL == (client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL))) {
		kfree(state);
		return -ENOMEM;
	}

	state->inversion = INVERSION_OFF;
	state->i2c = adapter;
	
	memcpy(client, &client_template, sizeof(struct i2c_client));
	client->adapter = adapter;
	client->addr = (0x08>>1);
	i2c_set_clientdata(client, (void*)state);
	
	ret = i2c_attach_client(client);
	if (ret) {
		kfree(client);
		kfree(state);
		return -EFAULT;
	}

	BUG_ON(!state->dvb);

	ret = dvb_register_frontend(ves1x93_ioctl, state->dvb, state,
					&ves1x93_info, THIS_MODULE);
	if (ret) {
		i2c_detach_client(client);
		kfree(client);
		kfree(state);
		return -EFAULT;
	}

	return 0;
}

static int detach_client(struct i2c_client *client)
{
	struct ves1x93_state *state = (struct ves1x93_state*)i2c_get_clientdata(client);
	dvb_unregister_frontend(ves1x93_ioctl, state->dvb);
	i2c_detach_client(client);
	BUG_ON(state->dvb);
	kfree(client);
	kfree(state);
	return 0;
}

static int command (struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct ves1x93_state *state = (struct ves1x93_state*)i2c_get_clientdata(client);

	dprintk ("%s\n", __FUNCTION__);

	switch (cmd) {
	case FE_REGISTER: {
		state->dvb = (struct dvb_adapter*)arg;
		break;
	}
	case FE_UNREGISTER: {
		state->dvb = NULL;
		break;
	}
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static struct i2c_driver driver = {
	.owner 		= THIS_MODULE,
	.name 		= "ves1x93",
	.id 		= I2C_DRIVERID_DVBFE_VES1X93,
	.flags 		= I2C_DF_NOTIFY,
	.attach_adapter = attach_adapter,
	.detach_client 	= detach_client,
	.command 	= command,
};

static struct i2c_client client_template = {
	I2C_DEVNAME("ves1x93"),
	.flags 		= I2C_CLIENT_ALLOW_USE,
	.driver  	= &driver,
};

static int __init init_ves1x93 (void)
{
	switch (board_type) {
	case BOARD_NOKIA_DBOX2:
		dprintk("%s: NOKIA_DBOX2\n", __FILE__);
		break;
	case BOARD_SAGEM_DBOX2:
		dprintk("%s: SAGEM_DBOX2\n", __FILE__);
		break;
	case BOARD_SIEMENS_PCI:
		dprintk("%s: SIEMENS_PCI\n", __FILE__);
		break;
	default:
		return -EIO;
	}

	return i2c_add_driver(&driver);
}


static void __exit exit_ves1x93 (void)
{
	if (i2c_del_driver(&driver))
		printk("vex1x93: driver deregistration failed\n");
}

module_init(init_ves1x93);
module_exit(exit_ves1x93);


MODULE_DESCRIPTION("VES1x93 DVB-S Frontend");
MODULE_AUTHOR("Ralph Metzler");
MODULE_LICENSE("GPL");
MODULE_PARM(debug,"i");
MODULE_PARM(board_type,"i");

