/*
 *  System-common area definitions for NEC PC-9800 series
 *
 *  Copyright (C) 1999	TAKAI Kousuke <tak@kmc.kyoto-u.ac.jp>,
 *			Kyoto University Microcomputer Club.
 */

#ifndef _ASM_I386_PC9800SCA_H_
#define _ASM_I386_PC9800SCA_H_

#define PC9800SCA_EXPMMSZ		(0x0401)	/* B */
#define PC9800SCA_SCSI_PARAMS		(0x0460)	/* 8 * 4B */
#define PC9800SCA_DISK_EQUIPS		(0x0482)	/* B */
#define PC9800SCA_XROM_ID		(0x04C0)	/* 52B */
#define PC9800SCA_BIOS_FLAG		(0x0501)	/* B */
#define PC9800SCA_MMSZ16M		(0x0594)	/* W */

/* PC-9821 have additional system common area in their BIOS-ROM segment. */

#define PC9821SCA__BASE			(0xF8E8 << 4)
#define PC9821SCA_ROM_ID		(PC9821SCA__BASE + 0x00)
#define PC9821SCA_ROM_FLAG4		(PC9821SCA__BASE + 0x05)
#define PC9821SCA_RSFLAGS		(PC9821SCA__BASE + 0x11)	/* B */

#endif /* !_ASM_I386_PC9800SCA_H_ */
