/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/


/*
 * Save log at DRAM and not file system.
 */
#ifndef MT_KERNEL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#if defined( __LINUX__ ) || defined( __DARWIN__ )
#include <pthread.h>
#elif defined (__WIN__ )
#include <windows.h>
HANDLE log_lock;
#endif

#define MAX_FILENAME 256
#define MTL_LOG_ENV  "MTL_LOG"
#define MTL_LOG_DEF  "-"

#else  /* defined MT_KERNEL */

#  ifdef __DARWIN__
#    include <string.h>
#    include <sys/systm.h>
     /* The function is not supplied in kernel, so it is added 
      * here below for Darwin Kernel */
     static char *strstr (const char *phaystack, const char *pneedle);
#    define printk(...) IOLog(__VA_ARGS__)     
#  endif /* defined __DARWIN__ */

#endif /* MT_KERNEL */


/* Here we force this feature out */
#undef MTL_LOG_MALLOC

/* Take local header before getting it from the release. */
#include "sys/mtl_sys_defs.h"

#include "mtl_common.h"

#ifdef __WIN__
char *cur_module;
#endif


const char* mtl_strerror( call_result_t errnum)
{
    switch (errnum) {
#define INFO(A, B, C) case A: return C;
        ERROR_LIST
#undef INFO
        default: return "Unknown error";
    }
}

const char* mtl_strerror_sym( call_result_t errnum)
{
    switch (errnum) {
#define INFO(A, B, C) case A: return #A;
        ERROR_LIST
#undef INFO
        default: return "Unknown error";
    }
}

const char*  mtl_basename(const char* filename)
{
    const char*  slash_p = strchr(filename, '/');
    if (slash_p)
    {
        filename = slash_p + 1;
    }
    return filename;
} /* mtl_basename */


static MT_bool extract_key(const char *str, const char *key, const char *suffix,
                        char *result, const int result_max_len)
{
    char *p, *pstr = (char *)str;
    int  i=0;

    if (!pstr)
        return FALSE;

    while ((p = strstr(pstr, key)))
    {
        if (!strncmp(p + strlen(key), suffix, strlen(suffix)))
        {
            char *q = p+strlen(key)+strlen(suffix);
            while (*q && *q != ' ')
            {
                *result++ = *q++;
                if (++i >= result_max_len)
                    break;
            }
            *result = '\0';
            return TRUE;
        }
        else
            pstr = p + strlen(key);
    }
    return FALSE;
}

/************** Kernel Specific Code ************************/

#ifdef MT_KERNEL

static struct log_info {
    char            *name;       // Module name
    struct log_info *next;       // Pointer to next record
    struct print_info {
        char *name;
        char sevs[MAX_MTL_LOG_SEVERITIES+1];
    } print_info[MAX_MTL_LOG_TYPES];
} *log_first = (struct log_info *)NULL,
    log_default = {
        NULL, NULL,
        {
            { "trace", "" },
            { "debug", "" },
            { "error", "1234" }
        }
    };

//TODO: Bug: strlen run on user space buffer!
//static char *mtl_strdup(const char *str)
//    return s ? strcpy(s, str) : NULL;
//#define mtl_strdup(str) ({ char *s = (char*)MALLOC(strlen(str)+1); s? strcpy(s, (str)) : NULL; })
static __INLINE__ char *mtl_strdup(const char *str)
{
	char *s = (char*)MALLOC(strlen(str)+1); 
	return s? strcpy(s, (str)) : NULL; 
}


static int debug_print = 0;

