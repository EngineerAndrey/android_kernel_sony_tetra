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
#include <linux/usb/otg.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/err.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/clk.h>
#ifdef CONFIG_MFD_BCMPMU
#include <linux/mfd/bcmpmu.h>
#else
#include <linux/mfd/bcm590xx/core.h>
#include <linux/mfd/bcm590xx/bcm590xx-usb.h>
#endif

#include <asm/io.h>
#include <mach/io_map.h>
#include <linux/usb/bcm_hsotgctrl.h>
#include "bcm_otg_adp.h"
#include "bcmpmu_otg_xceiv.h"

#define OTGCTRL1_VBUS_ON 0xDC
#define OTGCTRL1_VBUS_OFF 0xD8

#define HOST_TO_PERIPHERAL_DELAY_MS 1000
#define PERIPHERAL_TO_HOST_DELAY_MS 100

static int bcmpmu_otg_xceiv_set_vbus(struct otg_transceiver *otg, bool enabled)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data = dev_get_drvdata(otg->dev);
	int stat;

	/* The order of these operations has temporarily been
	 * swapped due to overcurrent issue caused by slow I2C
	 * operations. I2C operations take >200ms to complete */
	bcm_hsotgctrl_phy_set_vbus_stat(enabled);
	
	if (enabled) {
		dev_info(xceiv_data->dev, "Turning on VBUS\n");
		xceiv_data->vbus_enabled = true;
#ifdef CONFIG_MFD_BCMPMU
		stat =
		    xceiv_data->bcmpmu->usb_set(xceiv_data->bcmpmu,
						BCMPMU_USB_CTRL_VBUS_ON_OFF,
						1);
#else
		stat =
		    bcm590xx_reg_write(xceiv_data->bcm590xx,
				       BCM59055_REG_OTGCTRL1,
				       OTGCTRL1_VBUS_ON);
#endif
	} else {
		dev_info(xceiv_data->dev, "Turning off VBUS\n");
		xceiv_data->vbus_enabled = false;
#ifdef CONFIG_MFD_BCMPMU
		stat =
		    xceiv_data->bcmpmu->usb_set(xceiv_data->bcmpmu,
						BCMPMU_USB_CTRL_VBUS_ON_OFF,
						0);
#else
		stat =
		    bcm590xx_reg_write(xceiv_data->bcm590xx,
				       BCM59055_REG_OTGCTRL1,
				       OTGCTRL1_VBUS_OFF);
#endif
	}

	if (stat < 0)
		dev_warn(xceiv_data->dev, "Failed to set VBUS\n");
		
	return stat;
}

static void bcmpmu_otg_xceiv_shutdown(struct otg_transceiver *otg)
{
#if 0
	struct bcmpmu_otg_xceiv_data *xceiv_data = dev_get_drvdata(otg->dev);

	if (xceiv_data)
		bcm_hsotgctrl_phy_deinit(); /* De-initialize OTG core and PHY */
#endif
}

static void bcmpmu_otg_xceiv_select_host_mode(struct bcmpmu_otg_xceiv_data *xceiv_data, bool enable)
{
	if (enable) {
		dev_info(xceiv_data->dev, "Switching to Host\n");
		xceiv_data->host = true;
		bcm_hsotgctrl_set_phy_off(false);
		msleep(PERIPHERAL_TO_HOST_DELAY_MS);
		bcm_hsotgctrl_phy_set_id_stat(false);
	} else {
		dev_info(xceiv_data->dev, "Switching to Peripheral\n");
		bcm_hsotgctrl_phy_set_id_stat(true);
		if (xceiv_data->host) {
			xceiv_data->host = false;
			msleep(HOST_TO_PERIPHERAL_DELAY_MS);
		}
	}
}


