/* 
    VES1820  - Single Chip Cable Channel Receiver driver module
               used on the the Siemens DVB-C cards

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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>

#include "compat.h"
#include "dvb_frontend.h"


static int debug = 0;
#define dprintk	if (debug) printk


/**
 *  since we need only a few bits to store internal state we don't allocate
 *  extra memory but use frontend->data as bitfield
 */

#define SET_PWM(frontend,pwm) do { 		\
	(int) frontend->data &= ~0xff; 		\
	(int) frontend->data |= pwm; 		\
} while (0)

#define SET_REG0(frontend,reg0) do {		\
	(int) frontend->data &= ~(0xff << 8); 	\
	(int) frontend->data |= reg0 << 8; 	\
} while (0)

#define SET_TUNER(frontend,type) do {		\
	(int) frontend->data &= ~(0xff << 16); 	\
	(int) frontend->data |= type << 16;	\
} while (0)

#define GET_PWM(frontend) ((u8) ((int) frontend->data & 0xff))
#define GET_REG0(frontend) ((u8) (((int) frontend->data >> 8) & 0xff))
#define GET_TUNER(frontend) ((u8) (((int) frontend->data >> 16) & 0xff))



static
struct dvb_frontend_info ves1820_info = {
	.name	= "VES1820/Grundig tuner as used on the Siemens DVB-C card",
	.type	= FE_QAM,
	.frequency_stepsize	= 62500,
	.frequency_min		= 51000000,
	.frequency_max		= 858000000,
	.caps	= FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 |
		  FE_CAN_QAM_128 | FE_CAN_QAM_256
};



static
u8 ves1820_inittab [] =
{
  0x69, 0x6A, 0x9B, 0x0A, 0x52, 0x46, 0x26, 0x1A,
  0x43, 0x6A, 0xAA, 0xAA, 0x1E, 0x85, 0x43, 0x28,
  0xE0, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x40
};


static
int ves1820_writereg (struct dvb_i2c_bus *i2c, u8 reg, u8 data)
{
        int ret;
        u8 buf[] = { 0x00, reg, data };
	struct i2c_msg msg = { .addr = 0x09, .flags = 0, .buf = buf, .len = 3 };

	ret = i2c->xfer (i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: writereg error "
			"(reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			__FUNCTION__, reg, data, ret);

	mdelay(10);
	return (ret != 1) ? -EREMOTEIO : 0;
}


static
u8 ves1820_readreg (struct dvb_i2c_bus *i2c, u8 reg)
{
	int ret;
	u8 b0 [] = { 0x00, reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = 0x09, .flags = 0, .buf = b0, .len = 2 },
	                   { .addr = 0x09, .flags = I2C_M_RD, .buf = b1, .len = 1 } };


	ret = i2c->xfer (i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];
}


static
int tuner_write (struct dvb_i2c_bus *i2c, u8 addr, u8 data [4])
{
        int ret;
        struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = data, .len = 4 };

        ret = i2c->xfer (i2c, &msg, 1);

        if (ret != 1)
                printk("%s: i/o error (ret == %i)\n", __FUNCTION__, ret);

        return (ret != 1) ? -EREMOTEIO : 0;
}


/**
 *   set up the downconverter frequency divisor for a
 *   reference clock comparision frequency of 62.5 kHz.
 */
static
int tuner_set_tv_freq (struct dvb_frontend *frontend, u32 freq)
{
        u32 div;
	static u8 addr [] = { 0x61, 0x62 };
	static u8 byte3 [] = { 0x8e, 0x85 };
	int tuner_type = GET_TUNER(frontend);
        u8 buf [4];

	div = (freq + 36250000 + 31250) / 62500;
	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = byte3[tuner_type];

	if (tuner_type == 1) {
		buf[2] |= (div >> 10) & 0x60;
		buf[3] = (freq < 174000000 ? 0x88 :
			  freq < 470000000 ? 0x84 : 0x81);
	} else {
		buf[3] = (freq < 174000000 ? 0xa1 :
			  freq < 454000000 ? 0x92 : 0x34);
	}

        return tuner_write (frontend->i2c, addr[tuner_type], buf);
}


static
int probe_tuner (struct dvb_frontend *frontend)
{
	struct dvb_i2c_bus *i2c = frontend->i2c;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = NULL, .len = 0 };

	if (i2c->xfer(i2c, &msg, 1) == 1) {
		SET_TUNER(frontend,0);
		printk ("%s: setup for tuner spXXXX\n", __FILE__);
	} else {
		SET_TUNER(frontend,1);
		printk ("%s: setup for tuner sp5659c\n", __FILE__);
	}

	return 0;
}


