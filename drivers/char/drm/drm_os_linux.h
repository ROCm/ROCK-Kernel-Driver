#define __NO_VERSION__

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>

#define DRM_IOCTL_ARGS			struct inode *inode, struct file *filp, unsigned int cmd, unsigned long data
#define DRM_ERR(d)			-(d)
#define DRM_CURRENTPID			current->pid
#define DRM_UDELAY(d)			udelay(d)
#define DRM_READ8(addr)			readb(addr)
#define DRM_READ32(addr)		readl(addr)
#define DRM_WRITE8(addr, val)		writeb(val, addr)
#define DRM_WRITE32(addr, val)		writel(val, addr)
#define DRM_READMEMORYBARRIER()		mb()
#define DRM_WRITEMEMORYBARRIER()	wmb()
#define DRM_DEVICE	drm_file_t	*priv	= filp->private_data; \
			drm_device_t	*dev	= priv->dev

#define DRM_IRQ_ARGS	        int irq, void *arg, struct pt_regs *regs
#define DRM_TASKQUEUE_ARGS	void *arg

/* For data going from/to the kernel through the ioctl argument */
#define DRM_COPY_FROM_USER_IOCTL(arg1, arg2, arg3)	\
	if ( copy_from_user(&arg1, arg2, arg3) )	\
		return -EFAULT
#define DRM_COPY_TO_USER_IOCTL(arg1, arg2, arg3)	\
	if ( copy_to_user(arg1, &arg2, arg3) )		\
		return -EFAULT
/* Other copying of data from/to kernel space */
#define DRM_COPY_FROM_USER(arg1, arg2, arg3)		\
	copy_from_user(arg1, arg2, arg3)
#define DRM_COPY_TO_USER(arg1, arg2, arg3)		\
	copy_to_user(arg1, arg2, arg3)
/* Macros for copyfrom user, but checking readability only once */
#define DRM_VERIFYAREA_READ( uaddr, size ) 		\
	verify_area( VERIFY_READ, uaddr, size )
#define DRM_COPY_FROM_USER_UNCHECKED(arg1, arg2, arg3) 	\
	__copy_from_user(arg1, arg2, arg3)
#define DRM_GET_USER_UNCHECKED(val, uaddr)		\
	__get_user(val, uaddr)


/* malloc/free without the overhead of DRM(alloc) */
#define DRM_MALLOC(x) kmalloc(x, GFP_KERNEL)
#define DRM_FREE(x) kfree(x)

#define DRM_GETSAREA()							 \
do { 									 \
	struct list_head *list;						 \
	list_for_each( list, &dev->maplist->head ) {			 \
		drm_map_list_t *entry = (drm_map_list_t *)list;		 \
		if ( entry->map &&					 \
		     entry->map->type == _DRM_SHM &&			 \
		     (entry->map->flags & _DRM_CONTAINS_LOCK) ) {	 \
			dev_priv->sarea = entry->map;			 \
 			break;						 \
 		}							 \
 	}								 \
} while (0)

#define DRM_HZ HZ

#define DRM_WAIT_ON( ret, queue, timeout, condition )	\
do {							\
	DECLARE_WAITQUEUE(entry, current);		\
	unsigned long end = jiffies + (timeout);	\
	add_wait_queue(&(queue), &entry);		\
							\
	for (;;) {					\
		current->state = TASK_INTERRUPTIBLE;	\
		if (condition) 				\
			break;				\
		if((signed)(end - jiffies) <= 0) {	\
			ret = -EBUSY;			\
			break;				\
		}					\
		schedule_timeout((HZ/100 > 1) ? HZ/100 : 1);	\
		if (signal_pending(current)) {		\
			ret = -EINTR;			\
			break;				\
		}					\
	}						\
	current->state = TASK_RUNNING;			\
	remove_wait_queue(&(queue), &entry);		\
} while (0)


#define DRM_WAKEUP( queue ) wake_up_interruptible( queue )
#define DRM_INIT_WAITQUEUE( queue ) init_waitqueue_head( queue )
 
