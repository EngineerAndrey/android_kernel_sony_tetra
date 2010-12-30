#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/spinlock.h>

#include <asm/bug.h>
#include <asm/io.h>
#include <asm/atomic.h>

#include <mach/media_dec_regs.h>
#include <mach/ipc.h>
#include <mach/fifo.h>

#include "bcm2708_mdec.h"


#define BCM2708_MDEC_DRIVER_NAME "bcm2708_mdec"

#define BCM2708MDEC_DEBUG 0

#define bcm2708mdec_error(format, arg...) \
	printk(KERN_ERR BCM2708_MDEC_DRIVER_NAME ": %s" format, __func__, ##arg) 

#if BCM2708MDEC_DEBUG
#define bcm2708mdec_dbg(format, arg...) bcm2708mdec_error(format, ##arg)
#else
#define bcm2708mdec_dbg(format, arg...) printk(KERN_DEBUG BCM2708_MDEC_DRIVER_NAME ": %s" format, __func__, ##arg)
#endif

#define VC_MFS_SD_PREFIX "/mfs/sd/"	/* the path for mdeia file on VC SD card. */

#define MEDIA_DEC_REGISTER_RW(offset)	\
	(*((volatile unsigned long *)((u32)g_mdec->reg_base + (offset))))

#define START_SEQUENCE_NUM	1

typedef enum
{
   MEDIA_DEC_FLAGS_NONE = 0x0,
   MEDIA_DEC_FLAGS_END_OF_FRAME = 0x1,
   MEDIA_DEC_FLAGS_TIME_UNKNOWN = 0x8,

} MEDIA_DEC_FLAGS_T;

typedef enum {
	AUDIO_STREAM = 0x0,
	VIDEO_STREAM,
} MEDIA_STREAM_T;

typedef enum {
	PLAYBACK_IDLE		=	0x0,
	PLAYBACK_ENABLED,
	PLAYBACK_STARTED,
	PLAYBACK_CLOSED,
} playback_state_t;

typedef enum {
        AUDIO_MASK	=       0x1<<0,
        VIDEO_MASK	=	0x1<<1,
} playback_type_t;

struct play_data_request {
	void			*buf;
	unsigned int		buf_size;
	struct completion	*cmpl;
	struct list_head	node;
};

/* 
 * TODO:
 *    1. Each media control structure can be allocated at device open time and stored at filp
 *    	then all the following filp operations can use it.
 *    2. Then this sturcture can be linked into the global list that resides inside the g_mdec. 
 */
struct media_stream_ctl {
	struct semaphore        vc_buf_sem;
	struct semaphore        arm_buf_sem;
	spinlock_t              vc_fifo_lock;
	IPC_FIFO_T              vc_to_arm_fifo;
	spinlock_t              arm_fifo_lock;
	IPC_FIFO_T              arm_to_vc_fifo;
	atomic_t                sequence_num;
}; 

struct media_av_ctl {
	struct media_stream_ctl		audio_ctl;
	struct media_stream_ctl		video_ctl;
	u32				playback_type;	
	playback_state_t		state;
}; 

/* TODO:
 *	1. need to add spinlock to make sure there is only one stream setup on-the-fly at any single moment if we do
 *	   support multi-stream use case.
 */
struct bcm2708_mdec {
        u32			irq;
        void __iomem		*reg_base;
	struct semaphore	vc_ack_sem;
	struct media_av_ctl	av_stream_ctl;
	//char 			ioctl_cmd_buf[MAX_BCM2708_MDEC_IOCTL_CMD_SIZE];
};

/* hacky here; needs to make a per thread buffer */
static struct bcm2708_mdec *g_mdec = NULL;

static inline void dump_vc_to_arm_fifo(struct media_stream_ctl *stream_ctl)
{
#if BCM2708MDEC_DEBUG
        printk(KERN_ERR "vc_to_arm_video_fifo: write=0x%08x           read=0x%08x\n"
                                        "base=0x%08x            size=0x%08x\n"
                                        "entry_size=0x%08x\n",
                                        (u32)stream_ctl->vc_to_arm_fifo.write, (u32)stream_ctl->vc_to_arm_fifo.read,
                                        (u32)stream_ctl->vc_to_arm_fifo.base, stream_ctl->vc_to_arm_fifo.size,
                                        stream_ctl->vc_to_arm_fifo.entry_size);
#endif
}

static inline void dump_arm_to_vc_video_fifo(struct media_stream_ctl *stream_ctl)
{
#if BCM2708MDEC_DEBUG
        printk(KERN_ERR "arm_to_vc_video_fifo: write=0x%08x           read=0x%08x\n"
                                        "base=0x%08x            size=0x%08x\n"
                                        "entry_size=0x%08x\n",
                                        (u32)stream_ctl->arm_to_vc_fifo.write, (u32)stream_ctl->arm_to_vc_fifo.read,
                                        (u32)stream_ctl->arm_to_vc_fifo.base, stream_ctl->arm_to_vc_fifo.size,
                                        stream_ctl->arm_to_vc_fifo.entry_size);
#endif
}

static inline void dump_fifo_entry(MEDIA_DEC_FIFO_ENTRY_T *entry)
{
#if BCM2708MDEC_DEBUG
        printk(KERN_ERR "fifo entry:    buffer_id=0x%08x                buffer_size=0x%08x\n"
                                        "buffer_filled=0x%08x           buffer_ptr=0x%08x\n"
                                        "timestamp=0x%08x\n",
                                        entry->buffer_id, entry->buffer_size,
                                        entry->buffer_filled_size, entry->buffer_ptr,
                                        entry->timestamp);
#endif
}

static inline int notify_vc_and_wait_for_ack(void)
{
	int ret = 0;

	ipc_notify_vc_event(g_mdec->irq);

#if 0	
	ret = down_timeout(&g_mdec->vc_ack_sem, HZ * 3);
	if (ret)
		bcm2708mdec_error("Faided to acquire the semaphore, probably VC side is pegged!");
#endif
	down(&g_mdec->vc_ack_sem);
	
	return ret;
}


static int player_setup(bcm2708_mdec_setup_t *setup_cmd)
{
	int ret = 0;

	g_mdec->av_stream_ctl.state = PLAYBACK_IDLE;

        sema_init(&g_mdec->vc_ack_sem, 0);

	bcm2708mdec_dbg("player setup with video_type=%d audio_type=%d\n", 
				setup_cmd->video_type,
				setup_cmd->audio_type);

        /* Set up the debug mode */
        MEDIA_DEC_REGISTER_RW( MEDIA_DEC_DEBUG_MASK ) = 0;

        /* Set up the src width as 0xFFFFFFFF (ignore) */
        MEDIA_DEC_REGISTER_RW( MEDIA_DEC_SOURCE_X_OFFSET ) = 0xFFFFFFFF;
        MEDIA_DEC_REGISTER_RW( MEDIA_DEC_SOURCE_Y_OFFSET ) = 0xFFFFFFFF;
        MEDIA_DEC_REGISTER_RW( MEDIA_DEC_SOURCE_WIDTH_OFFSET ) = 0xFFFFFFFF;
        MEDIA_DEC_REGISTER_RW( MEDIA_DEC_SOURCE_HEIGHT_OFFSET ) = 0xFFFFFFFF;

        /* Set up the target codec */
        MEDIA_DEC_REGISTER_RW( MEDIA_DEC_VID_TYPE ) = setup_cmd->video_type;
        MEDIA_DEC_REGISTER_RW( MEDIA_DEC_AUD_TYPE ) = setup_cmd->audio_type;

        /* Enable the mode */
        MEDIA_DEC_REGISTER_RW( MEDIA_DEC_CONTROL_OFFSET ) = MEDIA_DEC_CONTROL_ENABLE_BIT;

	mb();

	ret = notify_vc_and_wait_for_ack();

	BUG_ON((MEDIA_DEC_REGISTER_RW( MEDIA_DEC_STATUS_OFFSET) & 0x1) != 0x1);

	/*
 	 * Initialize the stream control structure for both audio and video streams, if any. 
 	 *
 	 */
 
	g_mdec->av_stream_ctl.playback_type = 0;
	/* FIFO is set up on the VC side after enabl bit is set and the ARM side is doing the same. */
	if (MEDIA_DEC_VIDEO_CodingUnused != setup_cmd->video_type) {
	        ipc_fifo_setup_no_reset(&g_mdec->av_stream_ctl.video_ctl.vc_to_arm_fifo,
        	                &MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_OUT_WRITE_PTR_OFFSET),
                	        &MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_OUT_READ_PTR_OFFSET),
                        	ipc_bus_to_virt(MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_OUT_FIFO_START_OFFSET)),
	                        MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_OUT_FIFO_SIZE_OFFSET),
        	                MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_OUT_FIFO_ENTRY_OFFSET));

	        ipc_fifo_setup_no_reset(&g_mdec->av_stream_ctl.video_ctl.arm_to_vc_fifo,
        	                &MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_IN_WRITE_PTR_OFFSET),
                	        &MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_IN_READ_PTR_OFFSET),
                        	ipc_bus_to_virt(MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_IN_FIFO_START_OFFSET)),
	                        MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_IN_FIFO_SIZE_OFFSET),
        	                MEDIA_DEC_REGISTER_RW(MEDIA_DEC_VIDEO_IN_FIFO_ENTRY_OFFSET));

		sema_init(&g_mdec->av_stream_ctl.video_ctl.vc_buf_sem, 0);
		sema_init(&g_mdec->av_stream_ctl.video_ctl.arm_buf_sem, 0);
		spin_lock_init(&g_mdec->av_stream_ctl.video_ctl.vc_fifo_lock);
		spin_lock_init(&g_mdec->av_stream_ctl.video_ctl.arm_fifo_lock);
		atomic_set(&g_mdec->av_stream_ctl.video_ctl.sequence_num, 0);

		g_mdec->av_stream_ctl.playback_type |= VIDEO_MASK;
	}

	if (MEDIA_DEC_AUDIO_CodingUnused != setup_cmd->audio_type) {
                ipc_fifo_setup_no_reset(&g_mdec->av_stream_ctl.audio_ctl.vc_to_arm_fifo,
                                &MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_OUT_WRITE_PTR_OFFSET),
                                &MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_OUT_READ_PTR_OFFSET),
                                ipc_bus_to_virt(MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_OUT_FIFO_START_OFFSET)),
                                MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_OUT_FIFO_SIZE_OFFSET),
                                MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_OUT_FIFO_ENTRY_OFFSET));

                ipc_fifo_setup_no_reset(&g_mdec->av_stream_ctl.audio_ctl.arm_to_vc_fifo,
                                &MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_IN_WRITE_PTR_OFFSET),
                                &MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_IN_READ_PTR_OFFSET),
                                ipc_bus_to_virt(MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_IN_FIFO_START_OFFSET)),
                                MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_IN_FIFO_SIZE_OFFSET),
                                MEDIA_DEC_REGISTER_RW(MEDIA_DEC_AUDIO_IN_FIFO_ENTRY_OFFSET));

                sema_init(&g_mdec->av_stream_ctl.audio_ctl.vc_buf_sem, 0);
                sema_init(&g_mdec->av_stream_ctl.audio_ctl.arm_buf_sem, 0);
                spin_lock_init(&g_mdec->av_stream_ctl.audio_ctl.vc_fifo_lock);
                spin_lock_init(&g_mdec->av_stream_ctl.audio_ctl.arm_fifo_lock);
                atomic_set(&g_mdec->av_stream_ctl.audio_ctl.sequence_num, 0);

		g_mdec->av_stream_ctl.playback_type |= AUDIO_MASK;
	}
		
	g_mdec->av_stream_ctl.state = PLAYBACK_ENABLED;

	return ret;
}