static
int ves1820_init (struct dvb_frontend *frontend)
{
	struct dvb_i2c_bus *i2c = frontend->i2c;
	u8 b0 [] = { 0xff };
	u8 pwm;
	int i;
	struct i2c_msg msg [] = { { .addr = 0x50, .flags = 0, .buf = b0, .len = 1 },
	                   { .addr = 0x50, .flags = I2C_M_RD, .buf = &pwm, .len = 1 } };
        
        dprintk("VES1820: init chip\n");

	i2c->xfer (i2c, msg, 2);

	dprintk("VES1820: pwm=%02x\n", pwm);

	if (pwm == 0xff)
                pwm=0x48;

        ves1820_writereg (i2c, 0, 0);

	for (i=0; i<53; i++)
                ves1820_writereg (i2c, i, ves1820_inittab[i]);

        ves1820_writereg (i2c, 0x34, pwm);

	(int) frontend->data = 0;
	SET_PWM(frontend,pwm);

	probe_tuner (frontend);

	return 0;
}


static
int ves1820_setup_reg0 (struct dvb_frontend *frontend,
			u8 real_qam, fe_spectral_inversion_t inversion)
{
	struct dvb_i2c_bus *i2c = frontend->i2c;
	u8 reg0 = (ves1820_inittab[0] & 0xe3) | (real_qam << 2);

	switch (inversion) {
	case INVERSION_OFF:      /* XXX FIXME: reversed?? p. 25  */
		reg0 |= 0x20;
		break;

	case INVERSION_ON:
		reg0 &= 0xdf;
		break;

	default:
		return -EINVAL;
	}

	SET_REG0(frontend, reg0);

	ves1820_writereg (i2c, 0x00, reg0 & 0xfe);
        ves1820_writereg (i2c, 0x00, reg0);

	return 0;
}


static
int ves1820_reset (struct dvb_frontend *frontend)
{
	struct dvb_i2c_bus *i2c = frontend->i2c;
	u8 reg0 = GET_REG0(frontend);

	ves1820_writereg (i2c, 0x00, reg0 & 0xfe);
        ves1820_writereg (i2c, 0x00, reg0);

	return 0;
}


static
void ves1820_reset_uncorrected_block_counter (struct dvb_i2c_bus *i2c)
{
	ves1820_writereg (i2c, 0x10, ves1820_inittab[0x10] & 0xdf);
        ves1820_writereg (i2c, 0x10, ves1820_inittab[0x10]);
}


static
int ves1820_set_symbolrate (struct dvb_i2c_bus *i2c, u32 symbolrate)
{
        s32 BDR; 
        s32 BDRI;
        s16 SFIL=0;
        u16 NDEC = 0;
        u32 tmp, ratio;

#define XIN 57840000UL
#define FIN (57840000UL>>4)

        if (symbolrate > XIN/2) 
                symbolrate = XIN/2;

	if (symbolrate < 500000)
                symbolrate = 500000;

        if (symbolrate < XIN/16) NDEC = 1;
        if (symbolrate < XIN/32) NDEC = 2;
        if (symbolrate < XIN/64) NDEC = 3;

        if (symbolrate < (u32)(XIN/12.3)) SFIL = 1;
        if (symbolrate < (u32)(XIN/16))	 SFIL = 0;
        if (symbolrate < (u32)(XIN/24.6)) SFIL = 1;
        if (symbolrate < (u32)(XIN/32))	 SFIL = 0;
        if (symbolrate < (u32)(XIN/49.2)) SFIL = 1;
        if (symbolrate < (u32)(XIN/64))	 SFIL = 0;
        if (symbolrate < (u32)(XIN/98.4)) SFIL = 1;
        
        symbolrate <<= NDEC;
        ratio = (symbolrate << 4) / FIN;
        tmp =  ((symbolrate << 4) % FIN) << 8;
        ratio = (ratio << 8) + tmp / FIN;
        tmp = (tmp % FIN) << 8;
        ratio = (ratio << 8) + (tmp + FIN/2) / FIN;
        
        BDR = ratio;
        BDRI = (((XIN << 5) / symbolrate) + 1) / 2;
        
        if (BDRI > 0xFF) 
                BDRI = 0xFF;
        
        SFIL = (SFIL << 4) | ves1820_inittab[0x0E];
        
        NDEC = (NDEC << 6) | ves1820_inittab[0x03];

        ves1820_writereg (i2c, 0x03, NDEC);
        ves1820_writereg (i2c, 0x0a, BDR&0xff);
        ves1820_writereg (i2c, 0x0b, (BDR>> 8)&0xff);
        ves1820_writereg (i2c, 0x0c, (BDR>>16)&0x3f);

        ves1820_writereg (i2c, 0x0d, BDRI);
        ves1820_writereg (i2c, 0x0e, SFIL);

        return 0;
}


static
void ves1820_reset_pwm (struct dvb_frontend *frontend) 
{
	u8 pwm = GET_PWM(frontend);

	ves1820_writereg (frontend->i2c, 0x34, pwm); 
}


typedef struct {
        fe_modulation_t  QAM_Mode;
        int         NoOfSym;
        u8          Reg1;
        u8          Reg5;
        u8          Reg8;
        u8          Reg9;
} QAM_SETTING;


QAM_SETTING QAM_Values[] = {	
        {  QAM_16,  16, 140, 164, 162, 145 },
        {  QAM_32,  32, 140, 120, 116, 150 },
        {  QAM_64,  64, 106,  70,  67, 106 },
        { QAM_128, 128, 120,  54,  52, 126 },
        { QAM_256, 256,  92,  38,  35, 107 }
};


