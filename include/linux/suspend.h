#ifndef _LINUX_SWSUSP_H
#define _LINUX_SWSUSP_H

#ifdef CONFIG_X86
#include <asm/suspend.h>
#endif
#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pm.h>

#ifdef CONFIG_PM
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
	suspend_pagedir_t *suspend_pagedir;
	unsigned int num_pbes;
	struct swap_location {
		char filename[SWAP_FILENAME_MAXLENGTH];
	} swap_location[MAX_SWAPFILES];
};

#define SUSPEND_PD_PAGES(x)     (((x)*sizeof(struct pbe))/PAGE_SIZE+1)
   
/* mm/vmscan.c */
extern int shrink_mem(void);

/* mm/page_alloc.c */
extern void drain_local_pages(void);

extern unsigned int nr_copy_pages __nosavedata;
extern suspend_pagedir_t *pagedir_nosave __nosavedata;
#endif /* CONFIG_PM */

#ifdef CONFIG_SOFTWARE_SUSPEND

extern unsigned char software_suspend_enabled;

extern void software_suspend(void);
#else	/* CONFIG_SOFTWARE_SUSPEND */
static inline void software_suspend(void)
{
	printk("Warning: fake suspend called\n");
}
#endif	/* CONFIG_SOFTWARE_SUSPEND */


#ifdef CONFIG_PM
extern void refrigerator(unsigned long);

#else
static inline void refrigerator(unsigned long flag)
{

}
#endif	/* CONFIG_PM */

#endif /* _LINUX_SWSUSP_H */
