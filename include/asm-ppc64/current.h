#ifndef _PPC64_CURRENT_H
#define _PPC64_CURRENT_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Use r13 for current since the ppc64 ABI reserves it - Anton
 */

register struct task_struct *current asm ("r13");

#endif /* !(_PPC64_CURRENT_H) */
