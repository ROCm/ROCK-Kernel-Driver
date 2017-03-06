/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "kfd_priv.h"

/* Mapping queue priority to pipe priority, indexed by queue priority */
int pipe_priority_map[] = {
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_MEDIUM,
	KFD_PIPE_PRIORITY_CS_MEDIUM,
	KFD_PIPE_PRIORITY_CS_MEDIUM,
	KFD_PIPE_PRIORITY_CS_MEDIUM,
	KFD_PIPE_PRIORITY_CS_HIGH,
	KFD_PIPE_PRIORITY_CS_HIGH,
	KFD_PIPE_PRIORITY_CS_HIGH,
	KFD_PIPE_PRIORITY_CS_HIGH,
	KFD_PIPE_PRIORITY_CS_HIGH
};

/* Mapping queue priority to SPI priority, indexed by queue priority
 * SPI priority 2 and 3 are reserved for trap handler context save
 */
int spi_priority_map[] = {
	KFD_SPI_PRIORITY_EXTRA_LOW,
	KFD_SPI_PRIORITY_EXTRA_LOW,
	KFD_SPI_PRIORITY_EXTRA_LOW,
	KFD_SPI_PRIORITY_EXTRA_LOW,
	KFD_SPI_PRIORITY_EXTRA_LOW,
	KFD_SPI_PRIORITY_EXTRA_LOW,
	KFD_SPI_PRIORITY_EXTRA_LOW,
	KFD_SPI_PRIORITY_EXTRA_LOW,
	KFD_SPI_PRIORITY_LOW,
	KFD_SPI_PRIORITY_LOW,
	KFD_SPI_PRIORITY_LOW,
	KFD_SPI_PRIORITY_LOW,
	KFD_SPI_PRIORITY_LOW,
	KFD_SPI_PRIORITY_LOW,
	KFD_SPI_PRIORITY_LOW,
	KFD_SPI_PRIORITY_LOW
};

struct mqd_manager *mqd_manager_init(enum KFD_MQD_TYPE type,
					struct kfd_dev *dev)
{
	switch (dev->device_info->asic_family) {
	case CHIP_KAVERI:
		return mqd_manager_init_cik(type, dev);
	case CHIP_HAWAII:
		return mqd_manager_init_cik_hawaii(type, dev);
	case CHIP_CARRIZO:
		return mqd_manager_init_vi(type, dev);
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
		return mqd_manager_init_vi_tonga(type, dev);
	}

	return NULL;
}
