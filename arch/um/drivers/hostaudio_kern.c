/* 
 * Copyright (C) 2002 Steve Schmidtke 
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/module.h"
#include "linux/version.h"
#include "linux/init.h"
#include "linux/slab.h"
#include "linux/fs.h"
#include "linux/sound.h"
#include "linux/soundcard.h"
#include "kern_util.h"
#include "init.h"
#include "hostaudio.h"

/* Only changed from linux_main at boot time */
char *dsp = HOSTAUDIO_DEV_DSP;
char *mixer = HOSTAUDIO_DEV_MIXER;

#ifndef MODULE
static int set_dsp(char *name, int *add)
{
	dsp = uml_strdup(name);
	return(0);
}

__uml_setup("dsp=", set_dsp,
"dsp=<dsp device>\n"
"    This is used to specify the host dsp device to the hostaudio driver.\n"
"    The default is \"" HOSTAUDIO_DEV_DSP "\".\n\n"
);

static int set_mixer(char *name, int *add)
{
	mixer = uml_strdup(name);
	return(0);
}

__uml_setup("mixer=", set_mixer,
"mixer=<mixer device>\n"
"    This is used to specify the host mixer device to the hostaudio driver.\n"
"    The default is \"" HOSTAUDIO_DEV_MIXER "\".\n\n"
);
#endif

/* /dev/dsp file operations */

static ssize_t hostaudio_read(struct file *file, char *buffer, size_t count, 
			      loff_t *ppos)
{
        struct hostaudio_state *state = file->private_data;

#ifdef DEBUG
        printk("hostaudio: read called, count = %d\n", count);
#endif

        return(hostaudio_read_user(state, buffer, count, ppos));
}

static ssize_t hostaudio_write(struct file *file, const char *buffer, 
			       size_t count, loff_t *ppos)
{
        struct hostaudio_state *state = file->private_data;

#ifdef DEBUG
        printk("hostaudio: write called, count = %d\n", count);
#endif
        return(hostaudio_write_user(state, buffer, count, ppos));
}

static unsigned int hostaudio_poll(struct file *file, 
				   struct poll_table_struct *wait)
{
        unsigned int mask = 0;

#ifdef DEBUG
        printk("hostaudio: poll called (unimplemented)\n");
#endif

        return(mask);
}

static int hostaudio_ioctl(struct inode *inode, struct file *file, 
			   unsigned int cmd, unsigned long arg)
{
        struct hostaudio_state *state = file->private_data;

#ifdef DEBUG
        printk("hostaudio: ioctl called, cmd = %u\n", cmd);
#endif

        return(hostaudio_ioctl_user(state, cmd, arg));
}

static int hostaudio_open(struct inode *inode, struct file *file)
{
        struct hostaudio_state *state;
        int r = 0, w = 0;
        int ret;

#ifdef DEBUG
        printk("hostaudio: open called (host: %s)\n", dsp);
#endif

        state = kmalloc(sizeof(struct hostaudio_state), GFP_KERNEL);
        if(state == NULL) return(-ENOMEM);

        if(file->f_mode & FMODE_READ) r = 1;
        if(file->f_mode & FMODE_WRITE) w = 1;

        ret = hostaudio_open_user(state, r, w, dsp);
        if(ret < 0){
		kfree(state);
		return(ret);
        }

        file->private_data = state;
        return(0);
}

static int hostaudio_release(struct inode *inode, struct file *file)
{
        struct hostaudio_state *state = file->private_data;
        int ret;

#ifdef DEBUG
        printk("hostaudio: release called\n");
#endif

        ret = hostaudio_release_user(state);
        kfree(state);

        return(ret);
}

/* /dev/mixer file operations */

static int hostmixer_ioctl_mixdev(struct inode *inode, struct file *file, 
				  unsigned int cmd, unsigned long arg)
{
        struct hostmixer_state *state = file->private_data;

#ifdef DEBUG
        printk("hostmixer: ioctl called\n");
#endif

        return(hostmixer_ioctl_mixdev_user(state, cmd, arg));
}

static int hostmixer_open_mixdev(struct inode *inode, struct file *file)
{
        struct hostmixer_state *state;
        int r = 0, w = 0;
        int ret;

#ifdef DEBUG
        printk("hostmixer: open called (host: %s)\n", mixer);
#endif

        state = kmalloc(sizeof(struct hostmixer_state), GFP_KERNEL);
        if(state == NULL) return(-ENOMEM);

        if(file->f_mode & FMODE_READ) r = 1;
        if(file->f_mode & FMODE_WRITE) w = 1;

        ret = hostmixer_open_mixdev_user(state, r, w, mixer);
        
        if(ret < 0){
		kfree(state);
		return(ret);
        }

        file->private_data = state;
        return(0);
}

static int hostmixer_release(struct inode *inode, struct file *file)
{
        struct hostmixer_state *state = file->private_data;
	int ret;

#ifdef DEBUG
        printk("hostmixer: release called\n");
#endif

        ret = hostmixer_release_mixdev_user(state);
        kfree(state);

        return(ret);
}


/* kernel module operations */

static struct file_operations hostaudio_fops = {
        .owner          = THIS_MODULE,
        .llseek         = no_llseek,
        .read           = hostaudio_read,
        .write          = hostaudio_write,
        .poll           = hostaudio_poll,
        .ioctl          = hostaudio_ioctl,
        .mmap           = NULL,
        .open           = hostaudio_open,
        .release        = hostaudio_release,
};

static struct file_operations hostmixer_fops = {
        .owner          = THIS_MODULE,
        .llseek         = no_llseek,
        .ioctl          = hostmixer_ioctl_mixdev,
        .open           = hostmixer_open_mixdev,
        .release        = hostmixer_release,
};

struct {
	int dev_audio;
	int dev_mixer;
} module_data;

MODULE_AUTHOR("Steve Schmidtke");
MODULE_DESCRIPTION("UML Audio Relay");
MODULE_LICENSE("GPL");

static int __init hostaudio_init_module(void)
{
        printk(KERN_INFO "UML Audio Relay\n");

	module_data.dev_audio = register_sound_dsp(&hostaudio_fops, -1);
        if(module_data.dev_audio < 0){
                printk(KERN_ERR "hostaudio: couldn't register DSP device!\n");
                return -ENODEV;
        }

	module_data.dev_mixer = register_sound_mixer(&hostmixer_fops, -1);
        if(module_data.dev_mixer < 0){
                printk(KERN_ERR "hostmixer: couldn't register mixer "
		       "device!\n");
                unregister_sound_dsp(module_data.dev_audio);
                return -ENODEV;
        }

        return 0;
}

static void __exit hostaudio_cleanup_module (void)
{
       unregister_sound_mixer(module_data.dev_mixer);
       unregister_sound_dsp(module_data.dev_audio);
}

module_init(hostaudio_init_module);
module_exit(hostaudio_cleanup_module);

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
