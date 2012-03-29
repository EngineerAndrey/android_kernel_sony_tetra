/*******************************************************************************
Copyright 2012 Broadcom Corporation.  All rights reserved.

Unless you and Broadcom execute a separate written software license agreement
governing use of this software, this software is licensed to you under the
terms of the GNU General Public License version 2, available at
http://www.gnu.org/copyleft/gpl.html (the "GPL").

Notwithstanding the above, under no circumstances may you combine this software
in any way with any other Broadcom software provided under a license other than
the GPL, without Broadcom's express prior written consent.
*******************************************************************************/

#include <linux/kernel.h>

#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <linux/broadcom/vce.h>
#include <linux/broadcom/vtq.h>
#include "vceprivate.h"

/* Private configuration stuff -- not part of exposed API */
#include "vtqinit_priv.h"

typedef uint32_t vtq_datamemoffset_t;
typedef uint32_t vtq_progmemoffset_t;

struct vtq_global {
	/*
	  relocatedloaderkernelimage:

	  this is the binary 'text' of the "loader kernel" - the short
	  program that stays resident in VCE instruction memory and is
	  responsible for servicing requests in the FIFO, and loading
	  new images on-the-fly.

	  currently we only have one relocated loaderkernel image,
	  because we don't relocate it on-the-fly or paramterize it
	  with fifo configuration etc.  One day we may wish to
	  generate this at run time, at which point we will have to
	  decide whether this stays here, as a "VTQ global" or whether
	  to move it to the "per VCE" state
	*/
	const uint32_t *relocatedloaderkernelimage;
	vtq_progmemoffset_t loaderkernel_loadoffset;
	size_t loaderkernel_size;
	vtq_progmemoffset_t loaderkernel_firstentry;
	vtq_progmemoffset_t loaderkernel_loadimage_entrypoint;

	/*
	 *
	 * Debug:
	 *
	 */

	/* debug_fifo: when true all Q and de-Q events are printed to
	 * the kernel log */
	int debug_fifo;
};
typedef uint32_t prog_bitmap_t;
struct vtq_image {
	/* Track references to this image so that we can defer the
	 * freeing of it until current in-flight loads have
	 * completed. */
	struct kref ref;
	struct work_struct unregister_work;

	/* We record a bitmap of task-types supported by this image
	 * (i.e. for whom this image has entrypoints) */
	prog_bitmap_t bitmap;

	/* We keep a copy of the text and data in device addressable
	 * memory  */
	struct {
		void *virtaddr;
		size_t sz;
		uint32_t busaddr;
	} dmainfo;
	int textoffset;
	size_t textsz;
	int dataoffset;
	size_t datasz;

	/* Entry point (PC value) for each task-type supported by this
	 * image.  Array entry is only valid when the corresponding
	 * bit in the bitmap is set.  TODO: this ought not to be a big
	 * fat wasteful array. */
	uint32_t entrypts[VTQ_MAX_TASKS];
};
struct image_list {
	struct image_list *next;
	struct vtq_image *image;
};
struct vtq_task {
	/* we (currently) store task structs in an array.  We set the
	 * occupied flag to indicate that the entry is valid. */
	int occupied;

	/* List of images that can serve this task */
	struct image_list *suitable_images;
};
struct vtq_vce {
	/* Our route back to the VCE driver: */
	struct vce *driver_priv;
	struct vce_regmap io;

	/* We set this after the one-time initialization */
	int is_configured;

	/* Current circular buffer parameters (can be changed only
	 * when fifo empty) */
	unsigned int fifo_length;
	size_t fifo_entrysz;
	vtq_datamemoffset_t readptr_locn;
	vtq_datamemoffset_t writeptr_locn;
	vtq_datamemoffset_t circbuf_locn;
	uint32_t semanum;

	/* Space required in prog/data mem */
	vtq_progmemoffset_t progmem_reservation;
	vtq_datamemoffset_t datamem_reservation;

	/* link to the global (not per-VCE) state */
	struct vtq_global *global;

	/* This mutex must be held while data for arm->vce is used */
	struct mutex host_mutex;