void mtl_log_set(char* layer, char *info)
{
    struct log_info *p, *prev;

	printk("mtl_log_set: layer '%s', info '%s'\n", layer, info );
	
	if (!strcmp( layer, "mtl_log_dbg_print" )) {
		debug_print ^= 1;
		return;
	}

    if (strcmp(layer, "print"))
    {
        /*
         * Find necessary record
         */
        for(prev = p = log_first; p; prev = p, p = p->next)
        {
            if (!strcmp(layer, p->name))
                break;
        }
        if (!p)
        {
            /* Not found - create */
            /* Do not use MALLOC macro to avoid infinite recursion */
            p = (struct log_info *)QMALLOC(sizeof(struct log_info));
            memcpy(p, &log_default, sizeof(struct log_info));
            p->name = mtl_strdup(layer);
            if (prev)
                prev->next = p;
            else
                log_first = p;
        }
        /*
         * Now "p" is a pointer to corresponding record, either existed
         * or created just now
         */
        extract_key(info, "trace", ":", p->print_info[mtl_log_trace].sevs,
                    MAX_MTL_LOG_SEVERITIES);
        extract_key(info, "debug", ":", p->print_info[mtl_log_debug].sevs,
                    MAX_MTL_LOG_SEVERITIES);
        extract_key(info, "error",  ":",p->print_info[mtl_log_error].sevs,
                    MAX_MTL_LOG_SEVERITIES);

    }
    else
    {
        printk("<1>\n");
        printk("<1> Layers and severities for print\n");
        printk("<1> -------------------------------\n");
        for(p=log_first; p; p = p->next)
        {
            int i;
            
            printk("<1> Layer - \"%s\":\n", p->name);
            for (i=0; i<MAX_MTL_LOG_TYPES; i++)
            {
                printk("<1>     Name=\"%s\", severities=\"%s\"\n",
                       p->print_info[i].name, p->print_info[i].sevs);
            }
        }
        printk("<1>\n");
    }
}

/*
 *  mtl_common_cleanup
 */
void mtl_common_cleanup(void)
{
  struct log_info *p = log_first, *next;

  while ( p ) {
    if ( p->name ) {
      FREE(p->name);
      p->name = NULL;
    }
    next = p->next;
    FREE(p);
    p = next;
  }
}

void mtl_log(const char* layer, mtl_log_types log_type, char sev, 
             const char *fmt, ...)
{
    char            pbuff[MAX_MTL_LOG_LEN];
    struct log_info *p, *prev;
    va_list         ap;
	if (debug_print)
		printk("***** mtl_log:DEBUG: layer '%s', type %d, sev '%c'\n", layer, log_type, sev);

    /*
     * Find necessary record
     */
    for(prev = p = log_first; p; prev = p, p = p->next)
    {
        if (!strcmp(layer, p->name))
            break;
    }
 
    if (!p)
    {
		if (debug_print)
			printk("***** mtl_log:DEBUG: Not found layer '%s' - create\n", layer);
			
        // Not found - create
        // Avoid call to MALLOC to avoid infinite recursion
        p = (struct log_info *)QMALLOC(sizeof(struct log_info));
        if (p == NULL) {
            /* malloc failure. just return */
            return;
        }
        memcpy(p, &log_default, sizeof(struct log_info));
        p->name = mtl_strdup(layer);
        if (p->name == NULL) {
            FREE(p);
            return;
        }
        if (prev)
            prev->next = p;
        else
            log_first = p;
    }
	else
	{
		if (debug_print)
			printk("***** mtl_log:DEBUG: Found layer '%s', Name=\"%s\", sev=\"%s\"\n", 
				p->name, p->print_info[log_type].name, p->print_info[log_type].sevs );
	}

    /*
     * Now "p" is a pointer to corresponding record, either existed
     * or created just now
     */


    /*
     * Log printing
     */
    if (strchr(p->print_info[log_type].sevs, sev))
    {
		if (debug_print)
			printk("***** mtl_log:DEBUG: print string\n" );
        va_start (ap, fmt);
        vsprintf (pbuff, fmt, ap);
        va_end (ap);
        printk("<1> %s(%c): %s", layer, sev, pbuff);
    }
}

/************************************************************************/
/*           Kernel memory allocation logging/debugging                 */

#if 0
/* Alas, we do not have such machinery for the kernel (yet?) */

