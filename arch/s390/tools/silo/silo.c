/*
 *  arch/s390/boot/silo.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *
 *    Report bugs to: <linux390@de.ibm.com>
 *
 *    Author(s): Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *               Fritz Elfert <felfert@to.com> contributed support for
 *                	/etc/silo.conf based on Intel's lilo
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <linux/fs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <asm/ioctl.h>

#include "cfg.h"

CONFIG cf_options[] = {
  { cft_strg, "append",		NULL,		NULL,NULL },
  { cft_strg, "image",		NULL,		NULL,NULL },
  { cft_strg, "ipldevice",	NULL,		NULL,NULL },
  { cft_strg, "bootsect",	NULL,		NULL,NULL },
  { cft_strg, "map",		NULL,		NULL,NULL },
  { cft_strg, "parmfile",	NULL,		NULL,NULL },
  { cft_strg, "ramdisk",	NULL,		NULL,NULL },
  { cft_strg, "root",		NULL,		NULL,NULL },
  { cft_flag, "readonly",	NULL,		NULL,NULL },
  { cft_strg, "verbose",	NULL,		NULL,NULL },
  { cft_strg, "testlevel",	NULL,		NULL,NULL },
  { cft_end,  NULL,		NULL,		NULL,NULL }
};
  
/* from dasd.h */
#define DASD_PARTN_BITS 2
#define BIODASDRWTB _IOWR('D',5,int)
/* end */

#define SILO_CFG "/etc/silo.conf"
#define SILO_IMAGE "./image"
#define SILO_BOOTMAP "./boot.map"
#define SILO_PARMFILE "./parmfile"
#define SILO_BOOTSECT "/boot/ipleckd.boot"

#define PRINT_LEVEL(x,y...) if ( silo_options.verbosity >= x ) printf(y)
#define ERROR_LEVEL(x,y...) if ( silo_options.verbosity >= x ) fprintf(stderr,y)
#define TOGGLE(x) ((x)=((x)?(0):(1)))
#define GETARG(x) {int len=strlen(optarg);x=malloc(len);strncpy(x,optarg,len);PRINT_LEVEL(1,"%s set to %s\n",#x,optarg);}

#define ITRY(x) if ( (x) == -1 ) { ERROR_LEVEL(0,"%s (line:%d) '%s' returned %d='%s'\n", __FILE__,__LINE__,#x,errno,strerror(errno)); usage(); exit(1); }
#define NTRY(x) if ( (x) == 0 ) { ERROR_LEVEL(0,"%s (line:%d) '%s' returned %d='%s'\n", __FILE__,__LINE__,#x,errno,strerror(errno)); usage(); exit(1); }

#define MAX_CLUSTERS 256
#define PARTN_MASK ((1 << DASD_PARTN_BITS) - 1)

#define SILO_VERSION "1.1"

struct silo_options
  {
    short int verbosity;
    short int testlevel;
    char *image;
    char *ipldevice;
    char *parmfile;
    char *ramdisk;
    char *bootsect;
    char *conffile;
    char *bootmap;
  }
silo_options =
{
  1,				/* verbosity */
  2,				/* testlevel */
    SILO_IMAGE,			/* image */
    NULL,			/* ipldevice */
    SILO_PARMFILE,		/* parmfile */
    NULL,			/* initrd */
    SILO_BOOTSECT,		/* bootsector */
    SILO_CFG,                   /* silo.conf file */
    SILO_BOOTMAP,               /* boot.map */
};

struct blockdesc
  {
    unsigned long off;
    unsigned short ct;
    unsigned long addr;
  };

struct blocklist
  {
    struct blockdesc blk[MAX_CLUSTERS];
    unsigned short ix;
  };

