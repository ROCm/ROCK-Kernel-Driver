/* 
    Driver for Alps BSRV2 QPSK Frontend

    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>

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

#include <linux/module.h>
#include <linux/init.h>

#include "dvb_frontend.h"

static int debug = 0;
#define dprintk	if (debug) printk


static
struct dvb_frontend_info bsrv2_info = {
	name: "Alps BSRV2",
	type: FE_QPSK,
	frequency_min: 950000,
	frequency_max: 2150000,
	frequency_stepsize: 250,           /* kHz for QPSK frontends */
	frequency_tolerance: 29500,
	symbol_rate_min: 1000000,
	symbol_rate_max: 45000000,
/*      symbol_rate_tolerance: ???,*/
	notifier_delay: 50,                /* 1/20 s */
	caps:   FE_CAN_INVERSION_AUTO |
		FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QPSK
};



static
u8 init_1893_tab [] = {
        0x01, 0xA4, 0x35, 0x81, 0x2A, 0x0d, 0x55, 0xC4,
        0x09, 0x69, 0x00, 0x86, 0x4c, 0x28, 0x7F, 0x00,
        0x00, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	
        0x80, 0x00, 0x31, 0xb0, 0x14, 0x00, 0xDC, 0x20,
        0x81, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x55, 0x00, 0x00, 0x7f, 0x00
};


static
u8 init_1893_wtab[] =
{
        1,1,1,1,1,1,1,1, 1,1,0,0,1,1,0,0,
        0,1,0,0,0,0,0,0, 1,0,1,1,0,0,0,1,
        1,1,1,0,0,0,0,0, 0,0,1,1,0,0,0,0,
        1,1,1,0,1,1
};


static
int ves1893_writereg (struct dvb_i2c_bus *i2c, u8 reg, u8 data)
{
        u8 buf [] = { 0x00, reg, data };
	struct i2c_msg msg = { addr: 0x08, flags: 0, buf: buf, len: 3 };
	int err;

        if ((err = i2c->xfer (i2c, &msg, 1)) != 1) {
		dprintk ("%s: writereg error (err == %i, reg == 0x%02x, data == 0x%02x)\n", __FUNCTION__, err, reg, data);
		return -EREMOTEIO;
	}

        return 0;
}


static
u8 ves1893_readreg (struct dvb_i2c_bus *i2c, u8 reg)
{
	int ret;
	u8 b0 [] = { 0x00, reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { addr: 0x08, flags: 0, buf: b0, len: 2 },
			   { addr: 0x08, flags: I2C_M_RD, buf: b1, len: 1 } };

	ret = i2c->xfer (i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];
}


static
int sp5659_write (struct dvb_i2c_bus *i2c, u8 data [4])
{
        int ret;
        struct i2c_msg msg = { addr: 0x61, flags: 0, buf: data, len: 4 };

        ret = i2c->xfer (i2c, &msg, 1);

        if (ret != 1)
                printk("%s: i/o error (ret == %i)\n", __FUNCTION__, ret);

        return (ret != 1) ? -1 : 0;
}



/**
 *   set up the downconverter frequency divisor for a
 *   reference clock comparision frequency of 125 kHz.
 */
static
int sp5659_set_tv_freq (struct dvb_i2c_bus *i2c, u32 freq, u8 pwr)
{
        u32 div = (freq + 479500) / 125;
        u8 buf [4] = { (div >> 8) & 0x7f, div & 0xff, 0x95, (pwr << 5) | 0x30 };

	return sp5659_write (i2c, buf);
}


static
int ves1893_init (struct dvb_i2c_bus *i2c)
{
	int i;
        
	dprintk("%s: init chip\n", __FUNCTION__);

	for (i=0; i<54; i++)
		if (init_1893_wtab[i])
			ves1893_writereg (i2c, i, init_1893_tab[i]);

	return 0;
}


static
int ves1893_clr_bit (struct dvb_i2c_bus *i2c)
{
        ves1893_writereg (i2c, 0, init_1893_tab[0] & 0xfe);
        ves1893_writereg (i2c, 0, init_1893_tab[0]);
        ves1893_writereg (i2c, 3, 0x00);
        return ves1893_writereg (i2c, 3, init_1893_tab[3]);
}


