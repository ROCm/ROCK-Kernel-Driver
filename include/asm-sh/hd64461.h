#ifndef __ASM_SH_HD64461
#define __ASM_SH_HD64461
/*
 *	$Id: hd64461.h,v 1.1 2000/06/10 21:45:48 yaegashi Exp $
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *	Hitachi HD64461 companion chip support
 */
#include <linux/config.h>

#define HD64461_STBCR	0x10000
#define HD64461_SYSCR	0x10002
#define HD64461_SCPUCR	0x10004

#define HD64461_CPTWAR	0x11030	
#define HD64461_CPTWDR	0x11032
#define HD64461_CPTRAR	0x11034	
#define HD64461_CPTRDR	0x11036

#define HD64461_PCC0ISR         0x12000
#define HD64461_PCC0GCR         0x12002
#define HD64461_PCC0CSCR        0x12004
#define HD64461_PCC0CSCIER      0x12006
#define HD64461_PCC0SCR         0x12008
#define HD64461_PCC1ISR         0x12010
#define HD64461_PCC1GCR         0x12012
#define HD64461_PCC1CSCR        0x12014
#define HD64461_PCC1CSCIER      0x12016
#define HD64461_PCC1SCR         0x12018
#define HD64461_P0OCR           0x1202a
#define HD64461_P1OCR           0x1202c
#define HD64461_PGCR            0x1202e

#define HD64461_NIRR		0x15000
#define HD64461_NIMR		0x15002

#ifndef CONFIG_HD64461_IOBASE
#define CONFIG_HD64461_IOBASE	0xb0000000
#endif
#ifndef CONFIG_HD64461_IRQ
#define CONFIG_HD64461_IRQ	36
#endif

#define HD64461_IRQBASE	64

#endif