static int player_start(void)
{
	int ret = 0;

	bcm2708mdec_dbg("player start\n");

	if (PLAYBACK_ENABLED != g_mdec->av_stream_ctl.state)
		return -EIO;
	
        /* start to play */
        MEDIA_DEC_REGISTER_RW(MEDIA_DEC_CONTROL_OFFSET) |= MEDIA_DEC_CONTROL_PLAY_BIT;

	mb();

        ret = notify_vc_and_wait_for_ack();

        WARN_ON((MEDIA_DEC_REGISTER_RW( MEDIA_DEC_STATUS_OFFSET) & MEDIA_DEC_CONTROL_PLAY_BIT) != MEDIA_DEC_CONTROL_PLAY_BIT);

	g_mdec->av_stream_ctl.state = PLAYBACK_STARTED;

	return ret;
}

static int player_send_data(bcm2708_mdec_send_data_t *send_data_cmd, MEDIA_STREAM_T stream_type)
{
	MEDIA_DEC_FIFO_ENTRY_T entry;
	unsigned long flags, copy_bytes, total_bytes;
	void 	*buf_virt, *copy_ptr;
	int ret = 0;
	struct media_stream_ctl *stream_ctl;

	if (PLAYBACK_STARTED != g_mdec->av_stream_ctl.state)
                return -EIO;

	if (AUDIO_STREAM == stream_type) {
		stream_ctl = &g_mdec->av_stream_ctl.audio_ctl;
	}
	else if (VIDEO_STREAM == stream_type) {
		stream_ctl = &g_mdec->av_stream_ctl.video_ctl;
	}
	else 
		BUG();  

	if (0 == send_data_cmd->data_size)
		return 0;

#if 0
	bcm2708mdec_dbg("player send %s data with buf=0x%08x and size=0x%08x\n", 
				(AUDIO_STREAM == stream_type)?"audio":"video",
				(u32)send_data_cmd->data_buf,
				send_data_cmd->data_size);
#endif

	memset(&entry, 0, sizeof(&entry));
	total_bytes = send_data_cmd->data_size;
	copy_ptr = send_data_cmd->data_buf;

	/*
	 * Spinlock is used to protect against simualtenious access from 
	 * ARM side. And once the thread is waken up, it needs to check if
	 * FIFO is still empty because of potential access from other thread, or
	 * the wake up interrupt is an extra one.
	 */
	do {
		//spin_lock_irqsave(&stream_ctl->vc_fifo_lock, flags);
		if (!ipc_fifo_empty(&stream_ctl->vc_to_arm_fifo)) {
			ipc_fifo_read(&stream_ctl->vc_to_arm_fifo, &entry);
			BUG_ON(0 == entry.buffer_size);
		//	spin_unlock_irqrestore(&stream_ctl->vc_fifo_lock, flags);
		} else {
		//	spin_unlock_irqrestore(&stream_ctl->vc_fifo_lock, flags);		
			ret = down_interruptible(&stream_ctl->vc_buf_sem);
			if (ret < 0)
				return -ERESTARTSYS;
			else {
				continue;
			}
		}
		copy_bytes = (entry.buffer_size > total_bytes)?
				total_bytes:entry.buffer_size; 

		BUG_ON(0UL == entry.buffer_ptr);
	
		buf_virt = ioremap(__bus_to_phys(entry.buffer_ptr), copy_bytes);
		if (NULL == buf_virt) {
	                bcm2708mdec_error("failed to map the memory\n");
        	        return -ENOMEM;
        	}
#if 0
                bcm2708mdec_dbg("VC buffer bus addr=0x%08x, virt addr=0x%08x\n", 
				(u32)entry.buffer_ptr, (u32)buf_virt);
#endif

		if (copy_from_user(buf_virt, copy_ptr, copy_bytes)) {
			bcm2708mdec_error("failed to copy the user data\n");
			iounmap(buf_virt);
			return -EFAULT;
		}
		entry.buffer_filled_size	= copy_bytes;
		entry.sequence_number		= atomic_add_return(1, &stream_ctl->sequence_num);
		if (START_SEQUENCE_NUM == entry.sequence_number)
			entry.flags = MEDIA_DEC_FLAGS_NONE;
		else
			entry.flags = MEDIA_DEC_FLAGS_TIME_UNKNOWN;

		entry.timestamp			= 0;
		iounmap(buf_virt);

		total_bytes -= copy_bytes;
		copy_ptr = (void *)((u32)copy_ptr + copy_bytes);

		/* Now put the filled buf into output fifo; and we assume that
 		 * there should be a slot in the input FIFO. 
 		 * Afterwards, tell VC about this buffer with ringing doorbell.
 		 */
		while (1) {
			//spin_lock_irqsave(&stream_ctl->arm_fifo_lock, flags);
                	if (!ipc_fifo_full(&stream_ctl->arm_to_vc_fifo)) {
				ipc_fifo_write(&stream_ctl->arm_to_vc_fifo, &entry);
                        	//spin_unlock_irqrestore(&stream_ctl->arm_fifo_lock, flags);
				break;
               		} else {
                        	//spin_unlock_irqrestore(&stream_ctl->arm_fifo_lock, flags);
                        	ret = down_interruptible(&stream_ctl->arm_buf_sem);
				if (ret < 0)
                                	return -ERESTARTSYS;
				else {
					continue;
				}
			}
		}	

		mb();

		ipc_notify_vc_event(g_mdec->irq);

	} while (total_bytes > 0);

	return 0;
}

