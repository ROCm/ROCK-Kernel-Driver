#ifndef _RELAY_LOCKLESS_H
#define _RELAY_LOCKLESS_H

extern char *
lockless_reserve(struct rchan *rchan,
		 u32 slot_len,
		 struct timeval *time_stamp,
		 u32 *tsc,
		 int * interrupting,
		 int * errcode);

extern void 
lockless_commit(struct rchan *rchan,
		char * from,
		u32 len, 
		int deliver, 
		int interrupting);

extern void 
lockless_resume(struct rchan *rchan);

extern void 
lockless_finalize(struct rchan *rchan);

extern u32 
lockless_get_offset(struct rchan *rchan, u32 *max_offset);

extern void
lockless_reset(struct rchan *rchan, int init);

extern int
lockless_reset_index(struct rchan *rchan, u32 old_idx);

#endif/* _RELAY_LOCKLESS_H */
