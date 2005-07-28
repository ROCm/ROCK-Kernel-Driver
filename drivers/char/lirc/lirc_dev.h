/*
 * LIRC base driver
 * 
 * (L) by Artur Lipowski <alipowski@interia.pl>
 *        This code is licensed under GNU GPL
 *
 * $Id: lirc_dev.h,v 1.16 2005/02/19 15:30:20 lirc Exp $
 *
 */

#ifndef _LINUX_LIRC_DEV_H
#define _LINUX_LIRC_DEV_H

#define MAX_IRCTL_DEVICES 4
#define BUFLEN            16

//#define LIRC_BUFF_POWER_OF_2
#ifdef LIRC_BUFF_POWER_OF_2
#define mod(n, div) ((n) & ((div) -1))
#else
#define mod(n, div) ((n) % (div))
#endif
#include <linux/slab.h>
#include <linux/fs.h>
struct lirc_buffer
{
        wait_queue_head_t wait_poll;
	spinlock_t lock;

	unsigned char *data;
	unsigned int chunk_size;
	unsigned int size; /* in chunks */
	unsigned int fill; /* in chunks */
	int head, tail;    /* in chunks */
	/* Using chunks instead of bytes pretends to simplify boundary checking 
	 * And should allow for some performance fine tunning later */
};
static inline int lirc_buffer_init(struct lirc_buffer *buf,
				    unsigned int chunk_size,
				    unsigned int size)
{
	/* Adjusting size to the next power of 2 would allow for
	 * inconditional LIRC_BUFF_POWER_OF_2 optimization */
	init_waitqueue_head(&buf->wait_poll);
	spin_lock_init(&buf->lock);
	buf->head = buf->tail = buf->fill = 0;
	buf->chunk_size = chunk_size;
	buf->size = size;
	buf->data = kmalloc(size*chunk_size, GFP_KERNEL);
	if (buf->data == NULL)
		return -1;
	memset(buf->data, 0, size*chunk_size);
	return 0;
}
static inline void lirc_buffer_free(struct lirc_buffer *buf)
{
	kfree(buf->data);
	buf->data = NULL;
	buf->head = buf->tail = buf->fill = 0;
	buf->chunk_size = 0;
	buf->size = 0;
}
static inline int  lirc_buffer_full(struct lirc_buffer *buf)
{
	return (buf->fill >= buf->size);
}
static inline int  lirc_buffer_empty(struct lirc_buffer *buf)
{
	return !(buf->fill);
}
static inline int  lirc_buffer_available(struct lirc_buffer *buf)
{
    return (buf->size - buf->fill);
}
static inline void lirc_buffer_lock(struct lirc_buffer *buf, unsigned long *flags)
{
	spin_lock_irqsave(&buf->lock, *flags);
}
static inline void lirc_buffer_unlock(struct lirc_buffer *buf, unsigned long *flags)
{
	spin_unlock_irqrestore(&buf->lock, *flags);
}
static inline void _lirc_buffer_remove_1(struct lirc_buffer *buf)
{
	buf->head = mod(buf->head+1, buf->size);
	buf->fill -= 1;
}
static inline void lirc_buffer_remove_1(struct lirc_buffer *buf)
{
	unsigned long flags;
	lirc_buffer_lock(buf, &flags);
	_lirc_buffer_remove_1(buf);
	lirc_buffer_unlock(buf, &flags);
}
static inline void _lirc_buffer_read_1(struct lirc_buffer *buf,
				     unsigned char *dest)
{
	memcpy(dest, &buf->data[buf->head*buf->chunk_size], buf->chunk_size);
	buf->head = mod(buf->head+1, buf->size);
	buf->fill -= 1;
}
static inline void lirc_buffer_read_1(struct lirc_buffer *buf,
				      unsigned char *dest)
{
	unsigned long flags;
	lirc_buffer_lock(buf, &flags);
	_lirc_buffer_read_1(buf, dest);
	lirc_buffer_unlock(buf, &flags);
}
static inline void _lirc_buffer_write_1(struct lirc_buffer *buf,
				      unsigned char *orig)
{
	memcpy(&buf->data[buf->tail*buf->chunk_size], orig, buf->chunk_size);
	buf->tail = mod(buf->tail+1, buf->size);
	buf->fill++;
}
static inline void lirc_buffer_write_1(struct lirc_buffer *buf,
				       unsigned char *orig)
{
	unsigned long flags;
	lirc_buffer_lock(buf, &flags);
	_lirc_buffer_write_1(buf, orig);
	lirc_buffer_unlock(buf, &flags);
}
static inline void _lirc_buffer_write_n(struct lirc_buffer *buf,
					unsigned char* orig, int count)
{
	memcpy(&buf->data[buf->tail*buf->chunk_size], orig,
	       count*buf->chunk_size);
	buf->tail = mod(buf->tail+count, buf->size);
	buf->fill += count;
}
static inline void lirc_buffer_write_n(struct lirc_buffer *buf,
				       unsigned char* orig, int count)
{
	unsigned long flags;
	int space1;
	lirc_buffer_lock(buf,&flags);
	if( buf->head > buf->tail ) space1 = buf->head - buf->tail;
	else space1 = buf->size - buf->tail;
	
	if( count > space1 )
	{
		_lirc_buffer_write_n(buf, orig, space1);
		_lirc_buffer_write_n(buf, orig+(space1*buf->chunk_size),
				     count-space1);
	}
	else
	{
		_lirc_buffer_write_n(buf, orig, count);
	}
	lirc_buffer_unlock(buf, &flags);
}

