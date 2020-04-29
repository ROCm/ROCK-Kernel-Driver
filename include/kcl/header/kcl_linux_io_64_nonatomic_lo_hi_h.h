#ifndef AMDKCL_IO_64_NONATOMIC_LO_HI_H_H
#define AMDKCL_IO_64_NONATOMIC_LO_HI_H_H

#ifdef HAVE_LINUX_IO_64_NONATOMIC_LO_HI_H
#include <linux/io-64-nonatomic-lo-hi.h>
#else
#include <asm-generic/io-64-nonatomic-lo-hi.h>
#endif
#endif
