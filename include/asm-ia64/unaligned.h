#ifndef _ASM_IA64_UNALIGNED_H
#define _ASM_IA64_UNALIGNED_H

/*
 * The main single-value unaligned transfer routines.  Derived from
 * the Linux/Alpha version.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#define get_unaligned(ptr) \
	((__typeof__(*(ptr)))ia64_get_unaligned((ptr), sizeof(*(ptr))))

#define put_unaligned(x,ptr) \
	ia64_put_unaligned((unsigned long)(x), (ptr), sizeof(*(ptr)))

/*
 * EGCS 1.1 knows about arbitrary unaligned loads.  Define some
 * packed structures to talk about such things with.
 */
struct __una_u64 { __u64 x __attribute__((packed)); };
struct __una_u32 { __u32 x __attribute__((packed)); };
struct __una_u16 { __u16 x __attribute__((packed)); };

static inline unsigned long
__uldq (const unsigned long * r11)
{
	const struct __una_u64 *ptr = (const struct __una_u64 *) r11;
	return ptr->x;
}

static inline unsigned long
__uldl (const unsigned int * r11)
{
	const struct __una_u32 *ptr = (const struct __una_u32 *) r11;
	return ptr->x;
}

static inline unsigned long
__uldw (const unsigned short * r11)
{
	const struct __una_u16 *ptr = (const struct __una_u16 *) r11;
	return ptr->x;
}

static inline void
__ustq (unsigned long r5, unsigned long * r11)
{
	struct __una_u64 *ptr = (struct __una_u64 *) r11;
	ptr->x = r5;
}

static inline void
__ustl (unsigned long r5, unsigned int * r11)
{
	struct __una_u32 *ptr = (struct __una_u32 *) r11;
	ptr->x = r5;
}

static inline void
__ustw (unsigned long r5, unsigned short * r11)
{
	struct __una_u16 *ptr = (struct __una_u16 *) r11;
	ptr->x = r5;
}


/*
 * This function doesn't actually exist.  The idea is that when
 * someone uses the macros below with an unsupported size (datatype),
 * the linker will alert us to the problem via an unresolved reference
 * error.
 */
extern unsigned long ia64_bad_unaligned_access_length (void);

#define ia64_get_unaligned(_ptr,size)				\
({								\
	const void *ptr = (_ptr);				\
	unsigned long val;					\
								\
	switch (size) {						\
	      case 1:						\
		val = *(const unsigned char *) ptr;		\
		break;						\
	      case 2:						\
		val = __uldw((const unsigned short *)ptr);	\
		break;						\
	      case 4:						\
		val = __uldl((const unsigned int *)ptr);	\
		break;						\
	      case 8:						\
		val = __uldq((const unsigned long *)ptr);	\
		break;						\
	      default:						\
		val = ia64_bad_unaligned_access_length();	\
	}							\
	val;							\
})

#define ia64_put_unaligned(_val,_ptr,size)		\
do {							\
	const void *ptr = (_ptr);			\
	unsigned long val = (_val);			\
							\
	switch (size) {					\
	      case 1:					\
		*(unsigned char *)ptr = (val);		\
	        break;					\
	      case 2:					\
		__ustw(val, (unsigned short *)ptr);	\
		break;					\
	      case 4:					\
		__ustl(val, (unsigned int *)ptr);	\
		break;					\
	      case 8:					\
		__ustq(val, (unsigned long *)ptr);	\
		break;					\
	      default:					\
	    	ia64_bad_unaligned_access_length();	\
	}						\
} while (0)

#endif /* _ASM_IA64_UNALIGNED_H */
