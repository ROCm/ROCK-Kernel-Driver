#ifndef _SPARC64_CURRENT_H
#define _SPARC64_CURRENT_H

#include <asm/thread_info.h>

#define current		(current_thread_info()->task)

#endif /* !(_SPARC64_CURRENT_H) */
