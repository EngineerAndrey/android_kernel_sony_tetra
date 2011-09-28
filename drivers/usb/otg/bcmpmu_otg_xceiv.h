/*****************************************************************************
* Copyright 2006 - 2011 Broadcom Corporation.  All rights reserved.
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

#ifndef _BCMPMU_OTG_XCEIV_H
#define _BCMPMU_OTG_XCEIV_H

struct bcm_otg_xceiver {
	struct otg_transceiver xceiver;
	/* ADP functions and associated data structures */
#ifdef CONFIG_MFD_BCMPMU
	int (*do_adp_calibration_probe)(struct bcmpmu *bcmpmu);
	int (*do_adp_probe)(struct bcmpmu *bcmpmu);
	int (*do_adp_sense)(struct bcmpmu *bcmpmu);
	int (*do_adp_sense_then_probe)(struct bcmpmu *bcmpmu);
#else
	int (*do_adp_calibration_probe)(struct bcm590xx *bcm590xx);
	int (*do_adp_probe)(struct bcm590xx *bcm590xx);
	int (*do_adp_sense)(struct bcm590xx *bcm590xx);
	int (*do_adp_sense_then_probe)(struct bcm590xx *bcm590xx);
#endif	
};

struct bcmpmu_otg_xceiv_data {
	struct device *dev;
#ifdef CONFIG_MFD_BCMPMU
	struct bcmpmu *bcmpmu;
#else
	struct bcm590xx *bcm590xx;
#endif
	struct bcm_otg_xceiver otg_xceiver;

	/* OTG Work queue and work struct for each item for work queue */
	struct workqueue_struct *bcm_otg_work_queue;
	struct work_struct bcm_otg_vbus_invalid_work;
	struct work_struct bcm_otg_vbus_valid_work;
	struct work_struct bcm_otg_vbus_a_invalid_work;
	struct work_struct bcm_otg_vbus_a_valid_work;
	struct work_struct bcm_otg_adp_cprb_done_work;
	struct work_struct bcm_otg_adp_change_work;
	struct work_struct bcm_otg_id_status_change_work;
	bool host;
	bool vbus_enabled;
};

#endif /* _BCMPMU_OTG_XCEIV_H */
