#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <asm/uaccess.h>

/* What kind of fucking hack is this? */
#define MTRR_NEED_STRINGS

#include <asm/mtrr.h>
#include "mtrr.h"

static char *ascii_buffer;
static unsigned int ascii_buf_bytes;

extern unsigned int *usage_table;

#define LINE_SIZE      80

static int
mtrr_file_add(unsigned long base, unsigned long size,
	      unsigned int type, char increment, struct file *file, int page)
{
	int reg, max;
	unsigned int *fcount = file->private_data;

	max = num_var_ranges;
	if (fcount == NULL) {
		if ((fcount =
		     kmalloc(max * sizeof *fcount, GFP_KERNEL)) == NULL) {
			printk("mtrr: could not allocate\n");
			return -ENOMEM;
		}
		memset(fcount, 0, max * sizeof *fcount);
		file->private_data = fcount;
	}
	if (!page) {
		if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
			printk
			    ("mtrr: size and base must be multiples of 4 kiB\n");
			printk("mtrr: size: 0x%lx  base: 0x%lx\n", size, base);
			return -EINVAL;
		}
		base >>= PAGE_SHIFT;
		size >>= PAGE_SHIFT;
	}
	reg = mtrr_add_page(base, size, type, 1);
	if (reg >= 0)
		++fcount[reg];
	return reg;
}

static int
mtrr_file_del(unsigned long base, unsigned long size,
	      struct file *file, int page)
{
	int reg;
	unsigned int *fcount = file->private_data;

	if (!page) {
		if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
			printk
			    ("mtrr: size and base must be multiples of 4 kiB\n");
			printk("mtrr: size: 0x%lx  base: 0x%lx\n", size, base);
			return -EINVAL;
		}
		base >>= PAGE_SHIFT;
		size >>= PAGE_SHIFT;
	}
	reg = mtrr_del_page(-1, base, size);
	if (reg < 0)
		return reg;
	if (fcount == NULL)
		return reg;
	if (fcount[reg] < 1)
		return -EINVAL;
	--fcount[reg];
	return reg;
}

static ssize_t
mtrr_read(struct file *file, char *buf, size_t len, loff_t * ppos)
{
	if (*ppos >= ascii_buf_bytes)
		return 0;
	if (*ppos + len > ascii_buf_bytes)
		len = ascii_buf_bytes - *ppos;
	if (copy_to_user(buf, ascii_buffer + *ppos, len))
		return -EFAULT;
	*ppos += len;
	return len;
}

static ssize_t
mtrr_write(struct file *file, const char *buf, size_t len, loff_t * ppos)
/*  Format of control line:
    "base=%Lx size=%Lx type=%s"     OR:
    "disable=%d"
*/
{
	int i, err;
	unsigned long reg;
	unsigned long long base, size;
	char *ptr;
	char line[LINE_SIZE];

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;
	memset(line, 0, LINE_SIZE);
	if (len > LINE_SIZE)
		len = LINE_SIZE;
	if (copy_from_user(line, buf, len - 1))
		return -EFAULT;
	ptr = line + strlen(line) - 1;
	if (*ptr == '\n')
		*ptr = '\0';
	if (!strncmp(line, "disable=", 8)) {
		reg = simple_strtoul(line + 8, &ptr, 0);
		err = mtrr_del_page(reg, 0, 0);
		if (err < 0)
			return err;
		return len;
	}
	if (strncmp(line, "base=", 5)) {
		printk("mtrr: no \"base=\" in line: \"%s\"\n", line);
		return -EINVAL;
	}
	base = simple_strtoull(line + 5, &ptr, 0);
	for (; isspace(*ptr); ++ptr) ;
	if (strncmp(ptr, "size=", 5)) {
		printk("mtrr: no \"size=\" in line: \"%s\"\n", line);
		return -EINVAL;
	}
	size = simple_strtoull(ptr + 5, &ptr, 0);
	if ((base & 0xfff) || (size & 0xfff)) {
		printk("mtrr: size and base must be multiples of 4 kiB\n");
		printk("mtrr: size: 0x%Lx  base: 0x%Lx\n", size, base);
		return -EINVAL;
	}
	for (; isspace(*ptr); ++ptr) ;
	if (strncmp(ptr, "type=", 5)) {
		printk("mtrr: no \"type=\" in line: \"%s\"\n", line);
		return -EINVAL;
	}
	ptr += 5;
	for (; isspace(*ptr); ++ptr) ;
	for (i = 0; i < MTRR_NUM_TYPES; ++i) {
//		if (strcmp(ptr, mtrr_strings[i]))
			continue;
		base >>= PAGE_SHIFT;
		size >>= PAGE_SHIFT;
		err =
		    mtrr_add_page((unsigned long) base, (unsigned long) size, i,
				  1);
		if (err < 0)
			return err;
		return len;
	}
	printk("mtrr: illegal type: \"%s\"\n", ptr);
	return -EINVAL;
}

