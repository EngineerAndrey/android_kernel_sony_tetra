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
#include <linux/ipc/ipc.h>

#include <asm/uaccess.h>
#include <asm/sizes.h>
#include <asm/io.h>

#include "graphics_regs.h"
#include "graphics.h"
#include "graphics_driver.h"

#define GRAPHICS_DRIVER_NAME  "vc_graphics"

//#define BULK_DEBUG 1
//#define SEMAPHORE_DEBUG 1
//#define IOCTL_DEBUG 1
//#define FIFO_DEBUG 1

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
	struct semaphore bulk_req;
	struct named_semaphore sem_slots[20];
	volatile int request_bulk;
	int initialized;
	struct mutex ioctl_mutex;
	pid_t tid_owner;
	struct thread_state *owner_state;
} state;

struct thread_state {
	pid_t tid;
	struct graphics_ioctl_current current_server;
	struct graphics_ioctl_current current_client;
	struct thread_state *next;
};

struct file_private {
	pid_t tgid;
	struct thread_state *thread_state_list;
};

//NOTE: These defines were copied from VC4 src tree
#define EGLINTMAKECURRENT_ID  (0x4008)
#define EGL_SERVER_NO_GL_CONTEXT (0)
#define EGL_SERVER_NO_VG_CONTEXT (0)
#define EGL_SERVER_NO_SURFACE (0)
#define EGLINTDESTROYBYPID_ID (0x4022)

//NOTE: This structure will need to be updated whenever the eglMakeCurrent RPC interface changes
struct make_current_rpc {
	uint32_t id; //Must be EGLINTMAKECURRENT_ID
	uint32_t pid_0;
	uint32_t pid_1;
	uint32_t gltype;
	uint32_t servergl;
	uint32_t servergldraw;
	uint32_t serverglread;
	uint32_t servervg;
	uint32_t servervgsurf;
};

struct destroy_by_pid_rpc {
	uint32_t id; //Must be EGLINTDESTROYBYPID_ID
	uint32_t pid_0;
	uint32_t pid_1;
};

/****
 ****
 **** Helpers 
 ****
 ****/

static struct file_private *get_file_private(struct file *filp)
{
	return (struct file_private *)filp->private_data;	
} 

static struct thread_state *get_thread_state(struct file_private *file_priv)
{
	struct thread_state *ret = NULL;
	if (state.tid_owner == current->pid) {
		ret = state.owner_state;
		goto out;
	}

	ret = file_priv->thread_state_list;

	while (ret && ret->tid != current->pid) {
		ret = ret->next;
	}

	if (ret != NULL) goto out;

	ret = (struct thread_state *)kmalloc(sizeof(struct thread_state),
		GFP_KERNEL);

	if (ret == NULL) goto out;

	ret->tid = current->pid;
	ret->current_client.servergl = EGL_SERVER_NO_GL_CONTEXT;
	ret->current_client.servergldraw = EGL_SERVER_NO_SURFACE;
	ret->current_client.serverglread = EGL_SERVER_NO_SURFACE;
	ret->current_client.servervg = EGL_SERVER_NO_VG_CONTEXT;
	ret->current_client.servervgsurf = EGL_SERVER_NO_SURFACE;
	memcpy(&ret->current_server, &ret->current_client,
		sizeof(ret->current_server));
	ret->next = file_priv->thread_state_list;
	file_priv->thread_state_list = ret;
out:
	return ret;
} 

static volatile uint32_t *graphics_register(uint32_t offset)
{
	return (volatile uint32_t *)(state.base_address + offset);
}

static void graphics_fire_vc_interrupt(void)
{
	ipc_notify_vc_event(state.irq);
}

static void graphics_wait(void)
{
	down(&state.work_done);
}

static void graphics_post(void)
{
	up(&state.work_done);
}

static void graphics_bulk_wait(void)
{
	down(&state.bulk_req);
}

static void graphics_bulk_post(void)
{
	up(&state.bulk_req);
}