static
int ves1893_set_inversion (struct dvb_i2c_bus *i2c, fe_spectral_inversion_t inversion)
{
	u8 val;

	switch (inversion) {
	case INVERSION_OFF:
		val = 0xc0;
		break;
	case INVERSION_ON:
		val = 0x80;
		break;
	case INVERSION_AUTO:
		val = 0x40;
		break;
	default:
		return -EINVAL;
	}

	return ves1893_writereg (i2c, 0x0c, (init_1893_tab[0x0c] & 0x3f) | val);
}


static
int ves1893_set_fec (struct dvb_i2c_bus *i2c, fe_code_rate_t fec)
{
	if (fec == FEC_AUTO)
		return ves1893_writereg (i2c, 0x0d, 0x08);
	else if (fec < FEC_1_2 || fec > FEC_8_9)
		return -EINVAL;
	else
		return ves1893_writereg (i2c, 0x0d, fec - FEC_1_2);
}


static
fe_code_rate_t ves1893_get_fec (struct dvb_i2c_bus *i2c)
{
	return FEC_1_2 + ((ves1893_readreg (i2c, 0x0d) >> 4) & 0x7);
}


static
int ves1893_set_symbolrate (struct dvb_i2c_bus *i2c, u32 srate)
{
	u32 BDR;
        u32 ratio;
	u8  ADCONF, FCONF, FNR;
	u32 BDRI;
	u32 tmp;

	dprintk("%s: srate == %d\n", __FUNCTION__, srate);

	if (srate > 90100000UL/2)
		srate = 90100000UL/2;

	if (srate < 500000)
		srate = 500000;

#define MUL (1UL<<26)
#define FIN (90106000UL>>4)

	tmp = srate << 6;
	ratio = tmp / FIN;

	tmp = (tmp % FIN) << 8;
	ratio = (ratio << 8) + tmp / FIN;

	tmp = (tmp % FIN) << 8;
	ratio = (ratio << 8) + tmp / FIN;

	FNR = 0xff;

	if (ratio < MUL/3)           FNR = 0;
	if (ratio < (MUL*11)/50)     FNR = 1;
	if (ratio < MUL/6)           FNR = 2;
	if (ratio < MUL/9)           FNR = 3;
	if (ratio < MUL/12)          FNR = 4;
	if (ratio < (MUL*11)/200)    FNR = 5;
	if (ratio < MUL/24)          FNR = 6;
	if (ratio < (MUL*27)/1000)   FNR = 7;
	if (ratio < MUL/48)          FNR = 8;
	if (ratio < (MUL*137)/10000) FNR = 9;

	if (FNR == 0xff) {
		ADCONF = 0x89;
		FCONF  = 0x80;
		FNR	= 0;
	} else {
		ADCONF = 0x81;
		FCONF  = 0x88 | (FNR >> 1) | ((FNR & 0x01) << 5);
	}

	BDR = (( (ratio << (FNR >> 1)) >> 4) + 1) >> 1;
	BDRI = ( ((FIN << 8) / ((srate << (FNR >> 1)) >> 2)) + 1) >> 1;

        dprintk("FNR= %d\n", FNR);
        dprintk("ratio= %08x\n", ratio);
        dprintk("BDR= %08x\n", BDR);
        dprintk("BDRI= %02x\n", BDRI);

	if (BDRI > 0xff)
		BDRI = 0xff;

	ves1893_writereg (i2c, 0x06, 0xff & BDR);
	ves1893_writereg (i2c, 0x07, 0xff & (BDR >> 8));
	ves1893_writereg (i2c, 0x08, 0x0f & (BDR >> 16));

	ves1893_writereg (i2c, 0x09, BDRI);
	ves1893_writereg (i2c, 0x20, ADCONF);
	ves1893_writereg (i2c, 0x21, FCONF);

	if (srate < 6000000) 
		ves1893_writereg (i2c, 0x05, init_1893_tab[0x05] | 0x80);
	else
		ves1893_writereg (i2c, 0x05, init_1893_tab[0x05] & 0x7f);

	ves1893_writereg (i2c, 0x00, 0x00);
	ves1893_writereg (i2c, 0x00, 0x01);

	ves1893_clr_bit (i2c);

	return 0;
}


static
int ves1893_set_voltage (struct dvb_i2c_bus *i2c, fe_sec_voltage_t voltage)
{
	switch (voltage) {
	case SEC_VOLTAGE_13:
		return ves1893_writereg (i2c, 0x1f, 0x20);
	case SEC_VOLTAGE_18:
		return ves1893_writereg (i2c, 0x1f, 0x30);
	default:
		return -EINVAL;
	}
}


