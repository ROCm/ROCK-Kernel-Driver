/*
 * srm_env.c - Access to SRC environment variables through
 *             the linux procfs
 *
 * (C)2001, Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *
 * This driver is at all a modified version of Erik Mouw's
 * ./linux/Documentation/DocBook/procfs_example.c, so: thanky
 * you, Erik! He can be reached via email at
 * <J.A.K.Mouw@its.tudelft.nl>. It is based on an idea
 * provided by DEC^WCompaq's "Jumpstart" CD. They included
 * a patch like this as well. Thanks for idea!
 *
 *
 * This software has been developed while working on the LART
 * computing board (http://www.lart.tudelft.nl/). The
 * development has been sponsored by the Mobile Multi-media
 * Communications (http://www.mmc.tudelft.nl/) and Ubiquitous
 * Communications (http://www.ubicom.tudelft.nl/) projects.
 *
 * This program is free software; you can redistribute
 * it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation version 2.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place,
 * Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <asm/console.h>
#include <asm/uaccess.h>

#define DIRNAME		"srm_environment"	/* Subdir in /proc/	*/
#define VERSION		"0.0.2"			/* Module version	*/
#define NAME		"srm_env"		/* Module name		*/
#define DEBUG

MODULE_AUTHOR("Jan-Benedict Glaw <jbglaw@lug-owl.de>");
MODULE_DESCRIPTION("Accessing Alpha SRM environment through procfs interface");
MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;

typedef struct _srm_env {
	char			*name;
	unsigned long		id;
	struct proc_dir_entry	*proc_entry;
} srm_env_t;

static struct proc_dir_entry	*directory;
static srm_env_t	srm_entries[] = {
	{ "auto_action",	ENV_AUTO_ACTION		},
	{ "boot_dev",		ENV_BOOT_DEV		},
	{ "bootdef_dev",	ENV_BOOTDEF_DEV		},
	{ "booted_dev",		ENV_BOOTED_DEV		},
	{ "boot_file",		ENV_BOOT_FILE		},
	{ "booted_file",	ENV_BOOTED_FILE		},
	{ "boot_osflags",	ENV_BOOT_OSFLAGS	},
	{ "booted_osflags",	ENV_BOOTED_OSFLAGS	},
	{ "boot_reset",		ENV_BOOT_RESET		},
	{ "dump_dev",		ENV_DUMP_DEV		},
	{ "enable_audit",	ENV_ENABLE_AUDIT	},
	{ "license",		ENV_LICENSE		},
	{ "char_set",		ENV_CHAR_SET		},
	{ "language",		ENV_LANGUAGE		},
	{ "tty_dev",		ENV_TTY_DEV		},
	{ NULL,			0			},
};

static int srm_env_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	int		nbytes;
	unsigned long	ret;
	srm_env_t	*entry;

	MOD_INC_USE_COUNT;

	if(off != 0) {
		MOD_DEC_USE_COUNT;
		return -EFAULT;
	}

	entry	= (srm_env_t *)data;
	ret	= callback_getenv(entry->id, page, count);

	if((ret >> 61) == 0)
		nbytes = (int)ret;
	else
		nbytes = -EFAULT;

	MOD_DEC_USE_COUNT;

	return nbytes;
}


static int srm_env_write(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
#define BUFLEN	512
	int		nbytes;
	srm_env_t	*entry;
	char		buf[BUFLEN];
	unsigned long	ret1, ret2;

	MOD_INC_USE_COUNT;

	entry = (srm_env_t *) data;

	nbytes = strlen(buffer) + 1;
	if(nbytes > BUFLEN) {
		MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
		
	//memcpy(aligned_buffer, buffer, nbytes)

	if(copy_from_user(buf, buffer, count)) {
		MOD_DEC_USE_COUNT;
		return -EFAULT;
	}
	buf[count] = 0x00;

	ret1 = callback_setenv(entry->id, buf, count);
	if((ret1 >> 61) == 0) {
		do 
			ret2 = callback_save_env();
		while((ret2 >> 61) == 1);
		nbytes = (int)ret1;
	} else
		nbytes = -EFAULT;

	MOD_DEC_USE_COUNT;

	return nbytes;
}

static void srm_env_cleanup(void)
{
	srm_env_t	*entry;

	if(directory) {
		entry = srm_entries;
		while(entry->name != NULL && entry->id != 0) {
			if(entry->proc_entry) {
				remove_proc_entry(entry->name, directory);
				entry->proc_entry = NULL;
			}
			entry++;
		}
		remove_proc_entry(DIRNAME, NULL);
	}

	return;
}

static int __init srm_env_init(void)
{
	srm_env_t	*entry;
	
	if(!alpha_using_srm) {
		printk(KERN_INFO "%s: This Alpha system doesn't "
				"know about SRM...\n", __FUNCTION__);
		return -ENODEV;
	}

	directory = proc_mkdir(DIRNAME, NULL);
	if(directory == NULL)
		return -ENOMEM;
	
	directory->owner = THIS_MODULE;
	
	/* Now create all the nodes... */
	entry = srm_entries;
	while(entry->name != NULL && entry->id != 0) {
		entry->proc_entry = create_proc_entry(entry->name, 0644,
				directory);
		if(entry->proc_entry == NULL)
			goto cleanup;
		entry->proc_entry->data		= entry;
		entry->proc_entry->read_proc	= srm_env_read;
		entry->proc_entry->write_proc	= srm_env_write;
		entry->proc_entry->owner	= THIS_MODULE;
		entry++;
	}
	
	printk(KERN_INFO "%s: version %s loaded successfully\n", NAME,
			VERSION);
	return 0;

cleanup:
	srm_env_cleanup();
	return -ENOMEM;
}


static void __exit srm_env_exit(void)
{
	srm_env_cleanup();
	printk(KERN_INFO "%s: unloaded successfully\n", NAME);
	return;
}

module_init(srm_env_init);
module_exit(srm_env_exit);

