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
#include <linux/brlock.h>
#include <linux/fs.h>
#include <linux/uio.h>
#include <linux/tty.h>
#include <linux/in6.h>
#include <linux/completion.h>
#include <linux/seq_file.h>
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
EXPORT_SYMBOL(kmalloc);
EXPORT_SYMBOL(kfree);
#ifdef CONFIG_SMP
EXPORT_SYMBOL(kmalloc_percpu);
EXPORT_SYMBOL(kfree_percpu);
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
EXPORT_SYMBOL(def_blk_fops);
EXPORT_SYMBOL(update_atime);
EXPORT_SYMBOL(get_fs_type);
EXPORT_SYMBOL(user_get_super);
EXPORT_SYMBOL(get_super);
EXPORT_SYMBOL(drop_super);
EXPORT_SYMBOL(getname);
EXPORT_SYMBOL(names_cachep);
EXPORT_SYMBOL(fput);
EXPORT_SYMBOL(fget);
EXPORT_SYMBOL(igrab);
EXPORT_SYMBOL(iunique);
EXPORT_SYMBOL(iput);
EXPORT_SYMBOL(inode_init_once);
EXPORT_SYMBOL(follow_up);
EXPORT_SYMBOL(follow_down);
EXPORT_SYMBOL(lookup_mnt);
EXPORT_SYMBOL(path_lookup);
EXPORT_SYMBOL(path_walk);
EXPORT_SYMBOL(path_release);
EXPORT_SYMBOL(__user_walk);
EXPORT_SYMBOL(lookup_one_len);
EXPORT_SYMBOL(lookup_hash);
EXPORT_SYMBOL(sys_close);
EXPORT_SYMBOL(dcache_lock);
EXPORT_SYMBOL(d_alloc_root);
EXPORT_SYMBOL(d_delete);
EXPORT_SYMBOL(dget_locked);
EXPORT_SYMBOL(d_validate);
EXPORT_SYMBOL(d_rehash);
EXPORT_SYMBOL(d_invalidate);	/* May be it will be better in dcache.h? */
EXPORT_SYMBOL(d_move);
EXPORT_SYMBOL(d_instantiate);
EXPORT_SYMBOL(d_alloc);
EXPORT_SYMBOL(d_alloc_anon);
EXPORT_SYMBOL(d_splice_alias);
EXPORT_SYMBOL(d_lookup);
EXPORT_SYMBOL(d_path);
EXPORT_SYMBOL(mark_buffer_dirty);
EXPORT_SYMBOL(end_buffer_io_sync);
EXPORT_SYMBOL(end_buffer_async_write);
EXPORT_SYMBOL(__mark_inode_dirty);
EXPORT_SYMBOL(get_empty_filp);
EXPORT_SYMBOL(open_private_file);
EXPORT_SYMBOL(close_private_file);
EXPORT_SYMBOL(filp_open);
EXPORT_SYMBOL(filp_close);
EXPORT_SYMBOL(put_filp);
EXPORT_SYMBOL(files_lock);
EXPORT_SYMBOL(check_disk_change);
EXPORT_SYMBOL(invalidate_bdev);
EXPORT_SYMBOL(invalidate_inodes);
EXPORT_SYMBOL(__invalidate_device);
EXPORT_SYMBOL(invalidate_inode_pages);
EXPORT_SYMBOL_GPL(invalidate_inode_pages2);
EXPORT_SYMBOL(truncate_inode_pages);
EXPORT_SYMBOL(fsync_bdev);
EXPORT_SYMBOL(permission);
EXPORT_SYMBOL(vfs_permission);
EXPORT_SYMBOL(inode_setattr);
EXPORT_SYMBOL(inode_change_ok);
EXPORT_SYMBOL(write_inode_now);
EXPORT_SYMBOL(notify_change);
EXPORT_SYMBOL(set_blocksize);
EXPORT_SYMBOL(sb_set_blocksize);
EXPORT_SYMBOL(sb_min_blocksize);
EXPORT_SYMBOL(bdget);
EXPORT_SYMBOL(bdput);
EXPORT_SYMBOL(bd_claim);
EXPORT_SYMBOL(bd_release);
EXPORT_SYMBOL(open_bdev_excl);
EXPORT_SYMBOL(close_bdev_excl);
EXPORT_SYMBOL(open_by_devnum);
EXPORT_SYMBOL(__brelse);
EXPORT_SYMBOL(__bforget);
EXPORT_SYMBOL(ll_rw_block);
EXPORT_SYMBOL(sync_dirty_buffer);
EXPORT_SYMBOL(submit_bh);
EXPORT_SYMBOL(unlock_buffer);
EXPORT_SYMBOL(__wait_on_buffer);
EXPORT_SYMBOL(blockdev_direct_IO);
EXPORT_SYMBOL(block_write_full_page);
EXPORT_SYMBOL(block_read_full_page);
EXPORT_SYMBOL(block_prepare_write);
EXPORT_SYMBOL(block_sync_page);
EXPORT_SYMBOL(generic_cont_expand);
EXPORT_SYMBOL(cont_prepare_write);
EXPORT_SYMBOL(generic_commit_write);
EXPORT_SYMBOL(block_truncate_page);
EXPORT_SYMBOL(generic_block_bmap);
EXPORT_SYMBOL(generic_file_read);
EXPORT_SYMBOL(generic_file_sendfile);
EXPORT_SYMBOL(do_generic_mapping_read);
EXPORT_SYMBOL(file_ra_state_init);
EXPORT_SYMBOL(generic_file_write);
EXPORT_SYMBOL(generic_file_write_nolock);
EXPORT_SYMBOL(generic_file_mmap);
EXPORT_SYMBOL(generic_file_readonly_mmap);
EXPORT_SYMBOL(generic_ro_fops);
EXPORT_SYMBOL(dput);
EXPORT_SYMBOL(have_submounts);
EXPORT_SYMBOL(d_find_alias);
EXPORT_SYMBOL(d_prune_aliases);
EXPORT_SYMBOL(shrink_dcache_sb);
EXPORT_SYMBOL(shrink_dcache_parent);
EXPORT_SYMBOL(shrink_dcache_anon);
EXPORT_SYMBOL(find_inode_number);
EXPORT_SYMBOL(is_subdir);
EXPORT_SYMBOL(get_unused_fd);
EXPORT_SYMBOL(vfs_read);
EXPORT_SYMBOL(vfs_readv);
EXPORT_SYMBOL(vfs_write);
EXPORT_SYMBOL(vfs_writev);
EXPORT_SYMBOL(vfs_create);
EXPORT_SYMBOL(vfs_mkdir);
EXPORT_SYMBOL(vfs_mknod);
EXPORT_SYMBOL(vfs_symlink);
EXPORT_SYMBOL(vfs_link);
EXPORT_SYMBOL(vfs_rmdir);
EXPORT_SYMBOL(vfs_unlink);
EXPORT_SYMBOL(vfs_rename);
EXPORT_SYMBOL(vfs_statfs);
EXPORT_SYMBOL(vfs_fstat);
EXPORT_SYMBOL(vfs_stat);
EXPORT_SYMBOL(vfs_lstat);
EXPORT_SYMBOL(vfs_getattr);
EXPORT_SYMBOL(inode_add_bytes);
EXPORT_SYMBOL(inode_sub_bytes);
EXPORT_SYMBOL(inode_get_bytes);
EXPORT_SYMBOL(inode_set_bytes);
EXPORT_SYMBOL(lock_rename);
EXPORT_SYMBOL(unlock_rename);
EXPORT_SYMBOL(generic_read_dir);
EXPORT_SYMBOL(generic_fillattr);
EXPORT_SYMBOL(generic_file_llseek);
EXPORT_SYMBOL(remote_llseek);
EXPORT_SYMBOL(no_llseek);
EXPORT_SYMBOL(poll_initwait);
EXPORT_SYMBOL(poll_freewait);
EXPORT_SYMBOL(ROOT_DEV);
EXPORT_SYMBOL(find_get_page);
EXPORT_SYMBOL(find_lock_page);
EXPORT_SYMBOL(find_trylock_page);
EXPORT_SYMBOL(find_or_create_page);
EXPORT_SYMBOL(grab_cache_page_nowait);
EXPORT_SYMBOL(read_cache_page);
EXPORT_SYMBOL(read_cache_pages);
EXPORT_SYMBOL(mark_page_accessed);
EXPORT_SYMBOL(vfs_readlink);
EXPORT_SYMBOL(vfs_follow_link);
EXPORT_SYMBOL(page_readlink);
EXPORT_SYMBOL(page_follow_link);
EXPORT_SYMBOL(page_symlink_inode_operations);
EXPORT_SYMBOL(page_symlink);
EXPORT_SYMBOL(vfs_readdir);
EXPORT_SYMBOL(__break_lease);
EXPORT_SYMBOL(lease_get_mtime);
EXPORT_SYMBOL(lock_may_read);
EXPORT_SYMBOL(lock_may_write);
EXPORT_SYMBOL(dcache_dir_open);
EXPORT_SYMBOL(dcache_dir_close);
EXPORT_SYMBOL(dcache_dir_lseek);
EXPORT_SYMBOL(dcache_readdir);
EXPORT_SYMBOL(simple_getattr);
EXPORT_SYMBOL(simple_statfs);
EXPORT_SYMBOL(simple_lookup);
EXPORT_SYMBOL(simple_dir_operations);
EXPORT_SYMBOL(simple_dir_inode_operations);
EXPORT_SYMBOL(simple_link);
EXPORT_SYMBOL(simple_unlink);
EXPORT_SYMBOL(simple_rmdir);
EXPORT_SYMBOL(simple_rename);
EXPORT_SYMBOL(simple_sync_file);
EXPORT_SYMBOL(simple_readpage);
EXPORT_SYMBOL(simple_prepare_write);
EXPORT_SYMBOL(simple_commit_write);
EXPORT_SYMBOL(simple_empty);
EXPORT_SYMBOL(simple_fill_super);
EXPORT_SYMBOL(simple_pin_fs);
EXPORT_SYMBOL(simple_release_fs);
EXPORT_SYMBOL(fd_install);
EXPORT_SYMBOL(put_unused_fd);
EXPORT_SYMBOL(get_sb_bdev);
EXPORT_SYMBOL(kill_block_super);
EXPORT_SYMBOL(get_sb_nodev);
EXPORT_SYMBOL(get_sb_single);
EXPORT_SYMBOL(kill_anon_super);
EXPORT_SYMBOL(kill_litter_super);
EXPORT_SYMBOL(generic_shutdown_super);
EXPORT_SYMBOL(deactivate_super);
EXPORT_SYMBOL(sget);
EXPORT_SYMBOL(set_anon_super);
EXPORT_SYMBOL(do_select);

