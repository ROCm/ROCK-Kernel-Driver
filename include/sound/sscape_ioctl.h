#ifndef SSCAPE_IOCTL_H
#define SSCAPE_IOCTL_H


struct sscape_bootblock
{
  unsigned char code[256];
  unsigned version;
};

struct sscape_microcode
{
  unsigned char *code;	/* 65536 chars */
};

#define SND_SSCAPE_LOAD_BOOTB  _IOWR('P', 100, struct sscape_bootblock)
#define SND_SSCAPE_LOAD_MCODE  _IOW ('P', 101, struct sscape_microcode)

#endif