/* #include <execinfo.h> */
static const char*  getBackTrace(char* buf, int bufLen)
{
   enum    {Depth = 5};
   char*   pBuf = buf;
   void*   array[Depth];
   size_t  size = backtrace(array, Depth);
   if (size > 0)
   {
      char*   pBufLimit = buf + bufLen - 2;
      char**  syms = backtrace_symbols(array, size);
      char**  symsEnd = syms += size;
      char*   sym = syms[0];
      char*   pBufNext = pBuf + strlen(sym);
      while ((syms != symsEnd) && (pBufNext < pBufLimit))
      {
         strcpy(pBuf, sym);
         pBuf = pBufNext;
         *pBuf++ = '|'; 
         ++syms;
         if (syms != symsEnd)
         {
            sym = *syms;
            pBufNext += strlen(sym);
         }
      }
   }
   *pBuf = '\0';
   return buf;
} /* getBackTrace */
#endif

static const char*  mtl_malloc_modname = "MEMCHECK";
static const char*  mallog_magic    = "takeme4memcheck";

void*  mtl_log_vmalloc(const char* fn, int ln, int bsize)
{
   /*  char   symTrace[256]; 
    *  getBackTrace(symTrace, sizeof(symTrace));  */
   void*  ptr = (void*)QVMALLOC(bsize);
   mtl_log(mtl_malloc_modname, mtl_log_debug, '1', 
           "%s[%d]: 0x%p := vmalloc(%d) %s %s\n", 
           fn, ln, ptr, bsize, mallog_magic, "");
   return ptr;
}

void   mtl_log_vfree(const char* fn, int ln, void* ptr)
{
   mtl_log(mtl_malloc_modname, mtl_log_debug, '1', 
           "%s[%d]: vfree(0x%p) %s\n", fn, ln, ptr, mallog_magic);
   QVFREE(ptr);
}

void*  mtl_log_kmalloc(const char* fn, int ln, int bsize, unsigned g)
{
   /*  char   symTrace[256];
    *  getBackTrace(symTrace, sizeof(symTrace));  */
   void*  ptr = (void*)QCMALLOC(bsize, g);
   mtl_log(mtl_malloc_modname, mtl_log_debug, '1', 
           "%s[%d]: 0x%p := kmalloc(%d, 0x%x) %s %s\n", 
           fn, ln, ptr, bsize, g, mallog_magic, "");
   return ptr;
}

void   mtl_log_kfree(const char* fn, int ln, void *ptr)
{
   mtl_log(mtl_malloc_modname, mtl_log_debug, '1', 
           "%s[%d]: kfree(0x%p) %s\n", fn, ln, ptr, mallog_magic);
   QFREE(ptr);
}

/*                                                                      */
/************************************************************************/
#else /* not defined MT_KERNEL */

/************** User Specific Code ************************/

static struct log_info {
    char            *name;       // Module name
    FILE            *fp;         // File for output
    int             first;       // first usage == 1
    struct log_info *next;       // Pointer to next record
    struct print_info {
        char *name;
        char sevs[MAX_MTL_LOG_SEVERITIES+1];
    } print_info[MAX_MTL_LOG_TYPES];
} *log_first = (struct log_info *)NULL,
    log_default = {
        NULL, NULL, 1, NULL,
        {
            { "trace", "" },
            { "debug", "" },
            { "error", "" }
        }
    };

static FILE *open_logfile(const char *fname)
{
    FILE *rc;

    if (!strcmp(fname, "-") ||  !strcmp(fname, "&"))
        rc = stderr;
    else if (!strcmp(fname, ">"))
        rc = stdout;
    else if ((rc = fopen(fname, "w")) == NULL)
    {
        fprintf(stderr, "Can't open \"%s\" - %s\nUse stderr instead.\n",
                fname, strerror(errno));
        rc = stderr;
    }
    return rc;
}

