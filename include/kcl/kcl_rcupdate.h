#ifndef AMDKCL_RCUPDATE_H
#define AMDKCL_RCUPDATE_H

#include <linux/rcupdate.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
#define rcu_pointer_handoff(p) (p)
#endif

#endif /* AMDKCL_RCUPDATE_H */
