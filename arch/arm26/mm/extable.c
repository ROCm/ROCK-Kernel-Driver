/*
 *  linux/arch/arm/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
        const struct exception_table_entry *fixup;

        fixup = search_exception_tables(instruction_pointer(regs));
        if (fixup)
                regs->ARM_pc = fixup->fixup | PSR_I_BIT | MODE_SVC26;

        return fixup != NULL;
}

