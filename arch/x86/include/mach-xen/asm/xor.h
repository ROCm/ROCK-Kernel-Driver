#ifdef CONFIG_KMEMCHECK
/* kmemcheck doesn't handle MMX/SSE/SSE2 instructions */
# include <asm-generic/xor.h>
#elif defined(CONFIG_X86_32)
# include "../../asm/xor_32.h"
#else
# include "xor_64.h"
#endif
