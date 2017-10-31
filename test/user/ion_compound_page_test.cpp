/*
 * ion_compound_page_test.cpp
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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <linux/ion.h>
#include <ion/ion.h>
#include <sys/ioctl.h>

#include "test_module_ioctl.h"

#define TEST_DEV_PATH "/dev/test_cpa"
#define AERR(fmt, args...) __android_log_print(ANDROID_LOG_ERROR, "[Test-CPA-ERROR]", "%s:%d " fmt,__func__,__LINE__,##args)

static int ion_client = 0;
static int test_handle = 0;

int test_initialize()
{
	ion_client = ion_open();

	if (ion_client < 0)
	{
		AERR("ion_open failed.");
		return -1;
	}

	test_handle = open(TEST_DEV_PATH, O_RDWR);

	if (test_handle < 0)
	{
		AERR("Open test device failed.");

		if (0 != ion_close(ion_client))
		{
			AERR("Close ion_client failed in test_initialize.");
		}

		return -1;
	}

	return 0;
}

int test_uninitialize()
{
	if (0 != ion_close(ion_client))
	{
		AERR("Failed to close ion_client");
	}
	if (0 != close(test_handle))
	{
		AERR("Failed to close test_handle");
	}

	return 0;
}

int test_allocate_from_CPA(size_t size)
{
	ion_user_handle_t ion_hnd = -1;
	int shared_fd, ret;

	if (size <=0)
	{
		return -1;
	}

#if defined(ION_HEAP_TYPE_COMPOUND_PAGE_MASK)
	ret = ion_alloc(ion_client, size, 0, ION_HEAP_TYPE_COMPOUND_PAGE_MASK, 0, &ion_hnd);
#else
	ret = -1;
#endif

	if (ret < 0)
	{
		AERR("ion_alloc failed");
		return -1;
	}

	ret = ion_share(ion_client, ion_hnd, &shared_fd );
	if (0 != ret)
	{
		AERR("ion_share failed");
		shared_fd = -1;
	}

	ret = ion_free(ion_client, ion_hnd);
	if (0 != ret)
	{
		AERR("ion_free failed");

		if (-1 != shared_fd && 0 != close(shared_fd))
		{
			AERR("Close shared_fd failed in test_allocate_from_CPA.");
		}

		return -1;
	}

	return shared_fd;
}

void test_free_CPA_mem(int fd)
{
	if (fd <= 0)
	{
		return;
	}

	if (0 != close(fd))
	{
		AERR("Close fd failed in test_free_CPA_mem.");
	}
}

bool test_verify_allocated_buffer(int shared_fd, int mem_size)
{
	test_verify_args args;

	if (shared_fd <= 0)
	{
		AERR("Invalid shared fd.");
		return false;
	}

	args.shared_fd = shared_fd;
	args.mem_size = mem_size;

	if (0 != ioctl(test_handle, TEST_IOCTL_VERIFY_CPA, &args))
	{
		AERR("ioctl: test verify failed.");
		return false;
	}

	return (bool)args.verify_result;
}

bool test_start_simulate_memory_fragment(int &free_pages, int &allocated_pages, int &simulate_page_unit_size)
{
	test_simulate_args args;

	if (0 != ioctl(test_handle, TEST_IOCTL_START_SIMULATE_FRAGMENT, &args))
	{
		AERR("ioctl: test start simulate failed.");
		return false;
	}

	free_pages = args.system_free_pages;
	allocated_pages = args.test_allocated_page;
	simulate_page_unit_size = args.simulate_page_unit_size;

	return args.simulate_result;
}

bool test_free_one_simulate_page_unit(int &free_pages, int &allocated_pages)
{
	test_simulate_args args;

	if (0 != ioctl(test_handle, TEST_IOCTL_FREE_ONE_PAGE_UNIT, &args))
	{
		AERR("ioctl: test free simulate failed.");
		return false;
	}

	free_pages = args.system_free_pages;
	allocated_pages = args.test_allocated_page;

	return args.simulate_result;
}

bool test_stop_simulate_memory_fragment()
{
	test_simulate_args args;

	if (0 != ioctl(test_handle, TEST_IOCTL_STOP_SIMULATE_FRAGMENT, &args))
	{
		AERR("ioctl: test stop simulate failed.");
		return false;
	}

	return args.simulate_result;
}


#define TEST_ALLOC_NUM 5
#define TEST_ALLOC_FAIL_TIMES 200
#define TEST_ALLOC_DEFAULT_SIZE 2*1024*1024
#define TEST_IGNORE(x) (void)x

/* pre-defined memory size we want to test. */
static size_t mem_size_arr[TEST_ALLOC_NUM] = {1024, 1024*1024, 2*1024*1024, 2*1024*1014+3*1024, 64*1024*1024};

