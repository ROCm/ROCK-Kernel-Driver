/*
 * SBus helper functions
 *
 * Sun3x don't have a sbus, but many of the used devices are also
 * used on Sparc machines with sbus. To avoid having a lot of
 * duplicate code, we provide necessary glue stuff to make using
 * of the sbus driver code possible.
 *
 * (C) 1999 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 */

#include <linux/types.h>
#include <linux/init.h>

void __init sbus_init(void)
{

}

void *sparc_alloc_io (u32 address, void *virtual, int len, char *name,
                      u32 bus_type, int rdonly)
{
	return (void *)address;
}

int prom_getintdefault(int node, char *property, int deflt)
{
	return deflt;
}

int prom_getbool (int node, char *prop)
{
	return 1;
}

void prom_printf(char *fmt, ...)
{

}

void prom_halt (void)
{

}
