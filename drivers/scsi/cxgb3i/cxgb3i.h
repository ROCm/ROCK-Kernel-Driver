/*
 * cxgb3i.h: Chelsio S3xx iSCSI driver.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#ifndef __CXGB3I_H__
#define __CXGB3I_H__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/scatterlist.h>

/* from cxgb3 LLD */
#include "common.h"
#include "t3_cpl.h"
#include "t3cdev.h"
#include "cxgb3_ctl_defs.h"
#include "cxgb3_offload.h"
#include "firmware_exports.h"
#include "cxgb3i_offload.h"
/* from iscsi */
#include "../iscsi_tcp.h"

#define CXGB3I_SCSI_QDEPTH_DFLT	128
#define CXGB3I_MAX_TARGET	CXGB3I_MAX_CONN
#define CXGB3I_MAX_LUN		512
#define ISCSI_PDU_HEADER_MAX	(56 + 256) /* bhs + digests + ahs */

struct cxgb3i_adapter;
struct cxgb3i_hba;
struct cxgb3i_endpoint;

/**
 * struct cxgb3i_tag_format - cxgb3i ulp tag for steering pdu payload
 *
 * @idx_bits:	# of bits used to store itt (from iscsi laryer)
 * @age_bits:	# of bits used to store age (from iscsi laryer)
 * @rsvd_bits:	# of bits used by h/w
 * @rsvd_shift:	shift left
 * @rsvd_mask:  bit mask
 * @rsvd_tag_mask:  h/w tag bit mask
 *
 */
struct cxgb3i_tag_format {
	unsigned char idx_bits;
	unsigned char age_bits;
	unsigned char rsvd_bits;
	unsigned char rsvd_shift;
	u32 rsvd_mask;
	u32 rsvd_tag_mask;
};

/**
 * struct cxgb3i_ddp_info - cxgb3i direct data placement for pdu payload
 *
 * @llimit:	lower bound of the page pod memory
 * @ulimit:	upper bound of the page pod memory
 * @nppods:	# of page pod entries
 * @idx_last:	page pod entry last used
 * @map_lock:	lock to synchonize access to the page pod map
 * @map:	page pod map
 */
struct cxgb3i_ddp_info {
	unsigned int llimit;
	unsigned int ulimit;
	unsigned int nppods;
	unsigned int idx_last;
	spinlock_t map_lock;
	u8 *map;
};

/**
 * struct cxgb3i_hba - cxgb3i iscsi structure (per port)
 *
 * @snic:	cxgb3i adapter containing this port
 * @ndev:	pointer to netdev structure
 * @shost:	pointer to scsi host structure
 */
struct cxgb3i_hba {
	struct cxgb3i_adapter *snic;
	struct net_device *ndev;
	struct Scsi_Host *shost;
};

/**
 * struct cxgb3i_adapter - cxgb3i adapter structure (per pci)
 *
 * @listhead:	list head to link elements
 * @lock:	lock for this structure
 * @tdev:	pointer to t3cdev used by cxgb3 driver
 * @pdev:	pointer to pci dev
 * @hba_cnt:	# of hbas (the same as # of ports)
 * @hba:	all the hbas on this adapter
 * @tx_max_size: max. tx packet size supported
 * @rx_max_size: max. rx packet size supported
 * @tag_format: ulp tag format settings
 * @ddp:	ulp ddp state
 */
struct cxgb3i_adapter {
	struct list_head list_head;
	spinlock_t lock;
	struct t3cdev *tdev;
	struct pci_dev *pdev;
	unsigned char hba_cnt;
	struct cxgb3i_hba *hba[MAX_NPORTS];

	unsigned int tx_max_size;
	unsigned int rx_max_size;

	struct cxgb3i_tag_format tag_format;
	struct cxgb3i_ddp_info ddp;
};

/**
 * struct cxgb3i_conn - cxgb3i iscsi connection
 *
 * @tcp_conn:	pointer to iscsi_tcp_conn structure
 * @listhead:	list head to link elements
 * @conn:	pointer to iscsi_conn structure
 * @hba:	pointer to the hba this conn. is going through
 */
struct cxgb3i_conn {
	struct iscsi_tcp_conn tcp_conn;
	struct list_head list_head;
	struct cxgb3i_endpoint *cep;
	struct iscsi_conn *conn;
	struct cxgb3i_hba *hba;
};

/**
 * struct cxgb3i_endpoint - iscsi tcp endpoint
 *
 * @c3cn:	the h/w tcp connection representation
 * @hba:	pointer to the hba this conn. is going through
 * @cconn:	pointer to the associated cxgb3i iscsi connection
 */
struct cxgb3i_endpoint {
	struct s3_conn *c3cn;
	struct cxgb3i_hba *hba;
	struct cxgb3i_conn *cconn;
};

/*
 * Function Prototypes
 */
int cxgb3i_iscsi_init(void);
void cxgb3i_iscsi_cleanup(void);

struct cxgb3i_adapter *cxgb3i_adapter_add(struct t3cdev *);
void cxgb3i_adapter_remove(struct t3cdev *);
int cxgb3i_adapter_ulp_init(struct cxgb3i_adapter *);
void cxgb3i_adapter_ulp_cleanup(struct cxgb3i_adapter *);

struct cxgb3i_hba *cxgb3i_hba_find_by_netdev(struct net_device *);
struct cxgb3i_hba *cxgb3i_hba_host_add(struct cxgb3i_adapter *,
				       struct net_device *);
void cxgb3i_hba_host_remove(struct cxgb3i_hba *);

int cxgb3i_ulp2_init(void);
void cxgb3i_ulp2_cleanup(void);
int cxgb3i_conn_ulp_setup(struct cxgb3i_conn *, int, int);
void cxgb3i_ddp_tag_release(struct cxgb3i_adapter *, u32,
			    struct scatterlist *, unsigned int);
u32 cxgb3i_ddp_tag_reserve(struct cxgb3i_adapter *, unsigned int,
			   u32, unsigned int, struct scatterlist *,
			   unsigned int);
int cxgb3i_conn_ulp2_xmit(struct iscsi_conn *);
#endif
