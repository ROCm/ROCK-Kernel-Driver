/* Generated automatically by configure */
#ifdef HAVE_DRIVE_LETTERS
  #define G10_LOCALEDIR     "c:\\lib\\gnupg\\locale"
  #define GNUPG_LIBDIR      "c:\\lib\\gnupg"
  #define GNUPG_LIBEXECDIR  "c:\\lib\\gnupg"
  #define GNUPG_DATADIR     "c:\\lib\\gnupg"
  #define GNUPG_HOMEDIR     "c:\\gnupg"
#else
  #define G10_LOCALEDIR     "/usr/local/share/locale"
  #define GNUPG_LIBDIR      "/usr/local/lib/gnupg"
  #define GNUPG_DATADIR     "/usr/local/share/gnupg"
  #ifdef __VMS
    #define GNUPG_HOMEDIR "/SYS$LOGIN/gnupg"
  #else
    #define GNUPG_HOMEDIR "~/.gnupg"
  #endif
#endif
/* those are here to be redefined by handcrafted g10defs.h.
   Please note that the string version must not contain more
   than one character because the using code assumes strlen()==1 */
#ifdef HAVE_DOSISH_SYSTEM
#define DIRSEP_C '\\'
#define EXTSEP_C '.'
#define DIRSEP_S "\\"
#define EXTSEP_S "."
#else
#define DIRSEP_C '/'
#define EXTSEP_C '.'
#define DIRSEP_S "/"
#define EXTSEP_S "."
#endif
/* This file defines some basic constants for the MPI machinery.  We
 * need to define the types on a per-CPU basis, so it is done with
 * this file here.  */
#define BYTES_PER_MPI_LIMB  (SIZEOF_UNSIGNED_LONG)






