/*
 * Architecture-specific kernel symbols
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/string.h>
EXPORT_SYMBOL_NOVERS(memset);			/* gcc generates direct calls to memset()... */
EXPORT_SYMBOL(memchr);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strpbrk);

#include <linux/irq.h>
EXPORT_SYMBOL(isa_irq_to_vector_map);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(disable_irq_nosync);

#include <linux/interrupt.h>
EXPORT_SYMBOL(probe_irq_mask);

#include <asm/checksum.h>
EXPORT_SYMBOL(ip_fast_csum);		/* hand-coded assembly */

#include <asm/io.h>
EXPORT_SYMBOL(__ia64_memcpy_fromio);
EXPORT_SYMBOL(__ia64_memcpy_toio);
EXPORT_SYMBOL(__ia64_memset_c_io);
EXPORT_SYMBOL(io_space);

#include <asm/semaphore.h>
EXPORT_SYMBOL_NOVERS(__down);
EXPORT_SYMBOL_NOVERS(__down_interruptible);
EXPORT_SYMBOL_NOVERS(__down_trylock);
EXPORT_SYMBOL_NOVERS(__up);

#include <asm/page.h>
EXPORT_SYMBOL(clear_page);

#ifdef CONFIG_VIRTUAL_MEM_MAP
#include <linux/bootmem.h>
#include <asm/pgtable.h>
EXPORT_SYMBOL(vmalloc_end);
EXPORT_SYMBOL(ia64_pfn_valid);
EXPORT_SYMBOL(max_low_pfn);	/* defined by bootmem.c, but not exported by generic code */
#endif

#include <asm/processor.h>
EXPORT_SYMBOL(per_cpu__cpu_info);
#ifdef CONFIG_SMP
EXPORT_SYMBOL(__per_cpu_offset);
EXPORT_SYMBOL(per_cpu__local_per_cpu_offset);
#endif
EXPORT_SYMBOL(kernel_thread);

#include <asm/system.h>
#ifdef CONFIG_IA64_DEBUG_IRQ
EXPORT_SYMBOL(last_cli_ip);
#endif

#include <asm/tlbflush.h>

EXPORT_SYMBOL(flush_tlb_range);

#ifdef CONFIG_SMP

EXPORT_SYMBOL(smp_flush_tlb_all);

#include <asm/current.h>
#include <asm/hardirq.h>
EXPORT_SYMBOL(synchronize_irq);

#include <asm/smp.h>
EXPORT_SYMBOL(smp_call_function);
EXPORT_SYMBOL(smp_call_function_single);
EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(phys_cpu_present_map);
EXPORT_SYMBOL(ia64_cpu_to_sapicid);
#else /* !CONFIG_SMP */

EXPORT_SYMBOL(local_flush_tlb_all);

#endif /* !CONFIG_SMP */

#include <asm/uaccess.h>
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(__do_clear_user);
EXPORT_SYMBOL(__strlen_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__strnlen_user);

#include <asm/unistd.h>
EXPORT_SYMBOL(__ia64_syscall);

/* from arch/ia64/lib */
extern void __divsi3(void);
extern void __udivsi3(void);
extern void __modsi3(void);
extern void __umodsi3(void);
extern void __divdi3(void);
extern void __udivdi3(void);
extern void __moddi3(void);
extern void __umoddi3(void);

EXPORT_SYMBOL_NOVERS(__divsi3);
EXPORT_SYMBOL_NOVERS(__udivsi3);
EXPORT_SYMBOL_NOVERS(__modsi3);
EXPORT_SYMBOL_NOVERS(__umodsi3);
EXPORT_SYMBOL_NOVERS(__divdi3);
EXPORT_SYMBOL_NOVERS(__udivdi3);
EXPORT_SYMBOL_NOVERS(__moddi3);
EXPORT_SYMBOL_NOVERS(__umoddi3);

#if defined(CONFIG_MD_RAID5) || defined(CONFIG_MD_RAID5_MODULE)
extern void xor_ia64_2(void);
extern void xor_ia64_3(void);
extern void xor_ia64_4(void);
extern void xor_ia64_5(void);

EXPORT_SYMBOL_NOVERS(xor_ia64_2);
EXPORT_SYMBOL_NOVERS(xor_ia64_3);
EXPORT_SYMBOL_NOVERS(xor_ia64_4);
EXPORT_SYMBOL_NOVERS(xor_ia64_5);
#endif

extern unsigned long ia64_iobase;
EXPORT_SYMBOL(ia64_iobase);

#include <asm/pal.h>
EXPORT_SYMBOL(ia64_pal_call_phys_stacked);
EXPORT_SYMBOL(ia64_pal_call_phys_static);
EXPORT_SYMBOL(ia64_pal_call_stacked);
EXPORT_SYMBOL(ia64_pal_call_static);
EXPORT_SYMBOL(ia64_load_scratch_fpregs);
EXPORT_SYMBOL(ia64_save_scratch_fpregs);

extern struct efi efi;
EXPORT_SYMBOL(efi);

#include <linux/proc_fs.h>
extern struct proc_dir_entry *efi_dir;
EXPORT_SYMBOL(efi_dir);

#include <asm/machvec.h>
#ifdef CONFIG_IA64_GENERIC
EXPORT_SYMBOL(ia64_mv);
#endif
EXPORT_SYMBOL(machvec_noop);
EXPORT_SYMBOL(machvec_memory_fence);
EXPORT_SYMBOL(zero_page_memmap_ptr);
#ifdef CONFIG_PERFMON
#include <asm/perfmon.h>
EXPORT_SYMBOL(pfm_register_buffer_fmt);
EXPORT_SYMBOL(pfm_unregister_buffer_fmt);
EXPORT_SYMBOL(pfm_mod_fast_read_pmds);
EXPORT_SYMBOL(pfm_mod_read_pmds);
EXPORT_SYMBOL(pfm_mod_write_pmcs);
#endif

#ifdef CONFIG_NUMA
#include <asm/numa.h>
EXPORT_SYMBOL(cpu_to_node_map);
#endif

#include <asm/unwind.h>
EXPORT_SYMBOL(unw_init_from_blocked_task);
EXPORT_SYMBOL(unw_init_running);
EXPORT_SYMBOL(unw_unwind);
EXPORT_SYMBOL(unw_unwind_to_user);
EXPORT_SYMBOL(unw_access_gr);
EXPORT_SYMBOL(unw_access_br);
EXPORT_SYMBOL(unw_access_fr);
EXPORT_SYMBOL(unw_access_ar);
EXPORT_SYMBOL(unw_access_pr);

#ifdef CONFIG_SMP
# if __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 4)
/*
 * This is not a normal routine and we don't want a function descriptor for it, so we use
 * a fake declaration here.
 */
extern char ia64_spinlock_contention_pre3_4;
EXPORT_SYMBOL(ia64_spinlock_contention_pre3_4);
# else
/*
 * This is not a normal routine and we don't want a function descriptor for it, so we use
 * a fake declaration here.
 */
extern char ia64_spinlock_contention;
EXPORT_SYMBOL(ia64_spinlock_contention);
# endif
#endif

EXPORT_SYMBOL(ia64_max_iommu_merge_mask);

#include <linux/pm.h>
EXPORT_SYMBOL(pm_idle);
