#ifndef _LINUX_SWSUSP_H
#define _LINUX_SWSUSP_H

#include <asm/suspend.h>
#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/config.h>

extern unsigned char software_suspend_enabled;

#define NORESUME	 1
#define RESUME_SPECIFIED 2

#ifdef CONFIG_SOFTWARE_SUSPEND
/* page backup entry */
typedef struct pbe {
	unsigned long address;		/* address of the copy */
	unsigned long orig_address;	/* original address of page */
	swp_entry_t swap_address;	
	swp_entry_t dummy;		/* we need scratch space at 
					 * end of page (see link, diskpage)
					 */
} suspend_pagedir_t;

#define SWAP_FILENAME_MAXLENGTH	32

struct suspend_header {
	__u32 version_code;
	unsigned long num_physpages;
	char machine[8];
	char version[20];
	int num_cpus;
	int page_size;
	unsigned long suspend_pagedir;
	unsigned int num_pbes;
	struct swap_location {
		char filename[SWAP_FILENAME_MAXLENGTH];
	} swap_location[MAX_SWAPFILES];
};

#define SUSPEND_PD_PAGES(x)     (((x)*sizeof(struct pbe))/PAGE_SIZE+1)
   
extern struct tq_struct suspend_tq;

/* mm/vmscan.c */
extern int shrink_mem(void);

/* kernel/suspend.c */
extern void software_suspend(void);
extern void software_resume(void);
extern int resume_setup(char *str);

extern int register_suspend_notifier(struct notifier_block *);
extern int unregister_suspend_notifier(struct notifier_block *);
extern void refrigerator(unsigned long);

#else
#define software_suspend()		do { } while(0)
#define software_resume()		do { } while(0)
#define register_suspend_notifier(a)	do { } while(0)
#define unregister_suspend_notifier(a)	do { } while(0)
#define refrigerator(a)			do { BUG(); } while(0)
#endif

#endif /* _LINUX_SWSUSP_H */