static
int bsrv2_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct dvb_i2c_bus *i2c = fe->i2c;
                
        switch (cmd) {
        case FE_GET_INFO:
		memcpy (arg, &bsrv2_info, sizeof(struct dvb_frontend_info));
		break;

        case FE_READ_STATUS:
	{
		fe_status_t *status = arg;
		u8 sync = ves1893_readreg (i2c, 0x0e);

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

		*ber = ves1893_readreg (i2c, 0x15);
                *ber |= (ves1893_readreg (i2c, 0x16) << 8);
                *ber |= ((ves1893_readreg (i2c, 0x17) & 0x0f) << 16);
		*ber *= 10;
		break;
	}

        case FE_READ_SIGNAL_STRENGTH:
	{
		u8 signal = ~ves1893_readreg (i2c, 0x0b);
		*((u16*) arg) = (signal << 8) | signal;
		break;
	}

        case FE_READ_SNR:
	{
		u8 snr = ~ves1893_readreg (i2c, 0x1c);
		*(u16*) arg = (snr << 8) | snr;
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS: 
	{
		*(u32*) arg = ves1893_readreg (i2c, 0x18) & 0x7f;

		if (*(u32*) arg == 0x7f)
			*(u32*) arg = 0xffffffff;   /* counter overflow... */
		
		ves1893_writereg (i2c, 0x18, 0x00);  /* reset the counter */
		ves1893_writereg (i2c, 0x18, 0x80);  /* dto. */
		break;
	}

        case FE_SET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;

		sp5659_set_tv_freq (i2c, p->frequency, 0);
		ves1893_set_inversion (i2c, p->inversion);
		ves1893_set_fec (i2c, p->u.qpsk.fec_inner);
//		sp5659_set_tv_freq (i2c, p->frequency, 0);
		ves1893_set_symbolrate (i2c, p->u.qpsk.symbol_rate);
                break;
        }

	case FE_GET_FRONTEND:
	{
		struct dvb_frontend_parameters *p = arg;
		s32 afc;

		afc = ((int)((char)(ves1893_readreg (i2c, 0x0a) << 1)))/2;
		afc = (afc * (int)(p->u.qpsk.symbol_rate/8))/16;

		p->frequency += afc;
		p->inversion = (ves1893_readreg (i2c, 0x0f) & 2) ? 
					INVERSION_ON : INVERSION_OFF;
		p->u.qpsk.fec_inner = ves1893_get_fec (i2c);
	/*  XXX FIXME: timing offset !! */
		break;
	}

        case FE_SLEEP:
		ves1893_writereg (i2c, 0x1f, 0x00);    /*  LNB power off  */
		return ves1893_writereg (i2c, 0x00, 0x08);

        case FE_INIT:
		return ves1893_init (i2c);

	case FE_RESET:
		return ves1893_clr_bit (i2c);

	case FE_SET_TONE:
		return -EOPNOTSUPP;  /* the ves1893 can generate the 22k */
				     /* let's implement this when we have */
				     /* a box that uses the 22K_0 pin... */
	case FE_SET_VOLTAGE:
		return ves1893_set_voltage (i2c, (fe_sec_voltage_t) arg);

	default:
		return -EOPNOTSUPP;
        };
        
        return 0;
} 


static
int bsrv2_attach (struct dvb_i2c_bus *i2c)
{
	if ((ves1893_readreg (i2c, 0x1e) & 0xf0) != 0xd0)
		return -ENODEV;

	dvb_register_frontend (bsrv2_ioctl, i2c, NULL, &bsrv2_info);

	return 0;
}


static
void bsrv2_detach (struct dvb_i2c_bus *i2c)
{
	dvb_unregister_frontend (bsrv2_ioctl, i2c);
}


static
int __init init_bsrv2 (void)
{
	return dvb_register_i2c_device (THIS_MODULE, bsrv2_attach, bsrv2_detach);
}


static 
void __exit exit_bsrv2 (void)
{
	dvb_unregister_i2c_device (bsrv2_attach);
}


module_init(init_bsrv2);
module_exit(exit_bsrv2);


MODULE_DESCRIPTION("BSRV2 DVB-S Frontend");
MODULE_AUTHOR("Ralph Metzler");
MODULE_LICENSE("GPL");
MODULE_PARM(debug,"i");

