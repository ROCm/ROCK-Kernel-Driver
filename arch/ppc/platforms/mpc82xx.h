/*
 * arch/ppc/platforms/mpc82xx.h
 *
 * Board specific support for various 82xx platforms.
 *
 * Author: Allen Curtis <acurtis@onz.com>
 *
 * Copyright 2002 Ones and Zeros, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __CONFIG_82XX_PLATFORMS
#define __CONFIG_82XX_PLATFORMS

#ifdef CONFIG_8260

#ifdef CONFIG_EST8260
#include <platforms/est8260.h>
#endif

#ifdef CONFIG_SBS8260
#include <platforms/sbs8260.h>
#endif

#ifdef CONFIG_RPX6
#include <platforms/rpxsuper.h>
#endif

#ifdef CONFIG_WILLOW
#include <platforms/willow.h>
#endif

#ifdef CONFIG_TQM8260
#include <platforms/tqm8260.h>
#endif

#endif	/* CONFIG_8260 */

#endif