void mtl_log_DB_print()
{
    struct log_info *p;
    printf("<1>\n");
    printf("<1> Layers and severities for print\n");
    printf("<1> -------------------------------\n");
    for(p=log_first; p; p = p->next)
    {
        int i;
        
        printf("<1> Layer - \"%s\":\n", p->name);
        for (i=0; i<MAX_MTL_LOG_TYPES; i++)
        {
            printf("<1>     Name=\"%s\", severities=\"%s\"\n",
                   p->print_info[i].name, p->print_info[i].sevs);
        }
    }
    printf("<1>\n");
}

void mtl_log(const char* layer, mtl_log_types log_type, char sev, 
             const char *fmt, ...)
{
    static char  *mtl_log_env;
    static int   first=1;
#if defined( __LINUX__ )
    static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

    char            fname[MAX_FILENAME+1];
    struct log_info *p, *prev;
    va_list         ap;

#if defined( __LINUX__ )
    pthread_mutex_lock(&log_lock);
#elif defined( __WIN__ )
    if ( log_lock != NULL)
      WaitForSingleObject( log_lock, INFINITE );
#endif

    /*
     * Initialization
     */
    if (first)
    {
        first = 0;
        mtl_log_env = getenv(MTL_LOG_ENV);

        // Now fill in default record
        if (!extract_key(mtl_log_env, "file", ":", fname, MAX_FILENAME))
            strcpy(fname, MTL_LOG_DEF);
        log_default.fp = open_logfile(fname);
        extract_key(mtl_log_env, "trace", ":",
                    log_default.print_info[mtl_log_trace].sevs,
                    MAX_MTL_LOG_SEVERITIES);
        extract_key(mtl_log_env, "debug", ":",
                    log_default.print_info[mtl_log_debug].sevs,
                    MAX_MTL_LOG_SEVERITIES);
        extract_key(mtl_log_env, "error",  ":",
                    log_default.print_info[mtl_log_error].sevs,
                    MAX_MTL_LOG_SEVERITIES);

    }


    /*
     * Find necessary record
     */
    for(prev = p = log_first; p; prev = p, p = p->next)
    {
        if (!strcmp(layer, p->name))
            break;
    }
    if (!p)
    {
        // Not found - create
        p = malloc(sizeof(struct log_info));
        if (p == NULL) {
            /* malloc failure. just return */
#if defined( __LINUX__ )
            pthread_mutex_unlock(&log_lock);
#elif defined( __WIN__  )
		if ( log_lock != NULL )
			ReleaseMutex( log_lock );
#endif
            return;
        }
        memcpy(p, &log_default, sizeof(struct log_info));
        p->name = strdup(layer);
        if (prev)
            prev->next = p;
        else
            log_first = p;
    }
    /*
     * Now "p" is a pointer to corresponding record, either existed
     * or created just now
     */


    /*
     * First time for this module - check overwriting info in mtl_log_env
     */
    if (p->first)
    {
        p->first = 0;
        if (extract_key(mtl_log_env, layer, "_file:", fname, MAX_FILENAME))
            p->fp = open_logfile(fname);
        extract_key(mtl_log_env, layer, "_trace:", p->print_info[mtl_log_trace].sevs,
                    MAX_MTL_LOG_SEVERITIES);
        extract_key(mtl_log_env, layer, "_debug:", p->print_info[mtl_log_debug].sevs,
                    MAX_MTL_LOG_SEVERITIES);
        extract_key(mtl_log_env, layer, "_error:", p->print_info[mtl_log_error].sevs,
                    MAX_MTL_LOG_SEVERITIES);
    }

    /*
     * Log printing
     */
    if (strchr(p->print_info[log_type].sevs, sev))
    {
        fprintf(p->fp, "%s(%c): ", layer, sev);
        va_start (ap, fmt);
        vfprintf (p->fp, fmt, ap);
        va_end (ap);
        fflush(p->fp);
    }

#if defined( __LINUX__ )
    pthread_mutex_unlock(&log_lock);
#elif defined( __WIN__  )
	if ( log_lock != NULL )
		ReleaseMutex( log_lock );
#endif
}


#endif /* __KERNEL__ */



#if defined(__WIN__) || defined(VXWORKS_OS)

#define MAX_MOD_NAME_LEN						32