void
usage (void)
{
  printf ("Usage:\n");
  printf ("silo -d ipldevice [additional options]\n");
  printf ("-d /dev/node : set ipldevice to /dev/node\n");
  printf ("-f image : set image to image\n");
  printf ("-F conffile : specify configuration file (/etc/silo.conf)\n");
  printf ("-p parmfile : set parameter file to parmfile\n");
  printf ("-b bootsect : set bootsector to bootsect\n");
  printf ("Additional options\n");
  printf ("-B bootmap:\n");
  printf ("-v: increase verbosity level\n");
  printf ("-v#: set verbosity level to #\n");
  printf ("-t: decrease testing level\n");
  printf ("-h: print this message\n");
  printf ("-?: print this message\n");
  printf ("-V: print version\n");
}

int
read_cfg(struct silo_options *o)
{
	char *tmp;
	if (access(o->conffile, R_OK) && (errno == ENOENT))
		return 0;
	/* If errno != ENOENT, let cfg_open report an error */
	cfg_open(o->conffile);
	cfg_parse(cf_options);
	tmp = cfg_get_strg(cf_options, "ipldevice");
	if ( ! o->ipldevice  && tmp ) 
		o->ipldevice = tmp;
	tmp = cfg_get_strg(cf_options, "image");
	if ( ! strncmp(o-> image,SILO_IMAGE,strlen(SILO_IMAGE)) && tmp ) 
		o->image = tmp;
	tmp = cfg_get_strg(cf_options, "parmfile");
	if ( !strncmp(o->parmfile,SILO_PARMFILE,strlen(SILO_PARMFILE)) && tmp) 
		o->parmfile = tmp;
	if ( ! o -> ramdisk ) 
		o->ramdisk = cfg_get_strg(cf_options, "ramdisk");
	tmp = cfg_get_strg(cf_options, "bootsect");
	if ( !strncmp(o -> bootsect,SILO_BOOTSECT,strlen(SILO_BOOTSECT))&&tmp)
		o->bootsect = tmp;
	tmp = cfg_get_strg(cf_options, "map") ;
	if ( !strncmp(o -> bootmap,SILO_BOOTMAP,strlen(SILO_BOOTMAP)) && tmp) 
		o->bootmap = tmp; 
	tmp = cfg_get_strg(cf_options, "verbose");
	if ( tmp ) {
		unsigned short v;
		sscanf (tmp, "%hu", &v);
		o->verbosity = v;
	}
	tmp = cfg_get_strg(cf_options, "testlevel");
	if ( tmp ) {
		unsigned short t;
		sscanf (tmp, "%hu", &t);
		o->testlevel += t;
	}
	return 1;
}

char *
gen_tmpparm( char *pfile )
{
	char *append = cfg_get_strg(cf_options, "append");
	char *root = cfg_get_strg(cf_options, "root");
	int ro = cfg_get_flag(cf_options, "readonly");
	FILE *f,*of;
	char *fn;
	char c;
	char *tmpdir=NULL,*save=NULL;

	if (!append && !root && !ro)
		return pfile;
	of = fopen(pfile, "r");
	if ( of ) {
		NTRY( fn = tempnam(NULL,"parm."));
	} else {
		fn = pfile;
	}
	NTRY( f = fopen(fn, "a+"));
	if ( of ) {
		while ( ! feof (of) ) {
		  c=fgetc(of);
	  	fputc(c,f);
		}
	}
	if (root)
		fprintf(f, " root=%s", root);
	if (ro)
		fprintf(f, " ro");
	if (append)
		fprintf(f, " %s", append);
	fprintf(f, "\n");
	fclose(f);
	fclose(of);
	printf ("tempfile is %s\n",fn);
	return strdup(fn);
}

