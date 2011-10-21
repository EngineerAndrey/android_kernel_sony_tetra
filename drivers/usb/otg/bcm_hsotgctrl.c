/*****************************************************************************
*  Copyright 2001 - 2011 Broadcom Corporation.  All rights reserved.
*
*  Unless you and Broadcom execute a separate written software license
*  agreement governing use of this software, this software is licensed to you
*  under the terms of the GNU General Public License version 2, available at
*  http://www.gnu.org/licenses/old-license/gpl-2.0.html (the "GPL").
*
*  Notwithstanding the above, under no circumstances may you combine this
*  software in any way with any other Broadcom software provided under a
*  license other than the GPL, without Broadcom's express prior written
*  consent.
*
*****************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <mach/io_map.h>
#include <mach/rdb/brcm_rdb_hsotg_ctrl.h>
#include <plat/pi_mgr.h>
#include <asm/irq.h>
#include <linux/interrupt.h>
#include "bcm_hsotgctrl.h"

#define	PHY_MODE_OTG		2
#define 	BC11CFG_SW_OVERWRITE_KEY 0x55560000
#define	BC_CONFIG_DELAY_MS 2

#define USB_IRQ 79

struct bcm_hsotgctrl_drv_data {
	struct device *dev;
	struct clk *otg_clk;
	struct pi_mgr_dfs_node*  dfs_node;
	void *hsotg_ctrl_base;
};

static struct bcm_hsotgctrl_drv_data *local_hsotgctrl_handle = NULL;

/**
 * Do OTG init. Currently only for testing dataline pulsing when Vbus is off
 */
static ssize_t do_hsotgctrlinit(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	bcm_hsotgctrl_phy_set_vbus_stat(false);
	bcm_hsotgctrl_phy_set_non_driving(true);
	bcm_hsotgctrl_phy_set_non_driving(false);

	return count;
	
}
static DEVICE_ATTR(hsotgctrlinit, S_IWUSR, NULL, do_hsotgctrlinit);

