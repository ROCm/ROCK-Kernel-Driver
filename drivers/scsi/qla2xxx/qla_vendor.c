/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

#include "qla_os.h"
#include "qla_def.h"

/*
 * vendor specific op codes.
*/
#define	UCSCSI_DCMD		0x20 /* vendor specific command */
#define DAC_CDB_LEN		12
#define DAC_SENSE_LEN		64

#define DACMD_WRITE_CONF_ONDISK	0x4B
#define	DACMD_WRITE_CONFIG	0x06
#define	DACMD_WRITE_CONF2	0x3C
#define	DACMD_WRITE_CONFLABEL	0x49 /* Write configuration label */
#define	DACMD_WRITE_CONFIG_V3x	0x4F
#define	DACMD_ADD_CONFIG_V2x	0x18
#define	DACMD_ADD_CONFIG_V3x	0x4C
#define	DACMD_STORE_IMAGE	0x21
#define	DACMD_ADD_CAPACITY	0x2A /* add physical drives to existing array */
#define	DACMD_WRITE_IOPORT	0x3A /* write port B */
#define	DACMD_S2S_WRITEFULLCONF		0x60 /* write full configuration */
#define	DACMD_S2S_ADDFULLCONF		0x62 /* add   full configuration */
#define	DACMD_S2S_WRITELUNMAP_OLD	0x58 /* write LUN map information */
#define DACMD_S2S_WRITELUNMAP		0xD2	/* Write LUN MAP Information */
#define	DACMD_S2S_WRITE_IOPORT	0x66 /* write expanded IO port */
#define	DACMD_WRITE_V3x		0x34 /* write data from plain memory */
#define	DACMD_S2S_WRITESIG	0x4D /* write signature information */

#if !defined(s08bits)
#define	s08bits	char
#define	s16bits	short
#define	s32bits	int
#define	u08bits	unsigned s08bits
#define	u16bits	unsigned s16bits
#define	u32bits	unsigned s32bits
#endif

typedef struct dac_command
{
        u08bits	mb_Command;	/* Mail Box register 0	*/
        u08bits	mb_CmdID;	/* Mail Box register 1	*/
        u08bits	mb_ChannelNo;	/* Mail Box register 2	*/
        u08bits	mb_TargetID;	/* Mail Box register 3	*/
        u08bits	mb_DevState;	/* Mail Box register 4	*/
        u08bits	mb_MailBox5;	/* Mail Box register 5	*/
        u08bits	mb_MailBox6;	/* Mail Box register 6	*/
        u08bits	mb_SysDevNo;	/* Mail Box register 7	*/
        u32bits	mb_Datap;	/* Mail Box register 8-B */
        u08bits	mb_MailBoxC;	/* Mail Box register C	*/
        u08bits	mb_StatusID;	/* Mail box register D	*/
        u16bits	mb_Status;	/* Mail Box Register E,F */
}
dac_command_t;

typedef struct	dac_scdb
{
        u08bits db_ChannelTarget;	/* ChannelNo 7..4 & Target 3..0 */
        u08bits db_DATRET;		/* different bits, see below */
        u16bits	db_TransferSize;	/* Request/done size in bytes */
        u32bits db_PhysDatap;		/* Physical addr in host memory	*/
        u08bits db_CdbLen;		/* 6, 10 or 12			*/
        u08bits db_SenseLen;		/* If returned from DAC (<= 64)	*/
        u08bits	db_Cdb[DAC_CDB_LEN];	/* The CDB itself		*/
        u08bits	db_SenseData[DAC_SENSE_LEN];/* Result of request sense	*/
        u08bits db_StatusIn;		/* SCSI status returned		*/
        u08bits	db_Reserved1;
}
dac_scdb_t;