int
parse_options (struct silo_options *o, int argc, char *argv[])
{
  int rc = 0;
  int oc;

  while ((oc = getopt (argc, argv, "Vf:F:d:p:r:b:B:h?v::t::")) != -1)
    {
      switch (oc)
	{
	case 'V':
	  printf("silo version: %s\n",SILO_VERSION);
	  exit(0);
	case 'v':
	  {
	    unsigned short v;
	    if (optarg && sscanf (optarg, "%hu", &v))
	      o->verbosity = v;
	    else
	      o->verbosity++;
	    PRINT_LEVEL (1, "Verbosity value is now %hu\n", o->verbosity);
	    break;
	  }
	case 't':
	  {
	    unsigned short t;
	    if (optarg && sscanf (optarg, "%hu", &t))
	      o->testlevel -= t;
	    else
	      o->testlevel--;
            PRINT_LEVEL (1, "Testonly flag is now %d\n", o->testlevel);
	    break;
	  }
	case 'h':
	case '?':
	  usage ();
	  exit(0);
	case 'd':
	  GETARG (o->ipldevice);
	  break;
	case 'f':
	  GETARG (o->image);
	  break;
        case 'F':                         
 	   GETARG (o->conffile);              
 	   break;                          
	case 'p':
	  GETARG (o->parmfile);
	  break;
	case 'r':
	  GETARG (o->ramdisk);
	  break;
	case 'b':
	  GETARG (o->bootsect);
	  break;
	case 'B':
	  GETARG (o->bootmap);
	default:
	  rc = EINVAL;
	  break;
	}
    }
  read_cfg(o);
  return rc;
}

int
verify_device (char *name)
{
  int rc = 0;
  struct stat dst;
  struct stat st;
  ITRY (stat (name, &dst));
  if (S_ISBLK (dst.st_mode))
    {
      if (!(MINOR (dst.st_rdev) & PARTN_MASK))
	{
	  rc = dst.st_rdev;
	}
      else
	/* invalid MINOR & PARTN_MASK */
	{
	  ERROR_LEVEL (1, "Cannot boot from partition %d %d %d",
		       (int) PARTN_MASK, (int) MINOR (dst.st_rdev), (int) (PARTN_MASK & MINOR (dst.st_rdev)));
	  rc = -1;
	  errno = EINVAL;
	}
    }
  else
    /* error S_ISBLK */
    {
      ERROR_LEVEL (1, "%s is no block device\n", name);
      rc = -1;
      errno = EINVAL;
    }
  return rc;
}

int
verify_file (char *name, int dev)
{
  int rc = 0;
  struct stat dst;
  struct stat st;
  int bs = 1024;
  int l;

  ITRY(stat ( name, &dst ));
  if (S_ISREG (dst.st_mode))
    {
      if ((unsigned) MAJOR (dev) == (unsigned) MAJOR (dst.st_dev) && (unsigned) MINOR (dev) == (unsigned) (MINOR (dst.st_dev) & ~PARTN_MASK))
	{
	  /* whatever to do if all is ok... */
	}
      else
	/* devicenumber doesn't match */
	{
	  ERROR_LEVEL (1, "%s is not on device (%d/%d) but on (%d/%d)\n", name, (unsigned) MAJOR (dev), (unsigned) MINOR (dev), (unsigned) MAJOR (dst.st_dev), (unsigned) (MINOR (dst.st_dev) & ~PARTN_MASK));
	  rc = -1;
	  errno = EINVAL;
	}
    }
  else
    /* error S_ISREG */
    {
      ERROR_LEVEL (1, "%s is neither regular file nor linkto one\n", name);
      rc = -1;
      errno = EINVAL;
    }
  return rc;
}