static int player_send_video_data(bcm2708_mdec_send_data_t *send_data_cmd)
{
        return player_send_data(send_data_cmd, VIDEO_STREAM);
}

static int player_send_audio_data(bcm2708_mdec_send_data_t *send_data_cmd)
{
        return player_send_data(send_data_cmd, AUDIO_STREAM);
}

static int do_playback(bcm2708_mdec_play_t *play_cmd)
{
	int ret = 0;
	u32 time = 0, prev_time = 0;
	int count;
	char *temp_name;
	
	BUG_ON(MEDIA_DEC_DEBUG_FILENAME_LENGTH  <= play_cmd->filename_size);

	/* Set up the debug mode */
	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_DEBUG_MASK ) = 0;

	/* Set up the src width as 0xFFFFFFFF (ignore) */
	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_SOURCE_X_OFFSET ) = 0xFFFFFFFF;
	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_SOURCE_Y_OFFSET ) = 0xFFFFFFFF;
	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_SOURCE_WIDTH_OFFSET ) = 0xFFFFFFFF;
	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_SOURCE_HEIGHT_OFFSET ) = 0xFFFFFFFF;
	
	/* Set up the target codec */
	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_VID_TYPE ) = play_cmd->video_type;
	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_AUD_TYPE ) = play_cmd->audio_type;

