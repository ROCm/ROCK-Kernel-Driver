/*
 *  include/asm-s390/chandev.h
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 */

#include <asm/types.h>

typedef enum
{
	none=0,
	ctc=1,
	escon=2,
	lcs=4,
	osad=8,
	claw=16,
} chandev_type;

typedef struct chandev_model_info chandev_model_info;

struct chandev_model_info
{
	struct chandev_model_info *next;
	chandev_type chan_type;
	u16 cu_type;
	u8  cu_model;
	u8  max_port_no;
};

typedef struct chandev chandev;
struct chandev
{
	struct chandev *next;
	chandev_model_info *model_info;
	u16 devno;
	int irq;
};

typedef struct chandev_noauto_range chandev_noauto_range;
struct chandev_noauto_range
{
	struct chandev_noauto_range *next;
	u16     lo_devno;
	u16     hi_devno;
};

typedef struct chandev_force chandev_force;
struct chandev_force
{
	struct chandev_force *next;
	chandev_type chan_type;
	s32     devif_num; /* -1 don't care e.g. tr0 implies 0 */
        u16     read_devno;
	u16     write_devno;
        s16     port_no; /* where available e.g. lcs,-1 don't care */
	u8      do_ip_checksumming;
	u8      use_hw_stats; /* where available e.g. lcs */
};



typedef struct
{
	s32     devif_num; /* -1 don't care e.g. tr0 implies 0 */
        int     read_irq;
	int     write_irq;
        s16     forced_port_no; /* -1 don't care */
	u8      hint_port_no;
	u8      max_port_no;
	u8      do_ip_checksumming;
	u8      use_hw_stats; /* where available e.g. lcs */
} chandev_probeinfo;


typedef int (*chandev_probefunc)(chandev_probeinfo *probeinfo);


typedef struct chandev_probelist chandev_probelist;
struct chandev_probelist
{
	struct chandev_probelist *next;
	chandev_probefunc       probefunc;
	chandev_type            chan_type;
};
