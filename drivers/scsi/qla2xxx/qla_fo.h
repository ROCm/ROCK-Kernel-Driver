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

/*
 * QLogic ISP2x00 Failover Header 
 *
 */
#ifndef _QLA_FO_H
#define _QLA_FO_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "qlfo.h"
        /*
         * This structure definition is for a scsi I/O request NOT subject to
         * failover re-routing.  It is for the use of configuration operations
         * and diagnostics functions as definted in ExIoct.h
         */
        typedef struct scsi_cdb_request {
                struct adapter_state		*ha;
                uint16_t	target;
                uint16_t	lun;
                uint8_t		*cdb_ptr;	/* Pointer to cdb to be sent */
                uint8_t		cdb_len;	/* cdb length */
                uint8_t		direction;	/* Direction of I/O for buffer */
                uint8_t		scb_len;	/* Scsi completion block length */
                uint8_t		*scb_ptr;	/* Scsi completion block pointer */
                uint8_t		*buf_ptr;	/* Pointer to I/O buffer */
                uint16_t	buf_len;	/* Buffer size */
        }
        SCSI_REQ_t, *SCSI_REQ_p;


        /*
        * Special defines
        */
        typedef	union	_FO_HBA_STAT {
                FO_HBA_STAT_INPUT	input;
                FO_HBA_STAT_INFO	info;
        } FO_HBA_STAT;

        typedef	union	_FO_LUN_DATA {
                FO_LUN_DATA_INPUT	input;
                FO_LUN_DATA_LIST	list;
        } FO_LUN_DATA;

        typedef union	_FO_TARGET_DATA {
                FO_TARGET_DATA_INPUT    input;
                FO_DEVICE_DATABASE    list;
        } FO_TARGET_DATA;

#if defined(__cplusplus)
}
#endif

#endif	/* ifndef _QLA_FO_H */
