/*
 * Herein lies all the functions/variables that are "exported" for linkage
 * with dynamically loaded kernel modules.
 *			Jon.
 *
 * - Stacked module support and unified symbol table added (June 1994)
 * - External symbol table support added (December 1994)
 * - Versions on symbols added (December 1994)
 *   by Bjorn Ekwall <bj0rn@blox.se>
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <linux/kernel_stat.h>
#include <linux/vmalloc.h>
#include <linux/sys.h>
#include <linux/utsname.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>
#include <linux/hdreg.h>
#include <linux/skbuff.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/swap.h>
#include <linux/pagevec.h>
#include <linux/ctype.h>
#include <linux/file.h>
#include <linux/console.h>
#include <linux/poll.h>
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/capability.h>
#include <linux/highuid.h>
#include <linux/uio.h>
#include <linux/tty.h>
#include <linux/in6.h>
#include <linux/binfmts.h>
#include <linux/namei.h>
#include <linux/buffer_head.h>
#include <linux/root_dev.h>
#include <linux/percpu.h>
#include <linux/smp_lock.h>
#include <linux/dnotify.h>
#include <linux/mount.h>
#include <linux/ptrace.h>
#include <linux/time.h>
#include <linux/backing-dev.h>
#include <linux/percpu_counter.h>
#include <asm/checksum.h>

#if defined(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
#endif
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

extern struct timezone sys_tz;

extern int panic_timeout;

/* process memory management */
EXPORT_SYMBOL(do_mmap_pgoff);
EXPORT_SYMBOL(do_munmap);
EXPORT_SYMBOL(do_brk);
EXPORT_SYMBOL(exit_mm);
EXPORT_SYMBOL_GPL(exit_fs);
EXPORT_SYMBOL_GPL(copy_fs_struct);

/* internal kernel memory management */
EXPORT_SYMBOL(__alloc_pages);
EXPORT_SYMBOL(__get_free_pages);
EXPORT_SYMBOL(get_zeroed_page);
EXPORT_SYMBOL(__page_cache_release);
EXPORT_SYMBOL(__pagevec_lru_add);
EXPORT_SYMBOL(__free_pages);
EXPORT_SYMBOL(free_pages);
EXPORT_SYMBOL(num_physpages);
EXPORT_SYMBOL(kmem_find_general_cachep);
EXPORT_SYMBOL(kmem_cache_create);
EXPORT_SYMBOL(kmem_cache_destroy);
EXPORT_SYMBOL(kmem_cache_shrink);
EXPORT_SYMBOL(kmem_cache_alloc);
EXPORT_SYMBOL(kmem_cache_free);
EXPORT_SYMBOL(kmem_cache_size);
EXPORT_SYMBOL(set_shrinker);
EXPORT_SYMBOL(remove_shrinker);
EXPORT_SYMBOL(malloc_sizes);
EXPORT_SYMBOL(__kmalloc);
EXPORT_SYMBOL(kfree);
#ifdef CONFIG_SMP
EXPORT_SYMBOL(__alloc_percpu);
EXPORT_SYMBOL(free_percpu);
EXPORT_SYMBOL(percpu_counter_mod);
#endif
EXPORT_SYMBOL(vfree);
EXPORT_SYMBOL(__vmalloc);
EXPORT_SYMBOL(vmalloc);
EXPORT_SYMBOL(vmalloc_32);
EXPORT_SYMBOL(vmap);
EXPORT_SYMBOL(vunmap);
EXPORT_SYMBOL(vmalloc_to_page);
EXPORT_SYMBOL(remap_page_range);
#ifndef CONFIG_DISCONTIGMEM
EXPORT_SYMBOL(contig_page_data);
EXPORT_SYMBOL(mem_map);
EXPORT_SYMBOL(max_mapnr);
#endif
EXPORT_SYMBOL(high_memory);
EXPORT_SYMBOL(vmtruncate);
EXPORT_SYMBOL(find_vma);
EXPORT_SYMBOL(get_unmapped_area);
EXPORT_SYMBOL(init_mm);
EXPORT_SYMBOL(blk_queue_bounce);
EXPORT_SYMBOL(blk_congestion_wait);
#ifdef CONFIG_HIGHMEM
EXPORT_SYMBOL(kmap_high);
EXPORT_SYMBOL(kunmap_high);
EXPORT_SYMBOL(highmem_start_page);
EXPORT_SYMBOL(kmap_prot);
EXPORT_SYMBOL(kmap_pte);
#endif
#ifdef HASHED_PAGE_VIRTUAL
EXPORT_SYMBOL(page_address);
#endif
EXPORT_SYMBOL(get_user_pages);

/* filesystem internal functions */
EXPORT_SYMBOL(dcache_lock);
EXPORT_SYMBOL(invalidate_inode_pages);
EXPORT_SYMBOL_GPL(invalidate_inode_pages2);
EXPORT_SYMBOL(truncate_inode_pages);
EXPORT_SYMBOL(file_ra_state_init);
EXPORT_SYMBOL(ROOT_DEV);
EXPORT_SYMBOL(read_cache_pages);
EXPORT_SYMBOL(mark_page_accessed);

/* for stackable file systems (lofs, wrapfs, cryptfs, etc.) */
EXPORT_SYMBOL(lock_page);

/* device registration */
EXPORT_SYMBOL(register_blkdev);
EXPORT_SYMBOL(unregister_blkdev);
EXPORT_SYMBOL(tty_register_driver);
EXPORT_SYMBOL(tty_unregister_driver);
EXPORT_SYMBOL(tty_std_termios);