int
verify_options (struct silo_options *o)
{
  int rc = 0;
  int dev = 0;
  int crc = 0;
  if (!o->ipldevice || !o->image || !o->bootsect)
    {
     if (!o->ipldevice)
       fprintf(stderr,"ipldevice\n");
     if (!o->image)
       fprintf(stderr,"image\n");
     if (!o->bootsect)
       fprintf(stderr,"bootsect\n");

      usage ();
      exit (1);
    }
  PRINT_LEVEL (1, "Testlevel is set to %d\n",o->testlevel);

  PRINT_LEVEL (1, "IPL device is: '%s'", o->ipldevice);
  ITRY (dev = verify_device (o->ipldevice));
  PRINT_LEVEL (2, "...ok...(%d/%d)", (unsigned short) MAJOR (dev), (unsigned short) MINOR (dev));
  PRINT_LEVEL (1, "\n");

  PRINT_LEVEL (0, "bootsector is: '%s'", o->bootsect);
  ITRY (verify_file (o->bootsect, dev));
  PRINT_LEVEL (1, "...ok...");
  PRINT_LEVEL (0, "\n");

  if ( o -> testlevel > 0  && 
       ! strncmp( o->bootmap, SILO_BOOTMAP,strlen(SILO_BOOTMAP) )) {
     NTRY( o -> bootmap = tempnam(NULL,"boot."));
  }
  PRINT_LEVEL (0, "bootmap is set to: '%s'", o->bootmap);
  if ( access ( o->bootmap, O_RDWR ) == -1 ) {
    if ( errno == ENOENT ) {
      ITRY (creat ( o-> bootmap, O_RDWR ));
    } else {
      PRINT_LEVEL(1,"Cannot acces bootmap file '%s': %s\n",o->bootmap,
		  strerror(errno));
    }
  }
  ITRY (verify_file (o->bootmap, dev));
  PRINT_LEVEL (1, "...ok...");
  PRINT_LEVEL (0, "\n");

  PRINT_LEVEL (0, "Kernel image is: '%s'", o->image);
  ITRY (verify_file (o->image, dev));
  PRINT_LEVEL (1, "...ok...");
  PRINT_LEVEL (0, "\n");

  PRINT_LEVEL (0, "original parameterfile is: '%s'", o->parmfile);
  ITRY (verify_file (o->parmfile, dev));
  PRINT_LEVEL (1, "...ok...");
  o->parmfile = gen_tmpparm(o->parmfile);
  PRINT_LEVEL (0, "final parameterfile is: '%s'", o->parmfile);
  ITRY (verify_file (o->parmfile, dev));
  PRINT_LEVEL (1, "...ok...");
  PRINT_LEVEL (0, "\n");

  if (o->ramdisk)
    {
      PRINT_LEVEL (0, "initialramdisk is: '%s'", o->ramdisk);
      ITRY (verify_file (o->ramdisk, dev));
      PRINT_LEVEL (1, "...ok...");
      PRINT_LEVEL (0, "\n");
    }

  return crc;
}


int
add_file_to_blocklist (char *name, struct blocklist *lst, long addr)
{
  int fd;
  int devfd;
  struct stat fst;
  int i;
  int blk;
  int bs;
  int blocks;

  int rc = 0;

  ITRY (fd = open (name, O_RDONLY));
  ITRY (fstat (fd, &fst));
  ITRY (mknod ("/tmp/silodev", S_IFBLK | S_IRUSR | S_IWUSR, fst.st_dev));
  ITRY (devfd = open ("/tmp/silodev", O_RDONLY));
  ITRY (ioctl (fd, FIGETBSZ, &bs));
  blocks = (fst.st_size + bs - 1) / bs;
  for (i = 0; i < blocks; i++)
    {
      blk = i;
      ITRY (ioctl (fd, FIBMAP, &blk));
      if (blk)
	{
	  int oldblk = blk;
	  ITRY (ioctl (devfd, BIODASDRWTB, &blk));
	  if (blk <= 0)
	    {
	      ERROR_LEVEL (0, "BIODASDRWTB on blk %d returned %d\n", oldblk, blk);
	      break;
	    }
	}
      else
	{
	  PRINT_LEVEL (1, "Filled hole on blk %d\n", i);
	}
      if (lst->ix == 0 || i == 0  || 
	  lst->blk[lst->ix - 1].ct >= 128 ||
	  (lst->blk[lst->ix - 1].off + lst->blk[lst->ix - 1].ct != blk &&
	   !(lst->blk[lst->ix - 1].off == 0 && blk == 0)))
	{
	  if (lst->ix >= MAX_CLUSTERS)
	    {
	      rc = 1;
	      errno = ENOMEM;
	      break;
	    }
	  lst->blk[lst->ix].off = blk;
	  lst->blk[lst->ix].ct = 1;
	  lst->blk[lst->ix].addr = addr + i * bs;
	  lst->ix++;
	}
      else
	{
	  lst->blk[lst->ix - 1].ct++;
	}
    }
  ITRY(unlink("/tmp/silodev"));
  return rc;
}

