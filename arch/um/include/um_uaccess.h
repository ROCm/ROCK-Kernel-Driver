/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __ARCH_UM_UACCESS_H
#define __ARCH_UM_UACCESS_H

#include "linux/config.h"
#include "choose-mode.h"

#ifdef CONFIG_MODE_TT
#include "../kernel/tt/include/uaccess.h"
#endif

#ifdef CONFIG_MODE_SKAS
#include "../kernel/skas/include/uaccess.h"
#endif

#define access_ok(type, addr, size) \
	CHOOSE_MODE_PROC(access_ok_tt, access_ok_skas, type, addr, size)

static inline int verify_area(int type, const void * addr, unsigned long size)
{
	return(CHOOSE_MODE_PROC(verify_area_tt, verify_area_skas, type, addr,
				size));
}

static inline int copy_from_user(void *to, const void *from, int n)
{
	return(CHOOSE_MODE_PROC(copy_from_user_tt, copy_from_user_skas, to,
				from, n));
}

static inline int copy_to_user(void *to, const void *from, int n)
{
	return(CHOOSE_MODE_PROC(copy_to_user_tt, copy_to_user_skas, to, 
				from, n));
}

static inline int strncpy_from_user(char *dst, const char *src, int count)
{
	return(CHOOSE_MODE_PROC(strncpy_from_user_tt, strncpy_from_user_skas,
				dst, src, count));
}

static inline int __clear_user(void *mem, int len)
{
	return(CHOOSE_MODE_PROC(__clear_user_tt, __clear_user_skas, mem, len));
}

static inline int clear_user(void *mem, int len)
{
	return(CHOOSE_MODE_PROC(clear_user_tt, clear_user_skas, mem, len));
}

static inline int strnlen_user(const void *str, int len)
{
	return(CHOOSE_MODE_PROC(strnlen_user_tt, strnlen_user_skas, str, len));
}

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
