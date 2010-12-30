/************************************************************************************************/
/*                                                                                              */
/*  Copyright 2010  Broadcom Corporation                                                        */
/*                                                                                              */
/*     Unless you and Broadcom execute a separate written software license agreement governing  */
/*     use of this software, this software is licensed to you under the terms of the GNU        */
/*     General Public License version 2 (the GPL), available at                                 */
/*                                                                                              */
/*          http://www.broadcom.com/licenses/GPLv2.php                                          */
/*                                                                                              */
/*     with the following added to such license:                                                */
/*                                                                                              */
/*     As a special exception, the copyright holders of this software give you permission to    */
/*     link this software with independent modules, and to copy and distribute the resulting    */
/*     executable under terms of your choice, provided that you also meet, for each linked      */
/*     independent module, the terms and conditions of the license of that module.              */
/*     An independent module is a module which is not derived from this software.  The special  */
/*     exception does not apply to any modifications of the software.                           */
/*                                                                                              */
/*     Notwithstanding the above, under no circumstances may you combine this software in any   */
/*     way with any other Broadcom software provided under a license other than the GPL,        */
/*     without Broadcom's express prior written consent.                                        */
/*                                                                                              */
/*     Date     : Generated on 11/9/2010 1:16:58                                             */
/*     RDB file : //HERA/                                                                   */
/************************************************************************************************/

#ifndef __BRCM_RDB_MDM_RST_MGR_REG_H__
#define __BRCM_RDB_MDM_RST_MGR_REG_H__

#define MDM_RST_MGR_REG_WR_ACCESS_OFFSET                                  0x00000000
#define MDM_RST_MGR_REG_WR_ACCESS_TYPE                                    UInt32
#define MDM_RST_MGR_REG_WR_ACCESS_RESERVED_MASK                           0x7F0000FE
#define    MDM_RST_MGR_REG_WR_ACCESS_PRIV_ACCESS_MODE_SHIFT               31
#define    MDM_RST_MGR_REG_WR_ACCESS_PRIV_ACCESS_MODE_MASK                0x80000000
#define    MDM_RST_MGR_REG_WR_ACCESS_PASSWORD_SHIFT                       8
#define    MDM_RST_MGR_REG_WR_ACCESS_PASSWORD_MASK                        0x00FFFF00
#define    MDM_RST_MGR_REG_WR_ACCESS_RSTMGR_ACC_SHIFT                     0
#define    MDM_RST_MGR_REG_WR_ACCESS_RSTMGR_ACC_MASK                      0x00000001

#define MDM_RST_MGR_REG_SOFT_RSTN0_OFFSET                                 0x00000004
#define MDM_RST_MGR_REG_SOFT_RSTN0_TYPE                                   UInt32
#define MDM_RST_MGR_REG_SOFT_RSTN0_RESERVED_MASK                          0x7FFFFFEC
#define    MDM_RST_MGR_REG_SOFT_RSTN0_PRIV_ACCESS_MODE_SHIFT              31
#define    MDM_RST_MGR_REG_SOFT_RSTN0_PRIV_ACCESS_MODE_MASK               0x80000000
#define    MDM_RST_MGR_REG_SOFT_RSTN0_ATB_SOFT_RSTN_SHIFT                 4
#define    MDM_RST_MGR_REG_SOFT_RSTN0_ATB_SOFT_RSTN_MASK                  0x00000010
#define    MDM_RST_MGR_REG_SOFT_RSTN0_APB_OAN_SOFT_RSTN_SHIFT             1
#define    MDM_RST_MGR_REG_SOFT_RSTN0_APB_OAN_SOFT_RSTN_MASK              0x00000002
#define    MDM_RST_MGR_REG_SOFT_RSTN0_APB_SOFT_RSTN_SHIFT                 0
#define    MDM_RST_MGR_REG_SOFT_RSTN0_APB_SOFT_RSTN_MASK                  0x00000001

#define MDM_RST_MGR_REG_WGM_RSTN_OFFSET                                   0x00000008
#define MDM_RST_MGR_REG_WGM_RSTN_TYPE                                     UInt32
#define MDM_RST_MGR_REG_WGM_RSTN_RESERVED_MASK                            0x7FFFFFFE
#define    MDM_RST_MGR_REG_WGM_RSTN_PRIV_ACCESS_MODE_SHIFT                31
#define    MDM_RST_MGR_REG_WGM_RSTN_PRIV_ACCESS_MODE_MASK                 0x80000000
#define    MDM_RST_MGR_REG_WGM_RSTN_WGM_RECOVERREQ_SHIFT                  0
#define    MDM_RST_MGR_REG_WGM_RSTN_WGM_RECOVERREQ_MASK                   0x00000001

#define MDM_RST_MGR_REG_RFBB_RSTN_OFFSET                                  0x0000000C
#define MDM_RST_MGR_REG_RFBB_RSTN_TYPE                                    UInt32
#define MDM_RST_MGR_REG_RFBB_RSTN_RESERVED_MASK                           0x7FFFFFFE
#define    MDM_RST_MGR_REG_RFBB_RSTN_PRIV_ACCESS_MODE_SHIFT               31
#define    MDM_RST_MGR_REG_RFBB_RSTN_PRIV_ACCESS_MODE_MASK                0x80000000
#define    MDM_RST_MGR_REG_RFBB_RSTN_RF_BB_SOFT_RSTN_SHIFT                0
#define    MDM_RST_MGR_REG_RFBB_RSTN_RF_BB_SOFT_RSTN_MASK                 0x00000001

#define MDM_RST_MGR_REG_PMU_SHIM_RSTN_OFFSET                              0x00000010
#define MDM_RST_MGR_REG_PMU_SHIM_RSTN_TYPE                                UInt32
#define MDM_RST_MGR_REG_PMU_SHIM_RSTN_RESERVED_MASK                       0x7FFFFFFE
#define    MDM_RST_MGR_REG_PMU_SHIM_RSTN_PRIV_ACCESS_MODE_SHIFT           31
#define    MDM_RST_MGR_REG_PMU_SHIM_RSTN_PRIV_ACCESS_MODE_MASK            0x80000000
#define    MDM_RST_MGR_REG_PMU_SHIM_RSTN_PMU_SHIM_SOFT_RSTN_SHIFT         0
#define    MDM_RST_MGR_REG_PMU_SHIM_RSTN_PMU_SHIM_SOFT_RSTN_MASK          0x00000001

#endif /* __BRCM_RDB_MDM_RST_MGR_REG_H__ */