/* for stackable file systems (lofs, wrapfs, cryptfs, etc.) */
EXPORT_SYMBOL(default_llseek);
EXPORT_SYMBOL(dentry_open);
#ifdef CONFIG_MMU
EXPORT_SYMBOL(filemap_nopage);
#endif
EXPORT_SYMBOL(filemap_fdatawrite);
EXPORT_SYMBOL(filemap_fdatawait);
EXPORT_SYMBOL(lock_page);
EXPORT_SYMBOL(unlock_page);

/* device registration */
EXPORT_SYMBOL(register_chrdev);
EXPORT_SYMBOL(unregister_chrdev);
EXPORT_SYMBOL(register_blkdev);
EXPORT_SYMBOL(unregister_blkdev);
EXPORT_SYMBOL(tty_register_driver);
EXPORT_SYMBOL(tty_unregister_driver);
EXPORT_SYMBOL(tty_std_termios);

/* block device driver support */
EXPORT_SYMBOL(bmap);
EXPORT_SYMBOL(blkdev_open);
EXPORT_SYMBOL(blkdev_get);
EXPORT_SYMBOL(blkdev_put);
EXPORT_SYMBOL(ioctl_by_bdev);
EXPORT_SYMBOL(read_dev_sector);
EXPORT_SYMBOL(init_buffer);
EXPORT_SYMBOL_GPL(generic_file_direct_IO);
EXPORT_SYMBOL(generic_file_readv);
EXPORT_SYMBOL(generic_file_writev);
EXPORT_SYMBOL(iov_shorten);
EXPORT_SYMBOL_GPL(default_backing_dev_info);

