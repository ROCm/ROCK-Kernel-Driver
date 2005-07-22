/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP4xxx device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
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
 * QLogic ISP4xxx Failover Header 
 *
 */
#ifndef _QLA_FO_H
#define _QLA_FO_H

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

#endif	/* ifndef _QLA_FO_H */
