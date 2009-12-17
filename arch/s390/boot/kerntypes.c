/*
 * kerntypes.c
 *
 * Dummy module that includes headers for all kernel types of interest.
 * The kernel type information is used by the lcrash utility when
 * analyzing system crash dumps or the live system. Using the type
 * information for the running system, rather than kernel header files,
 * makes for a more flexible and robust analysis tool.
 *
 * This source code is released under the GNU GPL.
 */

/* generate version for this file */
typedef char *COMPILE_VERSION;

/* General linux types */

#include <linux/autoconf.h>
#include <linux/compile.h>
#include <linux/utsname.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#ifdef CONFIG_SLUB
 #include <linux/slub_def.h>
#endif
#ifdef CONFIG_SLAB
 #include <linux/slab_def.h>
#endif
#ifdef CONFIG_SLQB
 #include <linux/slqb_def.h>
#endif
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bitrev.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/bootmem.h>
#include <linux/buffer_head.h>
#include <linux/cache.h>
#include <linux/cdev.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>
#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/elevator.h>
#include <linux/fd.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/futex.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/if.h>
#include <linux/if_addr.h>
#include <linux/if_arp.h>
#include <linux/if_bonding.h>
#include <linux/if_ether.h>
#include <linux/if_tr.h>
#include <linux/if_tun.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/in_route.h>
#include <linux/inet.h>
#include <linux/inet_diag.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/inotify.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/ip.h>
#include <linux/ipsec.h>
#include <linux/ipv6.h>
#include <linux/ipv6_route.h>
#include <linux/interrupt.h>
#include <linux/irqflags.h>
#include <linux/irqreturn.h>
#include <linux/jbd2.h>
#include <linux/jffs2.h>
#include <linux/jhash.h>
#include <linux/jiffies.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/kexec.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/memory.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/mm_inline.h>
#include <linux/mm_types.h>
#include <linux/mman.h>
#include <linux/mmtimer.h>
#include <linux/mmzone.h>
#include <linux/mnt_namespace.h>
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/moduleparam.h>
#include <linux/mount.h>
#include <linux/mpage.h>
#include <linux/mqueue.h>
#include <linux/mtio.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/neighbour.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/netfilter_arp.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_decnet.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netlink.h>
#include <linux/netpoll.h>
#include <linux/pagemap.h>
#include <linux/param.h>
#include <linux/percpu.h>
#include <linux/percpu_counter.h>
#include <linux/pfn.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/poll.h>
#include <linux/posix-timers.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/posix_types.h>
#include <linux/preempt.h>
#include <linux/prio_tree.h>
#include <linux/proc_fs.h>
#include <linux/profile.h>
#include <linux/ptrace.h>
#include <linux/radix-tree.h>
#include <linux/ramfs.h>
#include <linux/raw.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>
#include <linux/reboot.h>
#include <linux/relay.h>
#include <linux/resource.h>
#include <linux/romfs_fs.h>
#include <linux/root_dev.h>
#include <linux/route.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/sem.h>
#include <linux/seq_file.h>
#include <linux/seqlock.h>
#include <linux/shm.h>
#include <linux/shmem_fs.h>
#include <linux/signal.h>
#include <linux/signalfd.h>
#include <linux/skbuff.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/statfs.h>
#include <linux/stddef.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/sys.h>
#include <linux/syscalls.h>
#include <linux/sysctl.h>
#include <linux/sysdev.h>
#include <linux/sysfs.h>
#include <linux/sysrq.h>
#include <linux/tc.h>
#include <linux/tcp.h>
#include <linux/thread_info.h>
#include <linux/threads.h>
#include <linux/tick.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/timerfd.h>
#include <linux/times.h>
#include <linux/timex.h>
#include <linux/topology.h>
#include <linux/transport_class.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/tty_ldisc.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <linux/utime.h>
#include <linux/uts.h>
#include <linux/utsname.h>
#include <linux/utsrelease.h>
#include <linux/version.h>
#include <linux/vfs.h>
#include <linux/vmalloc.h>
#include <linux/vmstat.h>
#include <linux/wait.h>
#include <linux/watchdog.h>
#include <linux/workqueue.h>
#include <linux/zconf.h>
#include <linux/zlib.h>

/*
 * s390 specific includes
 */

#include <asm/lowcore.h>
#include <asm/debug.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>
#include <asm/qdio.h>
#include <asm/zcrypt.h>
#include <asm/etr.h>
#include <asm/ipl.h>
#include <asm/setup.h>
#include <asm/schid.h>
#include <asm/chsc.h>

/* channel subsystem driver */
#include "drivers/s390/cio/cio.h"
#include "drivers/s390/cio/chsc.h"
#include "drivers/s390/cio/css.h"
#include "drivers/s390/cio/device.h"
#include "drivers/s390/cio/chsc_sch.h"

/* dasd device driver */
#include "drivers/s390/block/dasd_int.h"
#include "drivers/s390/block/dasd_diag.h"
#include "drivers/s390/block/dasd_eckd.h"
#include "drivers/s390/block/dasd_fba.h"

/* networking drivers */
#include "include/net/iucv/iucv.h"
#include "drivers/s390/net/fsm.h"
#include "drivers/s390/net/ctcm_main.h"
#include "drivers/s390/net/ctcm_fsms.h"
#include "drivers/s390/net/lcs.h"
#include "drivers/s390/net/qeth_core.h"
#include "drivers/s390/net/qeth_core_mpc.h"
#include "drivers/s390/net/qeth_l3.h"

/* zfcp device driver */
#include "drivers/s390/scsi/zfcp_def.h"
#include "drivers/s390/scsi/zfcp_fsf.h"

/* crypto device driver */
#include "drivers/s390/crypto/ap_bus.h"
#include "drivers/s390/crypto/zcrypt_api.h"
#include "drivers/s390/crypto/zcrypt_cca_key.h"
#include "drivers/s390/crypto/zcrypt_pcica.h"
#include "drivers/s390/crypto/zcrypt_pcicc.h"
#include "drivers/s390/crypto/zcrypt_pcixcc.h"
#include "drivers/s390/crypto/zcrypt_cex2a.h"

/* sclp device driver */
#include "drivers/s390/char/sclp.h"
#include "drivers/s390/char/sclp_rw.h"
#include "drivers/s390/char/sclp_tty.h"

/* vmur device driver */
#include "drivers/s390/char/vmur.h"

/* qdio device driver */
#include "drivers/s390/cio/qdio.h"
#include "drivers/s390/cio/qdio_thinint.c"
#include "drivers/s390/cio/qdio_perf.h"


/* KVM */
#include "include/linux/kvm.h"
#include "include/linux/kvm_host.h"
#include "include/linux/kvm_para.h"

/* Virtio */
#include "include/linux/virtio.h"
#include "include/linux/virtio_config.h"
#include "include/linux/virtio_ring.h"
#include "include/linux/virtio_9p.h"
#include "include/linux/virtio_console.h"
#include "include/linux/virtio_rng.h"
#include "include/linux/virtio_balloon.h"
#include "include/linux/virtio_net.h"
#include "include/linux/virtio_blk.h"

/*
 * include sched.c for types:
 *    - struct prio_array
 *    - struct runqueue
 */
#include "kernel/sched.c"
/*
 * include slab.c for struct kmem_cache
 */
#ifdef CONFIG_SLUB
 #include "mm/slub.c"
#endif
#ifdef CONFIG_SLAB
 #include "mm/slab.c"
#endif
#ifdef CONFIG_SLQB
 #include "mm/slqb.c"
#endif

/* include driver core private structures */
#include "drivers/base/base.h"