/* tty routines */
EXPORT_SYMBOL(tty_wait_until_sent);
EXPORT_SYMBOL(tty_flip_buffer_push);

/* filesystem registration */
EXPORT_SYMBOL(register_filesystem);
EXPORT_SYMBOL(unregister_filesystem);
EXPORT_SYMBOL(kern_mount);
EXPORT_SYMBOL(__mntput);
EXPORT_SYMBOL(may_umount);

/* executable format registration */
EXPORT_SYMBOL(register_binfmt);
EXPORT_SYMBOL(unregister_binfmt);
EXPORT_SYMBOL(search_binary_handler);
EXPORT_SYMBOL(prepare_binprm);
EXPORT_SYMBOL(compute_creds);
EXPORT_SYMBOL(remove_arg_zero);
EXPORT_SYMBOL(set_binfmt);

/* sysctl table registration */
EXPORT_SYMBOL(register_sysctl_table);
EXPORT_SYMBOL(unregister_sysctl_table);
EXPORT_SYMBOL(sysctl_string);
EXPORT_SYMBOL(sysctl_intvec);
EXPORT_SYMBOL(sysctl_jiffies);
EXPORT_SYMBOL(proc_dostring);
EXPORT_SYMBOL(proc_dointvec);
EXPORT_SYMBOL(proc_dointvec_jiffies);
EXPORT_SYMBOL(proc_dointvec_minmax);
EXPORT_SYMBOL(proc_doulongvec_ms_jiffies_minmax);
EXPORT_SYMBOL(proc_doulongvec_minmax);

