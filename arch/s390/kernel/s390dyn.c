/*
 *  arch/s390/kernel/s390dyn.c
 *   S/390 dynamic device attachment
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 */

#include <linux/init.h>

#include <asm/irq.h>
#include <asm/s390io.h>
#include <asm/s390dyn.h>

int s390_device_register( devreg_t *drinfo )
{
	return -EOPNOTSUPP;
}


int s390_device_deregister  ( devreg_t *dreg )
{
	return -EOPNOTSUPP;
}

int s390_request_irq_special( int                      irq,
                              io_handler_func_t        io_handler,
                              not_oper_handler_func_t  not_oper_handler,
                              unsigned long            irqflags,
                              const char              *devname,
                              void                    *dev_id)
{
	return -EOPNOTSUPP;
}

