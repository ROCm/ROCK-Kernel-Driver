#ifndef _PARISC_HP_MACHINES_H_ 
#define _PARISC_HP_MACHINES_H_ 

struct hp_hardware {
	unsigned short hw_type:5;		/* HPHW_xxx */
	unsigned short hversion;
	unsigned long  sversion:28;
	unsigned short opt;
	char *name; 
};

struct hp_device {
	unsigned short hw_type:5;	/* HPHW_xxx */
	unsigned short hversion;	/* HP-UX uses  hv_model:12 */
	unsigned int   sversion;	/* HP-UX uses sv_model:20 sv_opt:8 */
	unsigned short opt;
	unsigned int hversion_rev;
	unsigned int sversion_rev;
	struct hp_hardware * reference;  /* This is a pointer to the
                                            reference */
	unsigned int managed; /* this is if the device has a driver for it */
	void * hpa;

#ifdef __LP64__
	/* parms for pdc_pat_cell_module() call */
	unsigned long  pcell_loc;	/* Physical Cell location */
	unsigned long  mod_index;	/* PAT specific - Misc Module info */

	/* generic info returned from pdc_pat_cell_module() */
	unsigned long  mod_info;	/* PAT specific - Misc Module info */
	unsigned long  pmod_loc;	/* physical Module location */
	unsigned long  mod_path;	/* Module HW path */
#endif
};

enum cpu_type {
	pcx	= 0, /* pa7000		pa 1.0  */
	pcxs	= 1, /* pa7000		pa 1.1a */
	pcxt	= 2, /* pa7100		pa 1.1b */
	pcxt_	= 3, /* pa7200	(t')	pa 1.1c */
	pcxl	= 4, /* pa7100lc	pa 1.1d */
	pcxl2	= 5, /* pa7300lc	pa 1.1e */
	pcxu	= 6, /* pa8000		pa 2.0  */
	pcxu_	= 7, /* pa8200	(u+)	pa 2.0  */
	pcxw	= 8, /* pa8500		pa 2.0  */
	pcxw_	= 9  /* pa8600	(w+)	pa 2.0  */
};

extern char *cpu_name_version[][2]; /* mapping from enum cpu_type to strings */

struct pa_iodc_driver {
	unsigned short hw_type:5;		/* HPHW_xxx */
	unsigned short hversion;
	unsigned short hversion_rev;
	unsigned long  sversion:28;
	unsigned short sversion_rev;
	unsigned short opt;
	unsigned int check;  /* Components that are significant */
	char *name; 
	char *version; 
	int (* callback)(struct hp_device *d, struct pa_iodc_driver *dri);
};

#define DRIVER_CHECK_HWTYPE          1
#define DRIVER_CHECK_HVERSION        2
#define DRIVER_CHECK_SVERSION        4
#define DRIVER_CHECK_OPT             8
/* The following two are useless right now */
#define DRIVER_CHECK_HVERSION_REV   16
#define DRIVER_CHECK_SVERSION_REV   32
#define DRIVER_CHECK_EVERYTHING     63


#define HPHW_NPROC     0 
#define HPHW_MEMORY    1       
#define HPHW_B_DMA     2
#define HPHW_OBSOLETE  3
#define HPHW_A_DMA     4
#define HPHW_A_DIRECT  5
#define HPHW_OTHER     6
#define HPHW_BCPORT    7
#define HPHW_CIO       8
#define HPHW_CONSOLE   9
#define HPHW_FIO       10
#define HPHW_BA        11
#define HPHW_IOA       12
#define HPHW_BRIDGE    13
#define HPHW_FABRIC    14
#define HPHW_FAULTY    31

extern struct hp_hardware hp_hardware_list[];

char *parisc_getHWtype(	unsigned short hw_type );

/* Attention: first hversion, then sversion...! */
char *parisc_getHWdescription(	unsigned short hw_type,
                                unsigned long hversion,  /* have to be long ! */
				unsigned long sversion );

enum cpu_type parisc_get_cpu_type( unsigned long hversion );

extern int register_driver(struct pa_iodc_driver *driver);
#endif