#ifdef CONFIG_MFD_BCMPMU
static void bcmpmu_usb_event_notif_callback(struct bcmpmu * pmu_handle, unsigned char event, void *param1, void *otg_data)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data = (struct bcmpmu_otg_xceiv_data *)otg_data;

	if (!otg_data) {
		dev_info(xceiv_data->dev, "ERROR: xceiver data not passed by PMU callback\n");
		return;
	}

	switch (event) {
		case BCMPMU_USB_EVENT_SESSION_VALID:
			queue_work(xceiv_data->bcm_otg_work_queue, &xceiv_data->bcm_otg_vbus_a_valid_work);
			break;
		case BCMPMU_USB_EVENT_SESSION_INVALID:
			queue_work(xceiv_data->bcm_otg_work_queue, &xceiv_data->bcm_otg_vbus_a_invalid_work);
			break;
		case BCMPMU_USB_EVENT_VBUS_VALID:
			queue_work(xceiv_data->bcm_otg_work_queue, &xceiv_data->bcm_otg_vbus_valid_work);
			break;
		case BCMPMU_USB_EVENT_IN:
			break;
		case BCMPMU_USB_EVENT_ID_CHANGE:
			queue_work(xceiv_data->bcm_otg_work_queue, &xceiv_data->bcm_otg_id_status_change_work);
			break;
		case BCMPMU_USB_EVENT_USB_DETECTION: /* Rhea PMU driver uses this event instead of CHGR_DETECTION. Revisit later */
			queue_work(xceiv_data->bcm_otg_work_queue, &xceiv_data->bcm_otg_chg_detect_work);
			break;
		default:
			break;
	}
}
#else
static int bcmpmu_otg_xceiv_vbus_notif_handler(struct notifier_block *nb, unsigned long value, void *data)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(nb, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_vbus_validity_notifier);
	bool vbus_status = 0;

	if (!xceiv_data)
		return -EINVAL;

	vbus_status = bcmpmu_usb_get(BCMPMU_CTRL_GET_VBUS_STATUS, xceiv_data->bcm590xx);

	queue_work(xceiv_data->bcm_otg_work_queue, vbus_status ? &xceiv_data->bcm_otg_vbus_valid_work : &xceiv_data->bcm_otg_vbus_a_invalid_work);

	return 0;
}

static int bcmpmu_otg_xceiv_chg_detection_notif_handler(struct notifier_block *nb, unsigned long value, void *data)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(nb, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_chg_detection_notifier);

	if (xceiv_data)
		queue_work(xceiv_data->bcm_otg_work_queue, &xceiv_data->bcm_otg_chg_detect_work);
	else
		return -EINVAL;

	return 0;
}

static int bcmpmu_otg_xceiv_id_chg_notif_handler(struct notifier_block *nb, unsigned long value, void *data)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(nb, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_id_chg_notifier);

	dev_info(xceiv_data->dev, "ID change detected\n");

	if (xceiv_data)
		queue_work(xceiv_data->bcm_otg_work_queue, &xceiv_data->bcm_otg_id_status_change_work);
	else
		return -EINVAL;

	return 0;
}
#endif

static bool bcmpmu_otg_xceiv_check_id_gnd(struct bcmpmu_otg_xceiv_data *xceiv_data)
{
	unsigned int data=0;
	bool id_gnd = false;

#ifdef CONFIG_MFD_BCMPMU
	xceiv_data->bcmpmu->usb_get(xceiv_data->bcmpmu, BCMPMU_USB_CTRL_GET_ID_VALUE, &data);
	id_gnd = (data == PMU_USB_ID_GROUND);
#else
	data = bcmpmu_usb_get(BCMPMU_CTRL_GET_ID_VALUE, (void*)xceiv_data->bcm590xx);
	id_gnd = !data; /* Non-ACA interpretation */
#endif

	return id_gnd;
}

static int bcmpmu_otg_xceiv_set_peripheral(struct otg_transceiver *otg,
					   struct usb_gadget *gadget)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data = dev_get_drvdata(otg->dev);
	int status = 0;
	bool id_gnd = false;

	dev_info(xceiv_data->dev, "Setting Peripheral\n");
	otg->gadget = gadget;

#ifdef CONFIG_MFD_BCMPMU
	if (xceiv_data->bcmpmu->register_usb_callback) {
		/* Register callback functions for PMU events */
		status = xceiv_data->bcmpmu->register_usb_callback(xceiv_data->bcmpmu, bcmpmu_usb_event_notif_callback, (void*)xceiv_data);
	}
