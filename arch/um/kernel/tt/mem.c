/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/config.h"
#include "linux/mm.h"
#include "asm/uaccess.h"
#include "mem_user.h"
#include "kern_util.h"
#include "user_util.h"
#include "kern.h"
#include "tt.h"

void before_mem_tt(unsigned long brk_start)
{
	if(!jail || debug)
		remap_data(UML_ROUND_DOWN(&_stext), UML_ROUND_UP(&_etext), 1);
	remap_data(UML_ROUND_DOWN(&_sdata), UML_ROUND_UP(&_edata), 1);
	remap_data(UML_ROUND_DOWN(&__bss_start), UML_ROUND_UP(brk_start), 1);
}

#ifdef CONFIG_HOST_2G_2G
#define TOP 0x80000000
#else
#define TOP 0xc0000000
#endif

#define SIZE ((CONFIG_NEST_LEVEL + CONFIG_KERNEL_HALF_GIGS) * 0x20000000)
#define START (TOP - SIZE)

unsigned long set_task_sizes_tt(int arg, unsigned long *host_size_out, 
				unsigned long *task_size_out)
{
	/* Round up to the nearest 4M */
	*host_size_out = ROUND_4M((unsigned long) &arg);
	*task_size_out = START;
	return(START);
}

struct page *arch_validate_tt(struct page *page, int mask, int order)
{
        unsigned long addr, zero = 0;
        int i;

 again:
        if(page == NULL) return(page);
        if(PageHighMem(page)) return(page);

        addr = (unsigned long) page_address(page);
        for(i = 0; i < (1 << order); i++){
                current->thread.fault_addr = (void *) addr;
                if(__do_copy_to_user((void *) addr, &zero, 
                                     sizeof(zero),
                                     &current->thread.fault_addr,
                                     &current->thread.fault_catcher)){
                        if(!(mask & __GFP_WAIT)) return(NULL);
                        else break;
                }
                addr += PAGE_SIZE;
        }
        if(i == (1 << order)) return(page);
        page = alloc_pages(mask, order);
        goto again;
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
