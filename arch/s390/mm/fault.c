/*
 *  arch/s390/mm/fault.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1995  Linus Torvalds
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/hardirq.h>

extern void die(const char *,struct pt_regs *,long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * error_code:
 *             ****0004       Protection           ->  Write-Protection  (suprression)
 *             ****0010       Segment translation  ->  Not present       (nullification)
 *             ****0011       Page translation     ->  Not present       (nullification)
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
        struct task_struct *tsk;
        struct mm_struct *mm;
        struct vm_area_struct * vma;
        unsigned long address;
        unsigned long fixup;
        int write;
        unsigned long psw_mask;
        unsigned long psw_addr;

        /*
         *  get psw mask of Program old psw to find out,
         *  if user or kernel mode
         */

        psw_mask = S390_lowcore.program_old_psw.mask;
        psw_addr = S390_lowcore.program_old_psw.addr;

        /* 
         * get the failing address 
         * more specific the segment and page table portion of 
         * the address 
         */

        address = S390_lowcore.trans_exc_code&0x7ffff000;

        if (in_irq())
                die("page fault from irq handler",regs,error_code);

        tsk = current;
        mm = tsk->mm;

        down(&mm->mmap_sem);

        vma = find_vma(mm, address);
        if (!vma) {
	        printk("no vma for address %lX\n",address);
                goto bad_area;
        }
        if (vma->vm_start <= address) 
                goto good_area;
        if (!(vma->vm_flags & VM_GROWSDOWN)) {
                printk("VM_GROWSDOWN not set, but address %lX \n",address);
                printk("not in vma %p (start %lX end %lX)\n",vma,
                       vma->vm_start,vma->vm_end);
                goto bad_area;
        }
        if (expand_stack(vma, address)) {
                printk("expand of vma failed address %lX\n",address);
                printk("vma %p (start %lX end %lX)\n",vma,
                       vma->vm_start,vma->vm_end);
                goto bad_area;
        }
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
        write = 0;
        switch (error_code & 0xFF) {
                case 0x04:                                /* write, present*/
                        write = 1;
                        break;
                case 0x10:                                   /* not present*/
                case 0x11:                                   /* not present*/
                        if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE))) {
                                printk("flags %X of vma for address %lX wrong \n",
                                       vma->vm_flags,address);
                                printk("vma %p (start %lX end %lX)\n",vma,
                                       vma->vm_start,vma->vm_end);
                                goto bad_area;
                        }
                        break;
                default:
                       printk("code should be 4, 10 or 11 (%lX) \n",error_code&0xFF);  
                       goto bad_area;
        }
        handle_mm_fault(tsk, vma, address, write);

        up(&mm->mmap_sem);
        return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
        up(&mm->mmap_sem);

        /* User mode accesses just cause a SIGSEGV */
        if (psw_mask & PSW_PROBLEM_STATE) {
                tsk->thread.prot_addr = address;
                tsk->thread.error_code = error_code;
                tsk->thread.trap_no = 14;

                printk("User process fault: interruption code 0x%lX\n",error_code);
                printk("failing address: %lX\n",address);
		show_crashed_task_info();
                force_sig(SIGSEGV, tsk);
                return;
	}

        /* Are we prepared to handle this kernel fault?  */

        if ((fixup = search_exception_table(regs->psw.addr)) != 0) {
                regs->psw.addr = fixup;
                return;
        }

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 *
 * First we check if it was the bootup rw-test, though..
 */
        if (address < PAGE_SIZE)
                printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
        else
                printk(KERN_ALERT "Unable to handle kernel paging request");
        printk(" at virtual address %08lx\n",address);
/*
 * need to define, which information is useful here
 */

        lock_kernel();
        die("Oops", regs, error_code);
        do_exit(SIGKILL);
        unlock_kernel();
}

/*
                {
		  char c;
                  int i,j;
		  char *addr;
		  addr = ((char*) psw_addr)-0x20;
		  for (i=0;i<16;i++) {
		    if (i == 2)
		      printk("\n");
		    printk ("%08X:    ",(unsigned long) addr);
		    for (j=0;j<4;j++) {
		      printk("%08X ",*(unsigned long*)addr);
		      addr += 4;
		    }
		    addr -=0x10;
		    printk(" | ");
		    for (j=0;j<16;j++) {
		      printk("%c",(c=*addr++) < 0x20 ? '.' : c );
		    }

		    printk("\n");
		  }
                  printk("\n");
                }

*/






