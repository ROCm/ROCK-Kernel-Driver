/* * arch/ppc/platforms/zx4500.h
 * 
 * Board setup routines for Znyx ZX4500 cPCI board.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef	__PPC_PLATFORMS_ZX4500_H_
#define	__PPC_PLATFORMS_ZX4500_H_

/*
 * Define the addresses of CPLD registers in CLPD area.
 */
#define	ZX4500_CPLD_BOARD_ID		0xff800001
#define	ZX4500_CPLD_REV			0xff800002
#define	ZX4500_CPLD_RESET		0xff800011
#define	ZX4500_CPLD_PHY1		0xff800014
#define	ZX4500_CPLD_PHY2		0xff800015
#define	ZX4500_CPLD_PHY3		0xff800016
#define	ZX4500_CPLD_SYSCTL		0xff800017
#define	ZX4500_CPLD_EXT_FLASH		0xff800018
#define	ZX4500_CPLD_DUAL1		0xff800019
#define	ZX4500_CPLD_DUAL2		0xff80001A
#define	ZX4500_CPLD_STATUS		0xff800030
#define	ZX4500_CPLD_STREAM		0xff800032
#define	ZX4500_CPLD_PHY1_LED		0xff800034
#define	ZX4500_CPLD_PHY2_LED		0xff800035
#define	ZX4500_CPLD_PHY3_LED		0xff800036
#define	ZX4500_CPLD_PHY1_LNK		0xff80003C
#define	ZX4500_CPLD_PHY2_LNK		0xff80003D
#define	ZX4500_CPLD_PHY3_LNK		0xff80003E

#define	ZX4500_CPLD_RESET_SOFT		0x01	/* Soft Reset */
#define	ZX4500_CPLD_RESET_XBUS		0x40	/* Reset entire board */

#define	ZX4500_CPLD_SYSCTL_PMC		0x01	/* Enable INTA/B/C/D from PMC */
#define	ZX4500_CPLD_SYSCTL_BCM		0x04	/* Enable INTA from BCM */
#define	ZX4500_CPLD_SYSCTL_SINTA	0x08	/* Enable SINTA from 21554 */
#define	ZX4500_CPLD_SYSCTL_WD		0x20	/* Enable Watchdog Timer */
#define	ZX4500_CPLD_SYSCTL_PMC_TRI	0x80	/* Tri-state PMC EREADY */

#define	ZX4500_CPLD_DUAL2_LED_PULL	0x01	/* Pull LED */
#define	ZX4500_CPLD_DUAL2_LED_EXT_FAULT	0x02	/* External Fault LED */
#define	ZX4500_CPLD_DUAL2_LED_INT_FAULT	0x04	/* Internal Fault LED */
#define	ZX4500_CPLD_DUAL2_LED_OK	0x08	/* OK LED */
#define	ZX4500_CPLD_DUAL2_LED_CLK	0x10	/* CLK LED */

/*
 * Defines related to boot string stored in flash.
 */
#define	ZX4500_BOOT_STRING_ADDR		0xfff7f000
#define	ZX4500_BOOT_STRING_LEN		80

/*
 * Define the IDSEL that the PCI bus side of the 8240 is connected to.
 * This IDSEL must not be selected from the 8240 processor side.
 */
#define	ZX4500_HOST_BRIDGE_IDSEL	20


void zx4500_find_bridges(void);

#endif	/* __PPC_PLATFORMS_ZX4500_H_ */