/*
 * The only reason why mt_strtoull function is here
 * - I didn't find such function in MSDN library.
 *
 * The code of mt_strtoull borrowed from glibc library
 * with small changes
 */
/* Convert NPTR to an `unsigned long int' or `long int' in base BASE.
   If BASE is 0 the base is determined by the presence of a leading
   zero, indicating octal or a leading "0x" or "0X", indicating hexadecimal.
   If BASE is < 2 or > 36, it is reset to 10.
   If ENDPTR is not NULL, a pointer to the character after the last
   one converted is stored in *ENDPTR.  */
   
u_int64_t mt_strtoull (const char *nptr, char **endptr, int base)
{
    int           overflow, negative;
    u_int64_t     i, cutlim, cutoff;
    const char    *s;
    unsigned char c;
    const char    *save;

    if (base < 0 || base == 1 || base > 36)
        base = 10;

    s = nptr;

    /* Skip white space.  */
    while (isspace (*s))
        ++s;
    if (*s == '\0')
        goto noconv;

    /* Check for a sign.  */
    if (*s == '-')
    {
        negative = 1;
        ++s;
    }
    else if (*s == '+')
    {
        negative = 0;
        ++s;
    }
    else
        negative = 0;

    if (base == 16 && s[0] == '0' && toupper (s[1]) == 'X')
        s += 2;

    /* If BASE is zero, figure it out ourselves.  */
    if (base == 0)
    {
        if (*s == '0')
        {
            if (toupper (s[1]) == 'X')
            {
                s += 2;
                base = 16;
            }
            else
                base = 8;
        }
        else
            base = 10;
    }

    /* Save the pointer so we can check later if anything happened.  */
    save = s;

    cutoff = _UI64_MAX / (u_int32_t) base;
    cutlim = _UI64_MAX % (u_int32_t) base;

    overflow = 0;
    i = 0;
    for (c = *s; c != '\0'; c = *++s)
    {
        if (isdigit (c))
            c -= '0';
        else if (isalpha (c))
            c = toupper (c) - 'A' + 10;
        else
            break;
        if (c >= base)
            break;
        /* Check for overflow.  */
        if (i > cutoff || (i == cutoff && c > cutlim))
            overflow = 1;
        else
        {
            i *= (unsigned long int) base;
            i += c;
        }
    }

    /* Check if anything actually happened.  */
    if (s == save)
        goto noconv;

    /* Store in ENDPTR the address of one character
       past the last character we converted.  */
    if (endptr != NULL)
        *endptr = (char *) s;

    if (overflow)
        return _UI64_MAX;

    /* Return the result of the appropriate sign.  */
    return (negative ? (u_int64_t)(-(int64_t)i) : i);

noconv:
    /* There was no number to convert.  */
    if (endptr != NULL)
        *endptr = (char *) nptr;
    return 0L;
}

#endif 

#if defined(__WIN__)

#ifdef __KERNEL__

#include <wdm.h>

static KSPIN_LOCK s_mod_name_sp;
static char s_mod_name[MAX_MOD_NAME_LEN];
static KIRQL s_irql=0;
static KIRQL s_irql_synch=0;

#ifdef USE_RELAY_MOD_NAME
#define NT_CALL_MT_LOG(sev,type)			\
	char mod_name[MAX_MOD_NAME_LEN];		\
    char            pbuff[MAX_MTL_LOG_LEN];	\
    va_list         ap;						\
    mtl_log_get_name(mod_name); \
    va_start (ap, fmt);						\
    vsprintf (pbuff, fmt, ap);				\
    va_end (ap);							\
	mtl_log( mod_name, type, sev, "%s", pbuff)
#else
#define NT_CALL_MT_LOG(sev,type)			\
    char            pbuff[MAX_MTL_LOG_LEN];	\
    va_list         ap;						\
    va_start (ap, fmt);						\
    vsprintf (pbuff, fmt, ap);				\
    va_end (ap);							\
	mtl_log( MAKE_MOD_NAME, type, sev, "%s", pbuff)
