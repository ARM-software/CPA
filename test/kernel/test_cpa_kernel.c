/*
 * test_cpa_kernel.c
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/bug.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/uaccess.h>

#include "test_module_ioctl.h"

LIST_HEAD(allocated_pages);
static int page_counter;

struct platform_device test_device = {
	.name		= "test_cpa",
	.id		= -1,
#if defined(CONFIG_ARM64)
	.dev.coherent_dma_mask = DMA_BIT_MASK(64),
#else
	.dev.coherent_dma_mask = DMA_BIT_MASK(32),
#endif
	.dev.dma_mask = &test_device.dev.coherent_dma_mask,
};

static int test_open(struct inode *inode, struct file *filp);
static int test_release(struct inode *inode, struct file *filp);

#ifdef HAVE_UNLOCKED_IOCTL
static long test_ioctl(struct file *filp, unsigned int cmd,
					unsigned long arg);
#else
static int test_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
#endif



/* Linux misc device operations (/dev/test_cpa) */
const struct file_operations test_fops = {
	.owner = THIS_MODULE,
	.open = test_open,
	.release = test_release,
#ifdef HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl = test_ioctl,
#else
	.ioctl = test_ioctl,
#endif
	.compat_ioctl = test_ioctl,
};

struct miscdevice test_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "test_cpa",
	.fops = &test_fops,
};

/* Verify buffer allocated from CPA is 2MB compound page. */
int test_verify_allocated_buffer(test_verify_args __user *user_arg)
{
	test_verify_args arg;
	struct dma_buf *buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	struct scatterlist *sg;
	int fd, mem_size, i;
	bool result = true;

	if (0 != copy_from_user(&arg, (void __user *)user_arg,
				sizeof(test_verify_args)))
		return -EFAULT;

	fd = arg.shared_fd;
	mem_size = arg.mem_size;

	buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(buf)) {
		pr_err("Failed to get dma-buf from fd: %d\n", fd);
		return PTR_RET(buf);
	}

	attachment = dma_buf_attach(buf, &test_device.dev);
	if (NULL == attachment) {
		dma_buf_put(buf);
		return -EFAULT;
	}

	sgt = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(sgt)) {
		pr_err("Failed to map dma-buf attachment\n");
		dma_buf_detach(buf, attachment);
		dma_buf_put(buf);

		return -EFAULT;
	}

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		u32 size = sg_dma_len(sg);

		mem_size -= size;
		if (size != SZ_2M) {
			if (!(sgt->nents == 1 || i == sgt->nents-1)) {
				result = false;
				break;
			}
		}
	}

	if (mem_size > 0)
		result = 0;

	dma_buf_detach(buf, attachment);
	dma_buf_put(buf);

	if (0 != put_user(result, &user_arg->verify_result))
		return -EFAULT;

	return 0;
}

#define ALLOC_PAGE_ORDER 4
#define ALLOC_PAGE_SIZE 16
#define TEST_ALLOC_FAIL_TIMES 200
/*
 * MEM_FRAGOMENT_GAP * ALLOC_PAGE_SIZE should be
 * less than 512(2048k/4k)
 */
#define MEM_FRAGMENT_GAP 11

/*
 * Try to simulate system memory fragment problem
 * through allocating and free behaviors.
 */
int test_start_simulate_memory_fragment(test_simulate_args __user *user_arg)
{
	struct page *page, *tmp_page, *new_page;
	unsigned int fail_times = 0;
	unsigned int page_num = 0;
	int inserted = 0;

	/* these flags will not trigger OOM killer. */
	gfp_t gfp_flags = GFP_HIGHUSER | __GFP_ZERO |
			__GFP_NOWARN | __GFP_REPEAT;

	while (1) {
		new_page = alloc_pages(gfp_flags, ALLOC_PAGE_ORDER);

		if (new_page) {
			inserted = 0;
			/* insert new page in descending order. */
			list_for_each_entry_safe(page, tmp_page,
						&allocated_pages, lru) {
				if (new_page > page) {
					list_add(&new_page->lru, &page->lru);
					inserted = 1;
					break;
				}
			}

			if (!inserted)
				list_add(&new_page->lru, &allocated_pages);

			page_counter++;
		} else {
			fail_times++;
			if (fail_times >= TEST_ALLOC_FAIL_TIMES)
				break;
		}
	}

	page_num = 0;
	list_for_each_entry_safe(page, tmp_page, &allocated_pages, lru) {
		page_num++;
		if (page_num % MEM_FRAGMENT_GAP == 0)
			continue;

		list_del_init(&page->lru);
		__free_pages(page, ALLOC_PAGE_ORDER);
		page_counter--;
	}

	if (0 != put_user(true, &user_arg->simulate_result))
		goto error_process;

	if (0 != put_user(page_counter, &user_arg->alloc_times))
		goto error_process;

	if (0 != put_user(page_counter*ALLOC_PAGE_SIZE,
				&user_arg->test_allocated_page))
		goto error_process;

	if (0 != put_user(ALLOC_PAGE_SIZE,
				&user_arg->simulate_page_unit_size))
		goto error_process;

	if (0 != put_user(global_page_state(NR_FREE_PAGES),
				&user_arg->system_free_pages))
		goto error_process;

	return 0;

error_process:
	list_for_each_entry_safe(page, tmp_page, &allocated_pages, lru) {
		list_del_init(&page->lru);
		__free_pages(page, ALLOC_PAGE_ORDER);
		page_counter--;
	}

	return -EFAULT;
}

