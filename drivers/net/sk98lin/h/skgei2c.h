/******************************************************************************
 *
 * Name:	skgei2c.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.17 $
 * Date:	$Date: 1999/11/22 13:55:25 $
 * Purpose:	Special genesis defines for I2C
 *		(taken from Monalisa (taken from Concentrator))
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * History:
 *
 *	$Log: skgei2c.h,v $
 *	Revision 1.17  1999/11/22 13:55:25  cgoos
 *	Changed license header to GPL.
 *	
 *	Revision 1.16  1999/11/12 08:24:10  malthoff
 *	Change voltage warning and error limits
 *	(warning +-5%, error +-10%).
 *	
 *	Revision 1.15  1999/09/14 14:14:43  malthoff
 *	The 1000BT Dual Link adapter has got only one Fan.
 *	The second Fan has been removed.
 *	
 *	Revision 1.14  1999/05/27 13:40:50  malthoff
 *	Fan Divisor = 1. Assuming fan with 6500 rpm.
 *	
 *	Revision 1.13  1999/05/20 14:56:55  malthoff
 *	Bug Fix: Missing brace in SK_LM80_FAN_FAKTOR.
 *	
 *	Revision 1.12  1999/05/20 09:22:00  cgoos
 *	Changes for 1000Base-T (Fan sensors).
 *	
 *	Revision 1.11  1998/10/14 05:57:22  cgoos
 *	Fixed compilation warnings.
 *	
 *	Revision 1.10  1998/09/04 08:37:00  malthoff
 *	bugfix: correct the SK_I2C_GET_CTL() macro.
 *	
 *	Revision 1.9  1998/08/25 06:10:03  gklug
 *	add: thresholds for all sensors
 *
 *	Revision 1.8  1998/08/20 11:37:42  gklug
 *	chg: change Ioc to IoC
 *	
 *	Revision 1.7  1998/08/20 08:53:11  gklug
 *	fix: compiler errors
 *	add: Threshold values
 *	
 *	Revision 1.6  1998/08/17 11:37:09  malthoff
 *	Bugfix in SK_I2C_CTL macro. The parameter 'dev'
 *	has to be shifted 9 bits.
 *	
 *	Revision 1.5  1998/08/17 06:52:21  malthoff
 *	Remove unrequired macros.
 *	Add macros for accessing I2C SW register.
 *	
 *	Revision 1.4  1998/08/13 08:30:18  gklug
 *	add: conversion factors for read values
 *	add: new state SEN_VALEXT to read extension value of temperature sensor
 *
 *	Revision 1.3  1998/08/12 13:37:56  gklug
 *	rmv: error numbers and messages
 *
 *	Revision 1.2  1998/08/11 07:54:38  gklug
 *	add: sensor states for GE sensors
 *	add: Macro to access I2c hardware register
 *	chg: Error messages for I2c errors
 *
 *	Revision 1.1  1998/07/17 11:27:56  gklug
 *	Created.
 *
 *
 *
 ******************************************************************************/

/*
 * SKGEI2C.H	contains all SK-98xx specific defines for the I2C handling
 */

#ifndef _INC_SKGEI2C_H_
#define _INC_SKGEI2C_H_

/*
 * Macros to access the B2_I2C_CTRL
 */
#define SK_I2C_CTL(IoC,flag,dev,reg,burst) \
	SK_OUT32(IoC,B2_I2C_CTRL,\
		(flag ? 0x80000000UL : 0x0L ) | \
		(((SK_U32) reg << 16) & I2C_ADDR) | \
		(((SK_U32) dev << 9) & I2C_DEV_SEL) | \
		(( burst << 4) & I2C_BURST_LEN) )

#define SK_I2C_STOP(IoC) {				\
	SK_U32	I2cCtrl;				\
	SK_IN32(IoC, B2_I2C_CTRL, &I2cCtrl);		\
	SK_OUT32(IoC, B2_I2C_CTRL, I2cCtrl | I2C_STOP);	\
}

#define SK_I2C_GET_CTL(Ioc,pI2cCtrl)	SK_IN32(Ioc,B2_I2C_CTRL,pI2cCtrl)

/*
 * Macros to access the I2C SW Registers
 */
#define SK_I2C_SET_BIT(IoC, SetBits) {			\
	SK_U8	OrgBits;				\
	SK_IN8(IoC, B2_I2C_SW, &OrgBits);		\
	SK_OUT8(IoC, B2_I2C_SW, OrgBits | (SetBits));	\
}

#define SK_I2C_CLR_BIT(IoC,ClrBits) {			\
	SK_U8	OrgBits;				\
	SK_IN8(IoC, B2_I2C_SW, &OrgBits);		\
	SK_OUT8(IoC, B2_I2C_SW, OrgBits & ~(ClrBits));	\
}

#define SK_I2C_GET_SW(IoC,pI2cSw)	SK_IN8(IoC,B2_I2C_SW,pI2cSw)

/*
 * define the possible sensor states
 */
#define	SK_SEN_IDLE	0	/* Idle: sensor not read */
#define	SK_SEN_VALUE	1	/* Value Read cycle */
#define	SK_SEN_VALEXT	2	/* Extended Value Read cycle */

/*
 * Conversion factor to convert read Voltage sensor to milli Volt
 * Conversion factor to convert read Temperature sensor to 10th degree Celsius
 */
#define	SK_LM80_VT_LSB		22	/* 22mV LSB resolution */
#define	SK_LM80_TEMP_LSB	10	/* 1 degree LSB resolution */
#define	SK_LM80_TEMPEXT_LSB	5	/* 0.5 degree LSB resolution for the
					 * extension value 
					 */