struct lirc_plugin
{
	char name[40];
	int minor;
	int code_length;
	int sample_rate;
	unsigned long features;
	void* data;
	int (*add_to_buf) (void* data, struct lirc_buffer* buf);
	wait_queue_head_t* (*get_queue) (void* data);
	struct lirc_buffer *rbuf;
	int (*set_use_inc) (void* data);
	void (*set_use_dec) (void* data);
	int (*ioctl) (struct inode *,struct file *,unsigned int,
		      unsigned long);
	struct file_operations *fops;
	struct module *owner;
};
/* name:
 * this string will be used for logs
 *
 * minor:
 * indicates minor device (/dev/lirc) number for registered plugin
 * if caller fills it with negative value, then the first free minor 
 * number will be used (if available)
 *
 * code_length:
 * length of the remote control key code expressed in bits
 *
 * sample_rate:
 * sample_rate equal to 0 means that no polling will be performed and
 * add_to_buf will be triggered by external events (through task queue
 * returned by get_queue)
 *
 * data:
 * it may point to any plugin data and this pointer will be passed to
 * all callback functions
 *
 * add_to_buf:
 * add_to_buf will be called after specified period of the time or
 * triggered by the external event, this behavior depends on value of
 * the sample_rate this function will be called in user context. This
 * routine should return 0 if data was added to the buffer and
 * -ENODATA if none was available. This should add some number of bits
 * evenly divisible by code_length to the buffer
 *
 * get_queue:
 * this callback should return a pointer to the task queue which will
 * be used for external event waiting
 *
 * rbuf:
 * if not NULL, it will be used as a read buffer, you will have to
 * write to the buffer by other means, like irq's (see also
 * lirc_serial.c).
 *
 * set_use_inc:
 * set_use_inc will be called after device is opened
 *
 * set_use_dec:
 * set_use_dec will be called after device is closed
 *
 * ioctl:
 * Some ioctl's can be directly handled by lirc_dev but will be
 * forwared here if not NULL and only handled if it returns
 * -ENOIOCTLCMD (see also lirc_serial.c).
 *
 * fops:
 * file_operations for drivers which don't fit the current plugin model.
 * 
 * owner:
 * the module owning this struct
 *
 */


/* following functions can be called ONLY from user context
 *
 * returns negative value on error or minor number 
 * of the registered device if success
 * contens of the structure pointed by p is copied
 */
extern int lirc_register_plugin(struct lirc_plugin *p);

/* returns negative value on error or 0 if success
*/
extern int lirc_unregister_plugin(int minor);


#endif
