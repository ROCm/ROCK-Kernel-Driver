#ifndef ASM_IA64_CYCLONE_H
#define ASM_IA64_CYCLONE_H

#ifdef	CONFIG_IA64_CYCLONE
extern int use_cyclone;
extern int __init cyclone_setup(char*);
#else	/* CONFIG_IA64_CYCLONE */
#define use_cyclone 0
static inline void cyclone_setup(char* s)
{
	printk(KERN_ERR "Cyclone Counter: System not configured"
					" w/ CONFIG_IA64_CYCLONE.\n");
}
#endif	/* CONFIG_IA64_CYCLONE */
#endif	/* !ASM_IA64_CYCLONE_H */
