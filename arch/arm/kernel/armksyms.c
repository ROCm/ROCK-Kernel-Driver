/*
 *  linux/arch/arm/kernel/armksyms.c
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/in6.h>
#include <linux/syscalls.h>

#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>

/*
 * libgcc functions - functions that are used internally by the
 * compiler...  (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __divsi3(void);
extern void __lshrdi3(void);
extern void __modsi3(void);
extern void __muldi3(void);
extern void __ucmpdi2(void);
extern void __udivdi3(void);
extern void __umoddi3(void);
extern void __udivmoddi4(void);
extern void __udivsi3(void);
extern void __umodsi3(void);
extern void __do_div64(void);

extern void fpundefinstr(void);
extern void fp_enter(void);

/*
 * This has a special calling convention; it doesn't
 * modify any of the usual registers, except for LR.
 */
#define EXPORT_SYMBOL_ALIAS(sym,orig)		\
 const struct kernel_symbol __ksymtab_##sym	\
  __attribute__((section("__ksymtab"))) =	\
    { (unsigned long)&orig, #sym };

/*
 * floating point math emulator support.
 * These symbols will never change their calling convention...
 */
EXPORT_SYMBOL_ALIAS(kern_fp_enter,fp_enter);
EXPORT_SYMBOL_ALIAS(fp_printk,printk);
EXPORT_SYMBOL_ALIAS(fp_send_sig,send_sig);

EXPORT_SYMBOL_NOVERS(__backtrace);

	/* platform dependent support */
EXPORT_SYMBOL(udelay);

	/* networking */
EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_nocheck);
EXPORT_SYMBOL(__csum_ipv6_magic);

	/* io */
#ifndef __raw_readsb
EXPORT_SYMBOL_NOVERS(__raw_readsb);
#endif
#ifndef __raw_readsw
EXPORT_SYMBOL_NOVERS(__raw_readsw);
#endif
#ifndef __raw_readsl
EXPORT_SYMBOL_NOVERS(__raw_readsl);
#endif
#ifndef __raw_writesb
EXPORT_SYMBOL_NOVERS(__raw_writesb);
#endif
#ifndef __raw_writesw
EXPORT_SYMBOL_NOVERS(__raw_writesw);
#endif
#ifndef __raw_writesl
EXPORT_SYMBOL_NOVERS(__raw_writesl);
#endif

	/* string / mem functions */
EXPORT_SYMBOL_NOVERS(strcpy);
EXPORT_SYMBOL_NOVERS(strncpy);
EXPORT_SYMBOL_NOVERS(strcat);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(strcmp);
EXPORT_SYMBOL_NOVERS(strncmp);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strlen);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(strpbrk);
EXPORT_SYMBOL_NOVERS(strrchr);
EXPORT_SYMBOL_NOVERS(strstr);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memscan);
EXPORT_SYMBOL_NOVERS(__memzero);

	/* user mem (segment) */
EXPORT_SYMBOL(__arch_copy_from_user);
EXPORT_SYMBOL(__arch_copy_to_user);
EXPORT_SYMBOL(__arch_clear_user);
EXPORT_SYMBOL(__arch_strnlen_user);
EXPORT_SYMBOL(__arch_strncpy_from_user);

EXPORT_SYMBOL_NOVERS(__get_user_1);
EXPORT_SYMBOL_NOVERS(__get_user_2);
EXPORT_SYMBOL_NOVERS(__get_user_4);
EXPORT_SYMBOL_NOVERS(__get_user_8);

EXPORT_SYMBOL_NOVERS(__put_user_1);
EXPORT_SYMBOL_NOVERS(__put_user_2);
EXPORT_SYMBOL_NOVERS(__put_user_4);
EXPORT_SYMBOL_NOVERS(__put_user_8);

	/* gcc lib functions */
EXPORT_SYMBOL_NOVERS(__ashldi3);
EXPORT_SYMBOL_NOVERS(__ashrdi3);
EXPORT_SYMBOL_NOVERS(__divsi3);
EXPORT_SYMBOL_NOVERS(__lshrdi3);
EXPORT_SYMBOL_NOVERS(__modsi3);
EXPORT_SYMBOL_NOVERS(__muldi3);
EXPORT_SYMBOL_NOVERS(__ucmpdi2);
EXPORT_SYMBOL_NOVERS(__udivdi3);
EXPORT_SYMBOL_NOVERS(__umoddi3);
EXPORT_SYMBOL_NOVERS(__udivmoddi4);
EXPORT_SYMBOL_NOVERS(__udivsi3);
EXPORT_SYMBOL_NOVERS(__umodsi3);
EXPORT_SYMBOL_NOVERS(__do_div64);

	/* bitops */
EXPORT_SYMBOL(_set_bit_le);
EXPORT_SYMBOL(_test_and_set_bit_le);
EXPORT_SYMBOL(_clear_bit_le);
EXPORT_SYMBOL(_test_and_clear_bit_le);
EXPORT_SYMBOL(_change_bit_le);
EXPORT_SYMBOL(_test_and_change_bit_le);
EXPORT_SYMBOL(_find_first_zero_bit_le);
EXPORT_SYMBOL(_find_next_zero_bit_le);

#ifdef __ARMEB__
EXPORT_SYMBOL(_set_bit_be);
EXPORT_SYMBOL(_test_and_set_bit_be);
EXPORT_SYMBOL(_clear_bit_be);
EXPORT_SYMBOL(_test_and_clear_bit_be);
EXPORT_SYMBOL(_change_bit_be);
EXPORT_SYMBOL(_test_and_change_bit_be);
EXPORT_SYMBOL(_find_first_zero_bit_be);
EXPORT_SYMBOL(_find_next_zero_bit_be);
#endif

	/* syscalls */
EXPORT_SYMBOL(sys_write);
EXPORT_SYMBOL(sys_read);
EXPORT_SYMBOL(sys_lseek);
EXPORT_SYMBOL(sys_open);
EXPORT_SYMBOL(sys_exit);
EXPORT_SYMBOL(sys_wait4);
