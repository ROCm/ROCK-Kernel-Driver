#ifndef SSCAPE_IOCTL_H
#define SSCAPE_IOCTL_H


struct sscape_bootblock
{
  unsigned char code[256];
  unsigned version;
};

struct sscape_microcode
{
  unsigned char code[65536];
};

#define SND_SSCAPE_LOAD_BOOTB  _IOWR('P', 100, struct sscape_bootblock)

/* This ioctl is marked bad because the type is bigger than the IOCTL description */
#define SND_SSCAPE_LOAD_MCODE  _IOW_BAD('P', 101, struct sscape_microcode)

#endif
