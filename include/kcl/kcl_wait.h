#ifndef AMDKCL_WAIT_H
#define AMDKCL_WAIT_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0) && \
	!defined(OS_NAME_SUSE_15) && !defined(OS_NAME_SUSE_15_1)
#define wait_queue_entry_t wait_queue_t
#endif

#endif /* AMDKCL_WAIT_H */