#else
	/* We want to register notifiers during probe but that is not possible right now and there is no direct
	 ** link to remove these notifiers. Avoid an unnecessary remove notifer. Just check if it is already registered
	*/
	if (xceiv_data->bcm_otg_vbus_validity_notifier.notifier_call == NULL) {
		/* We would want to use A session invalid but that requires reading PMU reg for status. For now use insert/remove instead */
		xceiv_data->bcm_otg_vbus_validity_notifier.notifier_call = bcmpmu_otg_xceiv_vbus_notif_handler;
		bcmpmu_usb_add_notifier(BCMPMU_USB_EVENT_VBUS_VALID, &xceiv_data->bcm_otg_vbus_validity_notifier);
		bcmpmu_usb_add_notifier(BCMPMU_USB_EVENT_SESSION_INVALID, &xceiv_data->bcm_otg_vbus_validity_notifier);

		xceiv_data->bcm_otg_id_chg_notifier.notifier_call = bcmpmu_otg_xceiv_id_chg_notif_handler;
		bcmpmu_usb_add_notifier(BCMPMU_USB_EVENT_ID_CHANGE, &xceiv_data->bcm_otg_id_chg_notifier);

		xceiv_data->bcm_otg_chg_detection_notifier.notifier_call = bcmpmu_otg_xceiv_chg_detection_notif_handler;
		bcmpmu_usb_add_notifier(BCMPMU_USB_EVENT_CHGR_DETECTION, &xceiv_data->bcm_otg_chg_detection_notifier);
	}
#endif

	id_gnd = bcmpmu_otg_xceiv_check_id_gnd(xceiv_data);

	if (!id_gnd) {
#if 0
		int vbus_status;
#ifdef CONFIG_MFD_BCMPMU
		xceiv_data->bcmpmu->usb_get(xceiv_data->bcmpmu, BCMPMU_USB_CTRL_GET_VBUS_STATUS, &vbus_status);
#else
		vbus_status = bcmpmu_usb_get(BCMPMU_CTRL_GET_VBUS_STATUS, xceiv_data->bcm590xx);
#endif
		if (!vbus_status) {
			/* Non-ACA ID interpretation for now since RID_A is not tested yet on this platform */

			/* Temporarily disable USB PM to make USB work in ICS. The DWC OTG driver does not check
			 * the PM state before setting/clearing the soft disconnect bit when the gadget framework
			 * invokes the usb_gadget_ops->pullup() callback. This can lead to the DWC OTG DCTL register
			 * being accessed when the USB core is in powered off state. In ICS this happens on system
			 * startup, when the Android framework sets the default USB mode after the USB core has been
			 * powered off.
			 */
			bcm_hsotgctrl_phy_deinit(); /* Shutdown the core */
		}
#endif
	} else
		bcmpmu_otg_xceiv_select_host_mode(xceiv_data, id_gnd);

	return status;
}

static int bcmpmu_otg_xceiv_set_host(struct otg_transceiver *otg,
				     struct usb_bus *host)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data = dev_get_drvdata(otg->dev);
	int status = 0;

	dev_info(xceiv_data->dev, "Setting Host\n");
	otg->host = host;

	if (bcmpmu_otg_xceiv_check_id_gnd(xceiv_data))
		bcm_hsotgctrl_phy_set_id_stat(false);
	else
		bcm_hsotgctrl_phy_set_id_stat(true);

	return status;
}

static ssize_t bcmpmu_otg_xceiv_wake_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct usb_gadget *gadget;
	ssize_t result = 0;
	unsigned int val;
	struct bcmpmu_otg_xceiv_data *xceiv_data = dev_get_drvdata(dev);
	int error;

	gadget = xceiv_data->otg_xceiver.xceiver.gadget;

	result = sscanf(buf, "%u\n", &val);
	if (result != 1) {
		result = -EINVAL;
	} else if (val == 0) {
		dev_warn(xceiv_data->dev, "Illegal value\n");
	} else {
		dev_info(xceiv_data->dev, "Waking up host\n");
		error = usb_gadget_wakeup(gadget);
		if (error)
			dev_err(xceiv_data->dev,
				"Failed to issue wakeup\n");
	}

	return result < 0 ? result : count;
}
static DEVICE_ATTR(wake, S_IWUSR, NULL, bcmpmu_otg_xceiv_wake_store);

