/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#include <linux/signal.h>
#include <asm/ucontext.h>
#include "sigframe.h"

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

void foo(void)
{
	DEFINE(SIGCONTEXT_eax, offsetof (struct sigcontext, eax));
	DEFINE(SIGCONTEXT_ebx, offsetof (struct sigcontext, ebx));
	DEFINE(SIGCONTEXT_ecx, offsetof (struct sigcontext, ecx));
	DEFINE(SIGCONTEXT_edx, offsetof (struct sigcontext, edx));
	DEFINE(SIGCONTEXT_esi, offsetof (struct sigcontext, esi));
	DEFINE(SIGCONTEXT_edi, offsetof (struct sigcontext, edi));
	DEFINE(SIGCONTEXT_ebp, offsetof (struct sigcontext, ebp));
	DEFINE(SIGCONTEXT_esp, offsetof (struct sigcontext, esp));
	DEFINE(SIGCONTEXT_eip, offsetof (struct sigcontext, eip));
	BLANK();

	DEFINE(RT_SIGFRAME_sigcontext,
	       offsetof (struct rt_sigframe, uc.uc_mcontext));
}