static int
mtrr_ioctl(struct inode *inode, struct file *file,
	   unsigned int cmd, unsigned long arg)
{
	int err;
	mtrr_type type;
	struct mtrr_sentry sentry;
	struct mtrr_gentry gentry;

	switch (cmd) {
	default:
		return -ENOIOCTLCMD;
	case MTRRIOC_ADD_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, (void *) arg, sizeof sentry))
			return -EFAULT;
		err =
		    mtrr_file_add(sentry.base, sentry.size, sentry.type, 1,
				  file, 0);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_SET_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, (void *) arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_add(sentry.base, sentry.size, sentry.type, 0);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_DEL_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, (void *) arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_file_del(sentry.base, sentry.size, file, 0);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_KILL_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, (void *) arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_del(-1, sentry.base, sentry.size);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_GET_ENTRY:
		if (copy_from_user(&gentry, (void *) arg, sizeof gentry))
			return -EFAULT;
		if (gentry.regnum >= num_var_ranges)
			return -EINVAL;
		mtrr_if->get(gentry.regnum, &gentry.base, &gentry.size, &type);

		/* Hide entries that go above 4GB */
		if (gentry.base + gentry.size > 0x100000
		    || gentry.size == 0x100000)
			gentry.base = gentry.size = gentry.type = 0;
		else {
			gentry.base <<= PAGE_SHIFT;
			gentry.size <<= PAGE_SHIFT;
			gentry.type = type;
		}

		if (copy_to_user((void *) arg, &gentry, sizeof gentry))
			return -EFAULT;
		break;
	case MTRRIOC_ADD_PAGE_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, (void *) arg, sizeof sentry))
			return -EFAULT;
		err =
		    mtrr_file_add(sentry.base, sentry.size, sentry.type, 1,
				  file, 1);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_SET_PAGE_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, (void *) arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_add_page(sentry.base, sentry.size, sentry.type, 0);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_DEL_PAGE_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, (void *) arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_file_del(sentry.base, sentry.size, file, 1);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_KILL_PAGE_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, (void *) arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_del_page(-1, sentry.base, sentry.size);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_GET_PAGE_ENTRY:
		if (copy_from_user(&gentry, (void *) arg, sizeof gentry))
			return -EFAULT;
		if (gentry.regnum >= num_var_ranges)
			return -EINVAL;
		mtrr_if->get(gentry.regnum, &gentry.base, &gentry.size, &type);
		gentry.type = type;

		if (copy_to_user((void *) arg, &gentry, sizeof gentry))
			return -EFAULT;
		break;
	}
	return 0;
}

static int
mtrr_close(struct inode *ino, struct file *file)
{
	int i, max;
	unsigned int *fcount = file->private_data;

	if (fcount == NULL)
		return 0;
	max = num_var_ranges;
	for (i = 0; i < max; ++i) {
		while (fcount[i] > 0) {
			if (mtrr_del(i, 0, 0) < 0)
				printk("mtrr: reg %d not used\n", i);
			--fcount[i];
		}
	}
	kfree(fcount);
	file->private_data = NULL;
	return 0;
}

static struct file_operations mtrr_fops = {
	.owner   = THIS_MODULE,
	.read    = mtrr_read,
	.write   = mtrr_write,
	.ioctl   = mtrr_ioctl,
	.release = mtrr_close,
};

#  ifdef CONFIG_PROC_FS

static struct proc_dir_entry *proc_root_mtrr;

#  endif			/*  CONFIG_PROC_FS  */

static devfs_handle_t devfs_handle;

char * attrib_to_str(int x)
{
	return (x <= 6) ? mtrr_strings[x] : "?";
}

void compute_ascii(void)
{
	char factor;
	int i, max;
	mtrr_type type;
	unsigned long base, size;

	ascii_buf_bytes = 0;
	max = num_var_ranges;
	for (i = 0; i < max; i++) {
		mtrr_if->get(i, &base, &size, &type);
		if (size == 0)
			usage_table[i] = 0;
		else {
			if (size < (0x100000 >> PAGE_SHIFT)) {
				/* less than 1MB */
				factor = 'K';
				size <<= PAGE_SHIFT - 10;
			} else {
				factor = 'M';
				size >>= 20 - PAGE_SHIFT;
			}
			sprintf
			    (ascii_buffer + ascii_buf_bytes,
			     "reg%02i: base=0x%05lx000 (%4liMB), size=%4li%cB: %s, count=%d\n",
			     i, base, base >> (20 - PAGE_SHIFT), size, factor,
			     attrib_to_str(type), usage_table[i]);
			ascii_buf_bytes +=
			    strlen(ascii_buffer + ascii_buf_bytes);
		}
	}
	devfs_set_file_size(devfs_handle, ascii_buf_bytes);
#  ifdef CONFIG_PROC_FS
	if (proc_root_mtrr)
		proc_root_mtrr->size = ascii_buf_bytes;
#  endif			/*  CONFIG_PROC_FS  */
}

static int __init mtrr_if_init(void)
{
	int max = num_var_ranges;

	if ((ascii_buffer = kmalloc(max * LINE_SIZE, GFP_KERNEL)) == NULL) {
		printk("mtrr: could not allocate\n");
		return -ENOMEM;
	}
	ascii_buf_bytes = 0;
	compute_ascii();
#ifdef CONFIG_PROC_FS
	proc_root_mtrr =
	    create_proc_entry("mtrr", S_IWUSR | S_IRUGO, &proc_root);
	if (proc_root_mtrr) {
		proc_root_mtrr->owner = THIS_MODULE;
		proc_root_mtrr->proc_fops = &mtrr_fops;
	}
#endif
#ifdef USERSPACE_INTERFACE
	devfs_handle = devfs_register(NULL, "cpu/mtrr", DEVFS_FL_DEFAULT, 0, 0,
				      S_IFREG | S_IRUGO | S_IWUSR,
				      &mtrr_fops, NULL);
#endif
	return 0;
}

arch_initcall(mtrr_if_init);
