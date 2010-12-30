/*****************************************************************************
* Copyright 2010 - 2011 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/io.h>

// #include <linux/broadcom/timer.h>

#include <linux/timer.h>
#include "i2c-bsc.h"

#define FPGA

#define DEFAULT_I2C_BUS_SPEED    BSC_BUS_SPEED_50K
#define CMDBUSY_DELAY            100000
#define SES_TIMEOUT              (msecs_to_jiffies(100))

/* upper 5 bits of the master code */
#define MASTERCODE               0x08
#define MASTERCODE_MASK          0x07

#define BSC_DBG(dev, format, args...) \
   do { if (dev->debug) dev_err(dev->dev, format, ## args); } while (0)

#define MAX_PROC_BUF_SIZE         256
#define MAX_PROC_NAME_SIZE        15
#define PROC_GLOBAL_PARENT_DIR    "i2c"
#define PROC_ENTRY_DEBUG          "debug"

struct procfs
{
   char name[MAX_PROC_NAME_SIZE];
   struct proc_dir_entry *parent;
};

/*
 * BSC (I2C) private data structure
 */
struct bsc_i2c_dev
{
   struct device *dev;

   /* iomapped base virtual address of the registers */
   void __iomem *virt_base;
   
   /* I2C bus speed */
   enum bsc_bus_speed speed;

   /* the 8-bit master code (0000 1XXX, 0x08) used for high speed mode */
   unsigned char mastercode;

   /* to save the old BSC TIM register value */
   volatile uint32_t tim_val;

   /* flag to indicate whether the I2C bus is in high speed mode */
   unsigned int high_speed_mode;

   /* IRQ line number */
   int irq;

   /* Linux I2C adapter struct */
   struct i2c_adapter adapter;

   /* lock for data transfer */
   struct semaphore xfer_lock;

   /* to signal the command completion */
   struct completion	ses_done;

   struct procfs proc;

   volatile int debug;
};

static const __devinitconst char gBanner[] = KERN_INFO "Broadcom BSC (I2C) Driver: 1.00\n";

/*
 * Bus speed lookup table
 */
static const unsigned int gBusSpeedTable[BSC_SPD_MAXIMUM] =
{
   BSC_SPD_32K,
   BSC_SPD_50K,
   BSC_SPD_230K,
   BSC_SPD_380K,
   BSC_SPD_400K,
   BSC_SPD_430K,
   BSC_SPD_HS,
   BSC_SPD_100K_FPGA,
   BSC_SPD_400K_FPGA,
   BSC_SPD_HS_FPGA,
};

static struct proc_dir_entry *gProcParent;

/*
 * BSC ISR routine
 */
static irqreturn_t bsc_isr(int irq, void *devid)
{
   struct bsc_i2c_dev *dev = (struct bsc_i2c_dev *)devid;
   uint32_t status;

   /* get interrupt status */
   status = bsc_read_intr_status((uint32_t)dev->virt_base);

   /* got nothing, something is wrong */
   if (!status)
   {
      dev_err(dev->dev, "interrupt with zero status register!\n");
      return IRQ_NONE;
   }

   /* ack and clear the interrupts */
   bsc_clear_intr_status((uint32_t)dev->virt_base, status);

   if (status & I2C_MM_HS_ISR_SES_DONE_MASK)
   {
      complete(&dev->ses_done);
   }
   
   if (status & I2C_MM_HS_ISR_ERR_MASK)
   {
      dev_err(dev->dev, "bus error interrupt (timeout)\n");
   }
   
   return IRQ_HANDLED;
}


/*
 * We should not need to do this in software, but this is how the hardware was
 * designed and that leaves us with no choice but SUCK it
 *
 * When the CPU writes to the control, data, or CRC registers the CMDBUSY bit
 * will be set to high. It will be cleared after the writing action has been
 * transferred from APB clock domain to BSC clock domain and then the status
 * has transfered from BSC clock domain back to APB clock domain
 * 
 * We need to wait for the CMDBUSY to clear because the hardware does not have
 * CMD pipeline registers. This wait is to avoid a previous written CMD/data
 * to be overwritten by the following writing before the previous written
 * CMD/data was executed/synchronized by the hardware
 * 
 * We shouldn't set up an interrupt for this since the context switch overhead
 * is too expensive for this type of action and in fact 99% of time we will
 * experience no wait anyway
 * 
 */
static int bsc_wait_cmdbusy(struct bsc_i2c_dev *dev)
{
   unsigned int count = 0;

   while (bsc_read_intr_status((uint32_t)dev->virt_base) &
          I2C_MM_HS_ISR_CMDBUSY_MASK)
   {
      if (count > CMDBUSY_DELAY)
      {
         BSC_DBG(dev, "CMDBUSY timeout\n");
         return -ETIMEDOUT;
      }

      count++;
   }

   return 0;
}

static int bsc_send_cmd(struct bsc_i2c_dev *dev, BSC_CMD_t cmd)
{
   int rc;
   unsigned long time_left;

   /* make sure the hareware is ready */
   rc = bsc_wait_cmdbusy(dev);
   if (rc < 0)
      return rc;

   /* enable the session done (SES) interrupt */
   bsc_enable_intr((uint32_t)dev->virt_base,
         I2C_MM_HS_IER_I2C_INT_EN_MASK);

   /* mark as incomplete before sending the command */
   INIT_COMPLETION(dev->ses_done);

   /* send the command */
   isl_bsc_send_cmd((uint32_t)dev->virt_base, cmd);

   /*
    * Block waiting for the transaction to finish. When it's finished we'll
    * be signaled by the interrupt
    */
   time_left = wait_for_completion_timeout(&dev->ses_done, SES_TIMEOUT);
   bsc_disable_intr((uint32_t)dev->virt_base,
         I2C_MM_HS_IER_I2C_INT_EN_MASK);
   if (time_left == 0)
   {
      BSC_DBG(dev, "controller timed out\n");

      /* clear command */
      isl_bsc_send_cmd((uint32_t)dev->virt_base, BSC_CMD_NOACTION);

      return -ETIMEDOUT;
   }

   /* clear command */
   isl_bsc_send_cmd((uint32_t)dev->virt_base, BSC_CMD_NOACTION);

   return 0;
}

static int bsc_xfer_start(struct i2c_adapter *adapter)
{
   int rc;
   struct bsc_i2c_dev *dev = i2c_get_adapdata(adapter);

   /* now send the start command */
   rc = bsc_send_cmd(dev, BSC_CMD_START);
   if (rc < 0)
   {
      dev_err(dev->dev, "failed to send the start command\n");
      return rc;
   }

   return 0;
}

static int bsc_xfer_repstart(struct i2c_adapter *adapter)
{
   int rc;
   struct bsc_i2c_dev *dev = i2c_get_adapdata(adapter);

   rc = bsc_send_cmd(dev, BSC_CMD_RESTART);
   if (rc < 0)
   {
      dev_err(dev->dev, "failed to send the restart command\n");
      return rc;
   }

   return 0;
}

static int bsc_xfer_stop(struct i2c_adapter *adapter)
{
   int rc;
   struct bsc_i2c_dev *dev = i2c_get_adapdata(adapter);

   rc = bsc_send_cmd(dev, BSC_CMD_STOP);
   if (rc < 0)
   {
      dev_err(dev->dev, "failed to send the stop command\n");
      return rc;
   }

   return 0;
}

static int bsc_xfer_read_byte(struct i2c_adapter *adapter, uint16_t no_ack,
      unsigned char *data)
{
   int rc;
   struct bsc_i2c_dev *dev = i2c_get_adapdata(adapter);
   BSC_CMD_t cmd;

   if (no_ack)
      cmd = BSC_CMD_READ_NAK;
   else
      cmd = BSC_CMD_READ_ACK;

   /* send the read command */
   rc = bsc_send_cmd(dev, cmd);
   if (rc < 0)
      return rc;

   /*
    * Now read the data from the BSC DATA register. Since BSC does not have
    * an RX FIFO, we can only read one byte at a time
    */
   bsc_read_data((uint32_t)dev->virt_base, data, 1);
   
   return 0;
}

static int bsc_xfer_read(struct i2c_adapter *adapter, struct i2c_msg *msg)
{
   int rc;
   uint16_t no_ack;
   struct bsc_i2c_dev *dev = i2c_get_adapdata(adapter);
   unsigned int bytes_read, cnt = msg->len;
   unsigned char data, *buf = msg->buf;

   no_ack = msg->flags & I2C_M_NO_RD_ACK;
   bytes_read = 0;
   while (cnt > 0)
   {
      rc = bsc_xfer_read_byte(adapter, (no_ack || (cnt == 1)), &data);
      if (rc < 0)
      {
         BSC_DBG(dev, "problem experienced during data read\n");
         break;     
      }

      BSC_DBG(dev, "reading %2.2X\n", data);

      *buf = data;
      buf++;
      bytes_read++;
      cnt--;
   }

   return bytes_read;
}

static int bsc_xfer_write_byte(struct i2c_adapter *adapter, uint16_t nak_ok,
      unsigned char *data)
{
   int rc;
   unsigned long time_left;
   struct bsc_i2c_dev *dev = i2c_get_adapdata(adapter);

   /* make sure the hareware is ready */
   rc = bsc_wait_cmdbusy(dev);
   if (rc < 0)
      return rc;

   /* enable the session done (SES) interrupt */
   bsc_enable_intr((uint32_t)dev->virt_base,
         I2C_MM_HS_IER_I2C_INT_EN_MASK);

   /* mark as incomplete before sending the data */
   INIT_COMPLETION(dev->ses_done);

   /* send data */
   bsc_write_data((uint32_t)dev->virt_base, data, 1);

   /*
    * Block waiting for the transaction to finish. When it's finished we'll
    * be signaled by the interrupt
    */
   time_left = wait_for_completion_timeout(&dev->ses_done, SES_TIMEOUT);
   bsc_disable_intr((uint32_t)dev->virt_base,
         I2C_MM_HS_IER_I2C_INT_EN_MASK);
   if (time_left == 0)
   {
      BSC_DBG(dev, "controller timed out\n");
      return -ETIMEDOUT;
   }

   /* unexpected NAK */
   if (!bsc_get_ack((uint32_t)dev->virt_base) && !nak_ok)
   {
      BSC_DBG(dev, "unexpected NAK\n");
      return -EREMOTEIO;
   }

   return 0;
}

static int bsc_xfer_write(struct i2c_adapter *adapter, struct i2c_msg *msg)
{
   int rc;
   struct bsc_i2c_dev *dev = i2c_get_adapdata(adapter);
   unsigned int bytes_written, cnt = msg->len;
   unsigned char *buf = msg->buf;
   uint16_t nak_ok = msg->flags & I2C_M_IGNORE_NAK;

   bytes_written = 0;
	while (cnt > 0)
   {
      rc = bsc_xfer_write_byte(adapter, nak_ok, buf);
      if (rc < 0)
      {
         BSC_DBG(dev, "problem experienced during data write\n");
         break;     
      }

      BSC_DBG(dev, "writing %2.2X\n", *buf);
      buf++;
      bytes_written++;
      cnt--;
   }

	return bytes_written;
}

static int bsc_xfer_try_address(struct i2c_adapter *adapter,
      unsigned char addr, unsigned short nak_ok, unsigned int retries)
{
   unsigned int i;
   int rc = 0, success = 0;
   struct bsc_i2c_dev *dev = i2c_get_adapdata(adapter);

   BSC_DBG(dev, "0x%02x, %d\n", addr, retries);

   for (i = 0; i <= retries; i++)
   {
      rc = bsc_xfer_write_byte(adapter, nak_ok, &addr);
      if (rc >= 0)
      {
         success = 1;
         break;
      }
   
      /* no luck, let's keep trying */
      rc = bsc_xfer_stop(adapter);
      if (rc < 0)
         break;

      rc = bsc_xfer_start(adapter);
      if (rc < 0)
         break;
   }

   /* unable to find a slave */
   if (!success)
   {
      dev_err(dev->dev, "tried %u times to contact slave device at 0x%02x "
            "but no luck success=%d rc=%d\n", i + 1, addr >> 1, success, rc);
   }

   return rc;
}

static int bsc_xfer_do_addr(struct i2c_adapter *adapter, struct i2c_msg *msg)
{
   int rc;
   unsigned int retries;
   unsigned short flags = msg->flags;
   unsigned short nak_ok = msg->flags & I2C_M_IGNORE_NAK;
   unsigned char addr;

   retries = nak_ok ? 0 : adapter->retries;

   /* ten bit address */
   if (flags & I2C_M_TEN)
   {
      /* first byte is 11110XX0, where XX is the upper 2 bits of the 10 bits */
      addr = 0xF0 | ((msg->addr & 0x300) >> 7);
      rc = bsc_xfer_try_address(adapter, addr, nak_ok, retries);
      if (rc < 0)
         return -EREMOTEIO;

      /* then the remaining 8 bits */
      addr = msg->addr & 0xFF;
      rc = bsc_xfer_write_byte(adapter, nak_ok, &addr);
      if (rc < 0)
         return -EREMOTEIO;

      /* for read */
      if (flags & I2C_M_RD)
      {
         rc = bsc_xfer_repstart(adapter);
         if (rc < 0)
            return -EREMOTEIO;

         /* okay, now re-send the first 7 bits with the read bit */
         addr = 0xF0 | ((msg->addr & 0x300) >> 7);
         addr |= 0x01;
         rc = bsc_xfer_try_address(adapter, addr, nak_ok, retries);
         if (rc < 0)
            return -EREMOTEIO;
      }
   }
   else /* normal 7-bit address */
   {
      addr = msg->addr << 1;
      if (flags & I2C_M_RD)
         addr |= 1;
      if (flags & I2C_M_REV_DIR_ADDR)
         addr ^= 1;
      rc = bsc_xfer_try_address(adapter, addr, nak_ok, retries);
      if (rc < 0)
         return -EREMOTEIO;
   }

   return 0;
}

/*
 * Master tranfer function
 */
static int bsc_xfer(struct i2c_adapter *adapter, struct i2c_msg msgs[],
      int num)
{
   struct bsc_i2c_dev *dev = i2c_get_adapdata(adapter);
   struct i2c_msg *pmsg;
   int rc;
   unsigned short i, nak_ok;

   down(&dev->xfer_lock);

   /* send start command */
   rc = bsc_xfer_start(adapter);
   if (rc < 0)
   {
      dev_err(dev->dev, "start command failed\n");
      up(&dev->xfer_lock);
      return rc;
   }

   /* high-speed mode */
   if (dev->high_speed_mode)
   {
      /* send the master code in fast speed mode first */
      rc = bsc_xfer_write_byte(adapter, 1, &dev->mastercode);
      if (rc < 0)
      {
         dev_err(dev->dev, "high-speed master code failed\n");
         up(&dev->xfer_lock);
         return rc;
      }

      /* check to make sure no slave replied to the master code by accident */
      if (bsc_get_ack((uint32_t)dev->virt_base))
      {
         dev_err(dev->dev, "one of the slaves replied to the high-speed "
               "master code unexpectedly\n");
         up(&dev->xfer_lock);
         return -EREMOTEIO;
      }

      /* now configure the bus into high-speed mode */
      bsc_start_highspeed((uint32_t)dev->virt_base);

      /* now send the restart command in high-speed */
      rc = bsc_xfer_repstart(adapter);
      if (rc < 0)
      {
         dev_err(dev->dev, "restart command failed\n");
         up(&dev->xfer_lock);
         return rc;
      }
   }

   /* loop through all messages */
   for (i = 0; i < num; i++)
   {
      pmsg = &msgs[i];
      nak_ok = pmsg->flags & I2C_M_IGNORE_NAK;

      /* need restart + slave address */
      if (!(pmsg->flags & I2C_M_NOSTART))
      {
         /* send repeated start only on subsequent messages */
         if (i)
         {
            rc = bsc_xfer_repstart(adapter);
            if (rc < 0)
            {
               dev_err(dev->dev, "restart command failed\n");
               up(&dev->xfer_lock);
               return rc;
            }
         }

         rc = bsc_xfer_do_addr(adapter, pmsg);
         if (rc < 0)
         {
            dev_err(dev->dev, "NAK from device addr %2.2x msg#%d\n",
                  pmsg->addr, i);
            up(&dev->xfer_lock);
            return rc;
         }
      }

      /* read from the slave */
      if (pmsg->flags & I2C_M_RD)
      {
         rc = bsc_xfer_read(adapter, pmsg);
         BSC_DBG(dev, "read %d bytes msg#%d\n", rc, i);
         if (rc < pmsg->len)
         {
            dev_err(dev->dev, "read %d bytes but asked for %d bytes\n",
                  rc, pmsg->len);
            up(&dev->xfer_lock);
            return (rc < 0)? rc : -EREMOTEIO;
         }
      }
      else /* write to the slave */
      { 
         /* write bytes from buffer */
         rc = bsc_xfer_write(adapter, pmsg);
         BSC_DBG(dev, "wrote %d bytes msg#%d\n", rc, i);
         if (rc < pmsg->len)
         {
            dev_err(dev->dev, "wrote %d bytes but asked for %d bytes\n",
                  rc, pmsg->len);
            up(&dev->xfer_lock);
            return (rc < 0)? rc : -EREMOTEIO;
         }
      }
   }

   /* high-speed mode */
   if (dev->high_speed_mode)
   {
      /* restore the old BSC_TIM register value before the stop command */
      bsc_set_tim((uint32_t)dev->virt_base, dev->tim_val);
   }
   
   /* send stop command */
	rc = bsc_xfer_stop(adapter);
   if (rc < 0)
      dev_err(dev->dev, "stop command failed\n");

   /* high-speed mode */
   if (dev->high_speed_mode)
   {
      /* stop high-speed and switch back to fast-speed */
      bsc_stop_highspeed((uint32_t)dev->virt_base);
   }
   
   up(&dev->xfer_lock);
	return (rc < 0) ? rc : num;
}

static u32 bsc_functionality(struct i2c_adapter *adap)
{
   return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
          I2C_FUNC_10BIT_ADDR | I2C_FUNC_PROTOCOL_MANGLING;
}

static struct i2c_algorithm bsc_algo =
{
   .master_xfer = bsc_xfer,
   .functionality = bsc_functionality,
};

static int
proc_debug_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
   struct bsc_i2c_dev *dev = (struct bsc_i2c_dev *)data;
   int rc;
   unsigned int debug;
   unsigned char kbuf[MAX_PROC_BUF_SIZE];

   if (count > MAX_PROC_BUF_SIZE)
      count = MAX_PROC_BUF_SIZE;

   rc = copy_from_user(kbuf, buffer, count);
   if (rc)
   {
      dev_err(dev->dev, "copy_from_user failed status=%d", rc);
      return -EFAULT;
   }

   if (sscanf(kbuf, "%u", &debug) != 1)
   {
      printk(KERN_ERR "echo <debug> > %s\n", PROC_ENTRY_DEBUG);
      return count;
   }

   if (debug)
      dev->debug = 1;
   else
      dev->debug = 0;

   return count;
}

static int
proc_debug_read(char *buffer, char **start, off_t off, int count,
		int *eof, void *data)
{
   unsigned int len = 0;
   struct bsc_i2c_dev *dev = (struct bsc_i2c_dev *)data;

   if (off > 0)
      return 0;

   len += sprintf(buffer + len, "Debug print is %s\n",
         dev->debug ? "enabled" : "disabled");
   
   return len;
}

static int proc_init(struct platform_device *pdev)
{
   int rc;
   struct bsc_i2c_dev *dev = platform_get_drvdata(pdev);
   struct procfs *proc = &dev->proc;
   struct proc_dir_entry *proc_debug;
   
   snprintf(proc->name, sizeof(proc->name), "%s%d", PROC_GLOBAL_PARENT_DIR,
         pdev->id);

   /* sub directory */
   proc->parent = proc_mkdir(proc->name, gProcParent);
   if (proc->parent == NULL)
   {
      return -ENOMEM;
   }

   proc_debug = create_proc_entry(PROC_ENTRY_DEBUG, 0644, proc->parent);
   if (proc_debug == NULL)
   {
      rc = -ENOMEM;
      goto err_del_parent;
   }
   proc_debug->read_proc = proc_debug_read;
   proc_debug->write_proc = proc_debug_write;
   proc_debug->data = dev;

   return 0;

err_del_parent:
   remove_proc_entry(proc->name, gProcParent);
   return rc;
}

static int proc_term(struct platform_device *pdev)
{
   struct bsc_i2c_dev *dev = platform_get_drvdata(pdev);
   struct procfs *proc = &dev->proc;

   remove_proc_entry(PROC_ENTRY_DEBUG, proc->parent);
   remove_proc_entry(proc->name, gProcParent);

   return 0;
}

static int __init bsc_probe(struct platform_device *pdev)
{
   int rc, irq;
   struct bsc_adap_cfg *hw_cfg;
   struct bsc_i2c_dev *dev;
   struct i2c_adapter *adap;
   struct resource *iomem, *ioarea;

   printk(gBanner);

   /* get register memory resource */
   iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
   if (!iomem)
   {
      dev_err(&pdev->dev, "no mem resource\n");
      return -ENODEV;
   }

   /* get the interrupt number */
   irq = platform_get_irq(pdev, 0);
   if (irq == -ENXIO)
   {
      dev_err(&pdev->dev, "no irq resource\n");
      return -ENODEV;
   }

   /* mark the memory region as used */
   ioarea = request_mem_region(iomem->start, resource_size(iomem),
			pdev->name);
   if (!ioarea)
   {
		dev_err(&pdev->dev, "I2C region already claimed\n");
		return -EBUSY;
   }

   /* allocate memory for our private data structure */
   dev = kzalloc(sizeof(*dev), GFP_KERNEL);
   if (!dev)
   {
      dev_err(&pdev->dev, "unable to allocate mem for private data\n");
      rc = -ENOMEM;
      goto err_release_mem_region;
   }
   
   if (pdev->dev.platform_data)
   {
      hw_cfg = (struct bsc_adap_cfg *)pdev->dev.platform_data;
      dev->speed = hw_cfg->speed;
   }
   else
   {
      /* use default speed */
      dev->speed = DEFAULT_I2C_BUS_SPEED;
   }

   /* validate the speed parameter */
   if (dev->speed >= BSC_BUS_SPEED_MAX)
   {
      dev_err(&pdev->dev, "invalid bus speed parameter\n");
      rc = -EFAULT;
      goto err_release_mem_region;
   }

   /* high speed */
   if (dev->speed == BSC_BUS_SPEED_HS ||
       dev->speed == BSC_BUS_SPEED_HS_FPGA)
   {
      dev->high_speed_mode = 1;
      
   }

   dev->dev = &pdev->dev;
   init_MUTEX(&dev->xfer_lock);
   init_completion(&dev->ses_done);
   dev->irq = irq;
   dev->virt_base = ioremap(iomem->start, resource_size(iomem));
   if (!dev->virt_base)
   {
      dev_err(&pdev->dev, "ioremap of register space failed\n");
      rc = -ENOMEM;
      goto err_free_dev_mem;
   }

   platform_set_drvdata(pdev, dev);

   /*
    * TODO: add core clock code in the future 13 MHz for normal and 104 MHz
    * for high-speed
    */

   /*
    * Configure the BSC bus speed. If it's a high-speed device, it will start
    * off as fast speed
    */
   bsc_set_bus_speed((uint32_t)dev->virt_base,
         gBusSpeedTable[dev->speed]);

   /* init I2C controller */
   isl_bsc_init((uint32_t)dev->virt_base);

   /* disable and clear interrupts */
   bsc_disable_intr((uint32_t)dev->virt_base, 0xFF);
   bsc_clear_intr_status((uint32_t)dev->virt_base, 0xFF);

   /* disable BSC TX FIFO */
   bsc_set_FIFO((uint32_t)dev->virt_base, 0);

   /* high-speed mode */
   if (dev->speed == BSC_BUS_SPEED_HS)
   {
      dev->high_speed_mode = 1;

      /*
       * Since mastercode 0000 1000 is reserved for test and diagnostic
       * purpose, we add 1 here
       */
      dev->mastercode = (MASTERCODE | (MASTERCODE_MASK & pdev->id)) + 1;

      /* 
       * Auto-sense allows the slave device to stretch the clock for a long
       * time. Need to turn off auto-sense for high-speed mode
       */
      bsc_set_autosense((uint32_t)dev->virt_base, 0);

      /*
       * Now save the BSC_TIM register value as it will be modified before the
       * master going into high-speed mode. We need to restore the BSC_TIM
       * value when the device switches back to fast speed
       */
      dev->tim_val = bsc_get_tim((uint32_t)dev->virt_base);
   }
   else
   {
      dev->high_speed_mode = 0;
      bsc_set_autosense((uint32_t)dev->virt_base, 1);
   }

   /* register the ISR handler */
   rc = request_irq(dev->irq, bsc_isr, IRQF_SHARED, pdev->name, dev);
   if (rc)
   {
      dev_err(&pdev->dev, "failed to request irq %i\n", dev->irq);
      goto err_bsc_deinit;
   }

   dev_info(dev->dev, "bus %d at speed %d \n", pdev->id, dev->speed);

   adap = &dev->adapter;
   i2c_set_adapdata(adap, dev);
   adap->owner = THIS_MODULE;
   adap->class = UINT_MAX; /* can be used by any I2C device */
   snprintf(adap->name, sizeof(adap->name), "bsc-i2c%d", pdev->id);
   adap->algo = &bsc_algo;
   adap->dev.parent = &pdev->dev;
   adap->nr = pdev->id;

   /* TODO: register proc entries here */

   /*
    * Enable error (timeout) interrupt if it's not in high-speed mode. The
    * timeout counter does not work in high-speed mode
    */
   if (!dev->high_speed_mode)
      bsc_enable_intr((uint32_t)dev->virt_base,
            I2C_MM_HS_IER_ERR_INT_EN_MASK);

   rc = proc_init(pdev);
   if (rc)
   {
      dev_err(dev->dev, "failed to install procfs\n");
      goto err_free_irq;
   }

   /*
    * I2C device drivers may be active on return from
    * i2c_add_numbered_adapter()
    */
   rc = i2c_add_numbered_adapter(adap);
   if (rc) {
      dev_err(dev->dev, "failed to add adapter\n");
      goto err_proc_term;
   }

	return 0;

err_proc_term:
   proc_term(pdev);

err_free_irq:
   free_irq(dev->irq, dev);

err_bsc_deinit:
   bsc_set_autosense((uint32_t)dev->virt_base, 0);
   bsc_deinit((uint32_t)dev->virt_base);

   iounmap(dev->virt_base);

   platform_set_drvdata(pdev, NULL);

err_free_dev_mem:
   kfree(dev);

err_release_mem_region:
	release_mem_region(iomem->start, resource_size(iomem));

   return rc;
}

static int bsc_remove(struct platform_device *pdev)
{
   struct bsc_i2c_dev *dev = platform_get_drvdata(pdev);
   struct resource *iomem;

   i2c_del_adapter(&dev->adapter);

   proc_term(pdev);

   platform_set_drvdata(pdev, NULL);
   free_irq(dev->irq, dev);

   bsc_set_autosense((uint32_t)dev->virt_base, 0);
   bsc_deinit((uint32_t)dev->virt_base);

   iounmap(dev->virt_base);
   kfree(dev);

   iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
   release_mem_region(iomem->start, resource_size(iomem));

   return 0;
}

#ifdef CONFIG_PM
static int bsc_suspend(struct platform_device *pdev, pm_message_t state)
{
   (void)pdev;
   (void)state;
   
   /* TODO: add suspend support in the future */
   
   return 0;
}

static int bsc_resume(struct platform_device *pdev)
{
   (void)pdev;

   /* TODO: add resume support in the future */
   
   return 0;
}
#else
#define bsc_suspend    NULL
#define bsc_resume     NULL
#endif

static struct platform_driver bsc_driver = 
{
   .driver = 
   {
      .name = "bsc-i2c",
      .owner = THIS_MODULE,
   },
   .probe   = bsc_probe,
   .remove  = bsc_remove,
   .suspend = bsc_suspend,
   .resume  = bsc_resume,
};

static int __init bsc_init(void)
{
   int rc;

   gProcParent = proc_mkdir(PROC_GLOBAL_PARENT_DIR, NULL);
   if (gProcParent == NULL)
   {
      printk(KERN_ERR "I2C driver procfs failed\n");
      return -ENOMEM;
   }

   rc = platform_driver_register(&bsc_driver);
   if (rc < 0)
   {
      printk(KERN_ERR "I2C driver init failed\n");
      remove_proc_entry(PROC_GLOBAL_PARENT_DIR, NULL);
      return rc;
   }

   return 0; 
}

static void __exit bsc_exit(void)
{
   platform_driver_unregister(&bsc_driver);
   remove_proc_entry(PROC_GLOBAL_PARENT_DIR, NULL);
}

module_init(bsc_init);
module_exit(bsc_exit);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Broadcom I2C Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
