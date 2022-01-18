/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update mechanism for mutual exclusion
 *
 * Copyright IBM Corporation, 2001
 *
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 *
 * Based on the original work by Paul McKenney <paulmck@vnet.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		http://lse.sourceforge.net/locking/rcupdate.html
 *
 */
#ifndef AMDKCL_RCUPDATE_H
#define AMDKCL_RCUPDATE_H

#include <linux/rcupdate.h>
#include <linux/version.h>

#ifndef rcu_pointer_handoff
#define rcu_pointer_handoff(p) (p)
#endif

#ifndef rcu_replace_pointer
#if defined(rcu_dereference_protected) && defined(rcu_assign_pointer)
/**
 * rcu_replace_pointer() - replace an RCU pointer, returning its old value
 * @rcu_ptr: RCU pointer, whose old value is returned
 * @ptr: regular pointer
 * @c: the lockdep conditions under which the dereference will take place
 *
 * Perform a replacement, where @rcu_ptr is an RCU-annotated
 * pointer and @c is the lockdep argument that is passed to the
 * rcu_dereference_protected() call used to read that pointer.  The old
 * value of @rcu_ptr is returned, and @rcu_ptr is set to @ptr.
 */
#define rcu_replace_pointer(rcu_ptr, ptr, c)                            \
({                                                                      \
        typeof(ptr) __tmp = rcu_dereference_protected((rcu_ptr), (c));  \
        rcu_assign_pointer((rcu_ptr), (ptr));                           \
        __tmp;                                                          \
})
#endif
#endif

#endif /* AMDKCL_RCUPDATE_H */
