/* 
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/elfcore.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/console.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>

#include <asm/page.h>
#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/bitops.h>
#include <asm/checksum.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pci-bridge.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/hw_irq.h>
#include <asm/abs_addr.h>
#include <asm/cacheflush.h>
#ifdef CONFIG_PPC_ISERIES
#include <asm/iSeries/iSeries_pci.h>
#include <asm/iSeries/iSeries_proc.h>
#include <asm/iSeries/mf.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvLpConfig.h>
#endif

extern int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);
extern int do_signal(sigset_t *, struct pt_regs *);

int abs(int);

extern struct pci_dev * iSeries_veth_dev;
extern struct pci_dev * iSeries_vio_dev;

EXPORT_SYMBOL(do_signal);
EXPORT_SYMBOL(sys_ioctl);

EXPORT_SYMBOL(isa_io_base);
EXPORT_SYMBOL(pci_io_base);

EXPORT_SYMBOL(find_next_zero_bit);
EXPORT_SYMBOL(find_next_zero_le_bit);

EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);

EXPORT_SYMBOL(__down_interruptible);
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(naca);
EXPORT_SYMBOL(__down);

/* EXPORT_SYMBOL(csum_partial); already in net/netsyms.c */
EXPORT_SYMBOL(csum_partial_copy_generic);
EXPORT_SYMBOL(ip_fast_csum);
EXPORT_SYMBOL(csum_tcpudp_magic);

EXPORT_SYMBOL(__copy_tofrom_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__strnlen_user);

#ifdef CONFIG_MSCHUNKS
EXPORT_SYMBOL(msChunks);
#endif
EXPORT_SYMBOL(reloc_offset);

#ifdef CONFIG_PPC_ISERIES
EXPORT_SYMBOL(iSeries_proc_callback);
EXPORT_SYMBOL(HvCall0);
EXPORT_SYMBOL(HvCall1);
EXPORT_SYMBOL(HvCall2);
EXPORT_SYMBOL(HvCall3);
EXPORT_SYMBOL(HvCall4);
EXPORT_SYMBOL(HvCall5);
EXPORT_SYMBOL(HvCall6);
EXPORT_SYMBOL(HvCall7);
EXPORT_SYMBOL(HvLpEvent_unregisterHandler);
EXPORT_SYMBOL(HvLpEvent_registerHandler);
EXPORT_SYMBOL(mf_allocateLpEvents);
EXPORT_SYMBOL(mf_deallocateLpEvents);
EXPORT_SYMBOL(HvLpConfig_getLpIndex_outline);
#endif

EXPORT_SYMBOL(_insb);
EXPORT_SYMBOL(_outsb);
EXPORT_SYMBOL(_insw);
EXPORT_SYMBOL(_outsw);
EXPORT_SYMBOL(_insl);
EXPORT_SYMBOL(_outsl);
EXPORT_SYMBOL(_insw_ns);
EXPORT_SYMBOL(_outsw_ns);
EXPORT_SYMBOL(_insl_ns);
EXPORT_SYMBOL(_outsl_ns);
EXPORT_SYMBOL(ioremap);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);

#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);
EXPORT_SYMBOL(pci_map_single);
EXPORT_SYMBOL(pci_unmap_single);
EXPORT_SYMBOL(pci_map_sg);
EXPORT_SYMBOL(pci_unmap_sg);
#ifdef CONFIG_PPC_ISERIES
EXPORT_SYMBOL(iSeries_GetLocationData);
EXPORT_SYMBOL(iSeries_Device_ToggleReset);
EXPORT_SYMBOL(iSeries_memset_io);
EXPORT_SYMBOL(iSeries_memcpy_toio);
EXPORT_SYMBOL(iSeries_memcpy_fromio);
EXPORT_SYMBOL(iSeries_Read_Byte);
EXPORT_SYMBOL(iSeries_Read_Word);
EXPORT_SYMBOL(iSeries_Read_Long);
EXPORT_SYMBOL(iSeries_Write_Byte);
EXPORT_SYMBOL(iSeries_Write_Word);
EXPORT_SYMBOL(iSeries_Write_Long);
#endif /* CONFIG_PPC_ISERIES */
#ifndef CONFIG_PPC_ISERIES
EXPORT_SYMBOL(eeh_check_failure);
EXPORT_SYMBOL(eeh_total_mmio_ffs);
#endif /* CONFIG_PPC_ISERIES */
#endif /* CONFIG_PCI */

EXPORT_SYMBOL(iSeries_veth_dev);
EXPORT_SYMBOL(iSeries_vio_dev);

EXPORT_SYMBOL(start_thread);
EXPORT_SYMBOL(kernel_thread);

EXPORT_SYMBOL(flush_instruction_cache);
EXPORT_SYMBOL(_get_PVR);
EXPORT_SYMBOL(giveup_fpu);
EXPORT_SYMBOL(enable_kernel_fp);
EXPORT_SYMBOL(flush_icache_range);
EXPORT_SYMBOL(flush_icache_user_range);
EXPORT_SYMBOL(flush_dcache_page);
#ifdef CONFIG_SMP
#ifdef CONFIG_PPC_ISERIES
EXPORT_SYMBOL(__no_use_restore_flags);
EXPORT_SYMBOL(__no_use_save_flags);
EXPORT_SYMBOL(__no_use_sti);
EXPORT_SYMBOL(__no_use_cli);
#endif
#endif

EXPORT_SYMBOL(ppc_md);

EXPORT_SYMBOL(find_devices);
EXPORT_SYMBOL(find_type_devices);
EXPORT_SYMBOL(find_compatible_devices);
EXPORT_SYMBOL(find_path_device);
EXPORT_SYMBOL(device_is_compatible);
EXPORT_SYMBOL(machine_is_compatible);
EXPORT_SYMBOL(find_all_nodes);
EXPORT_SYMBOL(get_property);


EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(memscan);
EXPORT_SYMBOL_NOVERS(memcmp);

EXPORT_SYMBOL(abs);

EXPORT_SYMBOL(timer_interrupt);
EXPORT_SYMBOL(irq_desc);
EXPORT_SYMBOL(get_wchan);
EXPORT_SYMBOL(console_drivers);
#ifdef CONFIG_XMON
EXPORT_SYMBOL(xmon);
#endif

#ifdef CONFIG_DEBUG_KERNEL
extern void (*debugger)(struct pt_regs *regs);
extern int (*debugger_bpt)(struct pt_regs *regs);
extern int (*debugger_sstep)(struct pt_regs *regs);
extern int (*debugger_iabr_match)(struct pt_regs *regs);
extern int (*debugger_dabr_match)(struct pt_regs *regs);
extern void (*debugger_fault_handler)(struct pt_regs *regs);

EXPORT_SYMBOL(debugger);
EXPORT_SYMBOL(debugger_bpt);
EXPORT_SYMBOL(debugger_sstep);
EXPORT_SYMBOL(debugger_iabr_match);
EXPORT_SYMBOL(debugger_dabr_match);
EXPORT_SYMBOL(debugger_fault_handler);
#endif

EXPORT_SYMBOL(tb_ticks_per_usec);
EXPORT_SYMBOL(paca);
