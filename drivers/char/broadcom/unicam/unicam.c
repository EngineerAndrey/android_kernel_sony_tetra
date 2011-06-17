/*******************************************************************************
Copyright 2010 Broadcom Corporation.  All rights reserved.

Unless you and Broadcom execute a separate written software license agreement
governing use of this software, this software is licensed to you under the
terms of the GNU General Public License version 2, available at
http://www.gnu.org/copyleft/gpl.html (the "GPL").

Notwithstanding the above, under no circumstances may you combine this software
in any way with any other Broadcom software provided under a license other than
the GPL, without Broadcom's express prior written consent.
*******************************************************************************/

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <mach/irqs.h>
#include <asm/io.h>
#include <linux/clk.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <linux/broadcom/unicam.h>
#include <mach/rdb/brcm_rdb_sysmap.h>
#include <mach/rdb/brcm_rdb_pwrmgr.h>
#include <mach/rdb/brcm_rdb_cam.h>

#include <mach/memory.h>
#include <mach/rdb/brcm_rdb_mm_cfg.h>
#include <mach/rdb/brcm_rdb_mm_clk_mgr_reg.h>
#include <mach/rdb/brcm_rdb_root_clk_mgr_reg.h>
#include <mach/rdb/brcm_rdb_padctrlreg.h>
#include <mach/rdb/brcm_rdb_util.h>


//TODO - define the major device ID
#define UNICAM_DEV_MAJOR	0

#define RHEA_UNICAM_BASE_PERIPHERAL_ADDRESS	   MM_CSI0_BASE_ADDR
#define RHEA_MM_CFG_BASE_ADDRESS               MM_CFG_BASE_ADDR
#define RHEA_MM_CLK_BASE_ADDRESS               MM_CLK_BASE_ADDR
#define RHEA_PAD_CTRL_BASE_ADDRESS             PAD_CTRL_BASE_ADDR
#define RHEA_ROOT_CLK_BASE_ADDRESS             ROOT_CLK_BASE_ADDR


#define IRQ_UNICAM	     (156+32)

#define CSI0_UNICAM_PORT     0
#define CSI0_UNICAM_GPIO     12
#define CSI0_UNICAM_CLK      0

#define CSI1_UNICAM_PORT     1
#define CSI1_UNICAM_GPIO     13
#define CSI1_UNICAM_CLK      0

#define UNICAM_MEM_POOL_SIZE   SZ_8M

