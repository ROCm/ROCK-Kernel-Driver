/*
 * Copyright (C) 2003 Sistina Software Limited.
 * Copyright (C) 2004-2008 Red Hat, Inc. All rights reserved.
 *
 * Device-Mapper dirty region hash interface.
 *
 * This file is released under the GPL.
 */

#ifndef DM_REGION_HASH_H
#define DM_REGION_HASH_H

#include <linux/dm-dirty-log.h>

/*-----------------------------------------------------------------
 * Region hash
 *----------------------------------------------------------------*/
struct dm_rh_client;
struct dm_region;

/*
 * States a region can have.
 */
enum dm_rh_region_states {
	DM_RH_CLEAN	 = 0x01,	/* No writes in flight. */
	DM_RH_DIRTY	 = 0x02,	/* Writes in flight. */
	DM_RH_NOSYNC	 = 0x04,	/* Out of sync. */
	DM_RH_RECOVERING = 0x08,	/* Under resynchronization. */
};

/*
 * Region hash create/destroy.
 */
struct bio_list;
struct dm_rh_client *dm_rh_client_create(
		 unsigned max_recovery,
		 void (*dispatch)(void *dispatch_context,
				  struct bio_list *bios, int error),
		 void *dispatch_context,
		 void (*wake)(void *wake_context), void *wake_context,
		 struct dm_dirty_log *log, uint32_t region_size,
		 region_t nr_regions);
void dm_rh_client_destroy(struct dm_rh_client *rh);

/*
 * Conversion fns:
 *
 *   bio -> region
 *   sector -> region
 *   region -> sector
 */
region_t dm_rh_bio_to_region(struct dm_rh_client *rh, struct bio *bio);
region_t dm_rh_sector_to_region(struct dm_rh_client *rh, sector_t sector);
sector_t dm_rh_region_to_sector(struct dm_rh_client *rh, region_t region);

/*
 * Functions to set a caller context in a region.
 */
void *dm_rh_reg_get_context(struct dm_region *reg);
void dm_rh_reg_set_context(struct dm_region *reg, void *context);

/*
 * Get region size and key (ie. number of the region).
 */
sector_t dm_rh_get_region_size(struct dm_rh_client *rh);
sector_t dm_rh_get_region_key(struct dm_region *reg);

/*
 * Get/set/update region state (and dirty log).
 *
 * dm_rh_update_states
 * 	@errors_handled != 0 influences
 *	that the state of the region will be kept NOSYNC
 */
int dm_rh_get_state(struct dm_rh_client *rh, region_t region, int may_block);
void dm_rh_set_state(struct dm_rh_client *rh, region_t region,
		     enum dm_rh_region_states state, int may_block);
void dm_rh_update_states(struct dm_rh_client *rh, int errors_handled);

/* Flush the region hash and dirty log. */
int dm_rh_flush(struct dm_rh_client *rh);

/* Inc/dec pending count on regions. */
void dm_rh_inc(struct dm_rh_client *rh, region_t region);
void dm_rh_inc_pending(struct dm_rh_client *rh, struct bio_list *bios);
int dm_rh_dec(struct dm_rh_client *rh, region_t region);

/* Delay bios on regions. */
void dm_rh_delay(struct dm_rh_client *rh, struct bio *bio);
void dm_rh_delay_by_region(struct dm_rh_client *rh,
			   struct bio *bio, region_t region);

/*
 * Normally, the region hash will automatically call the dispatch function.
 * dm_rh_dispatch_bios() is for intentional dispatching of bios.
 */
void dm_rh_dispatch_bios(struct dm_rh_client *rh, region_t region, int error);

/*
 * Region recovery control.
 */
/* Prepare some regions for recovery by starting to quiesce them. */
int dm_rh_recovery_prepare(struct dm_rh_client *rh);
/* Try fetching a quiesced region for recovery. */
struct dm_region *dm_rh_recovery_start(struct dm_rh_client *rh);
/* Report recovery end on a region. */
void dm_rh_recovery_end(struct dm_rh_client *rh, struct dm_region *reg,
			int error);
/* Check for amount of recoveries in flight. */
int dm_rh_recovery_in_flight(struct dm_rh_client *rh);
/* Start/stop recovery. */
void dm_rh_stop_recovery(struct dm_rh_client *rh);
void dm_rh_start_recovery(struct dm_rh_client *rh);

#endif /* #ifdef DM_REGION_HASH_H */
