/* 
    Driver for Grundig 29504-491, a Philips TDA8083 based QPSK Frontend

    Copyright (C) 2001 Convergence Integrated Media GmbH

    written by Ralph Metzler <ralph@convergence.de>

    adoption to the new DVB frontend API and diagnostic ioctl's
    by Holger Waechtler <holger@convergence.de>

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
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"

#define FRONTEND_NAME "dvbfe_tda8083"

#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG FRONTEND_NAME ": " args); \
	} while (0)

static int debug;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");


struct tda8083_state {
	struct i2c_adapter *i2c;
	struct dvb_adapter *dvb;
};

static struct dvb_frontend_info tda8083_info = {
	.name			= "Grundig 29504-491, (TDA8083 based)",
	.type			= FE_QPSK,
	.frequency_min		= 950000,     /* FIXME: guessed! */
	.frequency_max		= 1400000,    /* FIXME: guessed! */
	.frequency_stepsize	= 125,   /* kHz for QPSK frontends */
/*      .frequency_tolerance	= ???,*/
	.symbol_rate_min	= 1000000,   /* FIXME: guessed! */
	.symbol_rate_max	= 45000000,  /* FIXME: guessed! */
/*      .symbol_rate_tolerance	= ???,*/
	.notifier_delay		= 0,
	.caps = FE_CAN_INVERSION_AUTO |
		FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
		FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO |
		FE_CAN_QPSK | FE_CAN_MUTE_TS
};

static u8 tda8083_init_tab [] = {
	0x04, 0x00, 0x4a, 0x79, 0x04, 0x00, 0xff, 0xea,
	0x48, 0x42, 0x79, 0x60, 0x70, 0x52, 0x9a, 0x10,
	0x0e, 0x10, 0xf2, 0xa7, 0x93, 0x0b, 0x05, 0xc8,
	0x9d, 0x00, 0x42, 0x80, 0x00, 0x60, 0x40, 0x00,
	0x00, 0x75, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};


static int tda8083_writereg (struct i2c_adapter *i2c, u8 reg, u8 data)
{
	int ret;
	u8 buf [] = { reg, data };
	struct i2c_msg msg = { .addr = 0x68, .flags = 0, .buf = buf, .len = 2 };

        ret = i2c_transfer(i2c, &msg, 1);

        if (ret != 1)
                dprintk ("%s: writereg error (reg %02x, ret == %i)\n",
			__FUNCTION__, reg, ret);

        return (ret != 1) ? -1 : 0;
}


static int tda8083_readregs (struct i2c_adapter *i2c, u8 reg1, u8 *b, u8 len)
{
	int ret;
	struct i2c_msg msg [] = { { .addr = 0x68, .flags = 0, .buf = &reg1, .len = 1 },
			   { .addr = 0x68, .flags = I2C_M_RD, .buf = b, .len = len } };

	ret = i2c_transfer(i2c, msg, 2);

	if (ret != 2)
		dprintk ("%s: readreg error (reg %02x, ret == %i)\n",
			__FUNCTION__, reg1, ret);

        return ret == 2 ? 0 : -1;
}


static inline u8 tda8083_readreg (struct i2c_adapter *i2c, u8 reg)
{
	u8 val;

	tda8083_readregs (i2c, reg, &val, 1);

	return val;
}


static int tsa5522_write (struct i2c_adapter *i2c, u8 data [4])
{
	int ret;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = 4 };

	ret = i2c_transfer(i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: i/o error (ret == %i)\n", __FUNCTION__, ret);

	return (ret != 1) ? -1 : 0;
}


/**
 *   set up the downconverter frequency divisor for a
 *   reference clock comparision frequency of 125 kHz.
 */
static int tsa5522_set_tv_freq (struct i2c_adapter *i2c, u32 freq)
{
	u32 div = freq / 125;
	u8 buf [4] = { (div >> 8) & 0x7f, div & 0xff, 0x8e, 0x00 };

	return tsa5522_write (i2c, buf);
}