static ssize_t dump_hsotgctrl(struct device *dev, 
	struct device_attribute *attr,
	char *buf)
{
	struct bcm_hsotgctrl_drv_data *hsotgctrl_drvdata = dev_get_drvdata(dev);
	void __iomem *hsotg_ctrl_base = hsotgctrl_drvdata->hsotg_ctrl_base;

	printk("\nusbotgcontrol: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_USBOTGCONTROL_OFFSET));
	printk("\nphy_cfg: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_PHY_CFG_OFFSET));
	printk("\nphy_p1ctl: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET));
	printk("\nbc11_status: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_BC11_STATUS_OFFSET));
	printk("\nbc11_cfg: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_BC11_CFG_OFFSET));
	printk("\ntp_in: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_TP_IN_OFFSET));
	printk("\ntp_out: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_TP_OUT_OFFSET));
	printk("\nphy_ctrl: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_PHY_CTRL_OFFSET));
	printk("\nusbreg: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_USBREG_OFFSET));
	printk("\nusbproben: 0x%x", readl(hsotg_ctrl_base + HSOTG_CTRL_USBPROBEN_OFFSET));

	return sprintf(buf, "hsotgctrl register dump\n");
}
static DEVICE_ATTR(hsotgctrldump, S_IRUSR, dump_hsotgctrl, NULL);

static ssize_t do_phy_shutdown(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	bcm_hsotgctrl_phy_set_vbus_stat(false);
	bcm_hsotgctrl_phy_set_non_driving(true);
	bcm_hsotgctrl_set_phy_off(true);

	return count;
}
static DEVICE_ATTR(phy_shutdown, S_IWUSR, NULL, do_phy_shutdown);

int bcm_hsotgctrl_en_clock(bool on)
{
	int rc=0;
	struct bcm_hsotgctrl_drv_data *bcm_hsotgctrl_handle = local_hsotgctrl_handle;

	if (!bcm_hsotgctrl_handle || !bcm_hsotgctrl_handle->otg_clk)
		return -EIO;

	if (on) {
#ifdef CONFIG_ARCH_RHEA
		if (bcm_hsotgctrl_handle->dfs_node)
			rc = pi_mgr_dfs_request_update(bcm_hsotgctrl_handle->dfs_node, PI_OPP_NORMAL);
			if (rc)
				dev_warn(bcm_hsotgctrl_handle->dev, "%s: error in updating DFS request before clock enabled\n", __func__);
#endif
		rc = clk_enable(bcm_hsotgctrl_handle->otg_clk);
	}
	else {
		clk_disable(bcm_hsotgctrl_handle->otg_clk);

#ifdef CONFIG_ARCH_RHEA
		if (bcm_hsotgctrl_handle->dfs_node) {
			rc = pi_mgr_dfs_request_update(bcm_hsotgctrl_handle->dfs_node, PI_MGR_DFS_MIN_VALUE);
			if (rc)
				dev_warn(bcm_hsotgctrl_handle->dev, "%s: DFS update request failed after clock disabled\n", __func__);
		} else
			dev_warn(bcm_hsotgctrl_handle->dev, "%s: error in updating DFS request after clock disabled\n", __func__);
#endif
	}

	if (rc) {
		dev_warn(bcm_hsotgctrl_handle->dev, "%s: error in controlling clock\n", __func__);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_hsotgctrl_en_clock);

int bcm_hsotgctrl_phy_init(void)
{
	int rc=0;
	int val;
	struct bcm_hsotgctrl_drv_data *bcm_hsotgctrl_handle = local_hsotgctrl_handle;

	if ((!bcm_hsotgctrl_handle->otg_clk) || (!bcm_hsotgctrl_handle->hsotg_ctrl_base) || (!bcm_hsotgctrl_handle->dev))
		return -EIO;

	bcm_hsotgctrl_en_clock(true);

	/* clear bit 15 RDB error */
	val = readl(bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);
	val &= ~HSOTG_CTRL_PHY_P1CTL_PLL_SUSPEND_ENABLE_MASK;
	writel(val, bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);
	schedule_timeout_interruptible(HZ/10);

	bcm_hsotgctrl_bc_reset();
	bcm_hsotgctrl_set_phy_off(false);
	bcm_hsotgctrl_phy_set_non_driving(false);
	bcm_hsotgctrl_phy_set_vbus_stat(true);

	enable_irq(USB_IRQ); /* Temp fix for BC detection coordination with PMU driver */
	return (rc);

}
EXPORT_SYMBOL_GPL(bcm_hsotgctrl_phy_init);

int bcm_hsotgctrl_phy_deinit(void)
{
	struct bcm_hsotgctrl_drv_data *bcm_hsotgctrl_handle = local_hsotgctrl_handle;

	if ((!bcm_hsotgctrl_handle->otg_clk) || (!bcm_hsotgctrl_handle->dev))
		return -EIO;

	bcm_hsotgctrl_phy_set_non_driving(true);
	bcm_hsotgctrl_set_phy_off(true);
	bcm_hsotgctrl_phy_set_vbus_stat(false);

	disable_irq(USB_IRQ); /* Temp fix for BC detection coordination with PMU driver */
	/* Disable the OTG core AHB clock */
	bcm_hsotgctrl_en_clock(false);

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_hsotgctrl_phy_deinit);

int bcm_hsotgctrl_bc_reset(void)
{
	int val;

	struct bcm_hsotgctrl_drv_data *bcm_hsotgctrl_handle = local_hsotgctrl_handle;

	if ((!bcm_hsotgctrl_handle->otg_clk) || (!bcm_hsotgctrl_handle->dev))
		return -EIO;

	val = readl(bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_BC11_CFG_OFFSET);

	/* Clear overwrite key */
	val &= ~(HSOTG_CTRL_BC11_CFG_BC11_OVWR_KEY_MASK | HSOTG_CTRL_BC11_CFG_SW_OVWR_EN_MASK);
	val |= (BC11CFG_SW_OVERWRITE_KEY | HSOTG_CTRL_BC11_CFG_SW_OVWR_EN_MASK); //We need this key written for this register access
	val |= HSOTG_CTRL_BC11_CFG_SW_RST_MASK;

	writel(val, bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_BC11_CFG_OFFSET); //Reset BC1.1 state machine

	msleep_interruptible(BC_CONFIG_DELAY_MS);

	val &= ~HSOTG_CTRL_BC11_CFG_SW_RST_MASK;
	writel(val, bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_BC11_CFG_OFFSET); //Clear reset

	val = readl(bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_BC11_CFG_OFFSET);

	/* Clear overwrite key so we don't accidently write to these bits */
	val &= ~(HSOTG_CTRL_BC11_CFG_BC11_OVWR_KEY_MASK | HSOTG_CTRL_BC11_CFG_SW_OVWR_EN_MASK);
	writel(val, bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_BC11_CFG_OFFSET);
}
EXPORT_SYMBOL_GPL(bcm_hsotgctrl_bc_reset);

int bcm_hsotgctrl_bc_vdp_src_off(void)
{
	int val;

	struct bcm_hsotgctrl_drv_data *bcm_hsotgctrl_handle = local_hsotgctrl_handle;

	if ((!bcm_hsotgctrl_handle->otg_clk) || (!bcm_hsotgctrl_handle->dev))
		return -EIO;

	val = readl(bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_BC11_CFG_OFFSET);

	/* Clear overwrite key */
	val &= ~(HSOTG_CTRL_BC11_CFG_BC11_OVWR_KEY_MASK | HSOTG_CTRL_BC11_CFG_SW_OVWR_EN_MASK);
	val |= (BC11CFG_SW_OVERWRITE_KEY | HSOTG_CTRL_BC11_CFG_SW_OVWR_EN_MASK); //We need this key written for this register access
	val &= ~HSOTG_CTRL_BC11_CFG_BC11_OVWR_SET_P0_MASK;

	writel(val, bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_BC11_CFG_OFFSET); //Reset BC1.1 state machine

	msleep_interruptible(BC_CONFIG_DELAY_MS);

	/* Clear overwrite key so we don't accidently write to these bits */
	val &= ~(HSOTG_CTRL_BC11_CFG_BC11_OVWR_KEY_MASK | HSOTG_CTRL_BC11_CFG_SW_OVWR_EN_MASK);
	writel(val, bcm_hsotgctrl_handle->hsotg_ctrl_base + HSOTG_CTRL_BC11_CFG_OFFSET);
}
EXPORT_SYMBOL_GPL(bcm_hsotgctrl_bc_vdp_src_off);

static int __devinit bcm_hsotgctrl_probe(struct platform_device *pdev)
{
	int error = 0;
	int val;
	struct resource *resource;
	struct bcm_hsotgctrl_drv_data *hsotgctrl_drvdata;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (NULL == resource)
		return -EIO;

	hsotgctrl_drvdata = kzalloc(sizeof(*hsotgctrl_drvdata), GFP_KERNEL);
	if (!hsotgctrl_drvdata) {
		dev_warn(&pdev->dev, "Memory allocation failed\n");
		return -ENOMEM;
	}

	local_hsotgctrl_handle = hsotgctrl_drvdata;

	hsotgctrl_drvdata->dev = &pdev->dev;
	hsotgctrl_drvdata->otg_clk = clk_get(NULL, "usb_otg_clk");

	if (!hsotgctrl_drvdata->otg_clk) {
		dev_warn(&pdev->dev, "Clock allocation failed\n");
		kfree(hsotgctrl_drvdata);
		return -EIO;
	}

#ifdef CONFIG_ARCH_RHEA
	/* Add a DFS request. Update when USB OTG AHB clock is enabled/disabled */
	hsotgctrl_drvdata->dfs_node = pi_mgr_dfs_add_request("bcm_hsotgctrl",
								  PI_MGR_PI_ID_ARM_SUB_SYSTEM, PI_MGR_DFS_MIN_VALUE);

	if (hsotgctrl_drvdata->dfs_node == NULL)
		dev_warn(hsotgctrl_drvdata->dev, "\n%s: DFS request failed for bcm_hsotgctrl\n", __func__);
#endif

	hsotgctrl_drvdata->hsotg_ctrl_base = ioremap(resource->start, SZ_4K);
	if (!hsotgctrl_drvdata->hsotg_ctrl_base) {
		dev_warn(&pdev->dev, "IO remap failed\n");
		clk_put(hsotgctrl_drvdata->otg_clk);
		kfree(hsotgctrl_drvdata);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, hsotgctrl_drvdata);

	/* Init the PHY. Later we will add our init/shutdown sequence */
	dev_info(hsotgctrl_drvdata->dev, "\n%s: Setting up USB OTG PHY and Clock\n", __func__);
	bcm_hsotgctrl_en_clock(true);

	schedule_timeout_interruptible(HZ/10);

	/* clear bit 15 RDB error */
	val = readl(hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);
	val &= ~HSOTG_CTRL_PHY_P1CTL_PLL_SUSPEND_ENABLE_MASK;
	writel(val, hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);
	schedule_timeout_interruptible(HZ/10);

	/* set Phy to driving mode */
	val = readl(hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);
	val &= ~HSOTG_CTRL_PHY_P1CTL_NON_DRIVING_MASK;
	writel(val, hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);

	schedule_timeout_interruptible(HZ/10);

	/* S/W reset Phy, actively low */
	val = readl(hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);
	val &= ~HSOTG_CTRL_PHY_P1CTL_SOFT_RESET_MASK;
	val &= ~HSOTG_CTRL_PHY_P1CTL_PHY_MODE_MASK;
	val |= PHY_MODE_OTG << HSOTG_CTRL_PHY_P1CTL_PHY_MODE_SHIFT; // use OTG mode
	writel(val, hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);

	schedule_timeout_interruptible(HZ/10);

	/* bring Phy out of reset */
	val = readl(hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);
	val |= HSOTG_CTRL_PHY_P1CTL_SOFT_RESET_MASK;
	writel(val, hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);

	schedule_timeout_interruptible(HZ/10);

	/* set the phy to functional state */
	val = readl(hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_CFG_OFFSET);
	val &= ~HSOTG_CTRL_PHY_CFG_PHY_IDDQ_I_MASK;
	writel(val, hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_PHY_CFG_OFFSET);

	schedule_timeout_interruptible(HZ/10);

	val = HSOTG_CTRL_USBOTGCONTROL_OTGSTAT2_MASK |
			HSOTG_CTRL_USBOTGCONTROL_OTGSTAT1_MASK |
			HSOTG_CTRL_USBOTGCONTROL_REG_OTGSTAT2_MASK |
			HSOTG_CTRL_USBOTGCONTROL_REG_OTGSTAT1_MASK |
			HSOTG_CTRL_USBOTGCONTROL_OTGSTAT_CTRL_MASK |
			HSOTG_CTRL_USBOTGCONTROL_UTMIOTG_IDDIG_SW_MASK | //Come up as device until we check PMU ID status to avoid turning on Vbus before checking
			HSOTG_CTRL_USBOTGCONTROL_USB_HCLK_EN_DIRECT_MASK |
			HSOTG_CTRL_USBOTGCONTROL_USB_ON_IS_HCLK_EN_MASK |
			HSOTG_CTRL_USBOTGCONTROL_USB_ON_MASK |
			HSOTG_CTRL_USBOTGCONTROL_PRST_N_SW_MASK |
			HSOTG_CTRL_USBOTGCONTROL_HRESET_N_SW_MASK |
			HSOTG_CTRL_USBOTGCONTROL_SOFT_PHY_RESETB_MASK |
			HSOTG_CTRL_USBOTGCONTROL_SOFT_DLDO_PDN_MASK |
			HSOTG_CTRL_USBOTGCONTROL_SOFT_ALDO_PDN_MASK;
	writel(val, hsotgctrl_drvdata->hsotg_ctrl_base + HSOTG_CTRL_USBOTGCONTROL_OFFSET);

	schedule_timeout_interruptible(HZ/10*3);

	dev_info(hsotgctrl_drvdata->dev, "\n%s: Setup USB OTG PHY and Clock Completed\n", __func__);

	error = device_create_file(&pdev->dev, &dev_attr_hsotgctrldump);

	if (error)
	{
		dev_warn(&pdev->dev, "Failed to create HOST file\n");
		goto Error_bcm_hsotgctrl_probe;
	}

	error = device_create_file(&pdev->dev, &dev_attr_hsotgctrlinit);

	if (error)
	{
		dev_warn(&pdev->dev, "Failed to create phy_shutdown file\n");
		goto Error_bcm_hsotgctrl_probe;
	}

	error = device_create_file(&pdev->dev, &dev_attr_phy_shutdown);

	if (error)
	{
		dev_warn(&pdev->dev, "Failed to create phy_shutdown file\n");
		goto Error_bcm_hsotgctrl_probe;
	}

	return 0;

Error_bcm_hsotgctrl_probe:
	iounmap(hsotgctrl_drvdata->hsotg_ctrl_base);
	clk_put(hsotgctrl_drvdata->otg_clk);
	kfree(hsotgctrl_drvdata);
	return error;
}

static int bcm_hsotgctrl_remove(struct platform_device *pdev)
{
	struct bcm_hsotgctrl_drv_data *hsotgctrl_drvdata = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_hsotgctrldump);
	device_remove_file(&pdev->dev, &dev_attr_hsotgctrlinit);

	iounmap(hsotgctrl_drvdata->hsotg_ctrl_base);

#ifdef CONFIG_ARCH_RHEA
	if (hsotgctrl_drvdata->dfs_node) {
		pi_mgr_dfs_request_remove(hsotgctrl_drvdata->dfs_node);
		hsotgctrl_drvdata->dfs_node = NULL;
	}
#endif

	clk_put(hsotgctrl_drvdata->otg_clk);
	local_hsotgctrl_handle = NULL;
	kfree(hsotgctrl_drvdata);

	return 0;
}

static struct platform_driver bcm_hsotgctrl_driver = {
	.driver = {
		   .name = "bcm_hsotgctrl",
		   .owner = THIS_MODULE,
	},
	.probe = bcm_hsotgctrl_probe,
	.remove = bcm_hsotgctrl_remove,
};

static int __init bcm_hsotgctrl_init(void)
{
	pr_info("Broadcom USB HSOTGCTRL Driver\n");

	return platform_driver_register(&bcm_hsotgctrl_driver);
}
module_init(bcm_hsotgctrl_init);

static void __exit bcm_hsotgctrl_exit(void)
{
	platform_driver_unregister(&bcm_hsotgctrl_driver);
}
module_exit(bcm_hsotgctrl_exit);

int bcm_hsotgctrl_phy_set_vbus_stat(bool on)
{
	unsigned long val;
	void *hsotg_ctrl_base;

	if (NULL != local_hsotgctrl_handle)
		hsotg_ctrl_base = local_hsotgctrl_handle->hsotg_ctrl_base;
	else
		return -ENODEV;

	val = readl(hsotg_ctrl_base + HSOTG_CTRL_USBOTGCONTROL_OFFSET);

	if (on) {
		val |= (HSOTG_CTRL_USBOTGCONTROL_REG_OTGSTAT2_MASK |
			HSOTG_CTRL_USBOTGCONTROL_REG_OTGSTAT1_MASK);
	} else {
		val &= ~(HSOTG_CTRL_USBOTGCONTROL_REG_OTGSTAT2_MASK |
			 HSOTG_CTRL_USBOTGCONTROL_REG_OTGSTAT1_MASK);
	}

	writel(val, hsotg_ctrl_base + HSOTG_CTRL_USBOTGCONTROL_OFFSET);

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_hsotgctrl_phy_set_vbus_stat);

int bcm_hsotgctrl_phy_set_non_driving(bool on)
{
	unsigned long val;
	void *hsotg_ctrl_base;

	if (NULL != local_hsotgctrl_handle)
		hsotg_ctrl_base = local_hsotgctrl_handle->hsotg_ctrl_base;
	else
		return -ENODEV;

	/* set Phy to driving mode */
	val = readl(hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);

	if (on)
		val |= HSOTG_CTRL_PHY_P1CTL_NON_DRIVING_MASK;
	else
		val &= ~HSOTG_CTRL_PHY_P1CTL_NON_DRIVING_MASK;

	writel(val, hsotg_ctrl_base + HSOTG_CTRL_PHY_P1CTL_OFFSET);
	return 0;
}
EXPORT_SYMBOL_GPL(bcm_hsotgctrl_phy_set_non_driving);

int bcm_hsotgctrl_set_phy_off(bool on)
{
	unsigned long val;
	void *hsotg_ctrl_base;

	if (NULL != local_hsotgctrl_handle)
		hsotg_ctrl_base = local_hsotgctrl_handle->hsotg_ctrl_base;
	else
		return -ENODEV;

	val = readl(hsotg_ctrl_base + HSOTG_CTRL_PHY_CFG_OFFSET);
	if (on)
		val |= HSOTG_CTRL_PHY_CFG_PHY_IDDQ_I_MASK;
	else
		val &= ~HSOTG_CTRL_PHY_CFG_PHY_IDDQ_I_MASK;

	writel(val, hsotg_ctrl_base + HSOTG_CTRL_PHY_CFG_OFFSET);

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_hsotgctrl_set_phy_off);

int bcm_hsotgctrl_phy_set_id_stat(bool floating)
{
	unsigned long val;
	void *hsotg_ctrl_base;

	if (NULL != local_hsotgctrl_handle)
		hsotg_ctrl_base = local_hsotgctrl_handle->hsotg_ctrl_base;
	else
		return -ENODEV;

	val = readl(hsotg_ctrl_base + HSOTG_CTRL_USBOTGCONTROL_OFFSET);

	if (floating) {
		val |= HSOTG_CTRL_USBOTGCONTROL_UTMIOTG_IDDIG_SW_MASK;
	} else {
		val &= ~HSOTG_CTRL_USBOTGCONTROL_UTMIOTG_IDDIG_SW_MASK;
	}

	writel(val, hsotg_ctrl_base + HSOTG_CTRL_USBOTGCONTROL_OFFSET);

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_hsotgctrl_phy_set_id_stat);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("USB HSOTGCTRL driver");
MODULE_LICENSE("GPL");