static ssize_t bcmpmu_otg_xceiv_vbus_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", xceiv_data->vbus_enabled ? "1" : "0");
}

static ssize_t bcmpmu_otg_xceiv_vbus_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct usb_hcd *hcd;
	ssize_t result = 0;
	unsigned int val;
	struct bcmpmu_otg_xceiv_data *xceiv_data = dev_get_drvdata(dev);
	int error;

	hcd = bus_to_hcd(xceiv_data->otg_xceiver.xceiver.host);

	result = sscanf(buf, "%u\n", &val);
	if (result != 1) {
		result = -EINVAL;
	} else if (val == 0) {
		dev_info(xceiv_data->dev, "Clearing PORT_POWER feature\n");
		error = hcd->driver->hub_control(hcd, ClearPortFeature,
						 USB_PORT_FEAT_POWER, 1, NULL,
						 0);
		if (error)
			dev_err(xceiv_data->dev,
				"Failed to clear PORT_POWER feature\n");
	} else {
		dev_info(xceiv_data->dev, "Setting PORT_POWER feature\n");
		error = hcd->driver->hub_control(hcd, SetPortFeature,
						 USB_PORT_FEAT_POWER, 1, NULL,
						 0);
		if (error)
			dev_err(xceiv_data->dev,
				"Failed to set PORT_POWER feature\n");
	}

	return result < 0 ? result : count;
}

static DEVICE_ATTR(vbus, S_IRUGO | S_IWUSR, bcmpmu_otg_xceiv_vbus_show,
		   bcmpmu_otg_xceiv_vbus_store);

static ssize_t bcmpmu_otg_xceiv_host_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", xceiv_data->host ? "1" : "0");
}

static ssize_t bcmpmu_otg_xceiv_host_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	ssize_t result = 0;
	unsigned int val;
	struct bcmpmu_otg_xceiv_data *xceiv_data = dev_get_drvdata(dev);

	result = sscanf(buf, "%u\n", &val);
	if (result != 1) {
		result = -EINVAL;
	} else {
		bcmpmu_otg_xceiv_select_host_mode(xceiv_data, !!val);
	}

	return result < 0 ? result : count;
}

static DEVICE_ATTR(host, S_IRUGO | S_IWUSR, bcmpmu_otg_xceiv_host_show,
		   bcmpmu_otg_xceiv_host_store);


static void bcmpmu_otg_xceiv_vbus_invalid_handler(struct work_struct *work)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(work, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_vbus_invalid_work);
	dev_info(xceiv_data->dev, "Vbus invalid\n");
}

static void bcmpmu_otg_xceiv_vbus_valid_handler(struct work_struct *work)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(work, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_vbus_valid_work);
	dev_info(xceiv_data->dev, "Vbus valid\n");
}

static void bcmpmu_otg_xceiv_vbus_a_invalid_handler(struct work_struct *work)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(work, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_vbus_a_invalid_work);
	dev_info(xceiv_data->dev, "A session invalid\n");

	/* Inform the core of session invalid level  */
	bcm_hsotgctrl_phy_set_vbus_stat(false);
}

static void bcmpmu_otg_xceiv_vbus_a_valid_handler(struct work_struct *work)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(work, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_vbus_a_valid_work);
	dev_info(xceiv_data->dev, "A session valid\n");
}

static void bcmpmu_otg_xceiv_adp_cprb_done_handler(struct work_struct *work)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(work, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_adp_cprb_done_work);
	dev_info(xceiv_data->dev, "ADP calibration probe done\n");
}

static void bcmpmu_otg_xceiv_adp_change_handler(struct work_struct *work)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(work, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_adp_change_work);
	dev_info(xceiv_data->dev, "ADP change detected\n");
}

