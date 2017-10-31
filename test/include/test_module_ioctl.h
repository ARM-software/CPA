/*
 * test_module_ioctl.h
 * Copyright (C) 2017 Arm Ltd.
 * SPDX-License-Identifier: GPL-2.0
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef __TEST_MODULE_IOCTL_H__
#define __TEST_MODULE_IOCTL_H__

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/fs.h>       /* file system operations */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int shared_fd;
	int mem_size;
	bool verify_result;			/* 0 means fail, 1 means success. */
} test_verify_args;

typedef struct {
	bool simulate_result;
	int alloc_times;
	int test_allocated_page;
	int system_free_pages;
	int simulate_page_unit_size;
} test_simulate_args;

#define IOC_BASE           0x82

#define TEST_IOCTL_VERIFY_CPA _IOWR(IOC_BASE, 1, test_verify_args)
#define TEST_IOCTL_START_SIMULATE_FRAGMENT _IOWR(IOC_BASE, 2, test_simulate_args)
#define TEST_IOCTL_FREE_ONE_PAGE_UNIT _IOWR(IOC_BASE, 3, test_simulate_args)
#define TEST_IOCTL_STOP_SIMULATE_FRAGMENT _IOWR(IOC_BASE, 4, test_simulate_args)

#ifdef __cplusplus
}
#endif

#endif /* __TEST_MODULE_IOCTL_H__ */
