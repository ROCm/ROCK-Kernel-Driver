#include <stdio.h>
#include <linux/stddef.h>
#include <asm/user.h>

extern int debugreg(void);

int main(int argc, char **argv)
{
  printf("#define TASK_DEBUGREGS(task) ((unsigned long *) "
	 "&(((char *) (task))[%d]))\n", debugreg());
  return(0);
}