static int tda8083_init (struct i2c_adapter *i2c)
{
	int i;
	
	dprintk("%s: init TDA8083\n", __FILE__);

	for (i=0; i<44; i++)
		tda8083_writereg (i2c, i, tda8083_init_tab[i]);

	return 0;
}


static int tda8083_set_inversion (struct i2c_adapter *i2c, fe_spectral_inversion_t inversion)
{
	/*  XXX FIXME: implement other modes than FEC_AUTO */
	if (inversion == INVERSION_AUTO)
		return 0;
	
	return -EINVAL;
}


static int tda8083_set_fec (struct i2c_adapter *i2c, fe_code_rate_t fec)
{
	if (fec == FEC_AUTO)
		return tda8083_writereg (i2c, 0x07, 0xff);

	if (fec >= FEC_1_2 && fec <= FEC_8_9)
		return tda8083_writereg (i2c, 0x07, 1 << (FEC_8_9 - fec));

	return -EINVAL;
}


static fe_code_rate_t tda8083_get_fec (struct i2c_adapter *i2c)
{
	u8 index;
	static fe_code_rate_t fec_tab [] = { FEC_8_9, FEC_1_2, FEC_2_3, FEC_3_4,
				       FEC_4_5, FEC_5_6, FEC_6_7, FEC_7_8 };

	index = tda8083_readreg(i2c, 0x0e) & 0x07;

	return fec_tab [index];
}


static int tda8083_set_symbolrate (struct i2c_adapter *i2c, u32 srate)
{
        u32 ratio;
	u32 tmp;
	u8 filter;

	if (srate > 32000000)
                srate = 32000000;
        if (srate < 500000)
                srate = 500000;

	filter = 0;
	if (srate < 24000000)
		filter = 2;
	if (srate < 16000000)
		filter = 3;

	tmp = 31250 << 16;
	ratio = tmp / srate;
        
	tmp = (tmp % srate) << 8;
	ratio = (ratio << 8) + tmp / srate;
        
	tmp = (tmp % srate) << 8;
	ratio = (ratio << 8) + tmp / srate;
	
	dprintk("tda8083: ratio == %08x\n", (unsigned int) ratio);

	tda8083_writereg (i2c, 0x05, filter);
	tda8083_writereg (i2c, 0x02, (ratio >> 16) & 0xff);
	tda8083_writereg (i2c, 0x03, (ratio >>  8) & 0xff);
	tda8083_writereg (i2c, 0x04, (ratio      ) & 0xff);
	
	tda8083_writereg (i2c, 0x00, 0x3c);
	tda8083_writereg (i2c, 0x00, 0x04);

	return 1;
}


static void tda8083_wait_diseqc_fifo (struct i2c_adapter *i2c, int timeout)
{
	unsigned long start = jiffies;

	while (jiffies - start < timeout &&
               !(tda8083_readreg(i2c, 0x02) & 0x80))
	{
		msleep(50);
	};
}


static int tda8083_send_diseqc_msg (struct i2c_adapter *i2c,
			     struct dvb_diseqc_master_cmd *m)
{
	int i;

	tda8083_writereg (i2c, 0x29, (m->msg_len - 3) | (1 << 2)); /* enable */

	for (i=0; i<m->msg_len; i++)
		tda8083_writereg (i2c, 0x23 + i, m->msg[i]);

	tda8083_writereg (i2c, 0x29, (m->msg_len - 3) | (3 << 2)); /* send!! */

	tda8083_wait_diseqc_fifo (i2c, 100);

	return 0;
}


static int tda8083_send_diseqc_burst (struct i2c_adapter *i2c, fe_sec_mini_cmd_t burst)
{
	switch (burst) {
	case SEC_MINI_A:
		tda8083_writereg (i2c, 0x29, (5 << 2));  /* send burst A */
		break;
	case SEC_MINI_B:
		tda8083_writereg (i2c, 0x29, (7 << 2));  /* send B */
		break;
	default:
		return -EINVAL;
	};
	
	tda8083_wait_diseqc_fifo (i2c, 100); 

	return 0;
}


