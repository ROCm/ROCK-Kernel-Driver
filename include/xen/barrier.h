#ifndef __XEN_BARRIER_H__
#define __XEN_BARRIER_H__

#include <asm/barrier.h>

#define xen_mb()  mb()
#define xen_rmb() rmb()
#define xen_wmb() wmb()

#endif /* __XEN_BARRIER_H__ */
