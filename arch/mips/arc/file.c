/*
 * file.c: ARCS firmware interface to files.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: file.c,v 1.1 1998/10/18 13:32:08 tsbogend Exp $
 */
#include <linux/init.h>
#include <asm/sgialib.h>

long __init prom_getvdirent(unsigned long fd, struct linux_vdirent *ent, unsigned long num, unsigned long *cnt)
{
	return romvec->get_vdirent(fd, ent, num, cnt);
}

long __init prom_open(char *name, enum linux_omode md, unsigned long *fd)
{
	return romvec->open(name, md, fd);
}

long __init prom_close(unsigned long fd)
{
	return romvec->close(fd);
}

long __init prom_read(unsigned long fd, void *buf, unsigned long num, unsigned long *cnt)
{
	return romvec->read(fd, buf, num, cnt);
}

long __init prom_getrstatus(unsigned long fd)
{
	return romvec->get_rstatus(fd);
}

long __init prom_write(unsigned long fd, void *buf, unsigned long num, unsigned long *cnt)
{
	return romvec->write(fd, buf, num, cnt);
}

long __init prom_seek(unsigned long fd, struct linux_bigint *off, enum linux_seekmode sm)
{
	return romvec->seek(fd, off, sm);
}

long __init prom_mount(char *name, enum linux_mountops op)
{
	return romvec->mount(name, op);
}

long __init prom_getfinfo(unsigned long fd, struct linux_finfo *buf)
{
	return romvec->get_finfo(fd, buf);
}

long __init prom_setfinfo(unsigned long fd, unsigned long flags, unsigned long msk)
{
	return romvec->set_finfo(fd, flags, msk);
}
