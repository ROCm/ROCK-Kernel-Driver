#ifndef AMDKCL_SUSPEND_H
#define AMDKCL_SUSPEND_H

#ifndef HAVE_KSYS_SYNC_HELPER
extern void _kcl_ksys_sync_helper(void);

static inline void ksys_sync_helper(void)
{
	_kcl_ksys_sync_helper();
}
#endif /* HAVE_KSYS_SYNC_HELPER */

#endif /* AMDKCL_SUSPEND_H */
