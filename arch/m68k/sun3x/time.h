#ifndef SUN3X_TIME_H
#define SUN3X_TIME_H

void sun3x_gettod (int *yearp, int *monp, int *dayp,
                   int *hourp, int *minp, int *secp);
unsigned long sun3x_gettimeoffset (void);
void sun3x_sched_init(void (*vector)(int, void *, struct pt_regs *));

#endif