int
write_bootsect (struct silo_options *o, struct blocklist *blklst)
{
  int i;
  int s_fd, d_fd, b_fd, bd_fd;
  struct stat s_st, d_st, b_st;
  int rc=0;
  int bs, boots;
  char *tmpdev;
  char buffer[4096]={0,};
  ITRY (d_fd = open (o->ipldevice, O_RDWR | O_SYNC));
  ITRY (fstat (d_fd, &d_st));
  ITRY (s_fd = open (o->bootmap, O_RDWR | O_TRUNC | O_CREAT | O_SYNC));
  ITRY (verify_file (o->bootsect, d_st.st_rdev));
  for (i = 0; i < blklst->ix; i++)
    {
      int offset = blklst->blk[i].off;
      int addrct = blklst->blk[i].addr | (blklst->blk[i].ct & 0xff);
      PRINT_LEVEL (1, "ix %i: offset: %06x count: %02x address: 0x%08x\n", i, offset, blklst->blk[i].ct & 0xff, blklst->blk[i].addr);
	if ( o->testlevel <= 1 ) {
	      NTRY (write (s_fd, &offset, sizeof (int)));
	      NTRY (write (s_fd, &addrct, sizeof (int)));
	}
    }
  ITRY (ioctl (s_fd,FIGETBSZ, &bs));
  ITRY (stat (o->bootmap, &s_st));
  if (s_st.st_size > bs )
    {
      ERROR_LEVEL (0,"%s is larger than one block\n", o->bootmap);
      rc = -1;
      errno = EINVAL;
    }
  boots=0;
  NTRY ( tmpdev = tmpnam(NULL) );
  ITRY (mknod (tmpdev, S_IFBLK | S_IRUSR | S_IWUSR, s_st.st_dev));
  ITRY (bd_fd = open (tmpdev, O_RDONLY));
  ITRY (ioctl(s_fd,FIBMAP,&boots));
  ITRY (ioctl (bd_fd, BIODASDRWTB, &boots));
  PRINT_LEVEL (1, "Bootmap is in block no: 0x%08x\n", boots);
  close (bd_fd);
  close(s_fd);
  ITRY (unlink(tmpdev));
  /* Now patch the bootsector */
  ITRY (b_fd = open (o->bootsect, O_RDONLY));
  NTRY (read (b_fd, buffer, 4096));
  memset (buffer + 0xe0, 0, 8);
  *(int *) (buffer + 0xe0) = boots;
  if ( o -> testlevel <= 0 ) {
    NTRY (write (d_fd, buffer, 4096));
    NTRY (write (d_fd, buffer, 4096));
  }
  close (b_fd);
  close (d_fd);
  return rc;
}

int
do_silo (struct silo_options *o)
{
  int rc = 0;

  int device_fd;
  int image_fd;
  struct blocklist blklist;
  memset (&blklist, 0, sizeof (struct blocklist));
  ITRY (add_file_to_blocklist (o->image, &blklist, 0x00000000));
  if (o->parmfile)
    {
      ITRY (add_file_to_blocklist (o->parmfile, &blklist, 0x00008000));
    }
  if (o->ramdisk)
    {
      ITRY (add_file_to_blocklist (o->ramdisk, &blklist, 0x00800000));
    }
  ITRY (write_bootsect (o, &blklist));
  return rc;
}

int
main (int argct, char *argv[])
{
  int rc = 0;
  char *save;
  char *tmpdir=getenv("TMPDIR");
  if (tmpdir) {
    NTRY( save=(char*)malloc(strlen(tmpdir)));
    NTRY( strncpy(save,tmpdir,strlen(tmpdir)));
  }
  ITRY( setenv("TMPDIR",".",1));
  ITRY (parse_options (&silo_options, argct, argv));
  ITRY (verify_options (&silo_options));
  if ( silo_options.testlevel > 0 ) {
    printf ("WARNING: silo does not modify your volume. Use -t2 to change IPL records\n");
  }
  ITRY (do_silo (&silo_options));
  if ( save )
    ITRY( setenv("TMPDIR",save,1)); 
  return rc;
}
