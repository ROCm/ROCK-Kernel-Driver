#ifndef __DVB_FUNCTIONS_H__
#define __DVB_FUNCTIONS_H__

/**
 *  a sleeping delay function, waits i ms
 *
 */
static inline
void dvb_delay(int i)
{
	current->state=TASK_INTERRUPTIBLE;
	schedule_timeout((HZ*i)/1000);
}

/* we don't mess with video_usercopy() any more,
we simply define out own dvb_usercopy(), which will hopefull become
generic_usercopy()  someday... */

extern int dvb_usercopy(struct inode *inode, struct file *file,
	                    unsigned int cmd, unsigned long arg,
			    int (*func)(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg));

extern void dvb_kernel_thread_setup (const char *thread_name);

#endif