typedef	struct dga_scdb
{
        u08bits	dsc_osreq[1024];	/* OS related buffer:sizeof(mdac_req_t) */

        u08bits	dsc_familyctlno;	/* Controller number within family */
        u08bits	dsc_ctlno;		/* Controller number */
        u08bits	dsc_chno;		/* Channel number */
        u08bits	dsc_tgt;		/* target ID */

        u08bits	dsc_lun;		/* Lun ID */
        u08bits	dsc_rebuildflag;	/* current rebuild flag */
        u16bits	dsc_status;		/* completion status */

        u08bits	dsc_scsiversion;	/* SCSI protocol version */
        u08bits	dsc_hostctlno;		/* host system controller number */
        u16bits	dsc_reqsenseseqno;	/* request sense sequence number */

        u32bits	dsc_events;		/* # events at start */

        u32bits	dsc_pollwaitchan;	/* sleep/wakeup channel */
        u32bits	dsc_poll;		/* polling value, if =0 op complete */

        struct dga_ctldev *dsc_ctp;	/* pointer back to controller */
        void *dsc_pdp;	/* pointer back to physical device */
        void *dsc_ldp;	/* pointer back to logical device */
        void (*dsc_intr)(void);	/* completion call back function */

        /* all save functions are used in S2S */
        u08bits	dsc_savedcdb[DAC_CDB_LEN];/* 12 bytes saved CDB from SCSI CDB */
        u32bits	(*dsc_statsintr)(struct dga_scdb *); /* statistics completion function */

        void (*dsc_savedintr)(void);	/* completion call back function */
        void *dsc_savedctp;		/* pointer back to controller */
        u08bits	dsc_savedfamilyctlno;	/* Controller number within family */
        u08bits	dsc_savedctlno;		/* Controller number */
        u08bits	dsc_savedchno;		/* Channel number */
        u08bits	dsc_savedtgt;		/* target ID */

        u08bits	dsc_savedlun;		/* Lun ID */
        u08bits	dsc_savedcdblen;	/* saved CDB len for SCDB */
        u08bits	dsc_scanmode;
        u08bits dsc_pageno;             /* pageno for data > 4K */
        u32bits	dsc_residue;
        u32bits	dsc_Reserved4;

        dac_command_t dsc_dcmd;		/* DCMD space, 16 bytes */
        dac_scdb_t dsc_scdb;		/* SCDB space */
        u32bits	dsc_EventSeqNo;
        u32bits	dsc_ReqSenseNo;

        u32bits	dsc_Reserved64[16];	/* leave this for OLD SCO driver bug */

        u08bits	dsc_data[256];		/* Rest is data */
}
dga_scdb_t;

/*
* qla2100_set_scsi_direction
*      This routine will set the proper direction for vendor specific
*      commands. 
*
*      Note: Vendors should modify this routine to set the proper 
*      direction of the transfer if they used vendor specific commands.
*
* Input:
*      ha = adapter block pointer.
*      sp = SCSI Request Block structure pointer.
*
* Returns:
*      0 = success, was able to issue command.
*/
void
qla2x00_set_vend_direction(scsi_qla_host_t *ha,
    struct scsi_cmnd *cmd, cmd_entry_t *pkt)
{
	dga_scdb_t	*dsp = (dga_scdb_t *) cmd;

	if (cmd->data_cmnd[0] == UCSCSI_DCMD) {
		switch( dsp->dsc_dcmd.mb_Command ) {
		case DACMD_WRITE_CONF_ONDISK:
		case DACMD_WRITE_CONFIG:
		case DACMD_WRITE_CONF2:
		case DACMD_WRITE_CONFLABEL:
		case DACMD_WRITE_CONFIG_V3x:
		case DACMD_ADD_CONFIG_V2x:
		case DACMD_ADD_CONFIG_V3x:
		case DACMD_STORE_IMAGE:
		case DACMD_ADD_CAPACITY:
		case DACMD_WRITE_IOPORT:
		case DACMD_S2S_WRITEFULLCONF:
		case DACMD_S2S_ADDFULLCONF:
		case DACMD_S2S_WRITELUNMAP_OLD:
		case DACMD_S2S_WRITELUNMAP:
		case DACMD_S2S_WRITE_IOPORT:
		case DACMD_WRITE_V3x:
		case DACMD_S2S_WRITESIG:
			pkt->control_flags |= BIT_6;
			break;
		default:
			pkt->control_flags |= BIT_5;
		}
	} else
		pkt->control_flags |= BIT_5;
}

