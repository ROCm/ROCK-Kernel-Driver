#ifndef S390_IOINFO_H
#define S390_IOINFO_H

extern unsigned int highest_subchannel;

extern ioinfo_t *ioinfo_head;
extern ioinfo_t *ioinfo_tail;
extern ioinfo_t *ioinfo[__MAX_SUBCHANNELS];

#endif