static int tda8083_set_tone (struct i2c_adapter *i2c, fe_sec_tone_mode_t tone)
{
	tda8083_writereg (i2c, 0x26, 0xf1);

	switch (tone) {
	case SEC_TONE_OFF:
		return tda8083_writereg (i2c, 0x29, 0x00);
	case SEC_TONE_ON:
		return tda8083_writereg (i2c, 0x29, 0x80);
	default:
		return -EINVAL;
	};
}


static int tda8083_set_voltage (struct i2c_adapter *i2c, fe_sec_voltage_t voltage)
{
	switch (voltage) {
	case SEC_VOLTAGE_13:
		return tda8083_writereg (i2c, 0x20, 0x00);
	case SEC_VOLTAGE_18:
		return tda8083_writereg (i2c, 0x20, 0x11);
	default:
		return -EINVAL;
	};
}


static int tda8083_ioctl(struct dvb_frontend *fe, unsigned int cmd,
			 void *arg)
{
	struct tda8083_state *state = fe->data;
	struct i2c_adapter *i2c = state->i2c;

        switch (cmd) {
	case FE_GET_INFO:
		memcpy (arg, &tda8083_info, sizeof(struct dvb_frontend_info));
                break;

        case FE_READ_STATUS:
	{
		fe_status_t *status=(fe_status_t *) arg;
		u8 signal = ~tda8083_readreg (i2c, 0x01);
		u8 sync = tda8083_readreg (i2c, 0x02);

		*status = 0;

		if (signal > 10)
			*status |= FE_HAS_SIGNAL;

		if (sync & 0x01)
			*status |= FE_HAS_CARRIER;

		if (sync & 0x02)
			*status |= FE_HAS_VITERBI;

		if (sync & 0x10)
			*status |= FE_HAS_SYNC;

		if ((sync & 0x1f) == 0x1f)
			*status |= FE_HAS_LOCK;

		break;
	}

        case FE_READ_BER:
                *((u32*) arg) = 0; /*   XXX FIXME: implement me!!! */
                return -EOPNOTSUPP;

        case FE_READ_SIGNAL_STRENGTH:
	{
		u8 signal = ~tda8083_readreg (i2c, 0x01);
                *((u16*) arg) = (signal << 8) | signal;
                break;
	}
        case FE_READ_SNR:
	{
		u8 snr = tda8083_readreg (i2c, 0x08);
                *((u16*) arg) = (snr << 8) | snr;
                break;
	}
        case FE_READ_UNCORRECTED_BLOCKS:
                *((u32*) arg) = 0; /*   XXX FIXME: implement me!!! */
                return -EOPNOTSUPP;


        case FE_SET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;

		tsa5522_set_tv_freq (i2c, p->frequency);
		tda8083_set_inversion (i2c, p->inversion);
		tda8083_set_fec (i2c, p->u.qpsk.fec_inner);
		tda8083_set_symbolrate (i2c, p->u.qpsk.symbol_rate);

		tda8083_writereg (i2c, 0x00, 0x3c);
		tda8083_writereg (i2c, 0x00, 0x04);

		break;
        }

	case FE_GET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;

		/*  FIXME: get symbolrate & frequency offset...*/
		/*p->frequency = ???;*/
		p->inversion = (tda8083_readreg (i2c, 0x0e) & 0x80) ?
				INVERSION_ON : INVERSION_OFF;
		p->u.qpsk.fec_inner = tda8083_get_fec (i2c);
		/*p->u.qpsk.symbol_rate = tda8083_get_symbolrate (i2c);*/
		break;
        }

	case FE_SLEEP:
		tda8083_writereg (i2c, 0x00, 0x02);
		break;

	case FE_INIT:
		tda8083_init (i2c);
		tda8083_writereg (i2c, 0x00, 0x3c);
		tda8083_writereg (i2c, 0x00, 0x04);
		break;

	case FE_DISEQC_SEND_MASTER_CMD:
		return tda8083_send_diseqc_msg (i2c, arg);

	case FE_DISEQC_SEND_BURST:
		tda8083_send_diseqc_burst (i2c, (fe_sec_mini_cmd_t) arg);
		tda8083_writereg (i2c, 0x00, 0x3c);
		tda8083_writereg (i2c, 0x00, 0x04);

		break;

	case FE_SET_TONE:
		tda8083_set_tone (i2c, (fe_sec_tone_mode_t) arg);
		tda8083_writereg (i2c, 0x00, 0x3c);
		tda8083_writereg (i2c, 0x00, 0x04);
		break;

	case FE_SET_VOLTAGE:
		tda8083_set_voltage (i2c, (fe_sec_voltage_t) arg);
		tda8083_writereg (i2c, 0x00, 0x3c);
		tda8083_writereg (i2c, 0x00, 0x04);
		break;

	default:
		return -EOPNOTSUPP;
	};

	return 0;
} 

