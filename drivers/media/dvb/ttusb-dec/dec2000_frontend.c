/*
 * TTUSB DEC-2000-t Frontend
 *
 * Copyright (C) 2003 Alex Woods <linux-dvb@giblets.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/init.h>
#include <linux/module.h>

#include "dvb_frontend.h"
#include "dvb_functions.h"

static int debug = 0;

#define dprintk	if (debug) printk

static struct dvb_frontend_info dec2000_frontend_info = {
	.name			= "TechnoTrend/Hauppauge DEC-2000-t Frontend",
	.type			= FE_OFDM,
	.frequency_min		= 51000000,
	.frequency_max		= 858000000,
	.frequency_stepsize	= 62500,
	.caps =	FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
		FE_CAN_HIERARCHY_AUTO,
};

static int dec2000_frontend_ioctl(struct dvb_frontend *fe, unsigned int cmd,
				  void *arg)
{
	dprintk("%s\n", __FUNCTION__);

	switch (cmd) {

	case FE_GET_INFO:
		dprintk("%s: FE_GET_INFO\n", __FUNCTION__);
		memcpy(arg, &dec2000_frontend_info,
		       sizeof (struct dvb_frontend_info));
		break;

	case FE_READ_STATUS: {
			fe_status_t *status = (fe_status_t *)arg;
			dprintk("%s: FE_READ_STATUS\n", __FUNCTION__);
			*status = FE_HAS_SIGNAL | FE_HAS_VITERBI |
				  FE_HAS_SYNC | FE_HAS_CARRIER | FE_HAS_LOCK;
			break;
		}

	case FE_READ_BER: {
			u32 *ber = (u32 *)arg;
			dprintk("%s: FE_READ_BER\n", __FUNCTION__);
			*ber = 0;
			return -ENOSYS;
			break;
		}

	case FE_READ_SIGNAL_STRENGTH: {
			dprintk("%s: FE_READ_SIGNAL_STRENGTH\n", __FUNCTION__);
			*(s32 *)arg = 0xFF;
			return -ENOSYS;
			break;
		}

	case FE_READ_SNR:
		dprintk("%s: FE_READ_SNR\n", __FUNCTION__);
		*(s32 *)arg = 0;
		return -ENOSYS;
		break;

	case FE_READ_UNCORRECTED_BLOCKS:
		dprintk("%s: FE_READ_UNCORRECTED_BLOCKS\n", __FUNCTION__);
		*(u32 *)arg = 0;
		return -ENOSYS;
		break;

	case FE_SET_FRONTEND:{
			struct dvb_frontend_parameters *p =
				(struct dvb_frontend_parameters *)arg;
			u8 b[] = { 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
				   0x00, 0xff, 0x00, 0x00, 0x00, 0xff };
			u32 freq;
			struct i2c_msg msg = { addr: 0x71, flags: 0, len:20 };

			dprintk("%s: FE_SET_FRONTEND\n", __FUNCTION__);

			dprintk("            frequency->%d\n", p->frequency);
			dprintk("            symbol_rate->%d\n",
				p->u.qam.symbol_rate);
			dprintk("            inversion->%d\n", p->inversion);

			freq = htonl(p->frequency / 1000);
			memcpy(&b[4], &freq, sizeof (int));
			msg.buf = b;
			fe->i2c->xfer(fe->i2c, &msg, 1);

			break;
		}

	case FE_GET_FRONTEND:
		dprintk("%s: FE_GET_FRONTEND\n", __FUNCTION__);
		break;

	case FE_SLEEP:
		dprintk("%s: FE_SLEEP\n", __FUNCTION__);
		return -ENOSYS;
		break;

	case FE_INIT:
		dprintk("%s: FE_INIT\n", __FUNCTION__);
		break;

	case FE_RESET:
		dprintk("%s: FE_RESET\n", __FUNCTION__);
		break;

	default:
		dprintk("%s: unknown IOCTL (0x%X)\n", __FUNCTION__, cmd);
		return -EINVAL;

	}

	return 0;
}

static int dec2000_frontend_attach(struct dvb_i2c_bus *i2c, void **data)
{
	dprintk("%s\n", __FUNCTION__);

	return dvb_register_frontend(dec2000_frontend_ioctl, i2c, NULL,
			      &dec2000_frontend_info);
}

static void dec2000_frontend_detach(struct dvb_i2c_bus *i2c, void *data)
{
	dprintk("%s\n", __FUNCTION__);

	dvb_unregister_frontend(dec2000_frontend_ioctl, i2c);
}

static int __init dec2000_frontend_init(void)
{
	return dvb_register_i2c_device(THIS_MODULE, dec2000_frontend_attach,
				       dec2000_frontend_detach);
}

static void __exit dec2000_frontend_exit(void)
{
	dvb_unregister_i2c_device(dec2000_frontend_attach);
}

module_init(dec2000_frontend_init);
module_exit(dec2000_frontend_exit);

MODULE_DESCRIPTION("TechnoTrend/Hauppauge DEC-2000-t Frontend");
MODULE_AUTHOR("Alex Woods <linux-dvb@giblets.org");
MODULE_LICENSE("GPL");

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug level");