#define UNICAM_DEBUG
#ifdef UNICAM_DEBUG
    #define dbg_print(fmt, arg...) \
    printk(KERN_ALERT "%s():" fmt, __func__, ##arg)
#else
    #define dbg_print(fmt, arg...)   do { } while (0)
#endif

#define err_print(fmt, arg...) \
    printk(KERN_ERR "%s():" fmt, __func__, ##arg)

static int unicam_major = UNICAM_DEV_MAJOR;
static struct class *unicam_class;
static void __iomem *unicam_base = NULL;
static void __iomem *mmcfg_base = NULL;
static void __iomem *mmclk_base = NULL;
static void __iomem *padctl_base = NULL;
static void __iomem *rootclk_base = NULL;

static struct clk *unicam_clk;

typedef struct {
    mem_t mempool;
    struct semaphore irq_sem;
} unicam_t;

void *unicam_mempool_base;	// declared and allocated in mach
static unsigned int unicam_mempool_size;

static int enable_unicam_clock(void);
static void disable_unicam_clock(void);
static void unicam_init_camera_intf(void);
static void unicam_open_csi(unsigned int port, unsigned int clk_src);
static void unicam_close_csi(unsigned int port, unsigned int clk_src);
static void unicam_sensor_control(unsigned int sensor_id, unsigned int enable);

static inline unsigned int reg_read(void __iomem *, unsigned int reg);
static inline void reg_write(void __iomem *, unsigned int reg, unsigned int value);

static irqreturn_t unicam_isr(int irq, void *dev_id)
{
    unicam_t *dev;
    unsigned int value;
    
    value = reg_read(unicam_base, CAM_STA_OFFSET);
    reg_write(unicam_base, CAM_STA_OFFSET, value);	// enable access		
		
    dev = (unicam_t *)dev_id;	
    up(&dev->irq_sem);

    return IRQ_RETVAL(1);
}

static int unicam_open(struct inode *inode, struct file *filp)
{
    int ret = 0;

    unicam_t *dev = kmalloc(sizeof(unicam_t), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    filp->private_data = dev;
	
    dev->mempool.ptr = unicam_mempool_base;
    dev->mempool.addr = virt_to_phys(dev->mempool.ptr);
    dev->mempool.size = unicam_mempool_size;

    sema_init(&dev->irq_sem, 0);
    
    unicam_init_camera_intf();

    ret = request_irq(IRQ_UNICAM, unicam_isr, IRQF_DISABLED | IRQF_SHARED, UNICAM_DEV_NAME, dev);
    if (ret){
        err_print("request_irq failed ret = %d\n", ret);
        goto err;
    }

    disable_irq(IRQ_UNICAM);
    return 0;

err:
    if (dev)
        kfree(dev);
    return ret;
}

static int unicam_release(struct inode *inode, struct file *filp)
{
    unicam_t *dev = (unicam_t *)filp->private_data;
    
    free_irq(IRQ_UNICAM, dev);
    if (dev)
        kfree(dev);

    return 0;
}

static int unicam_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long vma_size = vma->vm_end - vma->vm_start;
	unicam_t *dev = (unicam_t *)(filp->private_data);

    if (vma_size & (~PAGE_MASK)) {
        pr_err(KERN_ERR "unicam_mmap: mmaps must be aligned to a multiple of pages_size.\n");
        return -EINVAL;
    }

    if (!vma->vm_pgoff) {
        vma->vm_pgoff = RHEA_UNICAM_BASE_PERIPHERAL_ADDRESS >> PAGE_SHIFT;
    } else if (vma->vm_pgoff != (dev->mempool.addr >> PAGE_SHIFT)) {
        pr_err("%s(): unicam_mmap failed\n", __FUNCTION__);
        return -EINVAL;
    }	

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    /* Remap-pfn-range will mark the range VM_IO and VM_RESERVED */
    if (remap_pfn_range(vma,
                       vma->vm_start,
                       vma->vm_pgoff,
                       vma_size,
                       vma->vm_page_prot)) {
        pr_err("%s(): remap_pfn_range() failed\n", __FUNCTION__);
        return -EINVAL;
    }

    return 0;
}

static int unicam_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
    unicam_t *dev;
    int ret = 0;

    if(_IOC_TYPE(cmd) != BCM_UNICAM_MAGIC)
        return -ENOTTY;

    if(_IOC_NR(cmd) > UNICAM_CMD_LAST)
        return -ENOTTY;

    if(_IOC_DIR(cmd) & _IOC_READ)
        ret = !access_ok(VERIFY_WRITE, (void *) arg, _IOC_SIZE(cmd));

    if(_IOC_DIR(cmd) & _IOC_WRITE)
        ret |= !access_ok(VERIFY_READ, (void *) arg, _IOC_SIZE(cmd));

    if(ret)
        return -EFAULT;

    dev = (unicam_t *)(filp->private_data);

    switch (cmd)
    {
    case UNICAM_IOCTL_WAIT_IRQ:
    {        
        dbg_print("Enabling unicam interrupt\n");

        enable_irq(IRQ_UNICAM);
        dbg_print("Waiting for interrupt\n");
        if (down_interruptible(&dev->irq_sem))
        {
            disable_irq(IRQ_UNICAM);
            return -ERESTARTSYS;
        }
        dbg_print("Disabling unicam interrupt\n");
        disable_irq(IRQ_UNICAM);
    }
    break;

    case UNICAM_IOCTL_GET_MEMPOOL:
    {        
        dbg_print("Obtain the Memory Pool Address\n");
        if (copy_to_user((mem_t*)arg, &(dev->mempool), sizeof(mem_t)))
            ret = -EPERM;
    }
    break;

    case UNICAM_IOCTL_OPEN_CSI0:
    {        
        dbg_print("Open unicam CSI0 port \n");
        unicam_open_csi(CSI0_UNICAM_PORT, CSI0_UNICAM_CLK);
    }
    break;

    case UNICAM_IOCTL_CLOSE_CSI0:
    {        
        dbg_print("Close unicam CSI0 port \n");
        unicam_close_csi(CSI0_UNICAM_PORT, CSI0_UNICAM_CLK);
    }
    break;

    case UNICAM_IOCTL_OPEN_CSI1:
    {        
        dbg_print("Open unicam CSI1 port \n");
        unicam_open_csi(CSI1_UNICAM_PORT, CSI1_UNICAM_CLK);
    }
    break;

    case UNICAM_IOCTL_CLOSE_CSI1:
    {        
        dbg_print("close unicam CSI1 port \n");
        unicam_close_csi(CSI1_UNICAM_PORT, CSI1_UNICAM_CLK);
    }
    break;

    case UNICAM_IOCTL_CONFIG_SENSOR:
    {
        sensor_ctrl_t sensor_ctrl;
        
        dbg_print("Config Sensor \n");
        if (copy_from_user(&sensor_ctrl, (sensor_ctrl_t*)arg,  sizeof(sensor_ctrl_t)))
            ret = -EPERM;
  
        unicam_sensor_control(sensor_ctrl.sensor_id, sensor_ctrl.enable);
    }
    break;
	
    default:
    break;
   }
   
    return ret;
}

static struct file_operations unicam_fops =
{
    .open      = unicam_open,
    .release   = unicam_release,
    .mmap      = unicam_mmap,
    .ioctl     = unicam_ioctl,
};


static inline unsigned int reg_read(void __iomem * base_addr, unsigned int reg)
{
    unsigned int flags;

    flags = ioread32(base_addr + reg);
    return flags;
}

static inline void reg_write(void __iomem * base_addr, unsigned int reg, unsigned int value)
{
    iowrite32(value, base_addr + reg);
}

static void unicam_init_camera_intf(void)
{   
    // Init GPIO's to off
    gpio_request(CSI0_UNICAM_GPIO, "CAM_STNDBY0");
    gpio_direction_output(CSI0_UNICAM_GPIO, 0);
    gpio_set_value(CSI0_UNICAM_GPIO, 0);
    gpio_request(CSI1_UNICAM_GPIO, "CAM_STNDBY1");
    gpio_direction_output(CSI1_UNICAM_GPIO, 0);
    gpio_set_value(CSI1_UNICAM_GPIO, 0);
    msleep(10);
}

static void unicam_sensor_control(unsigned int sensor_id, unsigned int enable)
{
    // primary sensor 
    if (sensor_id == 0) {
        gpio_set_value(CSI0_UNICAM_GPIO, enable);
    }
    // secondary sensor
    else if (sensor_id == 1) {
        gpio_set_value(CSI1_UNICAM_GPIO, enable);    
    }
    msleep(10);
}

static void unicam_open_csi(unsigned int port, unsigned int clk_src)
{
    unsigned int value;
   
    if (port == 0) {
        // Set Camera CSI0 Phy & Clock Registers
        reg_write(mmcfg_base, MM_CFG_CSI0_LDO_CTL_OFFSET, 0x5A00000F);
    
        reg_write(mmclk_base, MM_CLK_MGR_REG_WR_ACCESS_OFFSET, 0xA5A501);	// enable access	
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI0_PHY_DIV_OFFSET, 0x00000888);	// csi0_rx0_bclkhs	
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI0_DIV_OFFSET, 0x00000040);
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI0_LP_CLKGATE_OFFSET, 0x00000303);  // default value
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI0_AXI_CLKGATE_OFFSET, 0x00000303);	// ...	
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI0_AXI_CLKGATE_OFFSET, (1 << MM_CLK_MGR_REG_DIV_TRIG_CSI0_LP_TRIGGER_SHIFT)); // CSI0 trigger change	
    		        
        // Select Camera Phy AFE 0 
        // AFE 0 Select:  CSI0 has PHY selection.
        value = reg_read(mmcfg_base, MM_CFG_CSI0_PHY_CTRL_OFFSET) & 0x7fffffff;
        reg_write(mmcfg_base, MM_CFG_CSI0_PHY_CTRL_OFFSET, value);	// enable access	
    }
    else {
        // Set Camera CSI1 Phy & Clock Registers
        reg_write(mmcfg_base, MM_CFG_CSI1_LDO_CTL_OFFSET, 0x5A00000F);
    
        reg_write(mmclk_base, MM_CLK_MGR_REG_WR_ACCESS_OFFSET, 0xA5A501);	// enable access	
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI1_PHY_DIV_OFFSET, 0x00000888);	// csi1_rx0_bclkhs	
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI1_DIV_OFFSET, 0x00000040);
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI1_LP_CLKGATE_OFFSET, 0x00000303);  // default value
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI1_AXI_CLKGATE_OFFSET, 0x00000303);	// ...	
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI1_AXI_CLKGATE_OFFSET, (1 << MM_CLK_MGR_REG_DIV_TRIG_CSI1_LP_TRIGGER_SHIFT)); // CSI1 trigger change	
    		        
        // Select Camera Phy AFE 1 
        // AFE 1 Select:  CSI0 has PHY selection.
        value = reg_read(mmcfg_base, MM_CFG_CSI0_PHY_CTRL_OFFSET) | 0x80000000;
        reg_write(mmcfg_base, MM_CFG_CSI0_PHY_CTRL_OFFSET, value);	// enable access	
    }
        
    if (clk_src == 0)
    {                   
        // Enable DIG0 clock out to sensor 
        // Select DCLK1  ( bits 10:8 = 0x000 => DCLK1 , bits 2:0 = 3 => 8 mAmps strength
        value = reg_read(padctl_base, PADCTRLREG_DCLK1_OFFSET) & (~PADCTRLREG_DCLK1_PINSEL_DCLK1_MASK);
        reg_write(padctl_base, PADCTRLREG_DCLK1_OFFSET, value);
            
        // Disable Dig Clk0
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_WR_ACCESS_OFFSET,0xA5A501);
        value = reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & (~ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH0_CLK_EN_MASK);
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET,value);
        if ((reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH0_STPRSTS_MASK) != 0) { 
            err_print("DIGITAL_CH0_STPRSTS: Clk not Stopped\n");
        }
				
        // Set Dig Clk0 Divider = 1 (13Mhz)
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG0_DIV_OFFSET, (1 << ROOT_CLK_MGR_REG_DIG0_DIV_DIGITAL_CH0_DIV_SHIFT));

        // Trigger Dig Clk0 
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG_TRG_OFFSET, (1 << ROOT_CLK_MGR_REG_DIG_TRG_DIGITAL_CH0_TRIGGER_SHIFT));
        msleep(1);
    
        // Start Dig Clk0
        value = reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) | (1 << ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH0_CLK_EN_SHIFT);		
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET,value);
        msleep(1);
            
        // Check Clock running
        if ((reg_read(rootclk_base , ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH0_STPRSTS_MASK) == 0 ) { 
            err_print("DIGITAL_CH0_STPRSTS: Clk not Started\n");
        }
    }
    else  //if (clk_src == 1)
    {                   
        // Enable DIG1 clock out sensor 
        // Select DCLK2  ( bits 10:8 = 0x000 => DCLK2 , bits 2:0 = 3 => 8 mAmps strength
        value = reg_read(padctl_base, PADCTRLREG_GPIO32_OFFSET) & (~PADCTRLREG_GPIO32_PINSEL_GPIO32_MASK);
        value |= (3 << PADCTRLREG_GPIO32_PINSEL_GPIO32_SHIFT);
        reg_write(padctl_base, PADCTRLREG_GPIO32_OFFSET, value);
            
        // Disable Dig Clk1
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_WR_ACCESS_OFFSET,0xA5A501);
        value = reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & ~(ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH1_CLK_EN_MASK);
        value |= (1 << ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH1_HW_SW_GATING_SEL_SHIFT);
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET,value);
        
        if ((reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH1_STPRSTS_MASK) != 0) { 
            err_print("DIGITAL_CH1_STPRSTS: Clk not Stopped\n");
        }
				
        // Set Dig Clk1 Divider = 1 (13Mhz)
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG1_DIV_OFFSET, (1 << ROOT_CLK_MGR_REG_DIG1_DIV_DIGITAL_CH1_DIV_SHIFT));
 
        // Trigger Dig Clk1 
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG_TRG_OFFSET, (1 << ROOT_CLK_MGR_REG_DIG_TRG_DIGITAL_CH1_TRIGGER_SHIFT));
        msleep(1);
    
        // Start Dig Clk1
        value = reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) | (1 << ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH1_CLK_EN_SHIFT);		
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET, value);
        msleep(1);
        
        // Check Clock running
        if ((reg_read(rootclk_base , ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH1_STPRSTS_MASK) == 0 ) { 
            err_print("DIGITAL_CH1_STPRSTS: Clk not Started\n");
        }
    }
}

