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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: ts_kernel_cache.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_CACHE_H
#define _TS_KERNEL_CACHE_H

/*
  See docs/cache_coherency.txt for information on how and why to use
  this file.
*/

#if defined(PPC)
/* Get the definition of consistent_sync(): */
#include <asm/io.h>
/* Get the definitions of PCI_DMA_TODEVICE, PCI_DMA_FROMDEVICE: */
#include <linux/pci.h>
#endif

typedef enum {
  TS_DMA_TO_DEVICE,
  TS_DMA_FROM_DEVICE
} tTS_DMA_DIRECTION;

#if defined(PPC)
#  define tsKernelCacheSync(start, size, direction) \
    consistent_sync(start, size, \
                    (direction == TS_DMA_TO_DEVICE) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE)
#elif defined(i386) || defined(__ia64__) || defined(__x86_64__)
#  define tsKernelCacheSync(start, size, direction) do { } while(0)
#else
#  error tsKernelCacheSync not defined for this architecture
#endif

#endif /* _TS_KERNEL_CACHE_H */
