#ifndef _RELAY_LOCKING_H
#define _RELAY_LOCKING_H

extern char *
locking_reserve(struct rchan *rchan,
		u32 slot_len, 
		struct timeval *time_stamp,
		u32 *tsc,
		int *err,
		int *interrupting);

extern void 
locking_commit(struct rchan *rchan,
	       char *from,
	       u32 len, 
	       int deliver, 
	       int interrupting);

extern void 
locking_resume(struct rchan *rchan);

extern void 
locking_finalize(struct rchan *rchan);

extern u32 
locking_get_offset(struct rchan *rchan, u32 *max_offset);

extern void 
locking_reset(struct rchan *rchan, int init);

extern int
locking_reset_index(struct rchan *rchan, u32 old_idx);

#endif	/* _RELAY_LOCKING_H */
