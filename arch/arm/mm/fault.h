void do_bad_area(struct task_struct *tsk, struct mm_struct *mm,
		 unsigned long addr, unsigned int fsr, struct pt_regs *regs);

void show_pte(struct mm_struct *mm, unsigned long addr);

int do_page_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs);

int do_translation_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs);

unsigned long search_exception_table(unsigned long addr);