#endif


NTKERNELAPI
KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch (
    PKSPIN_LOCK SpinLock
    );

void mtl_log_set_name( char * mod_name )
{
#ifndef USE_RELAY_MOD_NAME
	DbgPrint( "mtl_log_set_name: can't be here, irql = %d!!!\n", KeGetCurrentIrql());
	ASSERT(0);
#endif
	s_irql = KeAcquireSpinLockRaiseToSynch(&s_mod_name_sp);	
    	strcpy( s_mod_name, mod_name );
}

static __inline void mtl_log_get_name( char * mod_name )
{
	KIRQL irql = KeGetCurrentIrql();
#ifndef USE_RELAY_MOD_NAME
	DbgPrint( "mtl_log_set_name: can't be here, irql = %d!!!\n", irql);
	ASSERT(0);
#endif
    	if (irql != s_irql_synch) {
    		DbgPrint( "MDT.SYS: mtl_log_get_name: WARNING: unexpected  current IRQL (%d), new (=prev) irql = %d!!\n", irql, s_irql);
    		ASSERT(0);
	}
	irql = s_irql;	
    	strcpy( mod_name, s_mod_name );			
    	s_irql = 0;
    	KeReleaseSpinLock(&s_mod_name_sp, irql );	
}

NTSTATUS DllInitialize(PUNICODE_STRING RegistryPath)
{
#ifdef USE_RELAY_MOD_NAME
	KIRQL irql;
	// init spinlock
	KeInitializeSpinLock(&s_mod_name_sp);
	// find SYNC IRQL
	irql = KeAcquireSpinLockRaiseToSynch(&s_mod_name_sp);	
	s_irql_synch = KeGetCurrentIrql();
    	KeReleaseSpinLock(&s_mod_name_sp, irql );	
#endif

	DbgPrint("\n***** MTL_COMMON_KL: DllInitialize()");
	return STATUS_SUCCESS;
}

NTSTATUS DllUnload()
{
	DbgPrint("\n***** MTL_COMMON_KL: DllUnload()");
	return STATUS_SUCCESS;
}

NTSTATUS 
DriverEntry(
	IN	PDRIVER_OBJECT	pi_pDriverObject,
	IN	PUNICODE_STRING pi_pRegistryPath
	)
{ /* DriverEntry */

	DbgPrint("\n***** MTL_COMMON_KL: DriverEntry()");
	return STATUS_SUCCESS;

} /* DriverEntry */


#else  /* ifdef __KERNEL__ */

/* ----- User Space ----- */


#include <windows.h>

int s_mod_name_init = 1;
HANDLE s_mod_name_mutex = NULL;
char s_mod_name[MAX_MOD_NAME_LEN];

#ifdef USE_RELAY_MOD_NAME
#define NT_CALL_MT_LOG(sev,type)			\
	char mod_name[MAX_MOD_NAME_LEN];		\
    char            pbuff[MAX_MTL_LOG_LEN];	\
    va_list         ap;						\
    mtl_log_get_name(mod_name); \
    va_start (ap, fmt);						\
    vsprintf (pbuff, fmt, ap);				\
    va_end (ap);							\
	mtl_log( mod_name, type, sev, "%s", pbuff)
#else
#define NT_CALL_MT_LOG(sev,type)			\
    char            pbuff[MAX_MTL_LOG_LEN];	\
    va_list         ap;						\
    va_start (ap, fmt);						\
    vsprintf (pbuff, fmt, ap);				\
    va_end (ap);							\
	mtl_log( MAKE_MOD_NAME, type, sev, "%s", pbuff)
#endif

void mtl_log_get_name( char * mod_name )
{
#ifndef USE_RELAY_MOD_NAME
	printf( "MTL_COMMON: mtl_log_get_name: error in build - we can't get here !!!\n");
#endif
	strcpy( mod_name, s_mod_name );			
	if (s_mod_name_mutex != NULL) 				
		ReleaseMutex( s_mod_name_mutex );		
}

