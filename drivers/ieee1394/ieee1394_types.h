
#ifndef _IEEE1394_TYPES_H
#define _IEEE1394_TYPES_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/list.h>
#include <asm/byteorder.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
#include "linux22compat.h"
#else
#define V22_COMPAT_MOD_INC_USE_COUNT do {} while (0)
#define V22_COMPAT_MOD_DEC_USE_COUNT do {} while (0)
#define OWNER_THIS_MODULE owner: THIS_MODULE,

#define INIT_TQ_LINK(tq) INIT_LIST_HEAD(&(tq).list)
#define INIT_TQ_HEAD(tq) INIT_LIST_HEAD(&(tq))
#endif

/* This showed up around this time */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,12)

# ifndef MODULE_LICENSE
# define MODULE_LICENSE(x)
# endif

# ifndef min
# define min(x,y) ({ \
	const typeof(x) _x = (x);       \
	const typeof(y) _y = (y);       \
	(void) (&_x == &_y);            \
	_x < _y ? _x : _y; })
# endif

#endif /* Linux version < 2.4.12 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,18)
#include <asm/spinlock.h>
#else
#include <linux/spinlock.h>
#endif

#ifndef list_for_each_safe
#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

typedef u32 quadlet_t;
typedef u64 octlet_t;
typedef u16 nodeid_t;

#define BUS_MASK  0xffc0
#define NODE_MASK 0x003f
#define LOCAL_BUS 0xffc0
#define ALL_NODES 0x003f

#define NODE_BUS_FMT    "%d:%d"
#define NODE_BUS_ARGS(nodeid) \
	(nodeid & NODE_MASK), ((nodeid & BUS_MASK) >> 6)

#define HPSB_PRINT(level, fmt, args...) printk(level "ieee1394: " fmt "\n" , ## args)

#define HPSB_DEBUG(fmt, args...) HPSB_PRINT(KERN_DEBUG, fmt , ## args)
#define HPSB_INFO(fmt, args...) HPSB_PRINT(KERN_INFO, fmt , ## args)
#define HPSB_NOTICE(fmt, args...) HPSB_PRINT(KERN_NOTICE, fmt , ## args)
#define HPSB_WARN(fmt, args...) HPSB_PRINT(KERN_WARNING, fmt , ## args)
#define HPSB_ERR(fmt, args...) HPSB_PRINT(KERN_ERR, fmt , ## args)

#define HPSB_PANIC(fmt, args...) panic("ieee1394: " fmt "\n" , ## args)

#define HPSB_TRACE() HPSB_PRINT(KERN_INFO, "TRACE - %s, %s(), line %d", __FILE__, __FUNCTION__, __LINE__)


#ifdef __BIG_ENDIAN

static __inline__ void *memcpy_le32(u32 *dest, const u32 *__src, size_t count)
{
        void *tmp = dest;
	u32 *src = (u32 *)__src;

        count /= 4;

        while (count--) {
                *dest++ = swab32p(src++);
        }

        return tmp;
}

#else

static __inline__ void *memcpy_le32(u32 *dest, const u32 *src, size_t count)
{
        return memcpy(dest, src, count);
}

#endif /* __BIG_ENDIAN */

#endif /* _IEEE1394_TYPES_H */