/* free one page unit  hold in hand. */
int test_free_one_simulate_page_unit(test_simulate_args __user *user_arg)
{
	struct page *page;

	if (0 < page_counter) {
		page = list_first_entry(&allocated_pages, struct page, lru);
		list_del_init(&page->lru);
		__free_pages(page, ALLOC_PAGE_ORDER);
		page_counter--;
	}

	if (0 != put_user(true, &user_arg->simulate_result))
		return -EFAULT;

	if (0 != put_user(page_counter*ALLOC_PAGE_SIZE,
				&user_arg->test_allocated_page))
		return -EFAULT;

	if (0 != put_user(global_page_state(NR_FREE_PAGES),
				&user_arg->system_free_pages))
		return -EFAULT;

	return 0;
}

/* free all mem hold in hand.*/
int test_stop_simulate_memory_fragment(test_simulate_args __user *user_arg)
{
	struct page *page, *tmp_page;

	list_for_each_entry_safe(page, tmp_page, &allocated_pages, lru) {
		list_del_init(&page->lru);
		__free_pages(page, ALLOC_PAGE_ORDER);
	}

	page_counter = 0;

	if (0 != put_user(true, &user_arg->simulate_result))
		return -EFAULT;

	return 0;
}

static int test_open(struct inode *inode, struct file *filp)
{
	/* input validation */
	if (test_miscdevice.minor != iminor(inode)) {
		pr_err("open() Minor does not match\n");
		return -ENODEV;
	}

	return 0;
}

static int test_release(struct inode *inode, struct file *filp)
{
	/* input validation */
	if (test_miscdevice.minor != iminor(inode)) {
		pr_err("release() Minor does not match\n");
		return -ENODEV;
	}

	return 0;
}

#ifdef HAVE_UNLOCKED_IOCTL
static long test_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#else
static int test_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
			unsigned long arg)
#endif
{
	int err;

#ifndef HAVE_UNLOCKED_IOCTL
		/* inode not used */
		(void)inode;
#endif

	if (NULL == (void *)arg) {
		pr_err("ioctl arg was NULL\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case TEST_IOCTL_VERIFY_CPA:
		err = test_verify_allocated_buffer(
				(test_verify_args __user *)arg);
		break;
	case TEST_IOCTL_START_SIMULATE_FRAGMENT:
		err = test_start_simulate_memory_fragment(
				(test_simulate_args __user *)arg);
		break;
	case TEST_IOCTL_FREE_ONE_PAGE_UNIT:
		err = test_free_one_simulate_page_unit(
				(test_simulate_args __user *)arg);
		break;
	case TEST_IOCTL_STOP_SIMULATE_FRAGMENT:
		err = test_stop_simulate_memory_fragment(
				(test_simulate_args __user *)arg);
		break;
	default:
		pr_err("No handler for ioctl 0x%08X 0x%08lX\n",
			cmd, arg);
		err = -ENOTTY;
	}

	return err;
}

static int test_cpa_probe(struct platform_device *pdev)
{
	int ret;

	test_miscdevice.parent = get_device(&pdev->dev);

	ret = misc_register(&test_miscdevice);
	if (0 != ret) {
		pr_err("Failed to register misc device, misc_register() returned %d\n",
			ret);

		return ret;
	}

	return 0;
}

static int test_cpa_remove(struct platform_device *pdev)
{
	misc_deregister(&test_miscdevice);
	return 0;
}

static struct platform_driver test_driver = {
	.probe = test_cpa_probe,
	.remove = test_cpa_remove,
	.driver = { .name = "test_cpa" }
};


int test_module_init(void)
{
	int ret;

	ret = platform_device_register(&test_device);

	if (0 != ret) {
		pr_err("register platform device failed.");

		return ret;
	}

	ret = platform_driver_register(&test_driver);

	if (0 != ret) {
		platform_device_unregister(&test_device);
		platform_device_put(&test_device);

		return ret;
	}

	return 0;
}

void test_module_exit(void)
{
	platform_driver_unregister(&test_driver);
	platform_device_unregister(&test_device);
}

module_init(test_module_init);
module_exit(test_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Arm Ltd.");
