
/*
 * mtd/include/compatmac.h
 *
 * $Id: compatmac.h,v 1.4 2000/07/03 10:01:38 dwmw2 Exp $
 *
 * Extensions and omissions from the normal 'linux/compatmac.h'
 * files. hopefully this will end up empty as the 'real' one 
 * becomes fully-featured.
 */


/* First, include the parts which the kernel is good enough to provide 
 * to us 
 */
   
#ifndef __LINUX_MTD_COMPATMAC_H__
#define __LINUX_MTD_COMPATMAC_H__

#include <linux/compatmac.h>
#include <linux/types.h> /* used later in this header */
#include <linux/module.h>
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
#include <linux/vmalloc.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,0,0)
#  error "This kernel is too old: not supported by this file"
#endif

/* Modularization issues */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,18)
#  define __USE_OLD_SYMTAB__
#  define EXPORT_NO_SYMBOLS register_symtab(NULL);
#  define REGISTER_SYMTAB(tab) register_symtab(tab)
#else
#  define REGISTER_SYMTAB(tab) /* nothing */
#endif

#ifdef __USE_OLD_SYMTAB__
#  define __MODULE_STRING(s)         /* nothing */
#  define MODULE_PARM(v,t)           /* nothing */
#  define MODULE_PARM_DESC(v,t)      /* nothing */
#  define MODULE_AUTHOR(n)           /* nothing */
#  define MODULE_DESCRIPTION(d)      /* nothing */
#  define MODULE_SUPPORTED_DEVICE(n) /* nothing */
#endif

/*
 * "select" changed in 2.1.23. The implementation is twin, but this
 * header is new
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,22)
#  include <linux/poll.h>
#else
#  define __USE_OLD_SELECT__
#endif

/* Other change in the fops are solved using pseudo-types */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
#  define lseek_t      long long
#  define lseek_off_t  long long
#else
#  define lseek_t      int
#  define lseek_off_t  off_t
#endif

/* changed the prototype of read/write */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0) || defined(__alpha__)
# define count_t unsigned long
# define read_write_t long
#else
# define count_t int
# define read_write_t int
#endif


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,31)
# define release_t void
#  define release_return(x) return
#else
#  define release_t int
#  define release_return(x) return (x)
#endif

#if LINUX_VERSION_CODE < 0x20300
#define __exit
#endif
#if LINUX_VERSION_CODE < 0x20200
#define __init
#else
#include <linux/init.h>
#endif

#if LINUX_VERSION_CODE < 0x20300
#define init_MUTEX(x) do {*(x) = MUTEX;} while (0)
#define RQFUNC_ARG void
#define blkdev_dequeue_request(req) do {CURRENT = req->next;} while (0)
#else
#define RQFUNC_ARG request_queue_t *q
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,2,0)
#define __MOD_INC_USE_COUNT(mod)                                        \
        (atomic_inc(&(mod)->uc.usecount), (mod)->flags |= MOD_VISITED|MOD_USED_ONCE)
#define __MOD_DEC_USE_COUNT(mod)                                        \
        (atomic_dec(&(mod)->uc.usecount), (mod)->flags |= MOD_VISITED)
#endif



#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)

#define DECLARE_WAIT_QUEUE_HEAD(x) struct wait_queue *x = NULL
#define init_waitqueue_head init_waitqueue

static inline int try_inc_mod_count(struct module *mod)
{
	if (mod)
		__MOD_INC_USE_COUNT(mod);
	return 1;
}
#endif


/* Yes, I'm aware that it's a fairly ugly hack.
   Until the __constant_* macros appear in Linus' own kernels, this is
   the way it has to be done.
 DW 19/1/00
 */

#include <asm/byteorder.h>

#ifndef __constant_cpu_to_le16

#ifdef __BIG_ENDIAN
#define __constant_cpu_to_le64(x) ___swab64((x))
#define __constant_le64_to_cpu(x) ___swab64((x))
#define __constant_cpu_to_le32(x) ___swab32((x))
#define __constant_le32_to_cpu(x) ___swab32((x))
#define __constant_cpu_to_le16(x) ___swab16((x))
#define __constant_le16_to_cpu(x) ___swab16((x))
#define __constant_cpu_to_be64(x) ((__u64)(x))
#define __constant_be64_to_cpu(x) ((__u64)(x))
#define __constant_cpu_to_be32(x) ((__u32)(x))
#define __constant_be32_to_cpu(x) ((__u32)(x))
#define __constant_cpu_to_be16(x) ((__u16)(x))
#define __constant_be16_to_cpu(x) ((__u16)(x))
#else
#ifdef __LITTLE_ENDIAN
#define __constant_cpu_to_le64(x) ((__u64)(x))
#define __constant_le64_to_cpu(x) ((__u64)(x))
#define __constant_cpu_to_le32(x) ((__u32)(x))
#define __constant_le32_to_cpu(x) ((__u32)(x))
#define __constant_cpu_to_le16(x) ((__u16)(x))
#define __constant_le16_to_cpu(x) ((__u16)(x))
#define __constant_cpu_to_be64(x) ___swab64((x))
#define __constant_be64_to_cpu(x) ___swab64((x))
#define __constant_cpu_to_be32(x) ___swab32((x))
#define __constant_be32_to_cpu(x) ___swab32((x))
#define __constant_cpu_to_be16(x) ___swab16((x))
#define __constant_be16_to_cpu(x) ___swab16((x))
#else
#error No (recognised) endianness defined (unless it,s PDP)
#endif /* __LITTLE_ENDIAN */
#endif /* __BIG_ENDIAN */

#endif /* ifndef __constant_cpu_to_le16 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
  #define mod_init_t int  __init
  #define mod_exit_t void  
#else
  #define mod_init_t static int __init
  #define mod_exit_t static void __exit
#endif

#ifndef THIS_MODULE
#ifdef MODULE
#define THIS_MODULE (&__this_module)
#else
#define THIS_MODULE (NULL)
#endif
#endif

#if LINUX_VERSION_CODE < 0x20300
#include <linux/interrupt.h>
#define spin_lock_bh(lock) do {start_bh_atomic();spin_lock(lock);}while(0);
#define spin_unlock_bh(lock) do {spin_unlock(lock);end_bh_atomic();}while(0);
#else
#include <asm/softirq.h>
#include <linux/spinlock.h>
#endif

#endif /* __LINUX_MTD_COMPATMAC_H__ */