#if BCM2708MDEC_DEBUG
	play_cmd->filename[play_cmd->filename_size] = 0;
	bcm2708mdec_dbg("filename=%s size=%d\n", play_cmd->filename, play_cmd->filename_size);
#endif
	/* If user does not provide a full ppath filename, fix it. */
	if (strncmp(play_cmd->filename, VC_MFS_SD_PREFIX, strlen(VC_MFS_SD_PREFIX))) {
		temp_name = (char *)vmalloc(play_cmd->filename_size);
		if (NULL == temp_name) {
			bcm2708mdec_error("Unable to allocate name\n");
			return -ENOMEM;
		}
		strncpy(temp_name, play_cmd->filename, play_cmd->filename_size);
		strcpy(play_cmd->filename, VC_MFS_SD_PREFIX);
		strncat(play_cmd->filename, temp_name, play_cmd->filename_size);
		vfree(temp_name);
		play_cmd->filename_size += strlen(VC_MFS_SD_PREFIX);
	}

#if BCM2708MDEC_DEBUG
        play_cmd->filename[play_cmd->filename_size] = 0;
        bcm2708mdec_dbg("filename=%s size=%d\n", play_cmd->filename, play_cmd->filename_size);
#endif

	/* Write in the filename */	
	strncpy((char *)&MEDIA_DEC_REGISTER_RW( MEDIA_DEC_DEBUG_FILENAME), play_cmd->filename, play_cmd->filename_size);

	/* Enable the mode */
	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_CONTROL_OFFSET ) = MEDIA_DEC_CONTROL_ENABLE_BIT | MEDIA_DEC_CONTROL_LOCAL_FILEMODE_BIT;

	ipc_notify_vc_event(g_mdec->irq);	

	/* Wait for it to get ready */
	while ((MEDIA_DEC_REGISTER_RW( MEDIA_DEC_STATUS_OFFSET) & 0x1) != 0x1) {
		schedule_timeout(100);
		bcm2708mdec_dbg("slept for 1 sec in enabling playback\n");	
	}

	/* start to play */
	MEDIA_DEC_REGISTER_RW(MEDIA_DEC_CONTROL_OFFSET) |= MEDIA_DEC_CONTROL_PLAY_BIT;

        ipc_notify_vc_event(g_mdec->irq);

	/* Wait for it to start */
	while ((MEDIA_DEC_REGISTER_RW( MEDIA_DEC_STATUS_OFFSET) & MEDIA_DEC_CONTROL_PLAY_BIT) != MEDIA_DEC_CONTROL_PLAY_BIT) {
		schedule_timeout(100);
		bcm2708mdec_dbg("slept for 1 sec in playback\n");
	}

	for( count = 0; count < 10; count++ ) {
		prev_time = time;
		schedule_timeout(100);
		time = MEDIA_DEC_REGISTER_RW( MEDIA_DEC_PLAYBACK_TIME );
		if (time == prev_time)
			bcm2708mdec_dbg("the playback ts is not moving\n");
	}

	return ret;
}


