#ifndef AMDKCL_KREF_H
#define AMDKCL_KREF_H

#include <linux/kref.h>

#if !defined(HAVE_KREF_READ)
static inline unsigned int kref_read(const struct kref *kref)
{
	return atomic_read(&kref->refcount);
}
#endif

#endif /* AMDKCL_KREF_H */
