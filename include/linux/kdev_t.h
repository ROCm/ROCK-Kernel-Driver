#ifndef _LINUX_KDEV_T_H
#define _LINUX_KDEV_T_H
#ifdef __KERNEL__
/*
As a preparation for the introduction of larger device numbers,
we introduce a type kdev_t to hold them. No information about
this type is known outside of this include file.

Objects of type kdev_t designate a device. Outside of the kernel
the corresponding things are objects of type dev_t - usually an
integral type with the device major and minor in the high and low
bits, respectively. Conversion is done by

extern kdev_t to_kdev_t(int);

It is up to the various file systems to decide how objects of type
dev_t are stored on disk.
The only other point of contact between kernel and outside world
are the system calls stat and mknod, new versions of which will
eventually have to be used in libc.

[Unfortunately, the floppy control ioctls fail to hide the internal
kernel structures, and the fd_device field of a struct floppy_drive_struct
is user-visible. So, it remains a dev_t for the moment, with some ugly
conversions in floppy.c.]

Inside the kernel, we aim for a kdev_t type that is a pointer
to a structure with information about the device (like major,
minor, size, blocksize, sectorsize, name, read-only flag,
struct file_operations etc.).

However, for the time being we let kdev_t be almost the same as dev_t:

typedef struct { unsigned short major, minor; } kdev_t;

Admissible operations on an object of type kdev_t:
- passing it along
- comparing it for equality with another such object
- storing it in inode->i_rdev or tty->device
- using its bit pattern as argument in a hash function
- finding its major and minor
- complaining about it

An object of type kdev_t is created only by the function MKDEV(),
with the single exception of the constant 0 (no device).

Right now the other information mentioned above is usually found
in static arrays indexed by major or major,minor.

An obstacle to immediately using
    typedef struct { ... (* lots of information *) } *kdev_t
is the case of mknod used to create a block device that the
kernel doesn't know about at present (but first learns about
when some module is inserted).

aeb - 950811
*/


/*
 * NOTE NOTE NOTE!
 *
 * The kernel-internal "kdev_t" will eventually have
 * 20 bits for minor numbers, and 12 bits for majors.
 *
 * HOWEVER, the external representation is still 8+8
 * bits, and there is no way to generate the extended
 * "kdev_t" format yet. Which is just as well, since
 * we still use "minor" as an index into various
 * static arrays, and they are sized for a 8-bit index.
 */
typedef struct {
	unsigned short value;
} kdev_t;

#define KDEV_MINOR_BITS		8
#define KDEV_MAJOR_BITS		8

#define __mkdev(major,minor)	(((major) << KDEV_MINOR_BITS) + (minor))

#define mk_kdev(major, minor)	((kdev_t) { __mkdev(major,minor) } )

/*
 * The "values" are just _cookies_, usable for 
 * internal equality comparisons and for things
 * like NFS filehandle conversion.
 */
static inline unsigned int kdev_val(kdev_t dev)
{
	return dev.value;
}

static inline kdev_t val_to_kdev(unsigned int val)
{
	kdev_t dev;
	dev.value = val;
	return dev;
}

#define HASHDEV(dev)	(kdev_val(dev))
#define NODEV		(mk_kdev(0,0))

static inline int kdev_same(kdev_t dev1, kdev_t dev2)
{
	return dev1.value == dev2.value;
}

#define kdev_none(d1)	(!kdev_val(d1))

/* Mask off the high bits for now.. */
#define minor(dev)	((dev).value & 0xff)
#define major(dev)	(((dev).value >> KDEV_MINOR_BITS) & 0xff)

/* These are for user-level "dev_t" */
#define MINORBITS	8
#define MINORMASK	((1U << MINORBITS) - 1)

#define MAJOR(dev)	((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi)	(((ma) << MINORBITS) | (mi))

/*
 * Conversion functions
 */

static inline int kdev_t_to_nr(kdev_t dev)
{
	return MKDEV(major(dev), minor(dev));
}

static inline kdev_t to_kdev_t(int dev)
{
	return mk_kdev(MAJOR(dev),MINOR(dev));
}

#else /* __KERNEL__ */

/*
Some programs want their definitions of MAJOR and MINOR and MKDEV
from the kernel sources. These must be the externally visible ones.
*/
#define MAJOR(dev)	((dev)>>8)
#define MINOR(dev)	((dev) & 0xff)
#define MKDEV(ma,mi)	((ma)<<8 | (mi))
#endif /* __KERNEL__ */
#endif
