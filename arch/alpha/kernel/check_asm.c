#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <asm/io.h>

#define OUT(x) \
  asm ("\nxyzzy " x)
#define DEF(name, val) \
  asm volatile ("\nxyzzy #define " name " %0" : : "i"(val))

void foo(void)
{
	OUT("#ifndef __ASM_OFFSETS_H__");
	OUT("#define __ASM_OFFSETS_H__");
	OUT("");

	DEF("TI_TASK", offsetof(struct thread_info, task));
	DEF("TI_FLAGS", offsetof(struct thread_info, flags));
	DEF("TI_CPU", offsetof(struct thread_info, cpu));

	DEF("PT_PTRACED", PT_PTRACED);
	DEF("CLONE_VM", CLONE_VM);
	DEF("SIGCHLD", SIGCHLD);

	DEF("HAE_CACHE", offsetof(struct alpha_machine_vector, hae_cache));
	DEF("HAE_REG", offsetof(struct alpha_machine_vector, hae_register));

	OUT("");
	OUT("#endif /* __ASM_OFFSETS_H__ */");
}
