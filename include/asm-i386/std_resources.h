/*
 * include/asm-i386/std_resources.h
 */

#ifndef __ASM_I386_STD_RESOURCES_H
#define __ASM_I386_STD_RESOURCES_H

#include <linux/init.h>

void probe_roms(void) __init;
void request_graphics_resource(void) __init;
void request_standard_io_resources(void) __init;

#endif