void mtl_log_set_name( char * mod_name )
{
#ifndef USE_RELAY_MOD_NAME
	printf( "MTL_COMMON: mtl_log_set_name: error in build - we can't get here !!!\n");
#endif
	if (s_mod_name_init) {
		s_mod_name_mutex = CreateMutex(
 		NULL, 					// default security descriptor
  		FALSE,                  // not to acquire on creation
  		"MOD_NAME_MUTEX"					// object name: unnamed
		);
		if (s_mod_name_mutex == NULL) {
		  mtl_log( "MTLCOMMON", mtl_log_error, 1, "(mtl_log_set_name) CreateMutex failed (0x%x)\n", GetLastError() );
		}
		s_mod_name_init = 0;
	}

	if (s_mod_name_mutex != NULL) {
		WaitForSingleObject(
			s_mod_name_mutex,       // handle to object
  			INFINITE			// time-out interval
		);
	}
   	strcpy( s_mod_name, mod_name );			
}


BOOL APIENTRY DllMain( HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
					 )
{
	BOOL			l_fRetCode = TRUE;

    switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			log_lock = CreateMutex( NULL, FALSE, NULL	);
			break;

		case DLL_PROCESS_DETACH:
			if (log_lock != NULL)
				CloseHandle(log_lock);
			break;
		
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		break;
    }
    return l_fRetCode;
}

VOID 
DebugPrint(
	IN PUCHAR	pi_szFormat,
	...
	)
/*++

Routine Description:
    Writes a formatted ( printf() like ) string to output

Arguments:

	pi_nDbgLogLevel...... Level of debugging log.
	pi_szFormat.......... The format of the log.

Return Value:

	None .

--*/
{ /* DebugPrint */

	/* Log buffer for DebugPrint */
	static UCHAR	l_vLogBuff[ 1024 ];
	/* Formatted string length */
	int				l_nStrLen ;

	/* Variable argument list */    
	va_list l_Argptr;

	/* Init the variable argument list */   
	va_start(l_Argptr, pi_szFormat);

	/* Build the formatted string */
	l_nStrLen = vsprintf(&l_vLogBuff[0] , pi_szFormat , l_Argptr);

	/* If debug mode , print to debug window*/
	OutputDebugString(l_vLogBuff);

	/* Term the variable argument list */   
	va_end(l_Argptr);

} /* DebugPrint */

#endif  /* ifdef __KERNEL__ */

#if defined(_M_IX86)
#pragma message( "***** The code is being built for __i386__ architecture *****" )
#elif defined(_M_IA64)
#pragma message( "***** The code is being built for __ia64__ architecture *****" )
#elif defined(_M_AMD64)
#pragma message( "***** The code is being built for __amd64__ architecture *****" )
#else
#error Platform is not supported yet
#endif