int main(int argc, char** argv)
{
	int shared_fd;
	int i;
	int allocated_buffer_handle[8192];    //support 16GB maximum system memory
	int allocated_buffer_num = 0;
	int failed_times = 0;
	int first_failed_times = 0, second_failed_times = 0;
	int system_free_pages, test_allocated_pages, simulate_page_unit_size;

	TEST_IGNORE(argc);
	TEST_IGNORE(argv);

	printf("CPA test start!!!\n");

	if (0 != test_initialize())
	{
		printf("!!!!!Failed to initialize test env.!!!!!\n");
		return -1;
	}

	printf("\n===================Test 1 START===================.\n");
	for (i = 0; i < TEST_ALLOC_NUM; i++)
	{
		printf("%d. Verify CPA, ", i+1);

		shared_fd = test_allocate_from_CPA(mem_size_arr[i]);
		if (shared_fd <= 0)
		{
			printf("Alloc %zuKB from CPA failed.\n", (mem_size_arr[i]>>10));
			continue;
		}
		else
		{
			printf("Alloc %zuKB from CPA success.", (mem_size_arr[i]>>10));
		}

		if (!test_verify_allocated_buffer(shared_fd, mem_size_arr[i]))
		{
			printf("Verify CPA memory failed.\n");
		}
		else
		{
			printf("Verify CPA memory success.\n");
		}

		test_free_CPA_mem(shared_fd);
	}

	/*
	 * Try to allocate as much memory as possible from system memory.
	 * in CPA alloc code, there is a condition judge, which is:
	 * if the allocated size is larger than half of totalram_pages, CPA
	 * allocation will fail, so decided to alloc memory with basic unit
	 * 2MB until the system memory is exhausted.
	 */
	printf("%d. Try to allocate from CPA until system mem exhausts.\n", i+1);
	while(1)
	{
		int tmp_fd = test_allocate_from_CPA(TEST_ALLOC_DEFAULT_SIZE);

		if (tmp_fd <= 0) {
			failed_times++;
			if (failed_times == TEST_ALLOC_FAIL_TIMES)
			{
				break;
			}
			else
			{
				continue;
			}
		}

		if (!test_verify_allocated_buffer(tmp_fd, TEST_ALLOC_DEFAULT_SIZE))
		{
			printf("    >>> Failed to verify CPA memory, stop allocating.\n");
			break;
		}

		allocated_buffer_handle[allocated_buffer_num] = tmp_fd;
		allocated_buffer_num++;
	}

	for (i = 0; i < allocated_buffer_num; i++)
	{
		test_free_CPA_mem(allocated_buffer_handle[i]);
	}

	if (failed_times == TEST_ALLOC_FAIL_TIMES)
	{
		printf("    >>> Successfully exhausted system memory and no error happened.\n");
	}

	allocated_buffer_num = 0;

	printf("\n===================Test 1 END===================.\n\n\n");


	////////////////////////////////////////////////////////////////
	printf("\n===================Test 2 START===================.\n");
	printf("Start to simulate memory fragment in test-cpa module.\n");

	test_start_simulate_memory_fragment(system_free_pages, test_allocated_pages, simulate_page_unit_size);

	printf("    >>> After simulate memory fragment, system free memory: %d MB, test allocated memory: %d MB.\n",
			system_free_pages>>8, test_allocated_pages>>8);
	printf("    >>> System is in 2MB memory fragment situation.\n\n");
	printf("    >>> Try to allocate 2MB physical contiguous memory from CPA for %d times.\n", TEST_ALLOC_FAIL_TIMES);

	for (i = 0; i < TEST_ALLOC_FAIL_TIMES; i++)
	{
		int tmp_fd = test_allocate_from_CPA(TEST_ALLOC_DEFAULT_SIZE);
		if (tmp_fd > 0)
		{
			allocated_buffer_handle[allocated_buffer_num] = tmp_fd;
			allocated_buffer_num++;
		}
		else
		{
			first_failed_times++;
		}
	}

	//free all allocated CPA memory
	for (i = 0; i < allocated_buffer_num; i++)
	{
		test_free_CPA_mem(allocated_buffer_handle[i]);
	}
	allocated_buffer_num = 0;

	printf("        >>> Failed %d times. %dMB memory allocated.\n\n", first_failed_times,
			(TEST_ALLOC_FAIL_TIMES - first_failed_times)*(TEST_ALLOC_DEFAULT_SIZE/(1024*1024)));
	printf("    >>> Try to free %d KB pages in test-cpa module.\n", TEST_ALLOC_FAIL_TIMES*simulate_page_unit_size);

	// free some simulate page units
	for (i = 0; i < TEST_ALLOC_FAIL_TIMES; i++)
	{
		test_free_one_simulate_page_unit(system_free_pages, test_allocated_pages);
	}

	printf("	>>> After free some tests allocated page, system free memory: %d MB, test allocated memory: %d MB.\n\n",
			system_free_pages>>8, test_allocated_pages>>8);
	printf("    >>> Try to allocate 2MB physical contiguous memory from CPA for %d times.\n", TEST_ALLOC_FAIL_TIMES);

	for (i = 0; i < TEST_ALLOC_FAIL_TIMES; i++)
	{
		int tmp_fd = test_allocate_from_CPA(TEST_ALLOC_DEFAULT_SIZE);
		if (tmp_fd > 0)
		{
			allocated_buffer_handle[allocated_buffer_num] = tmp_fd;
			allocated_buffer_num++;
		}
		else
		{
			second_failed_times++;
		}
	}

	printf("        >>> Failed %d times.%d MB memory allocated.\n\n", second_failed_times,
			(TEST_ALLOC_FAIL_TIMES - second_failed_times)*(TEST_ALLOC_DEFAULT_SIZE/(1024*1024)));

	if (second_failed_times < first_failed_times)
	{
		printf("    >>> Expected result: CPA is affected by memory fragment problem.\n");
	}
	else
	{
		printf("    >>> Unexpected result: CPA is *not* affected by memory fragment problem.\n");
	}

	//free all allocated CPA memory
	for (i = 0; i < allocated_buffer_num; i++)
	{
		test_free_CPA_mem(allocated_buffer_handle[i]);
	}

	printf("Stop to simulate memory fragment in test-cpa module.\n");

	test_stop_simulate_memory_fragment();

	printf("\n===================Test 2 END===================.\n");

	test_uninitialize();
	printf("CPA test end!!!\n");

	return 0;
}
