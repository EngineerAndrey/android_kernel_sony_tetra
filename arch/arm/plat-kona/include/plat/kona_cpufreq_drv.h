/*******************************************************************************
* Copyright 2011 Broadcom Corporation.  All rights reserved.
*
*       @file   arch/arm/mach-bcm215xx/kona_cpufreq_drv.h
*
* Unless you and Broadcom execute a separate written software license agreement
* governing use of this software, this software is licensed to you under the
* terms of the GNU General Public License version 2, available at
* http://www.gnu.org/copyleft/gpl.html (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*******************************************************************************/

#ifndef KONA_CPUFREQ_DRV_H
#define KONA_CPUFREQ_DRV_H

struct kona_freq_tbl
{
	u32 cpu_freq;         /* in MHz */
	int opp;         /* Operating point eg: ECONOMY, NORMAL, TURBO */
};

/* Helper to initialize array of above structures */
#define FTBL_INIT(freq, __opp)  \
{                              \
	.cpu_freq    = freq,   \
	.opp = __opp,   \
}

struct kona_cpu_info
{
	/* Table of cpu frequencies and voltages supported for a cpu */
	struct kona_freq_tbl *freq_tbl;
	/* Number of entries in the DVFS table */
	int num_freqs;
	/* ID of the Power island to which this CCU belongs */
	int pi_id;
	/* CPU Frequency transition latency in ns */
	u32 kona_latency;
};

/* Platform data for BCM21553 cpufreq driver */
struct kona_cpufreq_drv_plat
{
	struct kona_cpu_info *info;
	/* Number of cpus (hence, number of entries in above table) */
	int nr_cpus;
};

#endif /* BCM_CPUFREQ_DRV_H */