static void unicam_close_csi(unsigned int port, unsigned int clk_src)
{
    unsigned int value;
    
    if (port == 0) {
        // Disable Camera CSI0 Phy & Clock Registers
        reg_write(mmclk_base, MM_CLK_MGR_REG_WR_ACCESS_OFFSET, 0xA5A501);	// enable access	
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI0_LP_CLKGATE_OFFSET, 0x00000302);  // default value
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI0_AXI_CLKGATE_OFFSET, 0x00000302);	// ...	
    }
    else {
        // Disable Camera CSI1 Phy & Clock Registers
        reg_write(mmclk_base, MM_CLK_MGR_REG_WR_ACCESS_OFFSET, 0xA5A501);	// enable access	
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI1_LP_CLKGATE_OFFSET, 0x00000302);  // default value
        reg_write(mmclk_base, MM_CLK_MGR_REG_CSI1_AXI_CLKGATE_OFFSET, 0x00000302);	// ...	
    } 
        
    if (clk_src == 0)
    {                   
        // Disable Dig Clk0
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_WR_ACCESS_OFFSET,0xA5A501);
        value = reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & (~ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH0_CLK_EN_MASK);
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET,value);
        if ((reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH0_STPRSTS_MASK) != 0) { 
            err_print("DIGITAL_CH0_STPRSTS: Clk not Stopped\n");
        }
    }
    else {                   
        // Disable Dig Clk1
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_WR_ACCESS_OFFSET,0xA5A501);
        value = reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & (~ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH1_CLK_EN_MASK);
        reg_write(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET,value);
        if ((reg_read(rootclk_base, ROOT_CLK_MGR_REG_DIG_CLKGATE_OFFSET) & ROOT_CLK_MGR_REG_DIG_CLKGATE_DIGITAL_CH1_STPRSTS_MASK) != 0) { 
            err_print("DIGITAL_CH1_STPRSTS: Clk not Stopped\n");
        }
    }
}