/* interrupt handling */
EXPORT_SYMBOL(add_timer);
EXPORT_SYMBOL(del_timer);
EXPORT_SYMBOL(request_irq);
EXPORT_SYMBOL(free_irq);
EXPORT_SYMBOL(irq_stat);

/* waitqueue handling */
EXPORT_SYMBOL(add_wait_queue);
EXPORT_SYMBOL(add_wait_queue_exclusive);
EXPORT_SYMBOL(remove_wait_queue);
EXPORT_SYMBOL(prepare_to_wait);
EXPORT_SYMBOL(prepare_to_wait_exclusive);
EXPORT_SYMBOL(finish_wait);
EXPORT_SYMBOL(autoremove_wake_function);

/* completion handling */
EXPORT_SYMBOL(wait_for_completion);
EXPORT_SYMBOL(complete);

/* The notion of irq probe/assignment is foreign to S/390 */

#if !defined(CONFIG_ARCH_S390)
EXPORT_SYMBOL(probe_irq_on);
EXPORT_SYMBOL(probe_irq_off);
#endif

#ifdef CONFIG_SMP
EXPORT_SYMBOL(del_timer_sync);
#endif
EXPORT_SYMBOL(mod_timer);

#ifdef CONFIG_SMP

/* Big-Reader lock implementation */
EXPORT_SYMBOL(__brlock_array);
#ifndef __BRLOCK_USE_ATOMICS
EXPORT_SYMBOL(__br_write_locks);
#endif
EXPORT_SYMBOL(__br_write_lock);
EXPORT_SYMBOL(__br_write_unlock);
#endif

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
EXPORT_SYMBOL(default_wake_function);
EXPORT_SYMBOL(__wake_up);
#if CONFIG_SMP
EXPORT_SYMBOL_GPL(__wake_up_sync); /* internal use only */
#endif
EXPORT_SYMBOL(wake_up_process);
EXPORT_SYMBOL(sleep_on);
EXPORT_SYMBOL(sleep_on_timeout);
EXPORT_SYMBOL(interruptible_sleep_on);
EXPORT_SYMBOL(interruptible_sleep_on_timeout);
EXPORT_SYMBOL(schedule);
#ifdef CONFIG_PREEMPT
EXPORT_SYMBOL(preempt_schedule);
#endif
EXPORT_SYMBOL(schedule_timeout);
EXPORT_SYMBOL(yield);
EXPORT_SYMBOL(__cond_resched);
EXPORT_SYMBOL(set_user_nice);
EXPORT_SYMBOL(task_nice);
EXPORT_SYMBOL_GPL(idle_cpu);
#if CONFIG_SMP
EXPORT_SYMBOL_GPL(set_cpus_allowed);
#endif
#if CONFIG_SMP || CONFIG_PREEMPT
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
#ifdef CONFIG_DEBUG_SPINLOCK_SLEEP
EXPORT_SYMBOL(__might_sleep);
#endif
#if defined(CONFIG_SMP) && defined(CONFIG_PREEMPT)
EXPORT_SYMBOL(__preempt_spin_lock);
EXPORT_SYMBOL(__preempt_write_lock);
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
EXPORT_SYMBOL(__bdevname);
EXPORT_SYMBOL(bdevname);
EXPORT_SYMBOL(cdevname);
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
EXPORT_SYMBOL(seq_escape);
EXPORT_SYMBOL(seq_printf);
EXPORT_SYMBOL(seq_open);
EXPORT_SYMBOL(seq_release);
EXPORT_SYMBOL(seq_read);
EXPORT_SYMBOL(seq_lseek);
EXPORT_SYMBOL(single_open);
EXPORT_SYMBOL(single_release);
EXPORT_SYMBOL(seq_release_private);

