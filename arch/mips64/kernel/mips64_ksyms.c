/*
 * Export MIPS64-specific functions needed for loadable modules.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/in6.h>
#include <linux/pci.h>

#include <asm/dma.h>
#include <asm/floppy.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/semaphore.h>
#include <asm/softirq.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>

extern void *__bzero(void *__s, size_t __count);
extern long __strncpy_from_user_nocheck_asm(char *__to,
                                            const char *__from, long __len);
extern long __strncpy_from_user_asm(char *__to, const char *__from,
                                    long __len);
extern long __strlen_user_nocheck_asm(const char *s);
extern long __strlen_user_asm(const char *s);
extern long __strnlen_user_nocheck_asm(const char *s);
extern long __strnlen_user_asm(const char *s);

EXPORT_SYMBOL(EISA_bus);

/*
 * String functions
 */
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL(simple_strtol);
EXPORT_SYMBOL_NOVERS(strcat);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strlen);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(strrchr);
EXPORT_SYMBOL_NOVERS(strtok);
EXPORT_SYMBOL_NOVERS(strpbrk);

EXPORT_SYMBOL(_clear_page);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(kernel_thread);

/*
 * Userspace access stuff.
 */
EXPORT_SYMBOL_NOVERS(__copy_user);
EXPORT_SYMBOL_NOVERS(__bzero);
EXPORT_SYMBOL_NOVERS(__strncpy_from_user_nocheck_asm);
EXPORT_SYMBOL_NOVERS(__strncpy_from_user_asm);
EXPORT_SYMBOL_NOVERS(__strlen_user_nocheck_asm);
EXPORT_SYMBOL_NOVERS(__strlen_user_asm);
EXPORT_SYMBOL_NOVERS(__strnlen_user_nocheck_asm);
EXPORT_SYMBOL_NOVERS(__strnlen_user_asm);


/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy);

/*
 * Functions to control caches.
 */
EXPORT_SYMBOL(_flush_page_to_ram);
EXPORT_SYMBOL(_flush_cache_l1);
#ifndef CONFIG_COHERENT_IO
EXPORT_SYMBOL(_dma_cache_wback_inv);
EXPORT_SYMBOL(_dma_cache_inv);
#endif

EXPORT_SYMBOL(invalid_pte_table);

/*
 * Semaphore stuff
 */
EXPORT_SYMBOL(__down_read);
EXPORT_SYMBOL(__down_write);
EXPORT_SYMBOL(__rwsem_wake);

/*
 * Base address of ports for Intel style I/O.
 */
EXPORT_SYMBOL(mips_io_port_base);

/*
 * Kernel hacking ...
 */
#include <asm/branch.h>
#include <linux/sched.h>

int register_fpe(void (*handler)(struct pt_regs *regs, unsigned int fcr31));
int unregister_fpe(void (*handler)(struct pt_regs *regs, unsigned int fcr31));

#ifdef CONFIG_MIPS_FPE_MODULE
EXPORT_SYMBOL(__compute_return_epc);
EXPORT_SYMBOL(register_fpe);
EXPORT_SYMBOL(unregister_fpe);
#endif

#ifdef CONFIG_VT
EXPORT_SYMBOL(screen_info);
#endif

EXPORT_SYMBOL(get_wchan);