/* block device driver support */
EXPORT_SYMBOL_GPL(default_backing_dev_info);

/* tty routines */
EXPORT_SYMBOL(tty_wait_until_sent);
EXPORT_SYMBOL(tty_flip_buffer_push);

/* interrupt handling */
EXPORT_SYMBOL(request_irq);
EXPORT_SYMBOL(free_irq);

/* waitqueue handling */
EXPORT_SYMBOL(add_wait_queue);
EXPORT_SYMBOL(add_wait_queue_exclusive);
EXPORT_SYMBOL(remove_wait_queue);
EXPORT_SYMBOL(prepare_to_wait);
EXPORT_SYMBOL(prepare_to_wait_exclusive);
EXPORT_SYMBOL(finish_wait);
EXPORT_SYMBOL(autoremove_wake_function);

/* The notion of irq probe/assignment is foreign to S/390 */

#if !defined(CONFIG_ARCH_S390)
EXPORT_SYMBOL(probe_irq_on);
EXPORT_SYMBOL(probe_irq_off);
#endif

#ifdef CONFIG_SMP
EXPORT_SYMBOL(del_timer_sync);
#endif
EXPORT_SYMBOL(del_timer);
EXPORT_SYMBOL(mod_timer);
EXPORT_SYMBOL(__mod_timer);

#ifdef HAVE_DISABLE_HLT
EXPORT_SYMBOL(disable_hlt);
EXPORT_SYMBOL(enable_hlt);
#endif

/* resource handling */
EXPORT_SYMBOL(request_resource);
EXPORT_SYMBOL(release_resource);
EXPORT_SYMBOL(allocate_resource);
EXPORT_SYMBOL(__request_region);
EXPORT_SYMBOL(__check_region);
EXPORT_SYMBOL(__release_region);
EXPORT_SYMBOL(ioport_resource);
EXPORT_SYMBOL(iomem_resource);

/* process management */
EXPORT_SYMBOL(complete_and_exit);
EXPORT_SYMBOL(schedule_timeout);
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
EXPORT_SYMBOL(kernel_flag);
#endif
EXPORT_SYMBOL(jiffies);
EXPORT_SYMBOL(jiffies_64);
EXPORT_SYMBOL(xtime);
EXPORT_SYMBOL(xtime_lock);
EXPORT_SYMBOL(do_gettimeofday);
EXPORT_SYMBOL(do_settimeofday);
#if (BITS_PER_LONG < 64)
EXPORT_SYMBOL(get_jiffies_64);
#endif
#if !defined(__ia64__)
EXPORT_SYMBOL(loops_per_jiffy);
#endif

/* misc */
EXPORT_SYMBOL(panic);
EXPORT_SYMBOL(panic_notifier_list);
EXPORT_SYMBOL(panic_timeout);
EXPORT_SYMBOL(sprintf);
EXPORT_SYMBOL(snprintf);
EXPORT_SYMBOL(sscanf);
EXPORT_SYMBOL(vsprintf);
EXPORT_SYMBOL(vsnprintf);
EXPORT_SYMBOL(vsscanf);
EXPORT_SYMBOL(simple_strtoull);
EXPORT_SYMBOL(simple_strtoul);
EXPORT_SYMBOL(simple_strtol);
EXPORT_SYMBOL(system_utsname);	/* UTS data */
EXPORT_SYMBOL(uts_sem);		/* UTS semaphore */
EXPORT_SYMBOL(machine_restart);
EXPORT_SYMBOL(machine_halt);
EXPORT_SYMBOL(machine_power_off);
EXPORT_SYMBOL(_ctype);
EXPORT_SYMBOL(secure_tcp_sequence_number);
EXPORT_SYMBOL(get_random_bytes);
EXPORT_SYMBOL(securebits);
EXPORT_SYMBOL(cap_bset);
EXPORT_SYMBOL(daemonize);
EXPORT_SYMBOL(csum_partial); /* for networking and md */

/* Miscellaneous access points */
EXPORT_SYMBOL(si_meminfo);

/* Added to make file system as module */
EXPORT_SYMBOL(sys_tz);

#ifdef CONFIG_UID16
EXPORT_SYMBOL(overflowuid);
EXPORT_SYMBOL(overflowgid);
#endif
EXPORT_SYMBOL(fs_overflowuid);
EXPORT_SYMBOL(fs_overflowgid);

/* library functions */
EXPORT_SYMBOL(strnicmp);
EXPORT_SYMBOL(strspn);
EXPORT_SYMBOL(strsep);

/* software interrupts */
EXPORT_SYMBOL(tasklet_init);
EXPORT_SYMBOL(tasklet_kill);
EXPORT_SYMBOL(do_softirq);
EXPORT_SYMBOL(raise_softirq);
EXPORT_SYMBOL(open_softirq);
EXPORT_SYMBOL(raise_softirq_irqoff);
EXPORT_SYMBOL(__tasklet_schedule);
EXPORT_SYMBOL(__tasklet_hi_schedule);

/* init task, for moving kthread roots - ought to export a function ?? */
EXPORT_SYMBOL(init_task);

EXPORT_SYMBOL(tasklist_lock);
EXPORT_SYMBOL(find_task_by_pid);
EXPORT_SYMBOL(next_thread);
#if defined(CONFIG_SMP) && defined(__GENERIC_PER_CPU)
EXPORT_SYMBOL(__per_cpu_offset);
#endif

/* debug */
EXPORT_SYMBOL(dump_stack);
EXPORT_SYMBOL(ptrace_notify);
EXPORT_SYMBOL(console_printk);

EXPORT_SYMBOL(current_kernel_time);