static int player_stop(void)
{
	int ret = 0;

	bcm2708mdec_dbg("player stops\n");

//	WARN_ON(0x1 != (0x1 & MEDIA_DEC_REGISTER_RW( MEDIA_DEC_CONTROL_OFFSET )));


	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_CONTROL_OFFSET ) = 0x0;

        mb();

        ret = notify_vc_and_wait_for_ack();
	
	mb();


//	BUG_ON((MEDIA_DEC_REGISTER_RW( MEDIA_DEC_STATUS_OFFSET) & 0x3) != 0x0);

#if 1        
	while ((MEDIA_DEC_REGISTER_RW( MEDIA_DEC_STATUS_OFFSET) & 0x3) != 0x0) {
                schedule_timeout(1);
	}
#endif

//	sema_init(&g_mdec->vc_ack_sem, 0);

	g_mdec->av_stream_ctl.state =  PLAYBACK_IDLE;

	return ret;
}


static int mdec_open( struct inode *inode, struct file *file_id)
{
	return 0;
}

static int mdec_release( struct inode *inode, struct file *file_id )
{
	int ret = 0;

	if (g_mdec->av_stream_ctl.state !=  PLAYBACK_IDLE)
		ret = player_stop();	
	
	return ret;
}

static ssize_t mdec_read( struct file *file, char *buffer, size_t count, loff_t *ppos )
{
	return -EINVAL;
}

