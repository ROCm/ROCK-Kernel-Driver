/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_RCUPDATE_H
#define AMDKCL_RCUPDATE_H

#include <linux/rcupdate.h>
#include <linux/version.h>

#ifndef rcu_pointer_handoff
#define rcu_pointer_handoff(p) (p)
#endif

#endif /* AMDKCL_RCUPDATE_H */