#define SK_LM80_FAN_FAKTOR	((22500L*60)/(1*2))
/* formula: counter = (22500*60)/(rpm * divisor * pulses/2)
 * assuming: 6500rpm, 4 pulses, divisor 1
 */

/*
 * Define sensor management data
 * Maximum is reached on copperfield with dual Broadcom.
 * Board specific maximum is in pAC->I2c.MaxSens
 */
#define	SK_MAX_SENSORS	8	/* maximal no. of installed sensors */
#define	SK_MIN_SENSORS	5	/* minimal no. of installed sensors */

/*
 * Defines for the individual Thresholds
 */

/* Temperature sensor */
#define	SK_SEN_ERRHIGH0		800	/* Temperature High Err Threshold */
#define	SK_SEN_WARNHIGH0	700	/* Temperature High Warn Threshold */
#define	SK_SEN_WARNLOW0		100	/* Temperature Low Err Threshold */
#define	SK_SEN_ERRLOW0		0	/* Temperature Low Warn Threshold */

/* VCC which should be 5 V */
#define	SK_SEN_ERRHIGH1		5588	/* Voltage PCI High Err Threshold */
#define	SK_SEN_WARNHIGH1	5346	/* Voltage PCI High Warn Threshold */
#define	SK_SEN_WARNLOW1		4664	/* Voltage PCI Low Err Threshold */
#define	SK_SEN_ERRLOW1		4422	/* Voltage PCI Low Warn Threshold */

/*
 * VIO may be 5 V or 3.3 V. Initialization takes two parts:
 * 1. Initialize lowest lower limit and highest higher limit.
 * 2. After the first value is read correct the upper or the lower limit to
 *    the appropriate C constant.
 *
 * Warning limits are +-5% of the exepected voltage.
 * Error limits are +-10% of the expected voltage.
 */
#define	SK_SEN_ERRHIGH2		5588	/* Voltage PCI-IO High Err Threshold */
#define	SK_SEN_WARNHIGH2	5346	/* Voltage PCI-IO High Warn Threshold */
#define	SK_SEN_WARNLOW2		3146	/* Voltage PCI-IO Low Err Threshold */
#define	SK_SEN_ERRLOW2		2970	/* Voltage PCI-IO Low Warn Threshold */

/* correction values for the second pass */
#define	SK_SEN_ERRHIGH2C	3630	/* Voltage PCI-IO High Err Threshold */
#define	SK_SEN_WARNHIGH2C	3476	/* Voltage PCI-IO High Warn Threshold */
#define	SK_SEN_WARNLOW2C	4664	/* Voltage PCI-IO Low Err Threshold */
#define	SK_SEN_ERRLOW2C		4422	/* Voltage PCI-IO Low Warn Threshold */

/*
 * VDD voltage
 */
#define	SK_SEN_ERRHIGH3		3630	/* Voltage ASIC High Err Threshold */
#define	SK_SEN_WARNHIGH3	3476	/* Voltage ASIC High Warn Threshold */
#define	SK_SEN_WARNLOW3		3146	/* Voltage ASIC Low Err Threshold */
#define	SK_SEN_ERRLOW3		2970	/* Voltage ASIC Low Warn Threshold */

/*
 * PLC_3V3 voltage
 * PHY_PLL_A_3V3 voltage
 */
#define	SK_SEN_ERRHIGH4		3630	/* Voltage PMA High Err Threshold */
#define	SK_SEN_WARNHIGH4	3476	/* Voltage PMA High Warn Threshold */
#define	SK_SEN_WARNLOW4		3146	/* Voltage PMA Low Err Threshold */
#define	SK_SEN_ERRLOW4		2970	/* Voltage PMA Low Warn Threshold */

/*
 * PHY_2V5 voltage
 */
#define	SK_SEN_ERRHIGH5		2750	/* Voltage PHY High Err Threshold */
#define	SK_SEN_WARNHIGH5	2640	/* Voltage PHY High Warn Threshold */
#define	SK_SEN_WARNLOW5		2376	/* Voltage PHY Low Err Threshold */
#define	SK_SEN_ERRLOW5		2222	/* Voltage PHY Low Warn Threshold */

/*
 * PHY_PLL_B_3V3 voltage
 */
#define	SK_SEN_ERRHIGH6		3630	/* Voltage PMA High Err Threshold */
#define	SK_SEN_WARNHIGH6	3476	/* Voltage PMA High Warn Threshold */
#define	SK_SEN_WARNLOW6		3146	/* Voltage PMA Low Err Threshold */
#define	SK_SEN_ERRLOW6		2970	/* Voltage PMA Low Warn Threshold */

/*
 * FAN 1 speed
 */
/* assuming: 6500rpm +-15%, 4 pulses,
 * warning at:	80 %
 * error at:	70 %
 * no upper limit
 */
#define	SK_SEN_ERRHIGH		20000	/* FAN Speed High Err Threshold */
#define	SK_SEN_WARNHIGH		20000	/* FAN Speed High Warn Threshold */
#define	SK_SEN_WARNLOW		5200	/* FAN Speed Low Err Threshold */
#define	SK_SEN_ERRLOW		4550	/* FAN Speed Low Warn Threshold */

extern	int SkLm80ReadSensor(SK_AC *pAC, SK_IOC IoC, SK_SENSOR *pSen);
#endif	/* n_INC_SKGEI2C_H */
