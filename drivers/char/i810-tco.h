/*
 *	i810-tco 0.02:	TCO timer driver for i810 chipsets
 *
 *	(c) Copyright 2000 kernel concepts <nils@kernelconcepts.de>, All Rights Reserved.
 *				http://www.kernelconcepts.de
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 *	Neither kernel concepts nor Nils Faerber admit liability nor provide 
 *	warranty for any of this software. This material is provided 
 *	"AS-IS" and at no charge.	
 *
 *	(c) Copyright 2000	kernel concepts <nils@kernelconcepts.de>
 *				developed for
 *                              Jentro AG, Haar/Munich (Germany)
 *
 *	TCO timer driver for i810 chipsets
 *	based on softdog.c by Alan Cox <alan@redhat.com>
 *
 *      The TCO timer is implemented in the 82801AA (82801AB) chip,
 *	see intel documentation from http://developer.intel.com,
 *	order number 290655-003
 *
 *	For history see i810-tco.c
 */


/*
 * Some address definitions for the i810 TCO
 */

#define	TCOBASE		ACPIBASE + 0x60	/* TCO base address		*/
#define TCO1_RLD	TCOBASE + 0x00	/* TCO Timer Reload and Current Value */
#define TCO1_TMR	TCOBASE + 0x01	/* TCO Timer Initial Value	*/
#define	TCO1_DAT_IN	TCOBASE + 0x02	/* TCO Data In Register		*/
#define	TCO1_DAT_OUT	TCOBASE + 0x03	/* TCO Data Out Register	*/
#define	TCO1_STS	TCOBASE + 0x04	/* TCO1 Status Register		*/
#define	TCO2_STS	TCOBASE + 0x06	/* TCO2 Status Register		*/
#define TCO1_CNT	TCOBASE + 0x08	/* TCO1 Control Register	*/
#define TCO2_CNT	TCOBASE + 0x0a	/* TCO2 Control Register	*/

#define	SMI_EN		ACPIBASE + 0x30	/* SMI Control and Enable Register */
