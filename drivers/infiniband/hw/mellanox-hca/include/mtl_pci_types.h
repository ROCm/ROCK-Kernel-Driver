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
#include <mtl_types.h>

#define MOSAL_PCI_HEADER_TYPE1	0x01	/* Bridge type device */
#define MOSAL_PCI_HEADER_TYPE0  0x00    /* Normal type device */
         
         
#ifdef MT_LITTLE_ENDIAN

typedef struct {
    u_int16_t	vid;	     /* vendor ID */
    u_int16_t	devid;	     /* device ID */
    u_int16_t	cmd;	     /* command register */
    u_int16_t	status;		 /* status register */
    u_int8_t	revid;	     /* revision ID */
    u_int8_t	class_code;	 /* class code */
    u_int8_t	subclass;	 /* sub class code */
    u_int8_t	progif;		 /* programming interface */
    u_int8_t	cache_line;	 /* cache line */
    u_int8_t	latency;	 /* latency time */
    u_int8_t	header_type; /* header type */
    u_int8_t	bist;		 /* BIST */
    u_int32_t	base0;		 /* base address 0 */
    u_int32_t	base1;		 /* base address 1 */
    u_int32_t	base2;		 /* base address 2 */
    u_int32_t	base3;		 /* base address 3 */
    u_int32_t	base4;		 /* base address 4 */
    u_int32_t	base5;		 /* base address 5 */
    u_int32_t	cis;		 /* cardBus CIS pointer */
    u_int16_t	sub_vid;     /* sub system vendor ID */
    u_int16_t	sub_sysid;   /* sub system ID */
    u_int32_t	rom_base;	 /* expansion ROM base address */
    u_int32_t	reserved0;	 /* reserved */
    u_int32_t	reserved1;	 /* reserved */
    u_int8_t	int_line;	 /* interrupt line */
    u_int8_t	int_pin;	 /* interrupt pin */
    u_int8_t	min_grant;	 /* min Grant */
    u_int8_t	max_latency; /* max Latency */
} MOSAL_PCI_hdr_type0_t;



typedef struct {
    u_int16_t	vid;	            /* vendor ID */
    u_int16_t	devid;	            /* device ID */
    u_int16_t	cmd;	            /* command register */
    u_int16_t	status;		        /* status register  */
    u_int8_t	revid;	            /* revision ID */
    u_int8_t	class_code;	        /* class code  */
    u_int8_t	sub_class;	        /* sub class code */
    u_int8_t	progif;		        /* programming interface */
    u_int8_t	cache_line;	        /* cache line */
    u_int8_t	latency;	        /* latency time */
    u_int8_t	header_type;	    /* header type  */
    u_int8_t	bist;		        /* BIST */
    u_int32_t	base0;		        /* base address 0 */
    u_int32_t	base1;		        /* base address 1 */
    u_int8_t	pri_bus;		    /* primary bus number      */
    u_int8_t	sec_bus;		    /* secondary bus number    */
    u_int8_t	sub_bus;		    /* subordinate bus number  */
    u_int8_t	sec_latency;	    /* secondary latency timer */
    u_int8_t	iobase;		        /* IO base  */
    u_int8_t	iolimit;	        /* IO limit */
    u_int16_t	sec_status;	        /* secondary status */
    u_int16_t	mem_base;	        /* memory base  */
    u_int16_t	mem_limit;	        /* memory limit */
    u_int16_t	pre_base;	        /* prefetchable memory base  */
    u_int16_t	pre_limit;	        /* prefetchable memory limit */
    u_int32_t	pre_base_upper;	    /* prefetchable memory base upper 32 bits */
    u_int32_t	pre_limit_upper;    /* prefetchable memory base upper 32 bits */
    u_int16_t	io_base_upper;	    /* IO base upper 16 bits  */
    u_int16_t	io_limit_upper;	    /* IO limit upper 16 bits */
    u_int32_t	reserved;	        /* reserved */
    u_int32_t	rom_base;	        /* expansion ROM base address */
    u_int8_t	int_line;	        /* interrupt line */
    u_int8_t	int_pin;	        /* interrupt pin  */
    u_int16_t	control;        	/* bridge control */

} MOSAL_PCI_hdr_type1_t;

