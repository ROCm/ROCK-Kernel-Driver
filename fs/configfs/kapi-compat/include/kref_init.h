#ifndef KAPI_KREF_INIT_H
#define KAPI_KREF_INIT_H

#include <linux/kref.h>

/* kernel folks 1, universe 0 */
#define kref_init(kref_obj, release_func)	kref_init(kref_obj)

#endif /* KAPI_KREF_INIT_H */
