#ifndef _ASM_UML_UNWIND_H
#define _ASM_UML_UNWIND_H

static inline void
unwind_module_init(struct module *mod, void *undwarf_ip, size_t unward_ip_size,
		   void *undwarf, size_t undwarf_size) {}

#endif /* _ASM_UML_UNWIND_H */