static ssize_t mdec_write( struct file *file, const char *buffer, size_t count, loff_t *ppos )
{
        return -EINVAL;
}

static int mdec_ioctl( struct inode *inode, struct file *file_id, unsigned int cmd, unsigned long arg )
{
	int ret = 0;
	unsigned long uncopied;
	u8 *ioctl_cmd_buf;

	BUG_ON(MAX_BCM2708_MDEC_IOCTL_CMD_SIZE < _IOC_SIZE(cmd));

	ioctl_cmd_buf = (u8 *)kmalloc(MAX_BCM2708_MDEC_IOCTL_CMD_SIZE, GFP_KERNEL);
	if (!ioctl_cmd_buf)
		return -ENOMEM;	

	if (0 != _IOC_SIZE(cmd)) {
		uncopied = 
			copy_from_user(ioctl_cmd_buf, (void *)arg, _IOC_SIZE(cmd));
		if (uncopied != 0)
			return -EFAULT;
	}

	switch (cmd) {
        case MDEC_IOCTL_PLAYER_SETUP:
                ret = player_setup((bcm2708_mdec_setup_t *)ioctl_cmd_buf);
                break;

        case MDEC_IOCTL_PLAYER_START:
                ret = player_start();
                break;

        case MDEC_IOCTL_PLAYER_SEND_VIDEO_DATA:
                ret = player_send_video_data((bcm2708_mdec_send_data_t *)ioctl_cmd_buf);
                break;

        case MDEC_IOCTL_PLAYER_SEND_AUDIO_DATA:
                ret = player_send_audio_data((bcm2708_mdec_send_data_t *)ioctl_cmd_buf);
                break;


        case MDEC_IOCTL_PLAYER_STOP:
                ret = player_stop();
                break;

	case MDEC_IOCTL_PLAYER_LOCAL_DBG:
		do_playback((bcm2708_mdec_play_t *)ioctl_cmd_buf);
		break; 

	default: 
		bcm2708mdec_error("Wrong IOCTL cmd\n");
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int mdec_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return 0;
}

static struct file_operations mdec_file_ops =
{
    owner:      THIS_MODULE,
    open:       mdec_open,
    release:    mdec_release,
    read:       mdec_read,
    write:      mdec_write,
    ioctl:      mdec_ioctl,
    mmap:       mdec_mmap,
};

static struct proc_dir_entry *mdec_create_proc_entry( const char * const name,
                                                     read_proc_t *read_proc,
                                                     write_proc_t *write_proc )
{
   struct proc_dir_entry *ret = NULL;

   ret = create_proc_entry( name, 0644, NULL);

   if (ret == NULL)
   {
      remove_proc_entry( name, NULL);
      printk(KERN_ALERT "could not initialize %s", name );
   }
   else
   {
      ret->read_proc  = read_proc;
      ret->write_proc = write_proc;
      ret->mode           = S_IFREG | S_IRUGO;
      ret->uid    = 0;
      ret->gid    = 0;
      ret->size           = 37;
   }
   return ret;
}

static int mdec_dummy_read(char *buf, char **start, off_t offset, int count, int *eof, void *data)
{
   int len = 0;

   if (offset > 0)
   {
      *eof = 1;
      return 0;
   }

   *eof = 1;

   return len;
}

#define INPUT_MAX_INPUT_STR_LENGTH   256

static int mdec_proc_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
        char *init_string = NULL;
	bcm2708_mdec_play_t cmd;
        int num;

        init_string = vmalloc(INPUT_MAX_INPUT_STR_LENGTH);

   if(NULL == init_string)
      return -EFAULT;

   memset(init_string, 0, INPUT_MAX_INPUT_STR_LENGTH);

   count = (count > INPUT_MAX_INPUT_STR_LENGTH) ? INPUT_MAX_INPUT_STR_LENGTH : count;

   if(copy_from_user(init_string, buffer, count))
   {
      return -EFAULT;
   }
   init_string[ INPUT_MAX_INPUT_STR_LENGTH  - 1 ] = 0;

	num = simple_strtol(init_string, 0, 0);
   bcm2708mdec_dbg("read from /proc is %d\n", num);

   if (8 == num) 
   {

   cmd.audio_type = MEDIA_DEC_AUDIO_CodingUnused;
   cmd.video_type = MEDIA_DEC_VIDEO_CodingAVC;
   strncpy(cmd.filename, "/mfs/sd/bond2.264", strlen("/mfs/sd/bond2.264"));
   cmd.filename_size = strlen("/mfs/sd/bond2.264");

   do_playback(&cmd);
   } else
   {
	player_stop();	
   }

   vfree(init_string);

   return count;
}