void NT_trace(char sev, char *fmt, ...) { NT_CALL_MT_LOG(sev,mtl_log_trace); }
void NT_trace1(char *fmt, ...) { NT_CALL_MT_LOG('1',mtl_log_trace); }
void NT_trace2(char *fmt, ...) { NT_CALL_MT_LOG('2',mtl_log_trace); }
void NT_trace3(char *fmt, ...) { NT_CALL_MT_LOG('3',mtl_log_trace); }
void NT_trace4(char *fmt, ...) { NT_CALL_MT_LOG('4',mtl_log_trace); }
void NT_trace5(char *fmt, ...) { NT_CALL_MT_LOG('5',mtl_log_trace); }
void NT_trace6(char *fmt, ...) { NT_CALL_MT_LOG('6',mtl_log_trace); }
void NT_trace7(char *fmt, ...) { NT_CALL_MT_LOG('7',mtl_log_trace); }
void NT_trace8(char *fmt, ...) { NT_CALL_MT_LOG('8',mtl_log_trace); }
void NT_trace9(char *fmt, ...) { NT_CALL_MT_LOG('9',mtl_log_trace); }
void NT_debug(char sev, char *fmt, ...) { NT_CALL_MT_LOG(sev,mtl_log_debug); }
void NT_debug1(char *fmt, ...) { NT_CALL_MT_LOG('1',mtl_log_debug); }
void NT_debug2(char *fmt, ...) { NT_CALL_MT_LOG('2',mtl_log_debug); }
void NT_debug3(char *fmt, ...) { NT_CALL_MT_LOG('3',mtl_log_debug); }
void NT_debug4(char *fmt, ...) { NT_CALL_MT_LOG('4',mtl_log_debug); }
void NT_debug5(char *fmt, ...) { NT_CALL_MT_LOG('5',mtl_log_debug); }
void NT_debug6(char *fmt, ...) { NT_CALL_MT_LOG('6',mtl_log_debug); }
void NT_debug7(char *fmt, ...) { NT_CALL_MT_LOG('7',mtl_log_debug); }
void NT_debug8(char *fmt, ...) { NT_CALL_MT_LOG('8',mtl_log_debug); }
void NT_debug9(char *fmt, ...) { NT_CALL_MT_LOG('9',mtl_log_debug); }
void NT_error(char sev, char *fmt, ...) { NT_CALL_MT_LOG(sev,mtl_log_error); }
void NT_error1(char *fmt, ...) { NT_CALL_MT_LOG('1',mtl_log_error); }
void NT_error2(char *fmt, ...) { NT_CALL_MT_LOG('2',mtl_log_error); }
void NT_error3(char *fmt, ...) { NT_CALL_MT_LOG('3',mtl_log_error); }
void NT_error4(char *fmt, ...) { NT_CALL_MT_LOG('4',mtl_log_error); }
void NT_error5(char *fmt, ...) { NT_CALL_MT_LOG('5',mtl_log_error); }
void NT_error6(char *fmt, ...) { NT_CALL_MT_LOG('6',mtl_log_error); }
void NT_error7(char *fmt, ...) { NT_CALL_MT_LOG('7',mtl_log_error); }
void NT_error8(char *fmt, ...) { NT_CALL_MT_LOG('8',mtl_log_error); }
void NT_error9(char *fmt, ...) { NT_CALL_MT_LOG('9',mtl_log_error); }


#endif /* defined(__WIN__)*/

#if defined(__DARWIN__) && defined(MT_KERNEL)

/* copy paste function implementation for strstr */
static char *strstr (const char *phaystack, const char *pneedle)
{
  const unsigned char *haystack, *needle;
  char  b, c;
  
  haystack = (const unsigned char *) phaystack;
  needle = (const unsigned char *) pneedle;
  
  b = *needle;
  if (b != '\0')
    {
      haystack--;
      do
	{
	  c = *++haystack;
	  if (c == '\0')
	    goto ret0;
	}
      while (c != b);
      
      c = *++needle;
      if (c == '\0')
	goto foundneedle;
      ++needle;
      goto jin;

      for (;;)
        {
          char a;
	  const unsigned char *rhaystack, *rneedle;
	  
	  do
	    {
	      a = *++haystack;
	      if (a == '\0')
		goto ret0;
	      if (a == b)
		break;
	      a = *++haystack;
	      if (a == '\0')
		goto ret0;
shloop:;    }
          while (a != b);

jin:	  a = *++haystack;
	  if (a == '\0')
	    goto ret0;

	  if (a != c)
	    goto shloop;

	  rhaystack = haystack-- + 1;
	  rneedle = needle;
	  a = *rneedle;

	  if (*rhaystack == a)
	    do
	      {
		if (a == '\0')
		  goto foundneedle;
		++rhaystack;
		a = *++needle;
		if (*rhaystack != a)
		  break;
		if (a == '\0')
		  goto foundneedle;
		++rhaystack;
		a = *++needle;
	      }
	    while (*rhaystack == a);

	  needle = rneedle;	

	  if (a == '\0')
	    break;
        }
    }
foundneedle:
  return (char*) haystack;
ret0:
  return 0;
}

#endif

