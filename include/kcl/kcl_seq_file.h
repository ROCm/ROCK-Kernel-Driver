#ifndef AMDKCL_SEQ_FILE_H
#define AMDKCL_SEQ_FILE_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
#ifndef DEFINE_SHOW_ATTRIBUTE
#define DEFINE_SHOW_ATTRIBUTE(__name)                   \
static int __name ## _open(struct inode *inode, struct file *file)  \
{                                   \
	return single_open(file, __name ## _show, inode->i_private);    \
}                                   \
									\
static const struct file_operations __name ## _fops = {         \
	.owner      = THIS_MODULE,                  \
	.open       = __name ## _open,              \
	.read       = seq_read,                 \
	.llseek     = seq_lseek,                    \
	.release    = single_release,               \
}
#endif
#endif

#endif