static int enable_unicam_clock(void)
{
    unsigned long rate;
    int ret;
	
    unicam_clk = clk_get(NULL, "csi0_axi_clk");
    if (!unicam_clk) {
        err_print("%s: error get clock\n", __func__);
        return -EIO;
    }
    
    ret = clk_enable(unicam_clk);
    if (ret) {
        err_print("%s: error enable unicam clock\n", __func__);
        return -EIO;
    }

    ret = clk_set_rate(unicam_clk, 250000000);
    if (ret) {
        err_print("%s: error changing clock rate\n", __func__);
        //return -EIO;
    }

    rate = clk_get_rate(unicam_clk);
    dbg_print("unicam_clk_clk rate %lu\n", rate);
    
    return  0;    
}

static void disable_unicam_clock(void)
{
    unicam_clk = clk_get(NULL, "csi0_axi_clk");
    if (!unicam_clk) return;
    
    clk_disable(unicam_clk);     
}

static int __init setup_unicam_mempool(char *str)
{
    if(str){
        get_option(&str, &unicam_mempool_size);
    }
	
    if (!unicam_mempool_size) 
        unicam_mempool_size = UNICAM_MEM_POOL_SIZE;
	
    dbg_print("Allocating camera relocatable heap of size = %d\n", unicam_mempool_size);
    unicam_mempool_base = alloc_bootmem_low_pages(unicam_mempool_size);
    if( !unicam_mempool_base )
        err_print("Failed to allocate relocatable heap memory\n");
    return 0;
}

