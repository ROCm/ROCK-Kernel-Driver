/*
 * misc.c: Miscellaneous ARCS PROM routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/bcache.h>
#include <asm/sgialib.h>
#include <asm/bootinfo.h>
#include <asm/system.h>

extern void *sgiwd93_host;
extern void reset_wd33c93(void *instance);

void prom_halt(void)
{
	bc_disable();
	cli();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	romvec->halt();
}

void prom_powerdown(void)
{
	bc_disable();
	cli();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	romvec->pdown();
}

/* XXX is this a soft reset basically? XXX */
void prom_restart(void)
{
	bc_disable();
	cli();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	romvec->restart();
}

void prom_reboot(void)
{
	bc_disable();
	cli();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	romvec->reboot();
}

void ArcEnterInteractiveMode(void)
{
	bc_disable();
	cli();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	romvec->imode();
}

long prom_cfgsave(void)
{
	return romvec->cfg_save();
}

struct linux_sysid *prom_getsysid(void)
{
	return romvec->get_sysid();
}

void __init prom_cacheflush(void)
{
	romvec->cache_flush();
}