static uint32_t graphics_fifo_entries(void)
{
	uint32_t read = *graphics_register(GRAPHICS_FIFO_READ); 
	uint32_t write = *graphics_register(GRAPHICS_FIFO_WRITE);
 	uint32_t entries = 0;
	if (read > write) {
		entries = GRAPHICS_FIFO_LENGTH - read + write;
	}
	else {
		entries = write - read;
	}
	return entries;
}

static uint32_t graphics_fifo_free_entries(void)
{
	return GRAPHICS_FIFO_LENGTH - graphics_fifo_entries() - 1;
}

static int graphics_fifo_empty(void)
{
	uint32_t read = *graphics_register(GRAPHICS_FIFO_READ); 
	uint32_t write = *graphics_register(GRAPHICS_FIFO_WRITE);
#ifdef FIFO_DEBUG 
	printk(KERN_INFO "graphics_fifo_empty: read %d, write %d\n",
		read, write);
#endif
	return read == write;
}

static int graphics_fifo_write(int system, const uint32_t *data, uint32_t count)
{
	uint32_t free_count = graphics_fifo_free_entries();
	int ret = 0;
	uint32_t write = 0;
	uint32_t *fifo = NULL;

#ifdef FIFO_DEBUG
	printk(KERN_INFO "graphics_fifo_write:"
			 " data = 0x%x, count = %d\n",
			 (uint32_t) data, count);
#endif

	if (free_count < count) {
		*graphics_register(GRAPHICS_FIFO_WRITE_REQ) = count;
		graphics_fire_vc_interrupt();
		do {
#ifdef FIFO_DEBUG
			printk(KERN_INFO "graphics_fifo_write:"
					" Not enough space:"
					" free = %d, count = %d\n", 
					free_count, count);
#endif
			graphics_wait();
		} while (*graphics_register(GRAPHICS_FIFO_WRITE_REQ));
	}

	write = *graphics_register(GRAPHICS_FIFO_WRITE);
	fifo = (uint32_t *)graphics_register(GRAPHICS_FIFO);

	if ((write + count) > GRAPHICS_FIFO_LENGTH) {
		uint32_t size = (GRAPHICS_FIFO_LENGTH - write) * 4;
	
		if (system) {
			memcpy(fifo + write, data, size);
		}
		else if (copy_from_user(fifo + write, data, size)) {
			ret = -EFAULT;
			goto out;
		}

		data += GRAPHICS_FIFO_LENGTH - write;
		write += count - GRAPHICS_FIFO_LENGTH;

		if (system) {
			memcpy(fifo, data, write * 4);
		}
		else if (copy_from_user(fifo, data, write * 4)) {
			ret = -EFAULT;
			goto out;
		}
	}
	else {
		if (system) {
			memcpy(fifo + write, data, count * 4);
		}
		else if (copy_from_user(fifo + write, data, count * 4)) {
			ret = -EFAULT;
			goto out;
		}
		write += count;
	}
	*graphics_register(GRAPHICS_FIFO_WRITE) = write;

	if (*graphics_register(GRAPHICS_FIFO_READ_REQ)) {
		if (graphics_fifo_entries() >= *graphics_register(GRAPHICS_FIFO_READ_REQ)) {
			*graphics_register(GRAPHICS_FIFO_READ_REQ) = 0;
			graphics_fire_vc_interrupt();
		}
	}
#ifdef FIFO_DEBUG
	printk(KERN_INFO "graphics_fifo_write:"
			 " read = %d, write = %d\n",
             *graphics_register(GRAPHICS_FIFO_READ),
             *graphics_register(GRAPHICS_FIFO_WRITE));
#endif
out:
	return ret;
}