static void bcmpmu_otg_xceiv_id_change_handler(struct work_struct *work)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(work, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_id_status_change_work);
	bool id_gnd = false;
	int vbus_status;

	dev_info(xceiv_data->dev, "ID change detected\n");

	id_gnd = bcmpmu_otg_xceiv_check_id_gnd(xceiv_data);

	bcmpmu_otg_xceiv_select_host_mode(xceiv_data, id_gnd);

	if (!id_gnd) {
#if 0
#ifdef CONFIG_MFD_BCMPMU
		xceiv_data->bcmpmu->usb_get(xceiv_data->bcmpmu, BCMPMU_USB_CTRL_GET_VBUS_STATUS, &vbus_status);
#else
		vbus_status = bcmpmu_usb_get(BCMPMU_CTRL_GET_VBUS_STATUS, xceiv_data->bcm590xx);
#endif
		if (!vbus_status) {
			/* Non-ACA ID interpretation for now since RID_A is not tested yet on this platform */
			bcm_hsotgctrl_phy_deinit(); /* Shutdown the core */
		}
#endif
	} else {
		bcm_hsotgctrl_phy_init();
		/* Non-ACA ID interpretation for now since RID_A is not tested yet on this platform */
		bcmpmu_otg_xceiv_select_host_mode(xceiv_data, id_gnd);
	}
}

static void bcmpmu_otg_xceiv_chg_detect_handler(struct work_struct *work)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data =
		container_of(work, struct bcmpmu_otg_xceiv_data,
			     bcm_otg_chg_detect_work);
	bool id_gnd = false;

	dev_info(xceiv_data->dev, "Charger detect event\n");

	id_gnd = bcmpmu_otg_xceiv_check_id_gnd(xceiv_data);

	if (!id_gnd) /* Non-ACA interpretation for now */
		bcm_hsotgctrl_phy_init();
}

