#ifdef __KERNEL__
#ifndef _ASM_KMAP_TYPES_H
#define _ASM_KMAP_TYPES_H

enum km_type {
	KM_BOUNCE_READ,
	KM_BOUNCE_WRITE,
	KM_TYPE_NR
};

#endif
#endif /* __KERNEL__ */
