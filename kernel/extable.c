/* Rewritten by Rusty Russell, on the backs of many others...
   Copyright (C) 2001 Rusty Russell, 2002 Rusty Russell IBM.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/module.h>
#include <linux/init.h>

#include <asm/semaphore.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];
extern const struct kernel_symbol __start___gpl_ksymtab[];
extern const struct kernel_symbol __stop___gpl_ksymtab[];

/* Protects extables and symbol tables */
spinlock_t modlist_lock = SPIN_LOCK_UNLOCKED;

/* The exception and symbol tables: start with kernel only. */
LIST_HEAD(extables);
LIST_HEAD(symbols);

static struct exception_table kernel_extable;
static struct kernel_symbol_group kernel_symbols;
static struct kernel_symbol_group kernel_gpl_symbols;

void __init extable_init(void)
{
	/* Add kernel symbols to symbol table */
	kernel_symbols.num_syms = (__stop___ksymtab - __start___ksymtab);
	kernel_symbols.syms = __start___ksymtab;
	kernel_symbols.gplonly = 0;
	list_add(&kernel_symbols.list, &symbols);
	kernel_gpl_symbols.num_syms = (__stop___gpl_ksymtab
				       - __start___gpl_ksymtab);
	kernel_gpl_symbols.syms = __start___gpl_ksymtab;
	kernel_gpl_symbols.gplonly = 1;
	list_add(&kernel_gpl_symbols.list, &symbols);

	/* Add kernel exception table to exception tables */
	kernel_extable.num_entries = (__stop___ex_table -__start___ex_table);
	kernel_extable.entry = __start___ex_table;
	list_add(&kernel_extable.list, &extables);
}