static struct i2c_client client_template;

static int tda8083_attach_adapter(struct i2c_adapter *adapter)
{
	struct tda8083_state *state;
	struct i2c_client *client;
	int ret;

	dprintk("Trying to attach to adapter 0x%x:%s.\n",
		adapter->id, adapter->name);

	if ((tda8083_readreg (adapter, 0x00)) != 0x05)
		return -ENODEV;

	if ( !(state = kmalloc(sizeof(struct tda8083_state), GFP_KERNEL)) )
		return -ENOMEM;

	if ( !(client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL)) ) {
		kfree(state);
		return -ENOMEM;
	}

	memset(state, 0, sizeof(struct tda8083_state));
	state->i2c = adapter;

	memcpy(client, &client_template, sizeof(struct i2c_client));
	client->adapter = adapter;
	client->addr = 0; //XXX
	i2c_set_clientdata(client, state);

	if ((ret = i2c_attach_client(client))) {
		kfree(state);
		kfree(client);
		return ret;
	}

	BUG_ON(!state->dvb);

	if ((ret = dvb_register_frontend(tda8083_ioctl, state->dvb, state,
					     &tda8083_info, THIS_MODULE))) {
		i2c_detach_client(client);
		kfree(state);
		kfree(client);
		return ret;
	}

	return 0;
}

static int tda8083_detach_client(struct i2c_client *client)
{
	struct tda8083_state *state = i2c_get_clientdata(client);

	dvb_unregister_frontend (tda8083_ioctl, state->dvb);
	i2c_detach_client(client);
	BUG_ON(state->dvb);
	kfree(client);
	kfree(state);
	return 0;
}

static int tda8083_command (struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tda8083_state *data = i2c_get_clientdata(client);
	dprintk ("%s\n", __FUNCTION__);

	switch (cmd) {
	case FE_REGISTER: {
		data->dvb = arg;
		break;
	}
	case FE_UNREGISTER: {
		data->dvb = NULL;
		break;
	}
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct i2c_driver driver = {
	.owner 		= THIS_MODULE,
	.name 		= FRONTEND_NAME,
	.id 		= I2C_DRIVERID_DVBFE_TDA8083,
	.flags 		= I2C_DF_NOTIFY,
	.attach_adapter = tda8083_attach_adapter,
	.detach_client 	= tda8083_detach_client,
	.command 	= tda8083_command,
};

static struct i2c_client client_template = {
	.name		= FRONTEND_NAME,
	.flags 		= I2C_CLIENT_ALLOW_USE,
	.driver  	= &driver,
};

static int __init init_tda8083 (void)
{
	return i2c_add_driver(&driver);
}

static void __exit exit_tda8083(void)
{
	if (i2c_del_driver(&driver))
		printk("grundig_29504_401: driver deregistration failed\n");
}

module_init(init_tda8083);
module_exit(exit_tda8083);

MODULE_DESCRIPTION("Grundig 29504-491 DVB frontend driver (TDA8083 Based)");
MODULE_AUTHOR("Ralph Metzler, Holger Waechtler");
MODULE_LICENSE("GPL");