	/* monotonically incrementing index into fifo (wraps at 2^32
	 * -- the actual fifo entry will be modulo the fifo size,
	 * which must therefore be power-of-2 */
	vtq_job_id_t writeptr;

	/* currently loaded image. */
	struct vtq_image *current_image;

	/* array (FIXME! not good!) of tasks */
	struct vtq_task tasks[VTQ_MAX_TASKS];
	/* Image loads that are in flight */
	struct vtq_image **inflightloads;

	/* a wait-queue for threads wanting to be notified when a VCE
	 * job is complete.  At the moment this serves two purposes:
	 * those waiting to enter the queue, and those waiting for a
	 * particular job to finish.  Consider breaking into two
	 * wait-queues... */
	wait_queue_head_t job_complete_wq;

	/* Anything relating to completions must take this mutex */
	spinlock_t vce_mutex;
	/* our cached copy of the VCE's FIFO read pointer */
	vtq_job_id_t last_known_readptr;

	/* this wait queue isn't used... should we keep this for
	 * debug, or ditch it? TODO! */
	wait_queue_head_t vce_given_work_wq;

	/* General stuff below needs both mutexes or special gloves */
	int on; /* yeah - I know it's wrong -- I'll fix
				it. :) */
	struct work_struct unload_work;
};

#define err_print(fmt, arg...) \
	printk(KERN_ERR "VTQ: ERROR: %s() %s:%d " fmt, \
		__func__, "vtq.c", __LINE__, ##arg)
#define dbg_print(fmt, arg...) \
	printk(KERN_WARNING "VTQ: debug: %s() %s:%d " fmt, \
		__func__, "vtq.c", __LINE__, ##arg)

int vtq_driver_init(struct vtq_global **vtq_global_out,
		    struct vce *vce,
		    struct proc_dir_entry *proc_vcedir)
{
	struct vtq_global *vtq_global;

	vtq_global = kmalloc(sizeof(*vtq_global), GFP_KERNEL);
	if (vtq_global == NULL) {
		err_print("kmalloc failure\n");
		goto err_kmalloc_vtq_global_state;
	}

	vtq_global->loaderkernel_size = 0;
	vtq_global->relocatedloaderkernelimage = NULL;
	vtq_global->loaderkernel_loadoffset = 0;
	vtq_global->loaderkernel_firstentry = 0;

	vtq_global->debug_fifo = 0; /* TODO: proc entry for this? */

	/* success */
	*vtq_global_out = vtq_global;
	return 0;

	/* error exit paths follow */

	/*kfree(vtq_global);*/
err_kmalloc_vtq_global_state:

	return -ENOENT;
}

void vtq_driver_term(struct vtq_global **vtq_global_state_ptr)
{
	kfree(*vtq_global_state_ptr);
	*vtq_global_state_ptr = NULL;
}

/* Me hates forward declarations, but I screwed up on these ones. :) */
static void try_unload_if_empty(struct work_struct *);
static void put_image_work(struct work_struct *);
static void put_image(struct vtq_image *image);

int vtq_pervce_init(struct vtq_vce **vtq_pervce_state_out,
		    struct vtq_global *global,
		    struct vce *vce,
		    struct proc_dir_entry *proc_vcedir)
{
	struct vtq_vce *vtq_pervce_state;
	int i;

	(void) proc_vcedir;

	vtq_pervce_state = kmalloc(sizeof(*vtq_pervce_state), GFP_KERNEL);
	if (vtq_pervce_state == NULL) {
		err_print("kmalloc failure\n");
		goto err_kmalloc_vtq_pervce_state;
	}

	mutex_init(&vtq_pervce_state->host_mutex);

	vtq_pervce_state->writeptr = 07734;
	vtq_pervce_state->last_known_readptr = vtq_pervce_state->writeptr;

	for (i = 0; i < VTQ_MAX_TASKS; i++)
		vtq_pervce_state->tasks[i].occupied = 0;

	vtq_pervce_state->global = global;

	vtq_pervce_state->driver_priv = vce;
	vce_init_base(&vtq_pervce_state->io, vtq_pervce_state->driver_priv);

	init_waitqueue_head(&vtq_pervce_state->job_complete_wq);

	spin_lock_init(&vtq_pervce_state->vce_mutex);

	init_waitqueue_head(&vtq_pervce_state->vce_given_work_wq);

	/* Configured later via ioctl */
	vtq_pervce_state->fifo_length = 0;
	vtq_pervce_state->is_configured = 0;
	vtq_pervce_state->inflightloads = NULL;

	vtq_pervce_state->current_image = NULL;

	vtq_pervce_state->on = 0;

	/* Initialize the work_t for unloading the loader after the
	 * FIFO is empty */
	INIT_WORK(&vtq_pervce_state->unload_work, try_unload_if_empty);

	/* success */

	*vtq_pervce_state_out = vtq_pervce_state;
	return 0;

	/* error exit paths follow */

	/* kfree(vtq_pervce_state); */
err_kmalloc_vtq_pervce_state:
	return -ENOENT;
}

void vtq_pervce_term(struct vtq_vce **vtq_pervce_state_ptr)
{
	flush_work(&(*vtq_pervce_state_ptr)->unload_work);
	kfree(*vtq_pervce_state_ptr);
	*vtq_pervce_state_ptr = NULL;
}

int vtq_configure(struct vtq_vce *v,
		  vtq_progmemoffset_t loader_base,
		  vtq_progmemoffset_t loader_run,
		  vtq_progmemoffset_t loadimage_entrypoint,
		  uint32_t *loader_text,
		  size_t loader_textsz,
		  vtq_datamemoffset_t datamem_reservation,
		  vtq_datamemoffset_t writepointer_locn,
		  vtq_datamemoffset_t readpointer_locn,
		  vtq_datamemoffset_t fifo_offset,
		  size_t fifo_length,
		  size_t fifo_entry_size,
		  uint32_t semaphore_id)
{
	struct vtq_image **inflightloads;
	unsigned int i;

	inflightloads = kmalloc(fifo_length * sizeof(*inflightloads),
			GFP_KERNEL);
	if (inflightloads == NULL) {
		err_print("Unable to allocate memory for perjob data\n");
		return -1;
	}
	for (i = 0; i < fifo_length; i++)
		inflightloads[i] = NULL;

	mutex_lock(&v->host_mutex);

	if (v->is_configured) {
		err_print("Attempt to re-configure VCE FIFO.  We "
			  "don't support that.\n");
		mutex_unlock(&v->host_mutex);
		return -1;
	}

	/* Not sure we should write to globals here... TODO: should we
	 * have a separate "global" configuration step?  Or should
	 * these things be per-VCE in the first place?  I think this
	 * will become clear when we think about it, but since we have
	 * only one VCE on Rhea, we haven't invested the time to think
	 * about it. */
	v->global->loaderkernel_loadoffset = loader_base;
	v->global->loaderkernel_firstentry = loader_run;
	v->global->loaderkernel_loadimage_entrypoint = loadimage_entrypoint;
	v->global->relocatedloaderkernelimage = loader_text;
	v->global->loaderkernel_size = loader_textsz;
	v->progmem_reservation = loader_base - 0x20;
	v->datamem_reservation = datamem_reservation;
	v->writeptr_locn = writepointer_locn;
	v->readptr_locn = readpointer_locn;
	v->circbuf_locn = fifo_offset;
	v->fifo_length = fifo_length;
	v->inflightloads = inflightloads;
	v->fifo_entrysz = fifo_entry_size;
	v->semanum = semaphore_id;
	v->is_configured = 1;
	mutex_unlock(&v->host_mutex);

	return 0;
}

static void vce_download_program_at(struct vtq_vce *v,
				    vtq_progmemoffset_t loadoffset,
				    const uint32_t *image,
				    size_t size)
{
	if (size & 3) {
		err_print("bad size\n");
		return;
	}

	while (size > 0) {
		vce_writeprog(&v->io, loadoffset, *image);
		loadoffset += 4;
		size -= 4;
		image++;
	}
}

static void _loadloader(struct vtq_vce *v)
{
	int s;

	s = vce_acquire(v->driver_priv);
	if (s < 0) {
		err_print("failed to acquire the VCE (signal?)\n");
		return;
	}

	vce_download_program_at(v,
				v->global->loaderkernel_loadoffset,
				v->global->relocatedloaderkernelimage,
				v->global->loaderkernel_size);
	/* Due to lack of fully working expression evaluation in the VCE
	   toolchain, we have to have worklist_reentry at the start of the
	   loader image. :( */
	vce_writereg(&v->io, 59, v->global->loaderkernel_loadoffset);
	vce_writereg(&v->io, 41, v->last_known_readptr);
	vce_writedata(&v->io, v->writeptr_locn, v->writeptr);
	vce_writedata(&v->io, v->readptr_locn, v->last_known_readptr);

	vce_setpc(&v->io, v->global->loaderkernel_firstentry);
	vce_run(&v->io);

	v->on = 1;
}

static void _unloadloader(struct vtq_vce *v)
{
	unsigned long flags;

	if (v->current_image != NULL) {
		put_image(v->current_image);
		v->current_image = NULL;
	}
	vce_stop(&v->io);
	/* Have to pulse the semaphore, else we'll be hosed next time! */
	vce_clearsema(&v->io, v->semanum);
	vce_setsema(&v->io, v->semanum);
	v->on = 0;
	/* We need to wait for any running ISR to complete before we
	 * can turn clocks off etc... */
	spin_lock_irqsave(&v->vce_mutex, flags);
	BUG_ON(v->on);
	spin_unlock_irqrestore(&v->vce_mutex, flags);

	/* Finally let VCE power down */
	mb();
	vce_release(v->driver_priv);
}

static void try_unload_if_empty(struct work_struct *work)
{
	struct vtq_vce *v;
	int distance;

	v = container_of(work, struct vtq_vce, unload_work);
	mutex_lock(&v->host_mutex);
	if (!v->on) {
		mutex_unlock(&v->host_mutex);
		return;
	}
	distance = v->writeptr - v->last_known_readptr;
	if (distance > 0) {
		mutex_unlock(&v->host_mutex);
		return;
	}
	BUG_ON(distance < 0);

	/* TODO: check a special flag to abort the shutdown sequence?
	 * or is that one optimization too far?  Consider. */

	_unloadloader(v);

	mutex_unlock(&v->host_mutex);
}

irqreturn_t vtq_isr(struct vtq_vce *vce)
{
	unsigned long flags;
	irqreturn_t ret;
	vtq_job_id_t our_rptr, their_rptr;
	uint32_t fifo_index;
	int distance;
	struct vtq_image *image;

	/* First we do a fast-path check without the mutex: */
	if (!vce->on)
		return IRQ_NONE;

	ret = IRQ_NONE;

	spin_lock_irqsave(&vce->vce_mutex, flags);
	if (!vce->on) {
		spin_unlock_irqrestore(&vce->vce_mutex, flags);
		return IRQ_NONE;
	}
	their_rptr = vce_readdata(&vce->io, vce->readptr_locn);

	our_rptr = vce->last_known_readptr;
	distance = their_rptr - our_rptr;
	if (distance > 0)
		ret = IRQ_HANDLED;
	while (distance > 0) {
		if (vce->global->debug_fifo)
			printk(KERN_INFO "VTQ: DEQ: %u\n", our_rptr);

		fifo_index = our_rptr & (vce->fifo_length-1);
		image = vce->inflightloads[fifo_index];
		if (image != NULL) {
			schedule_work(&image->unregister_work);
			vce->inflightloads[fifo_index] = NULL;
		}
		our_rptr++;
		distance = their_rptr - our_rptr;
	}
	vce->last_known_readptr = our_rptr;
	if (our_rptr == vce->writeptr)
		schedule_work(&vce->unload_work);

	spin_unlock_irqrestore(&vce->vce_mutex, flags);

	if (ret == IRQ_HANDLED) {
		/* I don't like "wake_up_all", but, we have the
		 * queue_job and the await_job both waiting on it.
		 * So, we need to wake them both up.  Should this be
		 * two separate wait queues?  I'm thinking that if
		 * there are multiple "await_job()s" waiting, they
		 * *all* want to be woken, but if there are multiple
		 * "queue_job()s" waiting, they don't all need to be
		 * woken. TODO: FIXME! */
		wake_up_all(&vce->job_complete_wq);
	} else {
		dbg_print("SPURIOUS INTERRUPT -- We're \"on\", "
		       "and received int, but had nothing to do\n");
	}

	return ret;
}


/* **************** */

/*
 *
 *  CONTEXT
 *
 */

struct vtq_context {
	struct vtq_vce *vce;
};

struct vtq_context *vtq_create_context(struct vtq_vce *vce)
{
	struct vtq_context *ctx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL) {
		err_print("kmalloc failure\n");
		goto err_kmalloc;
	}

	ctx->vce = vce;

	/* success */
	return ctx;

	/* error exit paths follow */

	/* kfree(ctx); */
err_kmalloc:

		return NULL;
}

void vtq_destroy_context(struct vtq_context *ctx)
{
	kfree(ctx);
}

/* **************** */

/*
 *
 *  IMAGE
 *
 */

struct vtq_image *vtq_register_image(struct vtq_context *ctx,
		const uint32_t *text,
		size_t textsz,
		const uint32_t *data,
		size_t datasz,
		size_t datamemreq)
{
	struct vtq_image *image;

	mutex_lock(&ctx->vce->host_mutex);

	if (datamemreq > ctx->vce->datamem_reservation || datasz > datamemreq) {
		err_print("DATA memory requirement too big\n");
		goto err_bad_image_param;
	}
	if (textsz > ctx->vce->global->loaderkernel_loadoffset) {
		err_print("INSTRUCTION memory requirement too big\n");
		goto err_bad_image_param;
	}
	if (textsz & 3 || datasz & 3 || datamemreq & 3) {
		err_print("this image has a bad text or data size\n");
		goto err_bad_image_param;
	}

	image = kmalloc(sizeof(*image), GFP_KERNEL);
	if (image == NULL) {
		err_print("failed to allocate image\n");
		goto err_find_image_id;
	}

	image->bitmap = 0;

	image->dmainfo.sz = textsz + datasz;
	/* TODO: we shouldn't really need this extra allocation... how
	 * can we avoid it? */
	image->dmainfo.virtaddr =
		dma_alloc_coherent(NULL, textsz + datasz,
			&image->dmainfo.busaddr, GFP_DMA);
	if (image->dmainfo.virtaddr == NULL) {
		err_print("dma_alloc_coherent failed\n");
		goto err_dma_alloc;
	}
	image->textoffset = 0;
	image->textsz = textsz;
	image->dataoffset = textsz;
	image->datasz = datasz;

	{
		char *imagecopy;

		imagecopy = image->dmainfo.virtaddr;
		memcpy(imagecopy + image->textoffset, text, textsz);
		memcpy(imagecopy + image->dataoffset, data, datasz);
	}

	kref_init(&image->ref);
	INIT_WORK(&image->unregister_work, put_image_work);

	mutex_unlock(&ctx->vce->host_mutex);

	/* success */

	return image;

	/*

	  error exit paths follow

	*/

	/* dma_free_coherent */
err_dma_alloc:

	kfree(image);
err_find_image_id:
err_bad_image_param:
	mutex_unlock(&ctx->vce->host_mutex);

	return NULL;
}

static void cleanup_image(struct kref *ref)
{
	struct vtq_image *image;

	image = container_of(ref, struct vtq_image, ref);

	dma_free_coherent(NULL,
			  image->dmainfo.sz,
			  image->dmainfo.virtaddr,
			  image->dmainfo.busaddr);

	kfree(image);
}

static void put_image(struct vtq_image *image)
{
	kref_put(&image->ref, cleanup_image);
}

static void put_image_work(struct work_struct *work)
{
	struct vtq_image *i;

	i = container_of(work, struct vtq_image, unregister_work);
	put_image(i);
}

static void get_image(struct vtq_image *image)
{
	kref_get(&image->ref);
}

void vtq_unregister_image(struct vtq_context *ctx, struct vtq_image *image)
{
	(void) ctx;

#if 0
	struct vtq_vce *v;

	v = ctx->vce;
	mutex_lock(&v->host_mutex);

	/* TODO: here we should go through and remove out entrypoints
	 * from tasks.  At the moment, they'd just keep a reference to
	 * us and the image will hang around.  We mandate in the API
	 * that tasks should be destroyed before images anyway, so
	 * there's no need to put this right right now. */

	mutex_unlock(&v->host_mutex);
#endif

	put_image(image);
}

/* **************** */

/*
 *
 *  TASK TYPE
 *
 */

static void vtq_add_task_entrypt(struct vtq_context *ctx,
		vtq_task_id_t prog_id,
		struct vtq_image *image,
		uint32_t entrypt)
{
	struct image_list *l;

	/* We need the host mutex while we modify the bitmap, as the
	 * queue and complete functions will trust this data */
	mutex_lock(&ctx->vce->host_mutex);
	get_image(image);
	image->bitmap |= (1<<prog_id);

	l = kmalloc(sizeof(*l), GFP_KERNEL);
	l->image = image;
	l->next = ctx->vce->tasks[prog_id].suitable_images;
	ctx->vce->tasks[prog_id].suitable_images = l;

	image->entrypts[prog_id] = entrypt;
	mutex_unlock(&ctx->vce->host_mutex);
}

static void vtq_remove_all_entrypts(struct vtq_context *ctx,
		vtq_task_id_t prog_id)
{
	struct vtq_image *image;
	struct image_list *image_list;
	struct image_list *next;

	mutex_lock(&ctx->vce->host_mutex);
	image_list = ctx->vce->tasks[prog_id].suitable_images;
	ctx->vce->tasks[prog_id].suitable_images = NULL;

	while (image_list != NULL) {
		image = image_list->image;
		next = image_list->next;
		kfree(image_list);
		image_list = next;
		image->bitmap &= ~(1<<prog_id);
		put_image(image);
	}
	mutex_unlock(&ctx->vce->host_mutex);
}

static vtq_task_id_t vtq_assign_prog_id(struct vtq_context *ctx)
{
	int prog_id;
	mutex_lock(&ctx->vce->host_mutex);
	for (prog_id = 0; prog_id < VTQ_MAX_TASKS; prog_id++) {
		if (!ctx->vce->tasks[prog_id].occupied) {
			ctx->vce->tasks[prog_id].occupied = 1;
			ctx->vce->tasks[prog_id].suitable_images = NULL;
			mutex_unlock(&ctx->vce->host_mutex);
			return prog_id;
		}
	}

	/* Argh! Out of task IDs */

	mutex_unlock(&ctx->vce->host_mutex);
	return -1;
}

static void vtq_free_prog_id(struct vtq_context *ctx, vtq_task_id_t prog_id)
{
	ctx->vce->tasks[prog_id].occupied = 0;
}

vtq_task_id_t vtq_create_task(struct vtq_context *ctx,
			      struct vtq_image *image,
			      uint32_t entrypt)
{
	vtq_task_id_t prog_id;

	prog_id = vtq_assign_prog_id(ctx);
	if (prog_id < 0)
		return prog_id;

	vtq_add_task_entrypt(ctx, prog_id, image, entrypt);

	return prog_id;
}

void vtq_destroy_task(struct vtq_context *ctx, vtq_task_id_t prog_id)
{
	vtq_remove_all_entrypts(ctx, prog_id);
	vtq_free_prog_id(ctx, prog_id);
}

/* **************** */

/*
 *
 *  JOB
 *
 */
vtq_job_id_t vtq_get_read_pointer(struct vtq_context *ctx)
{
	/* We assume atomic read so don't need to acquire vce_mutex
	 * while we read this here, and it doesn't matter to caller if
	 * this news is old by the time they get it. */

	return ctx->vce->last_known_readptr;
}

static int got_room_for_job(struct vtq_context *ctx, vtq_task_id_t task_id)
{
	int njobsinq;
	int room;
	uint32_t curimagebitmap;
	int imagehasentrypt, haveenoughroom;

	mutex_lock(&ctx->vce->host_mutex);

	njobsinq = ctx->vce->writeptr - ctx->vce->last_known_readptr;
	room = ctx->vce->fifo_length - njobsinq;
	if (ctx->vce->current_image != NULL) {
		curimagebitmap =
			ctx->vce->current_image->bitmap;
		imagehasentrypt =
			curimagebitmap & (1<<task_id);
	} else {
		imagehasentrypt = 0;
	}

	haveenoughroom = room >= (imagehasentrypt ? 1 : 2);
	if (!haveenoughroom)
		mutex_unlock(&ctx->vce->host_mutex);

	return haveenoughroom;
}

int vtq_queue_job(struct vtq_context *ctx,
		vtq_task_id_t task_id,
		vtq_job_arg_t arg0,
		vtq_job_arg_t arg1,
		vtq_job_arg_t arg2,
		vtq_job_arg_t arg3,
		vtq_job_arg_t arg4,
		vtq_job_arg_t arg5,
		uint32_t flags_do_not_use_me,
		vtq_job_id_t *job_id_out)
{
	int curimagehasentrypt;
	uint32_t curimagebitmap;
	uint32_t datamemaddress;
	uint32_t job_entrypoint;
	vtq_job_id_t next_job_id;
	uint32_t fifo_index;
	struct vtq_image *new_image;
	int s;

	(void) flags_do_not_use_me;

	/* got_room_for_job() would wait forever if we haven't had the
	 * one-time configuration to set up the FIFO.  We can be
	 * slightly more proactive by checking for a non-configured
	 * FIFO here.  It's not an exact science, as we don't have the
	 * mutex, but this is abnormal condition anyway, so we can
	 * just accept that behavior.  In any case, it would only be a
	 * problem if we allow deconfiguring or reconfiguring of the
	 * FIFO, which we currently do not. */
	if (!ctx->vce->is_configured) {
		err_print("Not configured yet.  Cannot queue job\n");
		return -1;
	}

	s = wait_event_interruptible(ctx->vce->job_complete_wq,
				     got_room_for_job(ctx, task_id));
	if (s) {
		dbg_print("wait_event_interruptible was interrupted\n");
		return s;
	}
	/* got room for job will leave the host_mutex acquired when
	 * the condition is met */

	/* spin lock is unlocked so isr can run, but isr won't turn us
	 * off now (because we said so above) and we are the only ones
	 * allowed to turn on, so this test should be safe */
	if (!ctx->vce->on) {
		_loadloader(ctx->vce);
		if (!ctx->vce->on) {
			/* Not sure whether this can ever happen, but
			 * it's certainly bad if it ever does.  I
			 * guess it can happen if we got a signal
			 * while trying to acquire VCE?  TODO: handle
			 * this case */
			err_print("oh no. :(\n");
			mutex_unlock(&ctx->vce->host_mutex);
			return -EINVAL;
		}
	}

	fifo_index = ctx->vce->writeptr & (ctx->vce->fifo_length-1);
	datamemaddress = ctx->vce->circbuf_locn
		+ ctx->vce->fifo_entrysz * fifo_index;

	/* Insert a load-image if req'd */
	if (ctx->vce->current_image != NULL) {
		curimagebitmap =
			ctx->vce->current_image->bitmap;
		curimagehasentrypt =
			curimagebitmap & (1<<task_id);
	} else {
		curimagehasentrypt = 0;
	}
	if (!curimagehasentrypt) {
		uint32_t imagehwaddr;

		BUG_ON(!ctx->vce->on);

		/* TODO: find_image_for_prog(prog_id); - or similar --
		 * for now, we just use first one... we assume there
		 * is one!  This is fine for now, but we'll eventually
		 * expect that a single task can be served by more
		 * than one image. */
		BUG_ON(ctx->vce->tasks[task_id].suitable_images == NULL);
		BUG_ON(ctx->vce->tasks[task_id].suitable_images->image == NULL);
		new_image = ctx->vce->tasks[task_id].suitable_images->image;
		get_image(new_image);
		ctx->vce->inflightloads[fifo_index] = new_image;
		imagehwaddr = new_image->dmainfo.busaddr;
		vce_writedata(&ctx->vce->io, datamemaddress,
			ctx->vce->global->loaderkernel_loadimage_entrypoint);
		vce_writedata(&ctx->vce->io, datamemaddress+4, 0);
		vce_writedata(&ctx->vce->io, datamemaddress+8,
			imagehwaddr + new_image->textoffset);
		vce_writedata(&ctx->vce->io, datamemaddress+12,
			new_image->textsz);
		vce_writedata(&ctx->vce->io, datamemaddress+16, 0);
		vce_writedata(&ctx->vce->io, datamemaddress+20,
			imagehwaddr + new_image->dataoffset);
		vce_writedata(&ctx->vce->io, datamemaddress+24,
			new_image->datasz);
		if (ctx->vce->global->debug_fifo) {
			printk(KERN_INFO "VTQ: Q: %u: "
			       "0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X (n/a)\n",
			       ctx->vce->writeptr,
			       ctx->vce->global->loaderkernel_loadimage_entrypoint,
			       0, imagehwaddr + new_image->textoffset,
			       new_image->textsz,
			       0, imagehwaddr + new_image->dataoffset,
			       new_image->datasz);
		}

		ctx->vce->writeptr++;
		if (ctx->vce->current_image != NULL)
			put_image(ctx->vce->current_image);
		get_image(new_image);
		ctx->vce->current_image = new_image;

		fifo_index = ctx->vce->writeptr & (ctx->vce->fifo_length-1);
		datamemaddress = ctx->vce->circbuf_locn +
			ctx->vce->fifo_entrysz * fifo_index;
	}

	BUG_ON(ctx->vce->inflightloads[fifo_index] != NULL);

	BUG_ON(!ctx->vce->on);

	job_entrypoint =
		ctx->vce->current_image->entrypts[task_id];
	vce_writedata(&ctx->vce->io, datamemaddress, job_entrypoint);
	vce_writedata(&ctx->vce->io, datamemaddress+4, arg0);
	vce_writedata(&ctx->vce->io, datamemaddress+8, arg1);
	vce_writedata(&ctx->vce->io, datamemaddress+12, arg2);
	vce_writedata(&ctx->vce->io, datamemaddress+16, arg3);
	vce_writedata(&ctx->vce->io, datamemaddress+20, arg4);
	vce_writedata(&ctx->vce->io, datamemaddress+24, arg5);
	if (ctx->vce->global->debug_fifo)
		printk(KERN_INFO "VTQ: Q: %u: "
		       "0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
		       ctx->vce->writeptr,
		       job_entrypoint,
		       arg0, arg1, arg2, arg3, arg4, arg5, 0);
	ctx->vce->writeptr++;
	wmb();
	vce_writedata(&ctx->vce->io,
		ctx->vce->writeptr_locn, ctx->vce->writeptr);
	next_job_id = ctx->vce->writeptr;

	mutex_unlock(&ctx->vce->host_mutex);

	vce_clearsema(&ctx->vce->io, ctx->vce->semanum);

	wake_up_all(&ctx->vce->vce_given_work_wq);

	if (job_id_out != NULL)
		*job_id_out = next_job_id;

	return 0;
}

static int job_is_complete(struct vtq_vce *v, vtq_job_id_t job)
{
	int distance;
	vtq_job_id_t lkrptr;

	lkrptr = v->last_known_readptr;
	distance = lkrptr - job;
	return distance >= 0;
}

int vtq_await_job(struct vtq_context *ctx, vtq_job_id_t job)
{
	int s;

	s = wait_event_interruptible(ctx->vce->job_complete_wq,
				     job_is_complete(ctx->vce, job));
	if (s) {
		dbg_print("wait_event_interruptible was interrupted\n");
		return s;
	}

	return 0;
}

/* ******************** */
