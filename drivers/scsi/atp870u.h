#ifndef _ATP870U_H
#define _ATP870U_H

/* $Id: atp870u.h,v 1.0 1997/05/07 15:09:00 root Exp root $

 * Header file for the ACARD 870U/W driver for Linux
 *
 * $Log: atp870u.h,v $
 * Revision 1.0  1997/05/07  15:09:00  root
 * Initial revision
 *
 */

#include <linux/types.h>

/* I/O Port */

#define MAX_CDB		12
#define MAX_SENSE	14
#define qcnt		32
#define ATP870U_SCATTER 128
#define ATP870U_CMDLUN 	1

struct atp_unit {
	unsigned long ioport;
	unsigned long pciport;
	unsigned char last_cmd;
	unsigned char in_snd;
	unsigned char in_int;
	unsigned char quhdu;
	unsigned char quendu;
	unsigned char scam_on;
	unsigned char global_map;
	unsigned char chip_veru;
	unsigned char host_idu;
	volatile int working;
	unsigned short wide_idu;
	unsigned short active_idu;
	unsigned short ultra_map;
	unsigned short async;
	unsigned short deviceid;
	unsigned char ata_cdbu[16];
	unsigned char sp[16];
	Scsi_Cmnd *querequ[qcnt];
	struct atp_id {
		unsigned char dirctu;
		unsigned char devspu;
		unsigned char devtypeu;
		unsigned long prdaddru;
		unsigned long tran_lenu;
		unsigned long last_lenu;
		unsigned char *prd_posu;
		unsigned char *prd_tableu;
		dma_addr_t prd_phys;
		Scsi_Cmnd *curr_req;
	} id[16];
	struct Scsi_Host *host;
	struct pci_dev *pdev;
	unsigned int unit;
};

static int atp870u_queuecommand(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
static int atp870u_abort(Scsi_Cmnd *);
static int atp870u_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int *);
static void send_s870(struct Scsi_Host *);

extern const char *atp870u_info(struct Scsi_Host *);
static Scsi_Host_Template atp870u_template;

#endif
