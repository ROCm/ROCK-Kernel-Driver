/* 
 * Copyright (C) 2002 Steve Schmidtke 
 * Licensed under the GPL
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "hostaudio.h"
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "os.h"

/* /dev/dsp file operations */

ssize_t hostaudio_read_user(struct hostaudio_state *state, char *buffer, 
			    size_t count, loff_t *ppos)
{
	ssize_t ret;

#ifdef DEBUG
        printk("hostaudio: read_user called, count = %d\n", count);
#endif

        ret = read(state->fd, buffer, count);

        if(ret < 0) return(-errno);
        return(ret);
}

ssize_t hostaudio_write_user(struct hostaudio_state *state, const char *buffer,
			     size_t count, loff_t *ppos)
{
	ssize_t ret;

#ifdef DEBUG
        printk("hostaudio: write_user called, count = %d\n", count);
#endif

        ret = write(state->fd, buffer, count);

        if(ret < 0) return(-errno);
        return(ret);
}

int hostaudio_ioctl_user(struct hostaudio_state *state, unsigned int cmd, 
			 unsigned long arg)
{
	int ret;
#ifdef DEBUG
        printk("hostaudio: ioctl_user called, cmd = %u\n", cmd);
#endif

        ret = ioctl(state->fd, cmd, arg);
	
        if(ret < 0) return(-errno);
        return(ret);
}

int hostaudio_open_user(struct hostaudio_state *state, int r, int w, char *dsp)
{
#ifdef DEBUG
        printk("hostaudio: open_user called\n");
#endif

        state->fd = os_open_file(dsp, of_set_rw(OPENFLAGS(), r, w), 0);

        if(state->fd >= 0) return(0);

        printk("hostaudio_open_user failed to open '%s', errno = %d\n",
	       dsp, errno);
        
        return(-errno); 
}

int hostaudio_release_user(struct hostaudio_state *state)
{
#ifdef DEBUG
        printk("hostaudio: release called\n");
#endif
        if(state->fd >= 0){
		close(state->fd);
		state->fd=-1;
        }

        return(0);
}

/* /dev/mixer file operations */

int hostmixer_ioctl_mixdev_user(struct hostmixer_state *state, 
				unsigned int cmd, unsigned long arg)
{
	int ret;
#ifdef DEBUG
        printk("hostmixer: ioctl_user called cmd = %u\n",cmd);
#endif

        ret = ioctl(state->fd, cmd, arg);
	if(ret < 0) 
		return(-errno);
	return(ret);
}

int hostmixer_open_mixdev_user(struct hostmixer_state *state, int r, int w,
			       char *mixer)
{
#ifdef DEBUG
        printk("hostmixer: open_user called\n");
#endif

        state->fd = os_open_file(mixer, of_set_rw(OPENFLAGS(), r, w), 0);

        if(state->fd >= 0) return(0);

        printk("hostaudio_open_mixdev_user failed to open '%s', errno = %d\n",
	       mixer, errno);
        
        return(-errno); 
}

int hostmixer_release_mixdev_user(struct hostmixer_state *state)
{
#ifdef DEBUG
        printk("hostmixer: release_user called\n");
#endif

        if(state->fd >= 0){
		close(state->fd);
		state->fd = -1;
        }

        return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
