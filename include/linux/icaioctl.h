/* Copyright (c) International Business Machines Corp., 2001 */
/*
 * linux/include/linux/icaioctl.h
 * 
 */



#ifndef _LINUX_ICAIOCTL_H_
#define _LINUX_ICAIOCTL_H_

enum _sizelimits {
  ICA_DES_DATALENGTH_MIN = 8,
  ICA_DES_DATALENGTH_MAX = 32 * 1024 * 1024 - 8,
  ICA_SHA_DATALENGTH = 20,
  ICA_SHA_BLOCKLENGTH = 64,
  ICA_RSA_DATALENGTH_MIN = 256/8,
  ICA_RSA_DATALENGTH_MAX = 2048/8
};


typedef struct _ica_rng_rec {
  unsigned int nbytes;
  char *buf;
} ica_rng_t;


// May have some porting issues here 

typedef struct _ica_rsa_modexpo {
  char         *inputdata;
  unsigned int  inputdatalength;
  char         *outputdata;
  unsigned int  outputdatalength;
  char         *b_key;
  char         *n_modulus;
} ica_rsa_modexpo_t;

typedef ica_rsa_modexpo_t ica_rsa_modmult_t;

typedef struct _ica_rsa_modexpo_crt {
  char         *inputdata;
  unsigned int  inputdatalength;
  char         *outputdata;
  unsigned int  outputdatalength;
  char         *bp_key;
  char         *bq_key;
  char         *np_prime;
  char         *nq_prime;
  char         *u_mult_inv;
} ica_rsa_modexpo_crt_t;

typedef unsigned char ica_des_vector_t[8];
typedef unsigned char ica_des_key_t[8];
typedef ica_des_key_t ica_des_single_t[1];
typedef ica_des_single_t ica_des_triple_t[3];

enum _ica_mode_des {
  DEVICA_MODE_DES_CBC = 0,
  DEVICA_MODE_DES_ECB = 1
};

enum _ica_direction_des {
  DEVICA_DIR_DES_ENCRYPT = 0,
  DEVICA_DIR_DES_DECRYPT = 1
};

typedef struct _ica_des {
  unsigned int      mode;
  unsigned int      direction;
  unsigned char    *inputdata;
  unsigned int      inputdatalength;
  ica_des_vector_t *iv;
  ica_des_key_t    *keys;
  unsigned char    *outputdata;
  int              outputdatalength;
} ica_des_t;
  
typedef struct _ica_desmac {
  unsigned char    *inputdata;
  unsigned int      inputdatalength;
  ica_des_vector_t *iv;
  ica_des_key_t    *keys;
  unsigned char    *outputdata;
  int              outputdatalength;
} ica_desmac_t;
  

typedef unsigned char ica_sha1_result_t[ICA_SHA_DATALENGTH];


typedef struct _ica_sha1 {
  unsigned char     *inputdata;
  unsigned int       inputdatalength;
  ica_sha1_result_t *outputdata;
  ica_sha1_result_t *initialh;
} ica_sha1_t;

/* The following structs are used by conversion functions
   on PowerPC 64 bit only.  They should not be used by externel
   applications.  Should the non PPC specific structs change, these
   structures may need to change as well.  Also, new conversion
   routines will need to be added to devica.c to deal with new 
   structs or structure members.
*/
#ifdef CONFIG_PPC64
typedef struct _ica_rng_rec_32 {
  unsigned int nbytes;
  unsigned int buf;
} ica_rng_t_32;

typedef struct _ica_des_32 {
  unsigned int   mode;
  unsigned int   direction;
  unsigned int    inputdata;
  unsigned int  inputdatalength;
  unsigned int     iv;
  unsigned int      keys;
  unsigned int      outputdata;
  unsigned int   outputdatalength;
} ica_des_t_32;

typedef struct _ica_sha1_32 {
  unsigned int       inputdata;
  unsigned int       inputdatalength;
  unsigned int       outputdata;
  unsigned int       initialh;
} ica_sha1_t_32;

typedef struct _ica_desmac_32 {
  unsigned int    inputdata;
  unsigned int      inputdatalength;
  unsigned int     iv;
  unsigned int     keys;
  unsigned int     outputdata;
  int              outputdatalength;
} ica_desmac_t_32;

typedef struct _ica_rsa_modexpo_crt_32 {
  unsigned int  inputdata;
  unsigned int  inputdatalength;
  unsigned int  outputdata;
  unsigned int  outputdatalength;
  unsigned int  bp_key;
  unsigned int  bq_key;
  unsigned int  np_prime;
  unsigned int  nq_prime;
  unsigned int  u_mult_inv;
} ica_rsa_modexpo_crt_t_32;

typedef struct _ica_rsa_modexpo_32 {
  unsigned int  inputdata;
  unsigned int  inputdatalength;
  unsigned int  outputdata;
  unsigned int  outputdatalength;
  unsigned int  b_key;
  unsigned int  n_modulus;
} ica_rsa_modexpo_t_32;

#endif

#define ICA_IOCTL_MAGIC '?'  // NOTE:  Need to allocate from linux folks

/*
 * Note: Some platforms only use 8 bits to define the parameter size.  As 
 * the macros in ioctl.h don't seem to mask off offending bits, they look
 * a little unsafe.  We should probably just not use the parameter size
 * at all for these ioctls.  I don't know if we'll ever run on any of those
 * architectures, but seems easier just to not count on this feature.
 */

#define ICASETBIND     _IOW(ICA_IOCTL_MAGIC, 0x01, int)
#define ICAGETBIND     _IOR(ICA_IOCTL_MAGIC, 0x02, int)
#define ICAGETCOUNT    _IOR(ICA_IOCTL_MAGIC, 0x03, int)
#define ICAGETID       _IOR(ICA_IOCTL_MAGIC, 0x04, int)
#define ICARSAMODEXPO  _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x05, 0)
#define ICARSACRT      _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x06, 0) 
#define ICARSAMODMULT  _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x07, 0)
#define ICADES         _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x08, 0)
#define ICADESMAC      _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x09, 0)
#define ICATDES        _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x0a, 0)
#define ICATDESSHA     _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x0b, 0)
#define ICATDESMAC     _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x0c, 0)
#define ICASHA1        _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x0d, 0)
#define ICARNG         _IOC(_IOC_READ, ICA_IOCTL_MAGIC, 0x0e, 0)
#define ICAGETVPD      _IOC(_IOC_READ, ICA_IOCTL_MAGIC, 0x0f, 0)

#ifdef __KERNEL__

#ifndef assertk
#ifdef NDEBUG
#  define assertk(expr) do {} while (0)
#else
#  define assertk(expr) \
        if(!(expr)) {                                   \
        printk( "Assertion failed! %s,%s,%s,line=%d\n", \
        #expr,__FILE__,__FUNCTION__,__LINE__);          \
        }
#endif
#endif


struct ica_operations {
  ssize_t (*read) (struct file *, char *, size_t, loff_t *, void *);
  int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long, void *);
};

typedef struct ica_worker {
  struct ica_operations *icaops;
  void * private_data;  
} ica_worker_t;


extern int ica_register_worker(int partitionnum, ica_worker_t *device);
extern int ica_unregister_worker(int partitionnum, ica_worker_t *device);

#endif /* __KERNEL__ */

#endif /* _LINUX_ICAIOCTL_H_ */
