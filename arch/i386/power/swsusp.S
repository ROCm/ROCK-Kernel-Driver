.text

/* Originally gcc generated, modified by hand
 *
 * This may not use any stack, nor any variable that is not "NoSave":
 *
 * Its rewriting one kernel image with another. What is stack in "old"
 * image could very well be data page in "new" image, and overwriting
 * your own stack under you is bad idea.
 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>

	.text

ENTRY(do_magic)
	pushl %ebx
	cmpl $0,8(%esp)
	jne resume
	call do_magic_suspend_1
	call save_processor_state

	movl %esp, saved_context_esp
	movl %eax, saved_context_eax
	movl %ebx, saved_context_ebx
	movl %ecx, saved_context_ecx
	movl %edx, saved_context_edx
	movl %ebp, saved_context_ebp
	movl %esi, saved_context_esi
	movl %edi, saved_context_edi
	pushfl ; popl saved_context_eflags

	call do_magic_suspend_2
	popl %ebx
	ret

resume:
	movl $swsusp_pg_dir-__PAGE_OFFSET,%ecx
	movl %ecx,%cr3

	call do_magic_resume_1
	movl $0,loop
	cmpl $0,nr_copy_pages
	je copy_done
copy_loop:
	movl $0,loop2
	.p2align 4,,7
copy_one_page:
	movl pagedir_nosave,%ecx
	movl loop,%eax
	movl loop2,%edx
	sall $4,%eax
	movl 4(%ecx,%eax),%ebx
	movl (%ecx,%eax),%eax
	movb (%edx,%eax),%al
	movb %al,(%edx,%ebx)

	movl loop2,%eax
	leal 1(%eax),%edx
	movl %edx,loop2
	movl %edx,%eax
	cmpl $4095,%eax
	jbe copy_one_page
	movl loop,%eax
	leal 1(%eax),%edx
	movl %edx,loop
	movl %edx,%eax
	cmpl nr_copy_pages,%eax
	jb copy_loop

copy_done:
	movl $__USER_DS,%eax

	movw %ax, %ds
	movw %ax, %es
	movl saved_context_esp, %esp
	movl saved_context_ebp, %ebp
	movl saved_context_eax, %eax
	movl saved_context_ebx, %ebx
	movl saved_context_ecx, %ecx
	movl saved_context_edx, %edx
	movl saved_context_esi, %esi
	movl saved_context_edi, %edi
	call restore_processor_state
	pushl saved_context_eflags ; popfl
	call do_magic_resume_2
	popl %ebx
	ret

       .section .data.nosave
loop:
       .quad 0
loop2:
       .quad 0
       .previous
