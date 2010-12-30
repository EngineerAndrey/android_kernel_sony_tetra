/*
 *  graphics_driver.c - graphics driver interface
 *
 *  Copyright (C) 2010 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This device provides a shared mechanism for writing to the mailboxes,
 * semaphores, doorbells etc. that are shared between the ARM and the VideoCore
 * processor
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/syscalls.h>
#include <linux/proc_fs.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/stat.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <asm/sizes.h>
#include <asm/io.h>
#include <mach/platform.h>
#include <mach/ipc.h>
#include "graphics_regs.h"
#include "graphics.h"
#include "graphics_driver.h"

#define GRAPHICS_DRIVER_NAME  "vc_graphics"

//#define BULK_DEBUG 1

/****
 ****
 **** Module state
 ****
 ****/

struct named_semaphore {
	uint32_t pid_0;
	uint32_t pid_1;
	uint32_t sem_no;
	uint32_t name;
	struct semaphore sem;
	int initialized;
};

static struct {
	volatile unsigned char *base_address;
	unsigned int irq;
	struct semaphore work_done;
	struct named_semaphore sem_slots[20];
	int initialized;
} graphics_state;

/****
 ****
 **** Helpers 
 ****
 ****/

static volatile uint32_t *graphics_register(uint32_t offset)
{
	return (volatile uint32_t *)(graphics_state.base_address + offset);
}

static void graphics_fire_vc_interrupt(void)
{
	ipc_notify_vc_event(graphics_state.irq);
}

static void graphics_wait(void)
{
	down(&graphics_state.work_done); //NEN_TODO:  Use down_interruptible()
}

static void graphics_post(void)
{
	up(&graphics_state.work_done);
}

static uint32_t graphics_fifo_free_entries(void)
{
	uint32_t read = *graphics_register(GRAPHICS_FIFO_READ); 
	uint32_t write = *graphics_register(GRAPHICS_FIFO_WRITE);
 	uint32_t free_entries = 0;
	if (read > write) {
		free_entries = read - write - 1;
	}
	else {
		free_entries = read + GRAPHICS_FIFO_LENGTH - write - 1;
	}
	return free_entries;
}

static int graphics_fifo_empty(void)
{
	uint32_t read = *graphics_register(GRAPHICS_FIFO_READ); 
	uint32_t write = *graphics_register(GRAPHICS_FIFO_WRITE);
	return read == write;
}

static int graphics_fifo_write(const uint32_t *data, uint32_t count)
{
	uint32_t free_count = graphics_fifo_free_entries();
	int ret = 0;
	uint32_t write = 0;
	uint32_t *fifo = NULL;

	if (free_count < count) {
		*graphics_register(GRAPHICS_FIFO_WRITE_REQ) = 1;
		graphics_fire_vc_interrupt();
		do {
			graphics_wait();
		} while (graphics_fifo_free_entries() < count);
	}

	write = *graphics_register(GRAPHICS_FIFO_WRITE);
   	fifo = (uint32_t *)graphics_register(GRAPHICS_FIFO);

	if ((write + count) > GRAPHICS_FIFO_LENGTH) {
		uint32_t size = (GRAPHICS_FIFO_LENGTH - write) * 4;
		if (copy_from_user(fifo + write, data, size)) {
			ret = -EFAULT;
			goto out;
		}

		data += GRAPHICS_FIFO_LENGTH - write;
		write += count - GRAPHICS_FIFO_LENGTH;

		if (copy_from_user(fifo, data, write * 4)) {
			ret = -EFAULT;
			goto out;
		}
	}
	else {
		if (copy_from_user(fifo + write, data, count * 4)) {
			ret = -EFAULT;
			goto out;
		}
		write += count;
	}
	*graphics_register(GRAPHICS_FIFO_WRITE) = write;
	if (*graphics_register(GRAPHICS_FIFO_READ_REQ)) {
		*graphics_register(GRAPHICS_FIFO_READ_REQ) = 0;
		graphics_fire_vc_interrupt();
	}
out:
	return ret;
}

static bool graphics_read_requested(void)
{
	return *graphics_register(GRAPHICS_FIFO_READ_REQ);
}
		
static void graphics_wait_vc_idle(void)
{
	while(!graphics_fifo_empty() || !graphics_read_requested()) {
		graphics_wait();
	}
}

static struct named_semaphore *get_named_semaphore(uint32_t name,
						   uint32_t pid_0,
						   uint32_t pid_1)
{
	int i;
	struct named_semaphore *s = graphics_state.sem_slots;
	struct named_semaphore *ret = NULL;
	for (i = 0; i < ARRAY_SIZE(graphics_state.sem_slots); i++) {
		if (s->initialized && s->name == name && s->pid_0 == pid_0 
						      && s->pid_1 == pid_1) {
			ret = s;
			break;
		}
		s++;
	}
	return ret;
}

