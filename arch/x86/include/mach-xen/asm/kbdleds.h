#ifndef _ASM_X86_KBDLEDS_H
#define _ASM_X86_KBDLEDS_H

/*
 * Some laptops take the 789uiojklm,. keys as number pad when NumLock is on.
 * This seems a good reason to start with NumLock off. That's why on X86 we
 * ask the bios for the correct state.
 */

#ifdef CONFIG_XEN_PRIVILEGED_GUEST
int kbd_defleds(void);
#else
static inline int kbd_defleds(void) { return 0; }
#endif

#endif /* _ASM_X86_KBDLEDS_H */