__setup("unicam_mem=", setup_unicam_mempool);

int __init unicam_init(void)
{
    int ret;

    dbg_print("unicam driver Init\n");

    ret = register_chrdev(0, UNICAM_DEV_NAME, &unicam_fops);
    if (ret < 0)
        return -EINVAL;
    else
        unicam_major = ret;

    unicam_class = class_create(THIS_MODULE, UNICAM_DEV_NAME);
    if (IS_ERR(unicam_class)) {
        err_print("Failed to create unicam class\n");
        unregister_chrdev(unicam_major, UNICAM_DEV_NAME);
        return PTR_ERR(unicam_class);
    }

    device_create(unicam_class, NULL, MKDEV(unicam_major, 0), NULL, UNICAM_DEV_NAME);
    
    enable_unicam_clock();
	
    // Map the unicam registers 
    unicam_base = (void __iomem *)ioremap_nocache(RHEA_UNICAM_BASE_PERIPHERAL_ADDRESS, SZ_4K);
    if (unicam_base == NULL)
        goto err;
		
    mmcfg_base = (void __iomem *)ioremap_nocache(RHEA_MM_CFG_BASE_ADDRESS, SZ_4K);
    if (mmcfg_base == NULL)
        goto err;

    mmclk_base = (void __iomem *)ioremap_nocache(RHEA_MM_CLK_BASE_ADDRESS, SZ_4K);
    if (mmclk_base == NULL)
        goto err;

    padctl_base = (void __iomem *)ioremap_nocache(RHEA_PAD_CTRL_BASE_ADDRESS, SZ_4K);
    if (padctl_base == NULL)
        goto err;

    rootclk_base = (void __iomem *)ioremap_nocache(RHEA_ROOT_CLK_BASE_ADDRESS, SZ_4K);
    if (rootclk_base == NULL)
        goto err;		

    return 0;

err:
    err_print("Failed to MAP the unicam IO space\n");
    unregister_chrdev(unicam_major, UNICAM_DEV_NAME);
    return ret;
}

void __exit unicam_exit(void)
{
    dbg_print("unicam driver Exit\n");
    if (unicam_base)
        iounmap(unicam_base);
		
    if (mmcfg_base)
        iounmap(mmcfg_base);

    if (mmclk_base)
        iounmap(mmclk_base);

    if (padctl_base)
        iounmap(padctl_base);

    if (rootclk_base)
        iounmap(rootclk_base);	

    disable_unicam_clock();		
   
    device_destroy(unicam_class, MKDEV(unicam_major, 0));
    class_destroy(unicam_class);
    unregister_chrdev(unicam_major, UNICAM_DEV_NAME);
}

module_init(unicam_init);
module_exit(unicam_exit);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("unicam device driver");
MODULE_LICENSE("GPL");