static struct named_semaphore *create_named_semaphore(uint32_t count, uint32_t name)
{
	int i;
	struct named_semaphore *s = graphics_state.sem_slots;
	struct named_semaphore *ret = NULL;
	for (i = 0; i < ARRAY_SIZE(graphics_state.sem_slots); i++) {
		if (!s->initialized) {
			s->pid_0 = current->tgid;
			s->pid_1 = 0;
			s->sem_no = i;
			s->name = name;
			sema_init(&s->sem, count);
			s->initialized = 1;
			ret = s;
			break;
		}
		s++;
	}
	return ret;
}

/****
 ****
 **** Character driver file operations 
 ****
 ****/

ssize_t graphics_read(struct file *filp, char __user *buf, size_t count,
		loff_t *f_pos)
{
	return 0;
}

ssize_t graphics_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *f_pos)
{
	return 0;
}

static int do_tx_ctrl(struct graphics_txrx_ctrl *ctrl)
{
	int ret = 0;
	ret = graphics_fifo_write(ctrl->request, ctrl->request_len/4);
	return ret;
}

static int do_rx_ctrl(struct graphics_txrx_ctrl *ctrl)
{
	int ret = 0;
	uint32_t *response = (uint32_t *)graphics_register(GRAPHICS_RESULT);
	uint32_t response_len = 0;

	if (response == NULL) {
		goto out;
	} 

	graphics_wait_vc_idle();

	response_len = *graphics_register(GRAPHICS_RESULT_WRITE) * 4;
	
	if (response_len > ctrl->response_max_len) {
		printk(KERN_ERR "graphics: do_rx_ctrl() response_len too big\n");
		ret = -EINVAL;
		goto out;
	}
	if (copy_to_user(ctrl->response, response, response_len)) {
		ret = -EFAULT;
		goto out;
	}
	ctrl->response_len = response_len;
out:
	return ret;
}

static int do_txrx_ctrl(struct graphics_txrx_ctrl *ctrl)
{
	int ret = 0;

	ret = do_tx_ctrl(ctrl);
	if (ret) goto out;

	ret = do_rx_ctrl(ctrl);
out:
	return ret;
}

static int ioctl_rpc_tx_bulk(struct graphics_ioctl_rpc_tx_bulk *rpc_tx_bulk)
{
	int ret = 0;
	uint32_t tx_bulk_bus = 0;
	void *tx_bulk = NULL;
	graphics_wait_vc_idle(); 

	*graphics_register(GRAPHICS_BULK_REQ_SIZE) = rpc_tx_bulk->tx_bulk_len;

	graphics_fire_vc_interrupt();

	while(*graphics_register(GRAPHICS_BULK_REQ_SIZE) != 0) {
		graphics_wait();
	}
	
	tx_bulk_bus = *graphics_register(GRAPHICS_BULK_ADDR);
	tx_bulk = ioremap(__bus_to_phys(tx_bulk_bus), rpc_tx_bulk->tx_bulk_len);
       
#ifdef BULK_DEBUG 
	printk(KERN_ERR "graphics: tx_bulk: bus = 0x%x, virt = 0x%x, length = %d\n",
		tx_bulk_bus,
		(uint32_t) tx_bulk,
		rpc_tx_bulk->tx_bulk_len);
#endif

	if (copy_from_user((void *)tx_bulk,
			   rpc_tx_bulk->tx_bulk, 
			   rpc_tx_bulk->tx_bulk_len)) {
		ret = -EFAULT;
		goto err_copy_bulk;
	}

	ret = do_txrx_ctrl(&rpc_tx_bulk->ctrl);
err_copy_bulk:	
	iounmap(tx_bulk);
	return ret;
}

