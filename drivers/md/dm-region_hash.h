/*
 * Copyright (C) 2003 Sistina Software Limited.
 * Copyright (C) 2004-2007 Red Hat Inc.
 *
 * This file is released under the GPL.
 */

#ifndef DM_RH_H
#define DM_RH_H

#include "dm.h"
#include <linux/dm-dirty-log.h>
#include "dm-bio-list.h"

/*-----------------------------------------------------------------
 * Region hash
 *----------------------------------------------------------------*/

/*
 * States a region can have.
 */
enum {
	RH_CLEAN	= 0x01,	/* No writes in flight. */
	RH_DIRTY	= 0x02,	/* Writes in flight. */
	RH_NOSYNC	= 0x04,	/* Out of sync. */
	RH_RECOVERING	= 0x08,	/* Under resynchronization. */
	RH_ERROR	= 0x10,	/* Error recovering region */
};

/*
 * Conversion fns
 */
region_t rh_bio_to_region(void *rh, struct bio *bio);
region_t rh_sector_to_region(void *rh, sector_t sector);
sector_t rh_region_to_sector(void *rh, region_t region);


/*
 * Functions to set a caller context in a region.
 */
void *rh_reg_get_context(void *reg);
void rh_reg_set_context(void *reg, void *context);

/*
 * Reagion hash and region parameters.
 */
region_t rh_get_region_size(void *rh);
sector_t rh_get_region_key(void *reg);

int rh_init(void **rh,
	    unsigned int max_recovery,
	    void (*dispatch)(void *dispatch_context, struct bio_list *bios),
	    void *dispatch_context,
	    void (*wake)(void *wake_context),
	    void *wake_context,
	    struct dm_dirty_log *log, uint32_t region_size, region_t nr_regions);
void rh_exit(void *rh);

int rh_state(void *rh, region_t region, int may_block);
void rh_update_states(void *rh);
void rh_flush(void *rh);

void rh_inc(void *rh, region_t region);
void rh_inc_pending(void *rh, struct bio_list *bios);
void rh_dec(void *rh, region_t region);
void rh_delay(void *rh, struct bio *bio);
void rh_delay_by_region(void *rh, struct bio *bio, region_t region);

int rh_recovery_prepare(void *rh);
void *rh_recovery_start(void *rh);
void rh_recovery_end(void *reg, int error);
void rh_stop_recovery(void *rh);
void rh_start_recovery(void *rh);

#endif