static
int ves1820_set_parameters (struct dvb_frontend *frontend,
			    struct dvb_frontend_parameters *p)
{
	struct dvb_i2c_bus* i2c = frontend->i2c;
        int real_qam;
        
        switch (p->u.qam.modulation) {
        case QAM_16 : real_qam = 0; break;
        case QAM_32 : real_qam = 1; break;
        case QAM_64 : real_qam = 2; break;
        case QAM_128: real_qam = 3; break;
        case QAM_256: real_qam = 4; break;
        default:
                return -EINVAL;
        }

	tuner_set_tv_freq (frontend, p->frequency);
	ves1820_set_symbolrate (i2c, p->u.qam.symbol_rate);
	ves1820_reset_pwm (frontend);

        ves1820_writereg (i2c, 0x01, QAM_Values[real_qam].Reg1);
        ves1820_writereg (i2c, 0x05, QAM_Values[real_qam].Reg5);
        ves1820_writereg (i2c, 0x08, QAM_Values[real_qam].Reg8);
        ves1820_writereg (i2c, 0x09, QAM_Values[real_qam].Reg9);

	ves1820_setup_reg0 (frontend, real_qam, p->inversion);
	
	return 0;
}



static
int ves1820_ioctl (struct dvb_frontend *frontend, unsigned int cmd, void *arg)
{
        switch (cmd) {
	case FE_GET_INFO:
		memcpy (arg, &ves1820_info, sizeof(struct dvb_frontend_info));
		break;

        case FE_READ_STATUS:
	{
		fe_status_t *status = (fe_status_t *) arg;
		int sync;

		*status = 0;

                sync = ves1820_readreg (frontend->i2c, 0x11);

		if (sync & 2)
			*status |= FE_HAS_SIGNAL;

		if (sync & 2)
			*status |= FE_HAS_CARRIER;

		if (sync & 2)           /* XXX FIXME! */
			*status |= FE_HAS_VITERBI;
		
		if (sync & 4)
			*status |= FE_HAS_SYNC;

		if (sync & 8)
			*status |= FE_HAS_LOCK;

		break;
	}

	case FE_READ_BER:
	{
		u32 ber = ves1820_readreg(frontend->i2c, 0x14) |
			        (ves1820_readreg(frontend->i2c, 0x15) << 8) |
			 ((ves1820_readreg(frontend->i2c, 0x16) & 0x0f) << 16);
		*((u32*) arg) = 10 * ber;
		break;
	}
	case FE_READ_SIGNAL_STRENGTH:
	{
		u8 gain = ves1820_readreg(frontend->i2c, 0x17);
		*((u16*) arg) = (gain << 8) | gain;
		break;
	}

	case FE_READ_SNR:
	{
		u8 quality = ~ves1820_readreg(frontend->i2c, 0x18);
		*((u16*) arg) = (quality << 8) | quality;
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS:
		*((u32*) arg) = ves1820_readreg (frontend->i2c, 0x13) & 0x7f;
		if (*((u32*) arg) == 0x7f)
			*((u32*) arg) = 0xffffffff;
		ves1820_reset_uncorrected_block_counter (frontend->i2c);
		break;

        case FE_SET_FRONTEND:
		return ves1820_set_parameters (frontend, arg);

	case FE_GET_FRONTEND:
		/*  XXX FIXME: implement! */
/*
                struct frontend *front = (struct frontend *)arg;
                
                front->afc=(int)((char)(readreg(client,0x19)));
                front->afc=(front->afc*(int)(front->param.u.qam.SymbolRate/8))/128;
*/
		break;

	case FE_SLEEP:
		ves1820_writereg (frontend->i2c, 0x1b, 0x02);  /* pdown ADC */
		ves1820_writereg (frontend->i2c, 0x00, 0x80);  /* standby */
		break;

        case FE_INIT:
                return ves1820_init (frontend);

        case FE_RESET:
		ves1820_reset (frontend);
		break;

        default:
                return -EINVAL;
        }

        return 0;
} 


static
int ves1820_attach (struct dvb_i2c_bus *i2c)
{
        if ((ves1820_readreg (i2c, 0x1a) & 0xf0) != 0x70)
                return -ENODEV;
        
        dvb_register_frontend (ves1820_ioctl, i2c, NULL, &ves1820_info);

        return 0;
}


static
void ves1820_detach (struct dvb_i2c_bus *i2c)
{
	dvb_unregister_frontend (ves1820_ioctl, i2c);
}


static
int __init init_ves1820 (void)
{
	return dvb_register_i2c_device (THIS_MODULE,
					ves1820_attach, ves1820_detach);
}


static
void __exit exit_ves1820 (void)
{
	dvb_unregister_i2c_device (ves1820_attach);
}


module_init(init_ves1820);
module_exit(exit_ves1820);

MODULE_DESCRIPTION("");
MODULE_AUTHOR("Ralph Metzler");
MODULE_LICENSE("GPL");
MODULE_PARM(debug,"i");

