#ifndef __KERNEL__
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/ioctl.h>

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/hdreg.h>
#include <linux/version.h>
#endif
#include <asm/dasd.h>

#define LINE_LENGTH 80
#define VTOC_START_CC 0x0
#define VTOC_START_HH 0x1

enum failure {unable_to_open,
	      unable_to_seek,
	      unable_to_write,
	      unable_to_read};

typedef struct ttr {
        __u16 tt;
        __u8  r;
} __attribute__ ((packed)) ttr_t;

typedef struct cchhb {
        __u16 cc;
        __u16 hh;
        __u8 b;
} __attribute__ ((packed)) cchhb_t;

typedef struct cchh {
        __u16 cc;
        __u16 hh;
} __attribute__ ((packed)) cchh_t;

typedef struct labeldate {
        __u8  year;
        __u16 day;
} __attribute__ ((packed)) labeldate_t;


typedef struct volume_label {
        char volkey[4];         /* volume key = volume label                 */
	char vollbl[4];	        /* volume label                              */
	char volid[6];	        /* volume identifier                         */
	__u8 security;	        /* security byte                             */
	cchhb_t vtoc;           /* VTOC address                              */
	char res1[5];	        /* reserved                                  */
        char cisize[4];	        /* CI-size for FBA,...                       */
                                /* ...blanks for CKD                         */
	char blkperci[4];       /* no of blocks per CI (FBA), blanks for CKD */
	char labperci[4];       /* no of labels per CI (FBA), blanks for CKD */
	char res2[4];	        /* reserved                                  */
	char lvtoc[14];	        /* owner code for LVTOC                      */
	char res3[29];	        /* reserved                                  */
} __attribute__ ((packed)) volume_label_t;


typedef struct extent {
        __u8  typeind;          /* extent type indicator                     */
        __u8  seqno;            /* extent sequence number                    */
        cchh_t llimit;          /* starting point of this extent             */
        cchh_t ulimit;          /* ending point of this extent               */
} __attribute__ ((packed)) extent_t;


typedef struct dev_const {
        __u16 DS4DSCYL;           /* number of logical cyls                  */
        __u16 DS4DSTRK;           /* number of tracks in a logical cylinder  */
        __u16 DS4DEVTK;           /* device track length                     */
        __u8  DS4DEVI;            /* non-last keyed record overhead          */
        __u8  DS4DEVL;            /* last keyed record overhead              */
        __u8  DS4DEVK;            /* non-keyed record overhead differential  */
        __u8  DS4DEVFG;           /* flag byte                               */
        __u16 DS4DEVTL;           /* device tolerance                        */
        __u8  DS4DEVDT;           /* number of DSCB's per track              */
        __u8  DS4DEVDB;           /* number of directory blocks per track    */
} __attribute__ ((packed)) dev_const_t;


typedef struct format1_label {
	char  DS1DSNAM[44];       /* data set name                           */
	__u8  DS1FMTID;           /* format identifier                       */
	char  DS1DSSN[6];         /* data set serial number                  */
	__u16 DS1VOLSQ;           /* volume sequence number                  */
	labeldate_t DS1CREDT;     /* creation date: ydd                      */
	labeldate_t DS1EXPDT;     /* expiration date                         */
	__u8  DS1NOEPV;           /* number of extents on volume             */
        __u8  DS1NOBDB;           /* no. of bytes used in last direction blk */
	__u8  DS1FLAG1;           /* flag 1                                  */
	char  DS1SYSCD[13];       /* system code                             */
	labeldate_t DS1REFD;      /* date last referenced                    */
        __u8  DS1SMSFG;           /* system managed storage indicators       */
        __u8  DS1SCXTF;           /* sec. space extension flag byte          */
        __u16 DS1SCXTV;           /* secondary space extension value         */
        __u8  DS1DSRG1;           /* data set organisation byte 1            */
        __u8  DS1DSRG2;           /* data set organisation byte 2            */
  	__u8  DS1RECFM;           /* record format                           */
	__u8  DS1OPTCD;           /* option code                             */
	__u16 DS1BLKL;            /* block length                            */
	__u16 DS1LRECL;           /* record length                           */
	__u8  DS1KEYL;            /* key length                              */
	__u16 DS1RKP;             /* relative key position                   */
	__u8  DS1DSIND;           /* data set indicators                     */
        __u8  DS1SCAL1;           /* secondary allocation flag byte          */
  	char DS1SCAL3[3];         /* secondary allocation quantity           */
	ttr_t DS1LSTAR;           /* last used track and block on track      */
	__u16 DS1TRBAL;           /* space remaining on last used track      */
        __u16 res1;               /* reserved                                */
	extent_t DS1EXT1;         /* first extent description                */
	extent_t DS1EXT2;         /* second extent description               */
	extent_t DS1EXT3;         /* third extent description                */
	cchhb_t DS1PTRDS;         /* possible pointer to f2 or f3 DSCB       */
} __attribute__ ((packed)) format1_label_t;