/* Program loader interfaces */
#ifdef CONFIG_MMU
EXPORT_SYMBOL(setup_arg_pages);
#endif
EXPORT_SYMBOL(copy_strings_kernel);
EXPORT_SYMBOL(do_execve);
EXPORT_SYMBOL(flush_old_exec);
EXPORT_SYMBOL(kernel_read);
EXPORT_SYMBOL(open_exec);

/* Miscellaneous access points */
EXPORT_SYMBOL(si_meminfo);

/* Added to make file system as module */
EXPORT_SYMBOL(sys_tz);
EXPORT_SYMBOL(file_fsync);
EXPORT_SYMBOL(fsync_buffers_list);
EXPORT_SYMBOL(clear_inode);
EXPORT_SYMBOL(init_special_inode);
EXPORT_SYMBOL(new_inode);
EXPORT_SYMBOL(__insert_inode_hash);
EXPORT_SYMBOL(remove_inode_hash);
EXPORT_SYMBOL(buffer_insert_list);
EXPORT_SYMBOL(make_bad_inode);
EXPORT_SYMBOL(is_bad_inode);
EXPORT_SYMBOL(__inode_dir_notify);

#ifdef CONFIG_UID16
EXPORT_SYMBOL(overflowuid);
EXPORT_SYMBOL(overflowgid);
#endif
EXPORT_SYMBOL(fs_overflowuid);
EXPORT_SYMBOL(fs_overflowgid);

/* all busmice */
EXPORT_SYMBOL(fasync_helper);
EXPORT_SYMBOL(kill_fasync);

/* binfmt_aout */
EXPORT_SYMBOL(get_write_access);

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
EXPORT_SYMBOL(cpu_raise_softirq);
EXPORT_SYMBOL(__tasklet_schedule);
EXPORT_SYMBOL(__tasklet_hi_schedule);

/* init task, for moving kthread roots - ought to export a function ?? */

EXPORT_SYMBOL(init_task);
EXPORT_SYMBOL(init_thread_union);

EXPORT_SYMBOL(tasklist_lock);
EXPORT_SYMBOL(find_task_by_pid);
EXPORT_SYMBOL(next_thread);
#if defined(CONFIG_SMP) && defined(__GENERIC_PER_CPU)
EXPORT_SYMBOL(__per_cpu_offset);
#endif

/* debug */
EXPORT_SYMBOL(dump_stack);
EXPORT_SYMBOL(ptrace_notify);

EXPORT_SYMBOL(current_kernel_time); 