static int ioctl_rpc_rx_bulk(struct graphics_ioctl_rpc_rx_bulk *rpc_rx_bulk)
{
	int ret = 0;
	uint32_t bulk_size = 0;
	uint32_t rx_bulk_bus = 0;
	void *rx_bulk = NULL;

	ret = do_txrx_ctrl(&rpc_rx_bulk->ctrl);
	if (ret != 0) goto out;
	
	graphics_wait_vc_idle();

	bulk_size = *graphics_register(GRAPHICS_BULK_SIZE_USED);
	rx_bulk_bus = *graphics_register(GRAPHICS_BULK_ADDR);
	if (rx_bulk_bus == 0) {
#ifdef BULK_DEBUG 
		printk(KERN_ERR "graphics: rx_bulk: Bus address NULL\n");
#endif
		ret = -EINVAL;
		goto out;
	}

	rx_bulk = ioremap(__bus_to_phys(rx_bulk_bus), bulk_size);
	rx_bulk_bus = *graphics_register(GRAPHICS_BULK_ADDR);
   
#ifdef BULK_DEBUG 
	printk(KERN_ERR
		"graphics: rx_bulk: bus = 0x%x, virt = 0x%x, length = %d\n",
		rx_bulk_bus,
		(uint32_t) rx_bulk,
		bulk_size);
#endif
	
	if (copy_to_user(rpc_rx_bulk->rx_bulk, rx_bulk, bulk_size)) {
		ret = -EFAULT;
	}
	
	iounmap(rx_bulk);
out:
	return ret;
}

