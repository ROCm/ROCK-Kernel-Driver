#include "linux/stddef.h"
#include "linux/sched.h"

int debugreg(void)
{
  return(offsetof(struct task_struct, thread.arch.debugregs));
}
