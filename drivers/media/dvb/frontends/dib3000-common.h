/*
 * .h-files for the common use of the frontend drivers made by DiBcom
 * DiBcom 3000-MB/MC/P
 *
 * DiBcom (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * based on GPL code from DibCom, which has
 *
 * Copyright (C) 2004 Amaury Demol for DiBcom (ademol@dibcom.fr)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * Acknowledgements
 *
 *  Amaury Demol (ademol@dibcom.fr) from DiBcom for providing specs and driver
 *  sources, on which this driver (and the dvb-dibusb) are based.
 *
 * see Documentation/dvb/README.dibusb for more information
 *
 */

#ifndef DIB3000_COMMON_H
#define DIB3000_COMMON_H


/* info and err, taken from usb.h, if there is anything available like by default,
 * please change !
 */
#define err(format, arg...) printk(KERN_ERR "%s: " format "\n" , __FILE__ , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format "\n" , __FILE__ , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format "\n" , __FILE__ , ## arg)

/* a PID for the pid_filter list, when in use */
struct dib3000_pid
{
	u16 pid;
	int active;
};

/* mask for enabling a specific pid for the pid_filter */
#define DIB3000_ACTIVATE_PID_FILTERING	(0x2000)

/* common values for tuning */
#define DIB3000_ALPHA_0					(     0)
#define DIB3000_ALPHA_1					(     1)
#define DIB3000_ALPHA_2					(     2)
#define DIB3000_ALPHA_4					(     4)

#define DIB3000_CONSTELLATION_QPSK		(     0)
#define DIB3000_CONSTELLATION_16QAM		(     1)
#define DIB3000_CONSTELLATION_64QAM		(     2)

#define DIB3000_GUARD_TIME_1_32			(     0)
#define DIB3000_GUARD_TIME_1_16			(     1)
#define DIB3000_GUARD_TIME_1_8			(     2)
#define DIB3000_GUARD_TIME_1_4			(     3)

#define DIB3000_TRANSMISSION_MODE_2K	(     0)
#define DIB3000_TRANSMISSION_MODE_8K	(     1)

#define DIB3000_SELECT_LP				(     0)
#define DIB3000_SELECT_HP				(     1)

#define DIB3000_FEC_1_2					(     1)
#define DIB3000_FEC_2_3					(     2)
#define DIB3000_FEC_3_4					(     3)
#define DIB3000_FEC_5_6					(     5)
#define DIB3000_FEC_7_8					(     7)

#define DIB3000_HRCH_OFF				(     0)
#define DIB3000_HRCH_ON					(     1)

#define DIB3000_DDS_INVERSION_OFF		(     0)
#define DIB3000_DDS_INVERSION_ON		(     1)

#define DIB3000_TUNER_WRITE_ENABLE(a)	(0xffff & (a << 7))
#define DIB3000_TUNER_WRITE_DISABLE(a)	(0xffff & ((a << 7) | (1 << 7)))

/* for auto search */
static u16 dib3000_seq[2][2][2] =     /* fft,gua,   inv   */
	{ /* fft */
		{ /* gua */
			{ 0, 1 },                   /*  0   0   { 0,1 } */
			{ 3, 9 },                   /*  0   1   { 0,1 } */
		},
		{
			{ 2, 5 },                   /*  1   0   { 0,1 } */
			{ 6, 11 },                  /*  1   1   { 0,1 } */
		}
	};

#define DIB3000_REG_MANUFACTOR_ID		(  1025)
#define DIB3000_I2C_ID_DIBCOM			(0x01b3)

#define DIB3000_REG_DEVICE_ID			(  1026)
#define DIB3000MB_DEVICE_ID				(0x3000)
#define DIB3000MC_DEVICE_ID				(0x3001)
#define DIB3000P_DEVICE_ID				(0x3002)

#endif // DIB3000_COMMON_H
