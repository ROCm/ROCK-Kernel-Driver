/*
 * some sbus structures and macros to make usage of sbus drivers possible
 */

#ifndef __M68K_SBUS_H
#define __M68K_SBUS_H

struct linux_sbus_device {
	struct {
		unsigned int which_io;
		unsigned int phys_addr;
	} reg_addrs[1];
};

extern void *sparc_alloc_io (u32, void *, int, char *, u32, int);
#define sparc_alloc_io(a,b,c,d,e,f)	(a)

#define ARCH_SUN4  0

#endif
