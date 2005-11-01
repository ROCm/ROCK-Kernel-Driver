#ifndef KAPI_ASSERT_SPIN_LOCKED_H
#define KAPI_ASSERT_SPIN_LOCKED_H

/*
http://linux.bkbits.net:8080/linux-2.5/gnupatch@420104cfKCNCOybFRuNN48-HuIMO6w
 *
 * We only care about checking the assertion in modern 2.6; we just want old
 * kernels to build without errors.  We have to watch out for old jbd headers
 * that expose this api.  (kernel namespace inconsistency 1, universe 0)
 */

#include <linux/types.h>
#include <linux/jbd.h>
#ifndef assert_spin_locked
#define assert_spin_locked(lock)	do { (void)(lock); } while(0)
#endif

#endif