static bool graphics_read_requested(void)
{
	uint32_t read_req = *graphics_register(GRAPHICS_FIFO_READ_REQ);
#ifdef FIFO_DEBUG
	printk(KERN_INFO "graphics_read_requested: %d\n", read_req);
#endif
	return read_req;
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
	struct named_semaphore *s = state.sem_slots;
	struct named_semaphore *ret = NULL;
	for (i = 0; i < ARRAY_SIZE(state.sem_slots); i++) {
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
	struct named_semaphore *s = state.sem_slots;
	struct named_semaphore *ret = NULL;
	for (i = 0; i < ARRAY_SIZE(state.sem_slots); i++) {
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
	ret = graphics_fifo_write(0, ctrl->request, ctrl->request_len/4);
	return ret;
}

static int do_rx_ctrl(struct graphics_txrx_ctrl *ctrl)
{
	int ret = 0;
	uint32_t *response;
	uint32_t response_len;

	if (ctrl->response == NULL) {
		goto out;
	} 

	response = (uint32_t *)graphics_register(GRAPHICS_RESULT);

	graphics_wait_vc_idle();

	response_len = *graphics_register(GRAPHICS_RESULT_WRITE) * 4;
	
	if (response_len > ctrl->response_max_len) {
		printk(KERN_ERR "graphics_do_rx_ctrl(): response_len too big\n");
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

static int do_txrx_ctrl(struct file_private *priv, struct thread_state *thread_state, struct graphics_txrx_ctrl *ctrl)
{
	int ret = 0;

	if(state.tid_owner != current->pid) {
		struct make_current_rpc current_rpc;
		struct graphics_ioctl_current *server;

		server = &thread_state->current_server;

		current_rpc.id = EGLINTMAKECURRENT_ID;
		current_rpc.pid_0 = current->tgid;
		current_rpc.pid_1 = 0;
		current_rpc.gltype = server->gltype;
		current_rpc.servergl = server->servergl;
		current_rpc.servergldraw = server->servergldraw;
		current_rpc.serverglread = server->serverglread;
		current_rpc.servervg = server->servervg;
		current_rpc.servervgsurf = server->servervgsurf;

		graphics_fifo_write(1, (uint32_t *)&current_rpc,
			sizeof(current_rpc)/sizeof(uint32_t));
			state.tid_owner = current->pid;
			state.owner_state = thread_state;
		}

		ret = do_tx_ctrl(ctrl);
		if (ret) goto out;

		ret = do_rx_ctrl(ctrl);

		memcpy(&thread_state->current_server, &thread_state->current_client,
			sizeof(thread_state->current_server));
	out:
		return ret;
	}

	static void request_bulk(uint32_t req_size)
	{
		*graphics_register(GRAPHICS_BULK_REQ_SIZE) = req_size;
		*graphics_register(GRAPHICS_BULK_REQ) = 1;
		graphics_fire_vc_interrupt();
		graphics_bulk_wait();
	}

	static int ioctl_rpc_tx_bulk(struct file_private *priv, struct thread_state *thread_state, struct graphics_ioctl_rpc_tx_bulk *rpc_tx_bulk)
	{
		int ret = 0;
		uint32_t tx_bulk_bus = 0;
		void *tx_bulk = NULL;

		graphics_wait_vc_idle(); 

		if (rpc_tx_bulk->tx_bulk_len > *graphics_register(GRAPHICS_BULK_SIZE)) {
			request_bulk(rpc_tx_bulk->tx_bulk_len);
		}
	
	tx_bulk_bus = *graphics_register(GRAPHICS_BULK_ADDR);
	tx_bulk = ioremap( __VC_BUS_TO_ARM_PHYS_ADDR( tx_bulk_bus ), rpc_tx_bulk->tx_bulk_len);
       
#ifdef BULK_DEBUG 
	printk(KERN_INFO "graphics_ioctl_rpc_tx_bulk:"
			 " bus = 0x%x, virt = 0x%x, length = %d\n",
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

	ret = do_txrx_ctrl(priv, thread_state, &rpc_tx_bulk->ctrl);
err_copy_bulk:	
	iounmap(tx_bulk);
	return ret;
}

static int ioctl_rpc_rx_bulk(struct file_private *file_priv,
				 struct thread_state *thread_state,
				 struct graphics_ioctl_rpc_rx_bulk *rpc_rx_bulk)
{
	int ret = 0;
	uint32_t bulk_size = 0;
	uint32_t rx_bulk_bus = 0;
	void *rx_bulk = NULL;

	ret = do_txrx_ctrl(file_priv, thread_state, &rpc_rx_bulk->ctrl);

	if (ret != 0) goto out;
	
	graphics_wait_vc_idle();

	bulk_size = *graphics_register(GRAPHICS_BULK_SIZE_USED);
	rx_bulk_bus = *graphics_register(GRAPHICS_BULK_ADDR);

	if (rx_bulk_bus == 0) {
		printk(KERN_ERR "graphics_ioctl_rpc_rx_bulk: Bus address NULL\n");
		ret = -EINVAL;
		goto out;
	}

	rx_bulk = ioremap(__VC_BUS_TO_ARM_PHYS_ADDR(rx_bulk_bus), bulk_size);
   
#ifdef BULK_DEBUG 
	printk(KERN_INFO
		"graphics_ioctl_rpc_rx_bulk:"
		" bus = 0x%x, virt = 0x%x, length = %d\n",
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
	struct file_private *file_priv = NULL;
	struct thread_state *thread_state = NULL;

	file_priv = get_file_private(filp);
	thread_state = get_thread_state(file_priv);

	mutex_lock(&state.ioctl_mutex);

	switch (cmd) {
	case GRAPHICS_IOCTL_RPC: {
		struct graphics_ioctl_rpc rpc;
#ifdef IOCTL_DEBUG
		printk(KERN_INFO "graphics_ioctl: RPC_TX_CTRL\n");
#endif
		if (copy_from_user(&rpc, (void *) arg, sizeof(rpc))) {
			ret = -EFAULT;
			goto err_cmd;
		}
#ifdef IOCTL_DEBUG
		if (rpc.ctrl.response) {
			printk(KERN_INFO "graphics_ioctl: RPC_RX_CTRL\n");
		}
#endif
		ret = do_txrx_ctrl(file_priv, thread_state, &rpc.ctrl);
		if (ret != 0) goto err_cmd;

		if (copy_to_user((void *) arg, &rpc, sizeof(rpc))) {
			ret = -EFAULT;
			goto err_cmd;
		}
	} break;
	case GRAPHICS_IOCTL_RPC_TX_BULK: {
		struct graphics_ioctl_rpc_tx_bulk rpc_tx_bulk;
#if defined(BULK_DEBUG) || defined(IOCTL_DEBUG)
		printk(KERN_INFO "graphics_ioctl: RPC_TX_BULK\n");
#endif
		if (copy_from_user(&rpc_tx_bulk, (void *) arg, 
						sizeof(rpc_tx_bulk))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		ret = ioctl_rpc_tx_bulk(file_priv, thread_state, &rpc_tx_bulk);
		if(ret) goto err_cmd;

		if (copy_to_user((void *) arg, &rpc_tx_bulk,
						sizeof(rpc_tx_bulk))) {
			ret = -EFAULT;
			goto err_cmd;
		}
	} break;
	case GRAPHICS_IOCTL_RPC_RX_BULK: {
		struct graphics_ioctl_rpc_rx_bulk rpc_rx_bulk;
#if defined(BULK_DEBUG) || defined(IOCTL_DEBUG)
		printk(KERN_INFO "graphics_ioctl: RPC_RX_BULK\n");
#endif
		if (copy_from_user(&rpc_rx_bulk, (void *) arg, 
						sizeof(rpc_rx_bulk))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		ret = ioctl_rpc_rx_bulk(file_priv, thread_state, &rpc_rx_bulk);
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
#ifdef IOCTL_DEBUG
		printk(KERN_INFO "graphics_ioctl: CREATE_SEM\n");
#endif

		if (copy_from_user(&create_sem, (void *) arg, sizeof(create_sem))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		s = create_named_semaphore(create_sem.count, create_sem.name);
		if (s) {
#ifdef SEMAPHORE_DEBUG
			printk(KERN_INFO "graphics_ioctl: Created semaphore name %d: number %d\n", create_sem.name, s->sem_no);
#endif
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
#ifdef IOCTL_DEBUG
		printk(KERN_INFO "graphics_ioctl: ACQUIRE_SEM\n");
#endif
		if (copy_from_user(&acquire_sem, (void *) arg, sizeof(acquire_sem))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		//NEN_TODO: validate process id
		s = state.sem_slots + acquire_sem.sem_no;
		ret = down_killable(&s->sem);
	} break;
	case GRAPHICS_IOCTL_RELEASE_SEM: {
		struct graphics_ioctl_release_sem release_sem;
		struct named_semaphore *s = NULL;
		if (copy_from_user(&release_sem, (void *) arg, sizeof(release_sem))) {
			ret = -EFAULT;
			goto err_cmd;
		}

		//NEN_TODO: validate process id
		s = state.sem_slots + release_sem.sem_no;
		up(&s->sem);
		ret = 0;
	} break;
	case GRAPHICS_IOCTL_CURRENT: {
#ifdef IOCTL_DEBUG
		printk(KERN_INFO "graphics_ioctl: CURRENT\n");
#endif
		if (copy_from_user(&thread_state->current_client, (void *)arg,
				   sizeof(thread_state->current_client))) {
			ret = -EFAULT;
			goto err_cmd;
		}
		ret = 0;
	} break;
	}
err_cmd:
#ifdef IOCTL_DEBUG
	printk(KERN_INFO "graphics_ioctl: exit %d\n", ret);
#endif
	mutex_unlock(&state.ioctl_mutex);
	return ret;
}

int graphics_open(struct inode *inode, struct file *filp)
{

	int ret = 0;
	struct file_private *file_priv = NULL;

	file_priv = kmalloc(sizeof(struct file_private), GFP_KERNEL);
	if (file_priv == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	filp->private_data = file_priv;

	file_priv->tgid = current->tgid;
	file_priv->thread_state_list = NULL;
out:
	return ret;	
}

int graphics_release(struct inode *inode, struct file *filp)
{
	struct file_private *file_priv = get_file_private(filp);
	struct make_current_rpc current_rpc;
	struct destroy_by_pid_rpc destroy_by_pid_rpc;

	mutex_lock(&state.ioctl_mutex);

	//Set current to NULL for owning pid (tgid)
	current_rpc.id = EGLINTMAKECURRENT_ID;
	current_rpc.pid_0 = file_priv->tgid;
	current_rpc.pid_1 = 0;
	current_rpc.gltype = 0;
	current_rpc.servergl = EGL_SERVER_NO_GL_CONTEXT;
	current_rpc.servergldraw = EGL_SERVER_NO_SURFACE;
	current_rpc.serverglread = EGL_SERVER_NO_SURFACE;
	current_rpc.servervg = EGL_SERVER_NO_VG_CONTEXT;
	current_rpc.servervgsurf = EGL_SERVER_NO_SURFACE;
	graphics_fifo_write(1, (uint32_t *)&current_rpc,
		sizeof(current_rpc)/sizeof(uint32_t));

	//Destroy the pid (tgid) server side
	destroy_by_pid_rpc.id = EGLINTDESTROYBYPID_ID;
	destroy_by_pid_rpc.pid_0 = file_priv->tgid;
	destroy_by_pid_rpc.pid_1 = 0;
	graphics_fifo_write(1, (uint32_t *)&destroy_by_pid_rpc,
		sizeof(destroy_by_pid_rpc)/sizeof(uint32_t)); 

	//Free kernel memory
	while (file_priv->thread_state_list != NULL) {
		struct thread_state *thread_state;
		thread_state = file_priv->thread_state_list;
		file_priv->thread_state_list = thread_state->next;
		kfree(thread_state);
	}

	kfree(file_priv);
	
	mutex_unlock(&state.ioctl_mutex);
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

//NOTE: These defines were copied from VC4 src tree
#define ASYNC_COMMAND_POST (1)
#define ASYNC_COMMAND_DESTROY (2)
#define KHRN_NO_SEMAPHORE (0xffffffff)

static irqreturn_t bcm2708_graphics_isr(int irq, void *dev_id)
{
	uint32_t sem_name = *graphics_register(GRAPHICS_ASYNC_SEM);
	uint32_t async_req = *graphics_register(GRAPHICS_ASYNC_REQ);

	if (async_req && sem_name != KHRN_NO_SEMAPHORE) {
		uint32_t command = *graphics_register(GRAPHICS_ASYNC_COMMAND);
		uint32_t pid_0 = *graphics_register(GRAPHICS_ASYNC_PID_0);
		uint32_t pid_1 = *graphics_register(GRAPHICS_ASYNC_PID_1);
		struct named_semaphore *s = get_named_semaphore(sem_name, pid_0, pid_1);
		if (s) {
			switch (command) {
			case ASYNC_COMMAND_POST:
#ifdef SEMAPHORE_DEBUG
				printk(KERN_INFO "graphics_isr: Semaphore post: name %d number %d\n", sem_name, s->sem_no);
#endif
				up(&s->sem);
				break;
			case ASYNC_COMMAND_DESTROY:
#ifdef SEMAPHORE_DEBUG
				printk(KERN_INFO "graphics_isr: Destroying semaphore name %d number %d\n", sem_name, s->sem_no);
#endif
				s->initialized = 0;
				break;
			default:
				printk(KERN_ERR "graphics_isr: Semaphore number %d: unknown command %d\n", s->sem_no, command);
				break;
			}
		}
		else {
			printk(KERN_ERR "graphics_isr: Couldn't find semaphore with name %d\n", sem_name);
		}
		*graphics_register(GRAPHICS_ASYNC_SEM) = KHRN_NO_SEMAPHORE;
		graphics_fire_vc_interrupt();
	}

	//requests from VC
	if (*graphics_register(GRAPHICS_FIFO_READ_REQ)) {
		if (graphics_fifo_entries() >= *graphics_register(GRAPHICS_FIFO_READ_REQ)) {
			*graphics_register(GRAPHICS_FIFO_READ_REQ) = 0;
			graphics_fire_vc_interrupt();
		}
	}

	//requests to VC
	if (*graphics_register(GRAPHICS_BULK_REQ)) {
		uint32_t req_size = *graphics_register(GRAPHICS_BULK_REQ_SIZE);
		uint32_t size = *graphics_register(GRAPHICS_BULK_SIZE);
		if (size >= req_size) {
			*graphics_register(GRAPHICS_BULK_REQ) = 0;
			graphics_bulk_post();
		}
	}
	graphics_post();
	return IRQ_HANDLED;
}

static int __devexit bcm2708_graphics_remove(struct platform_device *pdev)
{
	if (state.initialized) {
		graphics_driver_exit();
		remove_proc_entry("vc_graphics", NULL);
		free_irq(state.irq, NULL);
		state.initialized = 0;
	}
	return 0;
}

static int __devinit bcm2708_graphics_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret = -ENOENT;
	memset(&state, 0 , sizeof(state));
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		printk(KERN_ERR "graphics_probe: failed to get resource\n");
		ret = -ENODEV;
		goto err_platform_res;
	}

	state.request_bulk = 0;
	state.base_address = (void __iomem *)(res->start);
	state.irq = platform_get_irq(pdev, 0);

	if( state.irq < 0 ) {
		printk(KERN_ERR "graphics_probe: failed to get platform irq\n");
		ret = -ENODEV;
		goto err_irq;
	}

	sema_init(&state.work_done, 0);
	sema_init(&state.bulk_req, 0);
	mutex_init(&state.ioctl_mutex);

	ret = request_irq( state.irq, bcm2708_graphics_isr,
			IRQF_DISABLED, "bcm2708 graphics interrupt", NULL);
	if (ret < 0) {
		printk(KERN_ERR "graphics_probe: failed to get local irq\n");
		ret = -ENOENT;
		goto err_irq_handler;
	}

	ret = graphics_driver_init();
	if( ret < 0 ) {
		printk(KERN_ERR "graphics_probe: failed to register driver\n");
		ret = -ENOENT;
		goto err_driver;
	}

	state.tid_owner = 0;
        state.owner_state = NULL;

	state.initialized = 1;
	return 0;
err_driver:
	free_irq(state.irq, NULL);
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
