#include <linux/config.h>

#ifdef CONFIG_H8300H_GENERIC
#ifdef CONFIG_ROMKERNEL
#include "platform/h8300h/generic/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "platform/h8300h/generic/ram.ld"
#endif
#endif

#ifdef CONFIG_H8300H_AKI3068NET
#ifdef CONFIG_ROMKERNEL
#include "platform/h8300h/aki3068net/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "platform/h8300h/aki3068net/ram.ld"
#endif
#endif

#ifdef CONFIG_H8300H_H8MAX
#ifdef CONFIG_ROMKERNEL
#include "platform/h8300h/h8max/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "platform/h8300h/h8max/ram.ld"
#endif
#endif

#ifdef CONFIG_H8300H_SIM
#ifdef CONFIG_ROMKERNEL
#include "platform/h8300h/generic/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "platform/h8300h/generic/ram.ld"
#endif
#endif