#else /* MT_BIG_ENDIAN */

typedef struct {
    
    u_int16_t	devid;	     /* device ID */
    u_int16_t	vid;	     /* vendor ID */
    
    u_int16_t	status;		 /* status register */
    u_int16_t	cmd;	     /* command register */
    
    u_int8_t	progif;		 /* programming interface */
    u_int8_t	subclass;	 /* sub class code */
    u_int8_t	class_code;	 /* class code */
    u_int8_t	revid;	     /* revision ID */

    u_int8_t	bist;		 /* BIST */
    u_int8_t	header_type; /* header type */
    u_int8_t	latency;	 /* latency time */
    u_int8_t	cache_line;	 /* cache line */
    
    u_int32_t	base0;		 /* base address 0 */
    u_int32_t	base1;		 /* base address 1 */
    u_int32_t	base2;		 /* base address 2 */
    u_int32_t	base3;		 /* base address 3 */
    u_int32_t	base4;		 /* base address 4 */
    u_int32_t	base5;		 /* base address 5 */
    
    u_int32_t	cis;		 /* cardBus CIS pointer */

    u_int16_t	sub_sysid;   /* sub system ID */
    u_int16_t	sub_vid;     /* sub system vendor ID */
    
    u_int32_t	rom_base;	 /* expansion ROM base address */
    u_int32_t	reserved0;	 /* reserved */
    u_int32_t	reserved1;	 /* reserved */

    u_int8_t	max_latency; /* max Latency */
    u_int8_t	min_grant;	 /* min Grant */
    u_int8_t	int_pin;	 /* interrupt pin */
    u_int8_t	int_line;	 /* interrupt line */

} MOSAL_PCI_hdr_type0_t;



typedef struct {
    u_int16_t	devid;	            /* device ID */
    u_int16_t	vid;	            /* vendor ID */
    
    u_int16_t	status;		        /* status register  */
    u_int16_t	cmd;	            /* command register */

    u_int8_t	progif;		        /* programming interface */
    u_int8_t	sub_class;	        /* sub class code */
    u_int8_t	class_code;	        /* class code  */
    u_int8_t	revid;	            /* revision ID */
    
    u_int8_t	bist;		        /* BIST */
    u_int8_t	header_type;	    /* header type  */
    u_int8_t	latency;	        /* latency time */
    u_int8_t	cache_line;	        /* cache line */
    
    
    u_int32_t	base0;		        /* base address 0 */
    u_int32_t	base1;		        /* base address 1 */
    
    u_int8_t	sec_latency;	    /* secondary latency timer */
    u_int8_t	sub_bus;		    /* subordinate bus number  */
    u_int8_t	sec_bus;		    /* secondary bus number    */
    u_int8_t	pri_bus;		    /* primary bus number      */
    
    u_int16_t	sec_status;	        /* secondary status */
    u_int8_t	iolimit;	        /* IO limit */
    u_int8_t	iobase;		        /* IO base  */

    u_int16_t	mem_limit;	        /* memory limit */
    u_int16_t	mem_base;	        /* memory base  */
    
    u_int16_t	pre_limit;	        /* prefetchable memory limit */
    u_int16_t	pre_base;	        /* prefetchable memory base  */

    u_int32_t	pre_base_upper;	    /* prefetchable memory base upper 32 bits */
    u_int32_t	pre_limit_upper;    /* prefetchable memory base upper 32 bits */

    u_int16_t	io_limit_upper;	    /* IO limit upper 16 bits */
    u_int16_t	io_base_upper;	    /* IO base upper 16 bits  */
    
    u_int32_t	reserved;	        /* reserved */
    u_int32_t	rom_base;	        /* expansion ROM base address */

    u_int16_t	control;        	/* bridge control */
    u_int8_t	int_pin;	        /* interrupt pin  */
    u_int8_t	int_line;	        /* interrupt line */

} MOSAL_PCI_hdr_type1_t;

#endif

typedef union {
    MOSAL_PCI_hdr_type0_t type0;
    MOSAL_PCI_hdr_type1_t type1;
} MOSAL_PCI_cfg_hdr_t;