typedef struct format4_label {
	char  DS4KEYCD[44];       /* key code for VTOC labels: 44 times 0x04 */
        __u8  DS4IDFMT;           /* format identifier                       */
	cchhb_t DS4HPCHR;         /* highest address of a format 1 DSCB      */
        __u16 DS4DSREC;           /* number of available DSCB's              */
        cchh_t DS4HCCHH;          /* CCHH of next available alternate track  */
        __u16 DS4NOATK;           /* number of remaining alternate tracks    */
        __u8  DS4VTOCI;           /* VTOC indicators                         */
        __u8  DS4NOEXT;           /* number of extents in VTOC               */
        __u8  DS4SMSFG;           /* system managed storage indicators       */
        __u8  DS4DEVAC;           /* number of alternate cylinders. 
                                     Subtract from first two bytes of 
                                     DS4DEVSZ to get number of usable
				     cylinders. can be zero. valid
				     only if DS4DEVAV on.                    */
        dev_const_t DS4DEVCT;     /* device constants                        */
        char DS4AMTIM[8];         /* VSAM time stamp                         */
        char DS4AMCAT[3];         /* VSAM catalog indicator                  */
        char DS4R2TIM[8];         /* VSAM volume/catalog match time stamp    */
        char res1[5];             /* reserved                                */
        char DS4F6PTR[5];         /* pointer to first format 6 DSCB          */
        extent_t DS4VTOCE;        /* VTOC extent description                 */
        char res2[10];            /* reserved                                */
        __u8 DS4EFLVL;            /* extended free-space management level    */
        cchhb_t DS4EFPTR;         /* pointer to extended free-space info     */
        char res3[9];             /* reserved                                */
} __attribute__ ((packed)) format4_label_t;


char * vtoc_ebcdic_enc (
        unsigned char source[LINE_LENGTH],
        unsigned char target[LINE_LENGTH],
	int l);
char * vtoc_ebcdic_dec (
        unsigned char source[LINE_LENGTH],
	unsigned char target[LINE_LENGTH],
	int l);
void vtoc_set_extent (
        extent_t * ext,
        __u8 typeind,
        __u8 seqno,
        cchh_t * lower,
        cchh_t * upper);
void vtoc_set_cchh (
        cchh_t * addr,
	__u16 cc,
	__u16 hh);
void vtoc_set_cchhb (
        cchhb_t * addr,
        __u16 cc,
        __u16 hh,
        __u8 b);
void vtoc_set_date (
        labeldate_t * d,
        __u8 year,
        __u16 day);


int vtoc_read_volume_label (
        char * device,
        unsigned long vlabel_start,
        volume_label_t * vlabel);
int vtoc_write_volume_label (
        char * device,
        unsigned long vlabel_start,
        volume_label_t * vlabel);
void vtoc_read_label (
        char *device,
        unsigned long position,
        format4_label_t *f4,
        format1_label_t *f1);
void vtoc_write_label (
        char *device,
        unsigned long position,
	format4_label_t *f4,
	format1_label_t *f1);
void vtoc_init_format4_label (
        struct hd_geometry *geo,
        format4_label_t *f4lbl,
	unsigned int usable_partitions,
	unsigned int cylinders,
	unsigned int tracks,
	unsigned int blocks);
void vtoc_init_format1_label (
        char *volid,
        unsigned int blksize,
        extent_t *part_extent,
        format1_label_t *f1);
void vtoc_update_format4_label (
	format4_label_t *f4,
	cchhb_t *highest_f1,
	__u8 unused_update,
	__u16 freespace_update);