static int __devinit bcmpmu_otg_xceiv_probe(struct platform_device *pdev)
{
	int error = 0;
	struct bcmpmu_otg_xceiv_data *xceiv_data;
#ifdef CONFIG_MFD_BCMPMU
	struct bcmpmu *bcmpmu = pdev->dev.platform_data;
#else
	struct bcm590xx *bcm590xx = dev_get_drvdata(pdev->dev.parent);
#endif

	dev_info(&pdev->dev, "Probing started...\n");

	xceiv_data = kzalloc(sizeof(*xceiv_data), GFP_KERNEL);
	if (!xceiv_data) {
		dev_warn(&pdev->dev, "Memory allocation failed\n");
		return -ENOMEM;
	}

	xceiv_data->dev = &pdev->dev;
#ifdef CONFIG_MFD_BCMPMU
	xceiv_data->bcmpmu = bcmpmu;
#else
	xceiv_data->bcm590xx = bcm590xx;
#endif
	xceiv_data->otg_xceiver.xceiver.dev = xceiv_data->dev;
	xceiv_data->otg_xceiver.xceiver.label = "bcmpmu_otg_xceiv";
	xceiv_data->host = false;
	xceiv_data->vbus_enabled = false;

	/* Create a work queue for OTG work items */
	xceiv_data->bcm_otg_work_queue = create_workqueue("bcm_otg_events");
	if (xceiv_data->bcm_otg_work_queue == NULL) {
		dev_warn(&pdev->dev, "BCM OTG events work queue creation failed\n");
		kfree(xceiv_data);
		return -ENOMEM;
	}

	/* Create one work item per deferrable function */
	INIT_WORK(&xceiv_data->bcm_otg_vbus_invalid_work,
		  bcmpmu_otg_xceiv_vbus_invalid_handler);
	INIT_WORK(&xceiv_data->bcm_otg_vbus_valid_work,
		  bcmpmu_otg_xceiv_vbus_valid_handler);
	INIT_WORK(&xceiv_data->bcm_otg_vbus_a_invalid_work,
		  bcmpmu_otg_xceiv_vbus_a_invalid_handler);
	INIT_WORK(&xceiv_data->bcm_otg_vbus_a_valid_work,
		  bcmpmu_otg_xceiv_vbus_a_valid_handler);
	INIT_WORK(&xceiv_data->bcm_otg_adp_cprb_done_work,
		  bcmpmu_otg_xceiv_adp_cprb_done_handler);
	INIT_WORK(&xceiv_data->bcm_otg_adp_change_work,
		  bcmpmu_otg_xceiv_adp_change_handler);
	INIT_WORK(&xceiv_data->bcm_otg_id_status_change_work,
		  bcmpmu_otg_xceiv_id_change_handler);
	INIT_WORK(&xceiv_data->bcm_otg_chg_detect_work,
		  bcmpmu_otg_xceiv_chg_detect_handler);

	xceiv_data->otg_xceiver.xceiver.set_vbus =
		bcmpmu_otg_xceiv_set_vbus;
	xceiv_data->otg_xceiver.xceiver.set_peripheral =
		bcmpmu_otg_xceiv_set_peripheral;
	xceiv_data->otg_xceiver.xceiver.set_host =
		bcmpmu_otg_xceiv_set_host;
	xceiv_data->otg_xceiver.xceiver.shutdown =
		bcmpmu_otg_xceiv_shutdown;

	xceiv_data->otg_xceiver.do_adp_calibration_probe =
		bcm_otg_do_adp_calibration_probe;
	xceiv_data->otg_xceiver.do_adp_probe =
		bcm_otg_do_adp_probe;
	xceiv_data->otg_xceiver.do_adp_sense =
		bcm_otg_do_adp_sense;
	xceiv_data->otg_xceiver.do_adp_sense_then_probe =
		bcm_otg_do_adp_sense_then_probe;

	otg_set_transceiver(&xceiv_data->otg_xceiver.xceiver);

	platform_set_drvdata(pdev, xceiv_data);

	error = device_create_file(&pdev->dev, &dev_attr_host);
	if (error) {
		dev_warn(&pdev->dev, "Failed to create HOST file\n");
		goto error_attr_host;;
	}

	error = device_create_file(&pdev->dev, &dev_attr_vbus);
	if (error) {
		dev_warn(&pdev->dev, "Failed to create VBUS file\n");
		goto error_attr_vbus;
	}

	error = device_create_file(&pdev->dev, &dev_attr_wake);
	if (error) {
		dev_warn(&pdev->dev, "Failed to create WAKE file\n");
		goto error_attr_wake;
	}

	dev_info(&pdev->dev, "Probing successful\n");
	return 0;

error_attr_wake:
	device_remove_file(xceiv_data->dev, &dev_attr_vbus);

error_attr_vbus:
	device_remove_file(xceiv_data->dev, &dev_attr_host);

error_attr_host:
	destroy_workqueue(xceiv_data->bcm_otg_work_queue);
	kfree(xceiv_data);
	return error;
}

static int __exit bcmpmu_otg_xceiv_remove(struct platform_device *pdev)
{
	struct bcmpmu_otg_xceiv_data *xceiv_data = platform_get_drvdata(pdev);

	device_remove_file(xceiv_data->dev, &dev_attr_wake);
	device_remove_file(xceiv_data->dev, &dev_attr_vbus);
	device_remove_file(xceiv_data->dev, &dev_attr_host);

	destroy_workqueue(xceiv_data->bcm_otg_work_queue);
	kfree(xceiv_data);
#if 0
	bcm_hsotgctrl_phy_deinit();
#endif

	return 0;
}

static struct platform_driver bcmpmu_otg_xceiv_driver = {
	.probe = bcmpmu_otg_xceiv_probe,
	.remove = __exit_p(bcmpmu_otg_xceiv_remove),
	.driver = {
		   .name = "bcmpmu_otg_xceiv",
		   .owner = THIS_MODULE,
		   },
};

static int __init bcmpmu_otg_xceiv_init(void)
{
	pr_info("Broadcom USB OTG Transceiver Driver\n");

	return platform_driver_register(&bcmpmu_otg_xceiv_driver);
}
subsys_initcall(bcmpmu_otg_xceiv_init);

static void __exit bcmpmu_otg_xceiv_exit(void)
{
	platform_driver_unregister(&bcmpmu_otg_xceiv_driver);
}
module_exit(bcmpmu_otg_xceiv_exit);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("USB OTG transceiver driver");
MODULE_LICENSE("GPL");