static irqreturn_t bcm2708_mdec_isr(int irq, void *dev_id)
{
	bcm2708mdec_dbg("The MDEC device rxed one interrupt");

	up(&g_mdec->vc_ack_sem);

	if (g_mdec->av_stream_ctl.playback_type & AUDIO_MASK) {
                up(&g_mdec->av_stream_ctl.audio_ctl.vc_buf_sem);
		up(&g_mdec->av_stream_ctl.audio_ctl.arm_buf_sem);
	}
	if (g_mdec->av_stream_ctl.playback_type & VIDEO_MASK) {
		up(&g_mdec->av_stream_ctl.video_ctl.vc_buf_sem);
		up(&g_mdec->av_stream_ctl.video_ctl.arm_buf_sem);
        }

	return IRQ_HANDLED;
}

struct miscdevice mdec_misc_dev = {
    .minor =    MISC_DYNAMIC_MINOR,
    .name =     BCM2708_MDEC_DRIVER_NAME,
    .fops =     &mdec_file_ops
};

static int bcm2708_mdec_probe(struct platform_device *pdev)
{
        int ret = -ENXIO;
        int timeout = 0;
        struct resource *r;
	struct bcm2708_mdec *bcm_mdec = NULL;

        bcm_mdec = kzalloc(sizeof(struct bcm2708_mdec), GFP_KERNEL);
        if (bcm_mdec == NULL) {
                bcm2708mdec_error("Unable to allocate mdec structure\n");
                ret = -ENOMEM;
                goto err_mdec_alloc_failed;
        }
	g_mdec = bcm_mdec;
        platform_set_drvdata(pdev, bcm_mdec);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if(r == NULL) {
                bcm2708mdec_error("Unable to get mdec memory resource\n");
                ret = -ENODEV;
                goto err_no_io_base;
        }
        bcm2708mdec_dbg("MDEC registers start-end (0x%08x)-(0x%08x)\n", r->start, r->end);
	bcm_mdec->reg_base = (void __iomem *)r->start;

	bcm_mdec->irq = platform_get_irq(pdev, 0);
        if(bcm_mdec->irq < 0) {
                bcm2708mdec_error("Unable to get mdec irq resource\n");
                ret = -ENODEV;
                goto err_no_irq;
        }

        ret = request_irq(bcm_mdec->irq, bcm2708_mdec_isr, IRQF_DISABLED,
                                "bcm2708 mdec interrupt", (void *)bcm_mdec);
        if (ret < 0) {
                bcm2708mdec_error("Unable to register Interrupt for bcm2708 MDEC\n");
                goto err_no_irq;
        }

	sema_init(&g_mdec->vc_ack_sem, 0);

	ret = misc_register(&mdec_misc_dev);
	if (ret < 0) {
		bcm2708mdec_error("failed to register char device\n");
		goto err_reg_chrdev;
	}

       mdec_create_proc_entry("bcm2835_mdec", mdec_dummy_read, mdec_proc_write);

       bcm2708mdec_dbg("The MDEC device is probed successfully");

	// Turn off incase U-Boot was running splash screen
	MEDIA_DEC_REGISTER_RW( MEDIA_DEC_CONTROL_OFFSET ) = 0x0;
	while (MEDIA_DEC_REGISTER_RW( MEDIA_DEC_STATUS_OFFSET)) {
		schedule_timeout(10);
		timeout++;
		if (timeout > 100) {
			printk("Error disabling MDEC control\n");
			break;
		}
		bcm2708mdec_dbg("slept for 1 sec in disabling control\n");
	}

	return 0;

err_reg_chrdev:
	free_irq(bcm_mdec->irq, NULL);
err_no_irq:
err_no_io_base:
	kfree(bcm_mdec);
err_mdec_alloc_failed:
	return ret;

}

