/* 
 *  Driver for Dummy Frontend 
 *
 *  Written by Emard <emard@softhome.net>
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

#include <linux/module.h>
#include <linux/init.h>

#include "dvb_frontend.h"

static int sct = 0;


/* depending on module parameter sct deliver different infos
 */

static struct dvb_frontend_info dvb_s_dummyfe_info = {
	.name 			= "DVB-S dummy frontend",
	.type 			= FE_QPSK,
	.frequency_min 		= 950000,
	.frequency_max 		= 2150000,
	.frequency_stepsize 	= 250,           /* kHz for QPSK frontends */
	.frequency_tolerance 	= 29500,
	.symbol_rate_min 	= 1000000,
	.symbol_rate_max 	= 45000000,
/*      .symbol_rate_tolerance 	= ???,*/
	.notifier_delay		 = 50,                /* 1/20 s */
	.caps = FE_CAN_INVERSION_AUTO | 
	FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
	FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
	FE_CAN_QPSK
};

static struct dvb_frontend_info dvb_c_dummyfe_info = {
	.name = "DVB-C dummy frontend",
	.type = FE_QAM,
	.frequency_stepsize = 62500,
	.frequency_min = 51000000,
	.frequency_max = 858000000,
	.symbol_rate_min = (57840000/2)/64,     /* SACLK/64 == (XIN/2)/64 */
	.symbol_rate_max = (57840000/2)/4,      /* SACLK/4 */
#if 0
	.frequency_tolerance	= ???,
	.symbol_rate_tolerance	= ???,  /* ppm */  /* == 8% (spec p. 5) */
	.notifier_delay		= ?,
#endif
	.caps = FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 |
		FE_CAN_QAM_128 | FE_CAN_QAM_256 | 
		FE_CAN_FEC_AUTO | FE_CAN_INVERSION_AUTO
};

static struct dvb_frontend_info dvb_t_dummyfe_info = {
	.name = "DVB-T dummy frontend",
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
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO,
};

struct dvb_frontend_info *frontend_info(void)
{
	switch(sct)
	{
	case 2:
		return &dvb_t_dummyfe_info;
	case 1:
		return &dvb_c_dummyfe_info;
	case 0:
	default:
	        return &dvb_s_dummyfe_info;
	}
}


static int dvbdummyfe_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
        switch (cmd) {
        case FE_GET_INFO:
		memcpy (arg, frontend_info(), 
			sizeof(struct dvb_frontend_info));
		break;

        case FE_READ_STATUS:
	{
		fe_status_t *status = arg;
		*status = FE_HAS_SIGNAL
			| FE_HAS_CARRIER
			| FE_HAS_VITERBI
			| FE_HAS_SYNC
			| FE_HAS_LOCK;
		break;
	}

        case FE_READ_BER:
	{
		u32 *ber = (u32 *) arg;
		*ber = 0;
		break;
	}

        case FE_READ_SIGNAL_STRENGTH:
	{
		u8 signal = 0xff;
		*((u16*) arg) = (signal << 8) | signal;
		break;
	}

        case FE_READ_SNR:
	{
		u8 snr = 0xf0;
		*(u16*) arg = (snr << 8) | snr;
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS: 
		*(u32*) arg = 0;
		break;

        case FE_SET_FRONTEND:
                break;

	case FE_GET_FRONTEND:
		break;

        case FE_SLEEP:
		return 0;

        case FE_INIT:
		return 0;

	case FE_SET_TONE:
		return -EOPNOTSUPP;

	case FE_SET_VOLTAGE:
		return 0;

	default:
		return -EOPNOTSUPP;
        }
        return 0;
} 


static int dvbdummyfe_attach (struct dvb_i2c_bus *i2c, void **data)
{
	return dvb_register_frontend (dvbdummyfe_ioctl, i2c, NULL, frontend_info());
}


static void dvbdummyfe_detach (struct dvb_i2c_bus *i2c, void *data)
{
	dvb_unregister_frontend (dvbdummyfe_ioctl, i2c);
}


static int __init init_dvbdummyfe (void)
{
	return dvb_register_i2c_device (THIS_MODULE,
					dvbdummyfe_attach, 
					dvbdummyfe_detach);
}


static void __exit exit_dvbdummyfe (void)
{
	dvb_unregister_i2c_device (dvbdummyfe_attach);
}


module_init(init_dvbdummyfe);
module_exit(exit_dvbdummyfe);


MODULE_DESCRIPTION("DVB DUMMY Frontend");
MODULE_AUTHOR("Emard");
MODULE_LICENSE("GPL");
MODULE_PARM(sct, "i");
