/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/module.h"
#include "linux/string.h"
#include "linux/smp_lock.h"
#include "linux/spinlock.h"
#include <linux/highmem.h>
#include "asm/current.h"
#include "asm/delay.h"
#include "asm/processor.h"
#include "asm/unistd.h"
#include "asm/pgalloc.h"
#include "asm/pgtable.h"
#include "asm/page.h"
#include "asm/tlbflush.h"
#include "kern_util.h"
#include "user_util.h"
#include "os.h"
#include "helper.h"

EXPORT_SYMBOL(stop);
EXPORT_SYMBOL(uml_physmem);
EXPORT_SYMBOL(set_signals);
EXPORT_SYMBOL(get_signals);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(__const_udelay);
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(sys_waitpid);
EXPORT_SYMBOL(task_size);
EXPORT_SYMBOL(flush_tlb_range);
EXPORT_SYMBOL(host_task_size);
EXPORT_SYMBOL(arch_validate);

EXPORT_SYMBOL(region_pa);
EXPORT_SYMBOL(region_va);
EXPORT_SYMBOL(phys_mem_map);
EXPORT_SYMBOL(page_mem_map);
EXPORT_SYMBOL(page_to_phys);
EXPORT_SYMBOL(phys_to_page);
EXPORT_SYMBOL(high_physmem);
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(um_virt_to_phys);
EXPORT_SYMBOL(mode_tt);
EXPORT_SYMBOL(handle_page_fault);

EXPORT_SYMBOL(os_getpid);
EXPORT_SYMBOL(os_open_file);
EXPORT_SYMBOL(os_read_file);
EXPORT_SYMBOL(os_write_file);
EXPORT_SYMBOL(os_seek_file);
EXPORT_SYMBOL(os_pipe);
EXPORT_SYMBOL(os_file_type);
EXPORT_SYMBOL(os_close_file);
EXPORT_SYMBOL(helper_wait);
EXPORT_SYMBOL(os_shutdown_socket);
EXPORT_SYMBOL(os_connect_socket);
EXPORT_SYMBOL(run_helper);
EXPORT_SYMBOL(start_thread);
EXPORT_SYMBOL(dump_thread);

/* This is here because UML expands open to sys_open, not to a system
 * call instruction.
 */
EXPORT_SYMBOL(sys_open);
EXPORT_SYMBOL(sys_lseek);
EXPORT_SYMBOL(sys_read);
EXPORT_SYMBOL(sys_wait4);

#ifdef CONFIG_SMP

/* required for SMP */

extern void FASTCALL( __write_lock_failed(rwlock_t *rw));
EXPORT_SYMBOL_NOVERS(__write_lock_failed);

extern void FASTCALL( __read_lock_failed(rwlock_t *rw));
EXPORT_SYMBOL_NOVERS(__read_lock_failed);

#endif

#ifdef CONFIG_HIGHMEM
EXPORT_SYMBOL(kmap);
EXPORT_SYMBOL(kunmap);
EXPORT_SYMBOL(kmap_atomic);
EXPORT_SYMBOL(kunmap_atomic);
EXPORT_SYMBOL(kmap_atomic_to_page);
#endif