int graphics_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, 
		unsigned long arg)
{
	int ret = -1;

	switch (cmd) {
	case GRAPHICS_IOCTL_RPC: {
		struct graphics_ioctl_rpc rpc;
		if (copy_from_user(&rpc, (void *) arg, sizeof(rpc))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		ret = do_txrx_ctrl(&rpc.ctrl);
		if (ret != 0) goto err_cmd;

		if (copy_to_user((void *) arg, &rpc, sizeof(rpc))) {
			ret = -EFAULT;
			goto err_cmd;
		}
	} break;
	case GRAPHICS_IOCTL_RPC_TX_BULK: {
		struct graphics_ioctl_rpc_tx_bulk rpc_tx_bulk;
#ifdef BULK_DEBUG
		printk(KERN_INFO "graphics: tx_bulk_ioctl\n");
#endif
		if (copy_from_user(&rpc_tx_bulk, (void *) arg, 
						sizeof(rpc_tx_bulk))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		ret = ioctl_rpc_tx_bulk(&rpc_tx_bulk);
		if(ret) goto err_cmd;

		if (copy_to_user((void *) arg, &rpc_tx_bulk,
						sizeof(rpc_tx_bulk))) {
			ret = -EFAULT;
			goto err_cmd;
		}
	} break;
	case GRAPHICS_IOCTL_RPC_RX_BULK: {
		struct graphics_ioctl_rpc_rx_bulk rpc_rx_bulk;
#ifdef BULK_DEBUG
		printk(KERN_INFO "graphics: rx_bulk_ioctl\n");
#endif
		if (copy_from_user(&rpc_rx_bulk, (void *) arg, 
						sizeof(rpc_rx_bulk))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		ret = ioctl_rpc_rx_bulk(&rpc_rx_bulk);
		if (ret) goto err_cmd;

		if (copy_to_user((void *) arg, &rpc_rx_bulk,
						sizeof(rpc_rx_bulk))) {
			ret = -EFAULT;
			goto err_cmd;
		}
	} break;
	case GRAPHICS_IOCTL_CREATE_SEM: {
		struct graphics_ioctl_create_sem create_sem;
		struct named_semaphore *s;

		if (copy_from_user(&create_sem, (void *) arg, sizeof(create_sem))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		s = create_named_semaphore(create_sem.count, create_sem.name);
		if (s) {
			create_sem.sem_no = s->sem_no;
		}
		else {
			ret = -ENOMEM;
			goto err_cmd;
		}

		if (copy_to_user((void *) arg, &create_sem, sizeof(create_sem))) {
			ret = -EFAULT;
			goto err_cmd;
		}
		ret = 0;
	} break;
	case GRAPHICS_IOCTL_ACQUIRE_SEM: {
		struct graphics_ioctl_acquire_sem acquire_sem;
		struct named_semaphore *s = NULL;
		if (copy_from_user(&acquire_sem, (void *) arg, sizeof(acquire_sem))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		//NEN_TODO: validate process id
		s = graphics_state.sem_slots + acquire_sem.sem_no;
		down(&s->sem); //NEN_TODO: use down_interruptible()
		ret = 0;
	} break;
	case GRAPHICS_IOCTL_RELEASE_SEM: {
		struct graphics_ioctl_release_sem release_sem;
		struct named_semaphore *s = NULL;
		if (copy_from_user(&release_sem, (void *) arg, sizeof(release_sem))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		//NEN_TODO: validate process id
		s = graphics_state.sem_slots + release_sem.sem_no;
		up(&s->sem);
		ret = 0;
	} break;
	}
err_cmd:
	return ret;
}

int graphics_open(struct inode *inode, struct file *filp)
{
	return 0;
}

int graphics_release(struct inode *inode, struct file *filp)
{
	return 0;
}

struct file_operations graphics_fops = {
	.owner =	THIS_MODULE,
	.read =		graphics_read,
	.write =	graphics_write,
	.ioctl =	graphics_ioctl,
	.open =		graphics_open,
	.release =	graphics_release,
};

/****
 ****
 **** Misc device
 ****
 ****/

struct miscdevice graphics_dev = {
	.minor =    MISC_DYNAMIC_MINOR,
	.name =     GRAPHICS_DRIVER_NAME,
	.fops =     &graphics_fops
};

int __devinit graphics_driver_init(void)
{
	return misc_register(&graphics_dev);
}

void __devexit graphics_driver_exit(void)
{
	misc_deregister(&graphics_dev);
}

/****
 ****
 **** Platform device
 ****
 ****/

static irqreturn_t bcm2708_graphics_isr(int irq, void *dev_id)
{
	uint32_t sem_name = *graphics_register(GRAPHICS_ASYNC_SEM);
	if (sem_name != 0xffffffff) { //NEN_TODO: KHRN_NO_SEMAPHORE
		uint32_t command = *graphics_register(GRAPHICS_ASYNC_SEM);
		uint32_t pid_0 = *graphics_register(GRAPHICS_ASYNC_PID_0);
		uint32_t pid_1 = *graphics_register(GRAPHICS_ASYNC_PID_1);
		struct named_semaphore *s = get_named_semaphore(sem_name, pid_0, pid_1);
		if (s) {
			switch (command) {
			case 1: //NEN_TODO: ASYNC_COMMAND_POST:
				up(&s->sem);
				break;
			case 2: //NEN_TODO: ASYNC_COMMAND_DESTROY:
				s->initialized = 0;
				break;
			default:
				printk(KERN_ERR "graphics: semaphore number %d: unknown command %d\n", s->sem_no, command);
				break;
			}
		}
		else {
			printk(KERN_ERR "graphics: couldn't find semaphore with name %d\n", sem_name);
		}
		*graphics_register(GRAPHICS_ASYNC_SEM) = 0xffffffff; //NEN_TODO: KHRN_NO_SEMAPHORE
	}
	graphics_post();
	return IRQ_HANDLED;
}

static int __devexit bcm2708_graphics_remove(struct platform_device *pdev)
{
	if (graphics_state.initialized) {
		graphics_driver_exit();
		remove_proc_entry("vc_graphics", NULL);
		free_irq(graphics_state.irq, NULL);
		graphics_state.initialized = 0;
	}
	return 0;
}

static int __devinit bcm2708_graphics_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret = -ENOENT;
	memset(&graphics_state, 0 , sizeof(graphics_state));
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		printk(KERN_ERR "graphics: failed to get resource\n");
		ret = -ENODEV;
		goto err_platform_res;
	}
	graphics_state.base_address = (void __iomem *)(res->start);
	
	graphics_state.irq = platform_get_irq(pdev, 0);

	if( graphics_state.irq < 0 ) {
		printk(KERN_ERR "graphics: failed to get platform irq\n");
		ret = -ENODEV;
		goto err_irq;
	}

	sema_init(&graphics_state.work_done, 1);

	ret = request_irq( graphics_state.irq, bcm2708_graphics_isr,
			IRQF_DISABLED, "bcm2708 graphics interrupt", NULL);
	if (ret < 0) {
		printk(KERN_ERR "graphics: failed to get local irq\n");
		ret = -ENOENT;
		goto err_irq_handler;
	}

	ret = graphics_driver_init();
	if( ret < 0 ) {
		printk(KERN_ERR "graphics: failed to register driver\n");
		ret = -ENOENT;
		goto err_driver;
	}

	graphics_state.initialized = 1;
	return 0;
err_driver:
	free_irq(graphics_state.irq, NULL);
err_irq_handler:
err_irq:
err_platform_res:
	return ret;	
}


static struct platform_driver bcm2708_graphics_driver = {
	.probe		= bcm2708_graphics_probe,
	.remove		= __devexit_p(bcm2708_graphics_remove),
	.driver		= {
		.name = "bcm2835_GFX_",
		.owner = THIS_MODULE,
	},
};

/****
 ****
 **** Module
 ****
 ****/

static int __init bcm2708_graphics_init( void )
{
	return platform_driver_register(&bcm2708_graphics_driver);
}

static void __exit bcm2708_graphics_exit( void )
{
	platform_driver_unregister(&bcm2708_graphics_driver);
}

module_init(bcm2708_graphics_init);
module_exit(bcm2708_graphics_exit);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Graphics Kernel Driver");