static int __devexit bcm2708_mdec_remove(struct platform_device *pdev)
{
        struct bcm2708_mdec *bcm_mdec = platform_get_drvdata(pdev);

	free_irq(bcm_mdec->irq, NULL);
	misc_deregister(&mdec_misc_dev);
        kfree(bcm_mdec);
        bcm2708mdec_dbg("BCM2708 MDEC device removed!!\n");

        return 0;
}


static struct platform_driver bcm2708_mdec_driver = {
        .probe          = bcm2708_mdec_probe,
        .remove         = __devexit_p(bcm2708_mdec_remove),
        .driver = {
                .name = "bcm2835_MEDD"
        }
};

static int __init bcm2708_mdec_init(void)
{
        int ret;

        ret = platform_driver_register(&bcm2708_mdec_driver);
        if (ret)
                printk(KERN_ERR BCM2708_MDEC_DRIVER_NAME "%s : Unable to register BCM2708 MDEC driver\n", __func__);

        printk(KERN_INFO BCM2708_MDEC_DRIVER_NAME "Init %s !\n", ret ? "FAILED" : "OK");

        return ret;
}

static void __exit bcm2708_mdec_exit(void)
{
        /* Clean up .. */
        platform_driver_unregister(&bcm2708_mdec_driver);

        printk(KERN_INFO BCM2708_MDEC_DRIVER_NAME "BRCM MDEC driver exit OK\n");
}

module_init(bcm2708_mdec_init);
module_exit(bcm2708_mdec_exit);
