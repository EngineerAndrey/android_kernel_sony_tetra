/****************************************************************************
*
* Copyright 2010 --2011 Broadcom Corporation.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
*****************************************************************************/

#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/clkdev.h>
#include <asm/io.h>

#include <plat/pi_mgr.h>
#include <mach/pwr_mgr.h>
#include <plat/pwr_mgr.h>
#include <mach/io_map.h>
#include <mach/clock.h>
#include <plat/clock.h>
#if defined(CONFIG_KONA_PWRMGR_REV2)
#include <linux/completion.h>
#endif

#ifdef CONFIG_DEBUG_FS
#include <asm/uaccess.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#ifndef PWRMGR_I2C_VAR_DATA_REG
#define PWRMGR_I2C_VAR_DATA_REG 6
#endif /*PWRMGR_I2C_VAR_DATA_REG*/

#ifndef PWRMGR_HW_SEM_WA_PI_ID
#define PWRMGR_HW_SEM_WA_PI_ID 0
#endif

#ifndef PWRMGR_HW_SEM_LOCK_WA_PI_OPP
#define PWRMGR_HW_SEM_LOCK_WA_PI_OPP 2
#endif
#ifndef PWRMGR_HW_SEM_UNLOCK_WA_PI_OPP
#define PWRMGR_HW_SEM_UNLOCK_WA_PI_OPP 	PI_MGR_DFS_MIN_VALUE
#endif


#ifdef CONFIG_DEBUG_FS
#ifndef PWRMGR_EVENT_ID_TO_STR
static char* pwr_mgr_event2str(int event)
{
	static char str[10] ;
	sprintf(str,"event_%d",event);
	return str;
}
#define PWRMGR_EVENT_ID_TO_STR(e) pwr_mgr_event2str(e)
#endif /*PWRMGR_EVENT_ID_TO_STR*/
#endif /*CONFIG_DEBUG_FS*/


#define I2C_CMD0_DATA_SHIFT \
		PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_I2C_COMMAND_DATA__01_0_SHIFT
#define I2C_CMD0_DATA_MASK \
		PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_I2C_COMMAND_DATA__01_0_MASK
#define I2C_CMD1_DATA_SHIFT \
		PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_I2C_COMMAND_DATA__01_1_SHIFT
#define I2C_CMD1_DATA_MASK \
		PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_I2C_COMMAND_DATA__01_1_MASK

#define I2C_CMD0_SHIFT \
		PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_I2C_COMMAND__01_0_SHIFT
#define I2C_CMD0_MASK \
		PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_I2C_COMMAND__01_0_MASK
#define I2C_CMD1_SHIFT \
		PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_I2C_COMMAND__01_1_SHIFT
#define I2C_CMD1_MASK \
		PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_I2C_COMMAND__01_1_MASK

#define I2C_COMMAND_WORD(cmd1,cmd1_data,cmd0,cmd0_data) \
			( ((((u32)(cmd0)) << I2C_CMD0_SHIFT) & I2C_CMD0_MASK ) |\
				((((u32)(cmd0_data)) << I2C_CMD0_DATA_SHIFT) & I2C_CMD0_DATA_MASK) |\
				((((u32)(cmd1)) << I2C_CMD1_SHIFT) & I2C_CMD1_MASK) |\
				((((u32)(cmd1_data))  << I2C_CMD1_DATA_SHIFT)& I2C_CMD1_DATA_MASK) )

#define PWR_MGR_REG_ADDR(offset) (pwr_mgr.info->base_addr+(offset))
#define PWR_MGR_PI_EVENT_POLICY_ADDR(pi_offset,event_offset) (\
				pwr_mgr.info->base_addr+(pi->pi_info.pi_offset)+(event_offset))
#define PWR_MGR_PI_ADDR(pi_offset) (\
				pwr_mgr.info->base_addr+(pi->pi_info.pi_offset))

#if defined(CONFIG_KONA_PWRMGR_REV2)

#ifndef PWRMGR_NUM_EVENT_BANK
#define PWRMGR_NUM_EVENT_BANK			6
#endif

#define PWR_MGR_EVENT_ID_TO_BANK_REG_OFF(event) (PWRMGR_EVENT_BANK1_OFFSET+(4*((event)/32)))
#define PWR_MGR_EVENT_ID_TO_BIT_POS(event)	((event)%32)
#define PWR_MGR_I2C_CMD_OFF_TO_REG_OFF(x)	(4*((x)/2) +\
						PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_OFFSET)
#define PWR_MGR_I2C_CMD_OFF_TO_CMD_DATA_SHIFT(x) ((((x) % 2) == 0) ? \
												I2C_CMD0_DATA_SHIFT : I2C_CMD1_DATA_SHIFT)
#define PWR_MGR_I2C_CMD_OFF_TO_CMD_DATA_MASK(x) ((((x) % 2) == 0) ? \
												I2C_CMD0_DATA_MASK : I2C_CMD1_DATA_MASK)

#define PWR_MGR_INTR_MASK(x)     (1 << (x))
/*I2C SW seq operations*/
enum
{
	I2C_SEQ_READ,
	I2C_SEQ_WRITE,
};

#endif



static int pwr_debug = 0;
/* global spinlock for pwr mgr API */
static DEFINE_SPINLOCK(pwr_mgr_lock);

struct pwr_mgr_event
{
	void (*pwr_mgr_event_cb)(u32 event_id,void* param);
	void* param;
};

struct pwr_mgr
{
	struct pwr_mgr_info* info;
	struct pwr_mgr_event event_cb[PWR_MGR_NUM_EVENTS];
	struct pi_mgr_dfs_node* sem_dfs_client;
#ifndef CONFIG_KONA_PWRMGR_ENABLE_HW_SEM_WORKAROUND
	u32 i2c_sem_val;
#endif /*CONFIG_KONA_PWRMGR_ENABLE_HW_SEM_WORKAROUND*/
	bool sem_locked;
#if defined(CONFIG_KONA_PWRMGR_REV2)
	u32 i2c_seq_trg;
	struct completion i2c_seq_done;
	struct work_struct pwrmgr_work;
#endif
};

static struct pwr_mgr pwr_mgr;

int	pwr_mgr_event_trg_enable(int event_id,int event_trg_type)
{
    u32 reg_val = 0;
	u32 reg_offset;
	pwr_dbg("%s:event_id: %d, trg : %d\n",
		__func__,event_id,event_trg_type);
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(event_id >= PWR_MGR_NUM_EVENTS))
	{
		pwr_dbg("%s:invalid event id\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	reg_offset = event_id * 4;

    reg_val = readl(PWR_MGR_REG_ADDR(reg_offset));

	/*clear both pos & neg edge bits */
    reg_val &= ~PWRMGR_EVENT_NEGEDGE_CONDITION_ENABLE_MASK;
    reg_val &= ~PWRMGR_EVENT_POSEDGE_CONDITION_ENABLE_MASK;

	if(event_trg_type & PM_TRIG_POS_EDGE)
		reg_val |= PWRMGR_EVENT_POSEDGE_CONDITION_ENABLE_MASK;
    if(event_trg_type & PM_TRIG_NEG_EDGE)
	reg_val |= PWRMGR_EVENT_NEGEDGE_CONDITION_ENABLE_MASK;

    writel(reg_val, PWR_MGR_REG_ADDR(reg_offset));
	pwr_dbg("%s:reg_addr:%x value = %x\n",__func__,PWR_MGR_REG_ADDR(reg_offset),reg_val);
	spin_unlock(&pwr_mgr_lock);

	return 0;
}
EXPORT_SYMBOL(pwr_mgr_event_trg_enable);

int	pwr_mgr_get_event_trg_type(int event_id)
{
	u32 reg_val = 0;
	int trig_type = PM_TRIG_NONE;
	u32 reg_offset;
	pwr_dbg("%s:event_id: %d\n",
		__func__,event_id);
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(event_id >= PWR_MGR_NUM_EVENTS))
	{
		pwr_dbg("%s:invalid event id\n",__func__);
		return -EPERM;
	}
	reg_offset = event_id * 4;

    reg_val = readl(PWR_MGR_REG_ADDR(reg_offset));

	if(reg_val & PWRMGR_EVENT_NEGEDGE_CONDITION_ENABLE_MASK)
		trig_type |= PM_TRIG_NEG_EDGE;

	if(reg_val & PWRMGR_EVENT_POSEDGE_CONDITION_ENABLE_MASK)
		trig_type |= PM_TRIG_POS_EDGE;

	return trig_type;

}
EXPORT_SYMBOL(pwr_mgr_get_event_trg_type);

int	pwr_mgr_event_clear_events(u32 event_start, u32 event_end)
{
	u32 reg_val = 0;
	int inx;
	pwr_dbg("%s\n",__func__);
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}
	if(event_end == EVENT_ID_ALL)
	{
		event_end = PWR_MGR_NUM_EVENTS-1;
	}

	if(event_start == EVENT_ID_ALL)
	{
		event_start = 0;
	}

	if(unlikely(event_end >= PWR_MGR_NUM_EVENTS ||
			event_start > event_end))
	{
		pwr_dbg("%s:invalid event id\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	for(inx = event_start; inx <= event_end;inx++)
	{
		reg_val = readl(PWR_MGR_REG_ADDR(inx*4));
		if(reg_val & PWRMGR_EVENT_CONDITION_ACTIVE_MASK)
		{
			reg_val &= ~PWRMGR_EVENT_CONDITION_ACTIVE_MASK;
			writel(reg_val,PWR_MGR_REG_ADDR(inx*4));
		}
	}
	spin_unlock(&pwr_mgr_lock);
	return 0;

}
EXPORT_SYMBOL(pwr_mgr_event_clear_events);

bool pwr_mgr_is_event_active(int event_id)
{
	u32 reg_val = 0;
	pwr_dbg("%s : event_id = %d\n",__func__,event_id);
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return false;
	}

	if(unlikely(event_id >= PWR_MGR_NUM_EVENTS))
	{
		pwr_dbg("%s:invalid event id\n",__func__);
		return false;
	}
	reg_val = readl(PWR_MGR_REG_ADDR(event_id*4));
	return !!(reg_val & PWRMGR_EVENT_CONDITION_ACTIVE_MASK);

}
EXPORT_SYMBOL(pwr_mgr_is_event_active);

int	pwr_mgr_event_set(int event_id, int event_state)
{
	u32 reg_val = 0;
	pwr_dbg("%s : event_id = %d : enable = %d\n",__func__,event_id,!!event_state);
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(event_id >= PWR_MGR_NUM_EVENTS))
	{
		pwr_dbg("%s:invalid event id\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	reg_val = readl(PWR_MGR_REG_ADDR(event_id*4));
	if(event_state)
		reg_val |= PWRMGR_EVENT_CONDITION_ACTIVE_MASK;
	else
		reg_val &= ~PWRMGR_EVENT_CONDITION_ACTIVE_MASK;
	writel(reg_val,PWR_MGR_REG_ADDR(event_id*4));
	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_event_set);

int pwr_mgr_event_set_pi_policy(int event_id,int pi_id,const struct pm_policy_cfg* pm_policy_cfg)
{
	u32 reg_val = 0;
	const struct pi* pi;
	int realEventId, i;

	pwr_dbg("%s : event_id = %d : pi_id = %d, ac : %d, ATL : %d, policy: %d\n",
		__func__,event_id,pi_id,pm_policy_cfg->ac, pm_policy_cfg->atl, pm_policy_cfg->policy);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(event_id >= PWR_MGR_NUM_EVENTS || pi_id >= pwr_mgr.info->num_pi))
	{
		pwr_dbg("%s:invalid group/pi id\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	pi = pi_mgr_get(pi_id);
	BUG_ON(pi == NULL);

	realEventId = event_id*4;
	if (pwr_mgr.info->num_special_event_range) {
	    for (i = 0; i < pwr_mgr.info->num_special_event_range; i++) {
		if (event_id >= pwr_mgr.info->special_event_list[i].start && event_id <= pwr_mgr.info->special_event_list[i].end) {
		    realEventId = pwr_mgr.info->special_event_list[i].start * 4;
		    break;
		}
	    }
	}

	reg_val = readl(PWR_MGR_PI_EVENT_POLICY_ADDR(policy_reg_offset, realEventId));

	if(pm_policy_cfg->ac)
		reg_val |= (1 << pi->pi_info.ac_shift);
	else
		reg_val &= ~(1 << pi->pi_info.ac_shift);

	if(pm_policy_cfg->atl)
		reg_val |= (1 << pi->pi_info.atl_shift);
	else
		reg_val &= ~(1 << pi->pi_info.atl_shift);

	reg_val &= ~(PM_POLICY_MASK << pi->pi_info.pm_policy_shift);
	reg_val |= (pm_policy_cfg->policy & PM_POLICY_MASK) <<
								pi->pi_info.pm_policy_shift;

	pwr_dbg("%s:reg val %08x shift val: %08x\n",__func__,reg_val, pi->pi_info.pm_policy_shift);

	writel(reg_val, PWR_MGR_PI_EVENT_POLICY_ADDR(policy_reg_offset, realEventId));
	pwr_dbg("%s : event_id = %d : pi_id = %d, ac : %d, ATL : %d, policy: %d\n",
		__func__,event_id,pi_id,pm_policy_cfg->ac, pm_policy_cfg->atl, pm_policy_cfg->policy);


	pwr_dbg("%s:reg val %08x written to register: %08x\n",
		__func__,reg_val, PWR_MGR_PI_EVENT_POLICY_ADDR(policy_reg_offset,event_id*4));

	spin_unlock(&pwr_mgr_lock);

	return 0;
}
EXPORT_SYMBOL(pwr_mgr_event_set_pi_policy);

int pwr_mgr_event_get_pi_policy(int event_id,int pi_id,struct pm_policy_cfg* pm_policy_cfg)
{
	u32 reg_val = 0;
	const struct pi* pi;
	int realEventId, i;
	pwr_dbg("%s : event_id = %d : pi_id = %d\n",
				__func__,event_id, pi_id);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(event_id >= PWR_MGR_NUM_EVENTS || pi_id >= pwr_mgr.info->num_pi))
	{
		pwr_dbg("%s:invalid event/pi id\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	pi = pi_mgr_get(pi_id);
	BUG_ON(pi == NULL);
	realEventId = event_id*4;
	if (pwr_mgr.info->num_special_event_range) {
	    for (i = 0; i < pwr_mgr.info->num_special_event_range; i++) {
		if (event_id >= pwr_mgr.info->special_event_list[i].start && event_id <= pwr_mgr.info->special_event_list[i].end) {
		    realEventId = pwr_mgr.info->special_event_list[i].start * 4;
		    break;
		}
	    }
	}
	reg_val = readl(PWR_MGR_PI_EVENT_POLICY_ADDR(policy_reg_offset, realEventId));

	pm_policy_cfg->ac = !!(reg_val & (1 << pi->pi_info.ac_shift));
	pm_policy_cfg->atl = !!(reg_val & (1 << pi->pi_info.atl_shift));

	pm_policy_cfg->policy = (reg_val >> pi->pi_info.pm_policy_shift) & PM_POLICY_MASK;
	spin_unlock(&pwr_mgr_lock);

	return 0;
}
EXPORT_SYMBOL(pwr_mgr_event_get_pi_policy);

int pwr_mgr_set_pi_fixed_volt_map(int pi_id,bool activate)
{
	u32 reg_val = 0;
	const struct pi* pi;
	pwr_dbg("%s : pi_id = %d\n",
				__func__,pi_id);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(pi_id >= pwr_mgr.info->num_pi))
	{
		pwr_dbg("%s:invalid event/pi id\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	pi = pi_mgr_get(pi_id);
	BUG_ON(pi == NULL);

	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_FIXED_VOLTAGE_MAP_OFFSET));
	if(activate)
		reg_val |= pi->pi_info.fixed_vol_map_mask;
	else
		reg_val &= ~pi->pi_info.fixed_vol_map_mask;
	writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_FIXED_VOLTAGE_MAP_OFFSET));
	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_set_pi_fixed_volt_map);

int	pwr_mgr_set_pi_vmap(int pi_id,int vset, bool activate)
{
	u32 reg_val = 0;
	const struct pi* pi;
	pwr_dbg("%s : vset = %d : pi_id = %d\n",
				__func__,vset, pi_id);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(pi_id >= pwr_mgr.info->num_pi ||
		vset < VOLT0 || vset > VOLT2))
	{
		pwr_dbg("%s:invalid param\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	pi = pi_mgr_get(pi_id);
	BUG_ON(pi == NULL);

	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_VI_TO_VO0_MAP_OFFSET+4*vset));
	if(activate)
		reg_val |= pi->pi_info.vi_to_vOx_map_mask;
	else
		reg_val &= ~pi->pi_info.vi_to_vOx_map_mask;

	writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_VI_TO_VO0_MAP_OFFSET+4*vset));
	spin_unlock(&pwr_mgr_lock);
	return 0;

}
EXPORT_SYMBOL(pwr_mgr_set_pi_vmap);

int	pwr_mgr_pi_set_wakeup_override(int pi_id, bool clear)
{
	u32 reg_val = 0;
	const struct pi* pi;
	pwr_dbg("%s : clear = %d : pi_id = %d\n",
				__func__,clear, pi_id);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(pi_id >= pwr_mgr.info->num_pi))
	{
		pwr_dbg("%s:invalid param\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	pi = pi_mgr_get(pi_id);
	BUG_ON(pi == NULL);

	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));
	if(clear)
		reg_val &= ~pi->pi_info.wakeup_overide_mask;
	else
		reg_val |= pi->pi_info.wakeup_overide_mask;
	pr_info("%s:just before reg update...\n",__func__);
	writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));
	spin_unlock(&pwr_mgr_lock);
	return 0;

}
EXPORT_SYMBOL(pwr_mgr_pi_set_wakeup_override);

int pwr_mgr_set_pc_sw_override(int pc_pin, bool enable, int value)
{
	u32 reg_val = 0;
	u32 value_mask, enable_mask;

	pwr_dbg("%s : pc_pin = %d : enable = %d,value = %d\n",
				__func__,pc_pin, enable,value);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	spin_lock(&pwr_mgr_lock);

	switch (pc_pin)
    {
    case PC3:
    	value_mask = PWRMGR_PC3_SW_OVERRIDE_VALUE_MASK;
		enable_mask = PWRMGR_PC3_SW_OVERRIDE_ENABLE_MASK;
    	break;
    case PC2:
    	value_mask = PWRMGR_PC2_SW_OVERRIDE_VALUE_MASK;
		enable_mask = PWRMGR_PC2_SW_OVERRIDE_ENABLE_MASK;
		break;
    case PC1:
    	value_mask = PWRMGR_PC1_SW_OVERRIDE_VALUE_MASK;
		enable_mask = PWRMGR_PC1_SW_OVERRIDE_ENABLE_MASK;
		break;
	case PC0:
    	value_mask = PWRMGR_PC0_SW_OVERRIDE_VALUE_MASK;
		enable_mask = PWRMGR_PC0_SW_OVERRIDE_ENABLE_MASK;
	break;
    default:
		pwr_dbg("%s:invalid param\n",__func__);
    	return -EINVAL;
    }
    reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PC_PIN_OVERRIDE_CONTROL_OFFSET));
    if(enable)
		reg_val = reg_val | (value_mask | enable_mask);
    else
		reg_val = reg_val & (~enable_mask);
	 writel(reg_val, PWR_MGR_REG_ADDR(PWRMGR_PC_PIN_OVERRIDE_CONTROL_OFFSET));
	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_set_pc_sw_override);

int	pwr_mgr_set_pc_clkreq_override(int pc_pin, bool enable, int value)
{
	u32 reg_val = 0;
	u32 value_mask, enable_mask;

	pwr_dbg("%s : pc_pin = %d : enable = %d,value = %d\n",
				__func__,pc_pin, enable,value);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	spin_lock(&pwr_mgr_lock);

	switch (pc_pin)
    {
    case PC3:
    	value_mask = PWRMGR_PC3_CLKREQ_OVERRIDE_VALUE_MASK;
		enable_mask = PWRMGR_PC3_CLKREQ_OVERRIDE_ENABLE_MASK;
    	break;
    case PC2:
    	value_mask = PWRMGR_PC2_CLKREQ_OVERRIDE_VALUE_MASK;
		enable_mask = PWRMGR_PC2_CLKREQ_OVERRIDE_ENABLE_MASK;
		break;
    case PC1:
    	value_mask = PWRMGR_PC1_CLKREQ_OVERRIDE_VALUE_MASK;
		enable_mask = PWRMGR_PC1_CLKREQ_OVERRIDE_ENABLE_MASK;
		break;
	case PC0:
    	value_mask = PWRMGR_PC0_CLKREQ_OVERRIDE_VALUE_MASK;
		enable_mask = PWRMGR_PC0_CLKREQ_OVERRIDE_ENABLE_MASK;
	break;
    default:
		pwr_dbg("%s:invalid param\n",__func__);
    	return -EINVAL;
    }
    reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PC_PIN_OVERRIDE_CONTROL_OFFSET));
    if(enable)
		reg_val = reg_val | (value_mask | enable_mask);
    else
		reg_val = reg_val & (~enable_mask);
	writel(reg_val, PWR_MGR_REG_ADDR(PWRMGR_PC_PIN_OVERRIDE_CONTROL_OFFSET));
	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_set_pc_clkreq_override);

int pm_get_pc_value(int pc_pin)
{
	u32 reg_val = 0;
	u32 value;

	pwr_dbg("%s : pc_pin = %d \n",
				__func__,pc_pin);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PC_PIN_OVERRIDE_CONTROL_OFFSET));
	switch(pc_pin)
    {
    case PC3:
    	value = reg_val & PWRMGR_PC3_CURRENT_VALUE_MASK;
    	break;
    case PC2:
    	value = reg_val & PWRMGR_PC2_CURRENT_VALUE_MASK;
		break;
    case PC1:
    	value = reg_val & PWRMGR_PC1_CURRENT_VALUE_MASK;
		break;
    case PC0:
    	value = reg_val & PWRMGR_PC0_CURRENT_VALUE_MASK;
		break;
    default:
		pwr_dbg("%s:invalid param\n",__func__);
    	return -EINVAL;
    	break;
    }

    return (!!value);
}
EXPORT_SYMBOL(pm_get_pc_value);

int pwr_mgr_pi_counter_enable(int pi_id, bool enable)
{
	u32 reg_val = 0;
	const struct pi* pi;
	pwr_dbg("%s : pi_id = %d enable = %d\n",
				__func__, pi_id,enable);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(pi_id >= pwr_mgr.info->num_pi))
	{
		pwr_dbg("%s:invalid param\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	pi = pi_mgr_get(pi_id);
	BUG_ON(pi == NULL);
	reg_val = readl(PWR_MGR_PI_ADDR(counter_reg_offset));
	pwr_dbg("%s:counter reg val = %x\n",__func__,reg_val);

	if(enable)
		reg_val |= PWRMGR_PI_ARM_CORE_ON_COUNTER_PI_ARM_CORE_ON_COUNTER_ENABLE_MASK;
	else
		reg_val &= ~PWRMGR_PI_ARM_CORE_ON_COUNTER_PI_ARM_CORE_ON_COUNTER_ENABLE_MASK;
	writel(reg_val,PWR_MGR_PI_ADDR(counter_reg_offset));
	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_pi_counter_enable);

int pwr_mgr_pi_counter_read(int pi_id,bool* over_flow)
{
	u32 reg_val = 0;
	const struct pi* pi;
	pwr_dbg("%s : pi_id = %d\n",
				__func__, pi_id);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return 0;
	}

	if(unlikely(pi_id >= pwr_mgr.info->num_pi))
	{
		pwr_dbg("%s:invalid param\n",__func__);
		return 0;
	}
	pi = pi_mgr_get(pi_id);
	BUG_ON(pi == NULL);
	reg_val = readl(PWR_MGR_PI_ADDR(counter_reg_offset));
	pwr_dbg("%s:counter reg val = %x\n",__func__,reg_val);

	if(over_flow)
		*over_flow = !!(reg_val &
						PWRMGR_PI_ARM_CORE_ON_COUNTER_PI_ARM_CORE_ON_COUNTER_OVERFLOW_MASK);
	return ((reg_val & PWRMGR_PI_ARM_CORE_ON_COUNTER_PI_ARM_CORE_ON_COUNTER_MASK)
					>> PWRMGR_PI_ARM_CORE_ON_COUNTER_PI_ARM_CORE_ON_COUNTER_SHIFT);
}
EXPORT_SYMBOL(pwr_mgr_pi_counter_read);

#ifdef CONFIG_KONA_PWRMGR_ENABLE_HW_SEM_WORKAROUND

int pwr_mgr_pm_i2c_sem_lock()
{
    unsigned long flgs;
    int pc_val;
    int ins = 0;
	const int max_wait_count = 10000;

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EINVAL;
	}
	BUG_ON(pwr_mgr.sem_locked);
	if((pwr_mgr.info->flags & PM_HW_SEM_NO_DFS_REQ) == 0)
	{
		if(!pwr_mgr.sem_dfs_client)
			pwr_mgr.sem_dfs_client = pi_mgr_dfs_add_request("sem_wa",PWRMGR_HW_SEM_WA_PI_ID, PWRMGR_HW_SEM_LOCK_WA_PI_OPP);
		else
			pi_mgr_dfs_request_update(pwr_mgr.sem_dfs_client,PWRMGR_HW_SEM_LOCK_WA_PI_OPP);
	}
    spin_lock_irqsave(&pwr_mgr_lock,flgs);

	mdelay(2);
    do
    {
		udelay(1);
        pc_val = pm_get_pc_value(PC3);
        ins++;
	}while(pc_val == 0 && ins < max_wait_count);

	if(ins == max_wait_count)
		__WARN();
	pwr_mgr_pm_i2c_enable(false);
	pwr_mgr.sem_locked = true;
	spin_unlock_irqrestore(&pwr_mgr_lock,flgs);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_pm_i2c_sem_lock);

int pwr_mgr_pm_i2c_sem_unlock()
{
	unsigned long flgs;
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EINVAL;
	}
	BUG_ON(pwr_mgr.sem_locked == false);
	spin_lock_irqsave(&pwr_mgr_lock,flgs);
	pwr_mgr_pm_i2c_enable(true);
	pwr_mgr.sem_locked = false;
	spin_unlock_irqrestore(&pwr_mgr_lock,flgs);
	if((pwr_mgr.info->flags & PM_HW_SEM_NO_DFS_REQ) == 0)
		pi_mgr_dfs_request_update(pwr_mgr.sem_dfs_client,PWRMGR_HW_SEM_UNLOCK_WA_PI_OPP);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_pm_i2c_sem_unlock);

#else
int pwr_mgr_pm_i2c_sem_lock()
{
	u32 value, read_val,write_val;
	u32 max_val;
	u32 ret = 0;
	u32 insurance = 1000;
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EINVAL;
	}

	BUG_ON(pwr_mgr.sem_locked);

	if((pwr_mgr.info->flags & PM_HW_SEM_NO_DFS_REQ) == 0)
	{
		if(!pwr_mgr.sem_dfs_client)
			pwr_mgr.sem_dfs_client = pi_mgr_dfs_add_request("sem_wa",PWRMGR_HW_SEM_WA_PI_ID, PWRMGR_HW_SEM_LOCK_WA_PI_OPP);
		else
			pi_mgr_dfs_request_update(pwr_mgr.sem_dfs_client,PWRMGR_HW_SEM_LOCK_WA_PI_OPP);
	}
	udelay(2);

	max_val = 1 + (PWRMGR_I2C_HARDWARE_SEMAPHORE_WRITE_I2C_HARDWARE_SEMAPHORE_WRITE_VALUE_MASK >>
				PWRMGR_I2C_HARDWARE_SEMAPHORE_WRITE_I2C_HARDWARE_SEMAPHORE_WRITE_VALUE_SHIFT);

	spin_lock(&pwr_mgr_lock);
	pwr_mgr.i2c_sem_val = (pwr_mgr.i2c_sem_val+1)% max_val;
	if(pwr_mgr.i2c_sem_val == 0)
		pwr_mgr.i2c_sem_val++;

	value = pwr_mgr.i2c_sem_val;
	write_val = (value << PWRMGR_I2C_HARDWARE_SEMAPHORE_WRITE_I2C_HARDWARE_SEMAPHORE_WRITE_VALUE_SHIFT)
				& PWRMGR_I2C_HARDWARE_SEMAPHORE_WRITE_I2C_HARDWARE_SEMAPHORE_WRITE_VALUE_MASK;
	pwr_dbg("%s: value = %x max_val = %x\n",__func__,value,max_val);
	do
	{
		writel(write_val,
			PWR_MGR_REG_ADDR(PWRMGR_I2C_HARDWARE_SEMAPHORE_WRITE_OFFSET));
		udelay(1);
		read_val = readl(PWR_MGR_REG_ADDR(PWRMGR_I2C_HARDWARE_SEMAPHORE_READ_OFFSET));
		read_val &= PWRMGR_I2C_HARDWARE_SEMAPHORE_READ_I2C_HARDWARE_SEMAPHORE_READ_VALUE_MASK;
		read_val >>= PWRMGR_I2C_HARDWARE_SEMAPHORE_READ_I2C_HARDWARE_SEMAPHORE_READ_VALUE_SHIFT;
		insurance--;
	}while(read_val != value && insurance);

	if(read_val != value)
	{
		pr_info("%s: failed to acquire PMU I2C HW sem !!\n",__func__);
		ret = -EAGAIN;
	}
	else
		pwr_mgr.sem_locked = true;
	spin_unlock(&pwr_mgr_lock);
	return ret;
}
EXPORT_SYMBOL(pwr_mgr_pm_i2c_sem_lock);

int pwr_mgr_pm_i2c_sem_unlock()
{
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EINVAL;
	}
	BUG_ON(pwr_mgr.sem_locked == false);
	spin_lock(&pwr_mgr_lock);
	writel(0,
			PWR_MGR_REG_ADDR(PWRMGR_I2C_HARDWARE_SEMAPHORE_WRITE_OFFSET));
	pwr_mgr.sem_locked = false;
	spin_unlock(&pwr_mgr_lock);
	if((pwr_mgr.info->flags & PM_HW_SEM_NO_DFS_REQ) == 0)
		pi_mgr_dfs_request_update(pwr_mgr.sem_dfs_client,PWRMGR_HW_SEM_UNLOCK_WA_PI_OPP);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_pm_i2c_sem_unlock);

#endif /*CONFIG_KONA_PWRMGR_ENABLE_HW_SEM_WORKAROUND*/

int	pwr_mgr_pm_i2c_enable(bool enable)
{
	u32 reg_val;
	pwr_dbg("%s:enable = %d\n",__func__,enable);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EINVAL;
	}
	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_POWER_MANAGER_I2C_ENABLE_OFFSET));
	if(enable)
		reg_val |= PWRMGR_POWER_MANAGER_I2C_ENABLE_POWER_MANAGER_I2C_ENABLE_MASK;
	else
		reg_val &= ~PWRMGR_POWER_MANAGER_I2C_ENABLE_POWER_MANAGER_I2C_ENABLE_MASK;
	writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_POWER_MANAGER_I2C_ENABLE_OFFSET));
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_pm_i2c_enable);

int	pwr_mgr_set_v0x_specific_i2c_cmd_ptr(int v0x, const struct v0x_spec_i2c_cmd_ptr* cmd_ptr)
{
	u32 reg_val;
	u32 offset;
	pwr_dbg("%s:v0x = %d\n",__func__,v0x);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EINVAL;
	}

	if(unlikely(v0x < VOLT0 || v0x > VOLT2))
	{
		pwr_dbg("%s:ERROR - invalid param\n",__func__);
		return -EINVAL;
	}
	spin_lock(&pwr_mgr_lock);
	offset = PWRMGR_VO0_SPECIFIC_I2C_COMMAND_POINTER_OFFSET +4*v0x;

	reg_val = (cmd_ptr->set2_val << PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_SET2_VALUE_SHIFT)
    				& PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_SET2_VALUE_MASK;
    reg_val |= (cmd_ptr->set2_ptr << PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_SET2_SHIFT)
    				& PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_SET2_MASK;
    reg_val |= (cmd_ptr->set1_val << PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_SET1_VALUE_SHIFT)
    				& PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_SET1_VALUE_MASK;
    reg_val |= (cmd_ptr->set1_ptr << PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_SET1_SHIFT)
    				& PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_SET1_MASK;
    reg_val |= (cmd_ptr->zerov_ptr << PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_ZEROV_SHIFT)
    				& PWRMGR_VO_SPECIFIC_I2C_COMMAND_PTR_ZEROV_MASK;
    reg_val |= (cmd_ptr->other_ptr << PWRMGR_VO_SPECIFIC_I2C_COMMAND_POINTER_SHIFT)
    				& PWRMGR_VO_SPECIFIC_I2C_COMMAND_POINTER_MASK;
    writel(reg_val, PWR_MGR_REG_ADDR(offset));

	pwr_dbg("%s: %x set to %x register\n",__func__,reg_val,PWR_MGR_REG_ADDR(offset));
	spin_unlock(&pwr_mgr_lock);
	return 0;

}
int pwr_mgr_pm_i2c_cmd_write(const struct i2c_cmd* i2c_cmd , u32 num_cmds)
{
	u32 inx;
	u32 reg_val;
	u8 cmd0,cmd1;
	u8 cmd0_data,cmd1_data;

	pwr_dbg("%s:num_cmds = %d\n",__func__,num_cmds);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EINVAL;
	}

	if(unlikely((pwr_mgr.info->flags & PM_PMU_I2C) == 0))
	{
		pwr_dbg("%s:ERROR - invalid param\n",__func__);
		return -EINVAL;
	}
	if(unlikely(num_cmds > PM_I2C_CMD_MAX))
	{
		pwr_dbg("%s:ERROR - invalid param(num_cmds > PM_I2C_CMD_MAX)\n",__func__);
		return -EINVAL;
	}

	spin_lock(&pwr_mgr_lock);
	for(inx=0; inx < (num_cmds+1)/2; inx++)
	{
		cmd0 = i2c_cmd[inx*2].cmd;
		cmd0_data = i2c_cmd[inx*2].cmd_data;

		if((2*inx+1) < num_cmds)
		{
			cmd1 = i2c_cmd[inx*2+1].cmd;
			cmd1_data = i2c_cmd[inx*2+1].cmd_data;
		}
		else
		{
			reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_OFFSET + inx*4));
			cmd1 = (reg_val & I2C_CMD1_MASK) >> I2C_CMD1_SHIFT;
			cmd1_data = (reg_val & I2C_CMD1_DATA_MASK) >> I2C_CMD1_DATA_SHIFT;
		}
		pwr_dbg("%s:cmd0 = %x cmd0_data = %x cmd1 = %x cmd1_data = %x",__func__, cmd0,cmd0_data,cmd1,cmd1_data);
		reg_val = I2C_COMMAND_WORD(cmd1,cmd1_data,cmd0,cmd0_data);
		writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_OFFSET + inx*4));
		pwr_dbg("%s: %x set to %x register\n",__func__,reg_val,PWR_MGR_REG_ADDR(PWRMGR_POWER_MANAGER_I2C_COMMAND_DATA_LOCATION_01_OFFSET + inx*4));
	}
	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_pm_i2c_cmd_write);

int pwr_mgr_pm_i2c_var_data_write(const u8* var_data,int count)
{
	u32 inx;
	u32 reg_val;

	pwr_dbg("%s:\n",__func__);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EINVAL;
	}

	if(unlikely((pwr_mgr.info->flags & PM_PMU_I2C) == 0 || (count > PWRMGR_I2C_VAR_DATA_REG*4)))
	{
		pwr_dbg("%s:ERROR - invalid param or not supported\n",__func__);
		return -EINVAL;
	}
	spin_lock(&pwr_mgr_lock);

	for(inx=0; inx < count/4; inx++)
	{
		reg_val = (var_data[inx*4] <<PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_I2C_VARIABLE_DATA_00_SHIFT) &
			PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_I2C_VARIABLE_DATA_00_MASK;
		reg_val |= (var_data[inx*4+1] <<PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_I2C_VARIABLE_DATA_01_SHIFT) &
			PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_I2C_VARIABLE_DATA_01_MASK;
		reg_val |= (var_data[inx*4+2] <<PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_I2C_VARIABLE_DATA_02_SHIFT) &
			PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_I2C_VARIABLE_DATA_02_MASK;
		reg_val |= (var_data[inx*4+3] <<PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_I2C_VARIABLE_DATA_03_SHIFT) &
			PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_I2C_VARIABLE_DATA_03_MASK;

		writel(reg_val, PWR_MGR_REG_ADDR(PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_OFFSET + inx*4));
		pwr_dbg("%s: %x set to %x register\n",__func__,
		reg_val,PWR_MGR_REG_ADDR(PWRMGR_POWER_MANAGER_I2C_VARIABLE_DATA_LOCATION_01_OFFSET + inx*4));
	}

	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_pm_i2c_var_data_write);

int	pwr_mgr_arm_core_dormant_enable(bool enable)
{
	u32 reg_val = 0;
	pwr_dbg("%s : enable = %d\n",
				__func__,enable);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EINVAL;
	}
	spin_lock(&pwr_mgr_lock);
	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));
	if(enable)
		reg_val &= ~PWRMGR_PI_DEFAULT_POWER_STATE_ARM_CORE_DORMANT_DISABLE_MASK;
	else
		reg_val |= PWRMGR_PI_DEFAULT_POWER_STATE_ARM_CORE_DORMANT_DISABLE_MASK;
	writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));
	spin_unlock(&pwr_mgr_lock);

	return 0;
}
EXPORT_SYMBOL(pwr_mgr_arm_core_dormant_enable);

int	pwr_mgr_pi_retn_clamp_enable(int pi_id,bool enable)
{
	u32 reg_val = 0;
	const struct pi* pi;
	pwr_dbg("%s : pi_id = %d enable = %d\n",
				__func__, pi_id,enable);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(unlikely(pi_id >= pwr_mgr.info->num_pi))
	{
		pwr_dbg("%s:invalid param\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	pi = pi_mgr_get(pi_id);
	BUG_ON(pi == NULL);
	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));
	if(enable)
		reg_val &= ~pi->pi_info.rtn_clmp_dis_mask;
	else
		reg_val |= pi->pi_info.rtn_clmp_dis_mask;
	writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));

	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_pi_retn_clamp_enable);

int pwr_mgr_ignore_power_ok_signal(bool ignore)
{
	u32 reg_val = 0;
	pwr_dbg("%s :ignore = %d\n",
				__func__, ignore);

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);

	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));

	if(ignore)
		reg_val |= PWRMGR_PI_DEFAULT_POWER_STATE_POWER_OK_MASK_MASK;
	else
		reg_val &= ~PWRMGR_PI_DEFAULT_POWER_STATE_POWER_OK_MASK_MASK;

	writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));
	spin_unlock(&pwr_mgr_lock);

	return 0;
}
EXPORT_SYMBOL(pwr_mgr_ignore_power_ok_signal);

int pwr_mgr_ignore_dap_powerup_request(bool ignore)
{
	u32 reg_val = 0;
	pwr_dbg("%s :ignore = %d\n",
				__func__, ignore);

	if (unlikely(!pwr_mgr.info)) {
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n", __func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));

	if (ignore)
		reg_val |= \
	    PWRMGR_PI_DEFAULT_POWER_STATE_IGNORE_DAP_POWERUPREQ_MASK;
	else
		reg_val &= \
	    ~PWRMGR_PI_DEFAULT_POWER_STATE_IGNORE_DAP_POWERUPREQ_MASK;

	writel(reg_val, PWR_MGR_REG_ADDR(PWRMGR_PI_DEFAULT_POWER_STATE_OFFSET));

	spin_unlock(&pwr_mgr_lock);

	return 0;
}
EXPORT_SYMBOL(pwr_mgr_ignore_dap_powerup_request);

int pwr_mgr_register_event_handler(u32 event_id, void (*pwr_mgr_event_cb)(u32 event_id,void* param),
											void* param)
{
	int ret = 0;
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}
	if(unlikely(event_id >= PWR_MGR_NUM_EVENTS))
	{
		pwr_dbg("%s:invalid event id\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	if(likely(!pwr_mgr.event_cb[event_id].pwr_mgr_event_cb))
	{
		pwr_mgr.event_cb[event_id].pwr_mgr_event_cb = pwr_mgr_event_cb;
		pwr_mgr.event_cb[event_id].param = param;
	}
	else
	{
		ret = -EINVAL;
		pwr_dbg("%s:Handler already registered for event id: %d\n",__func__, event_id);
	}
	spin_unlock(&pwr_mgr_lock);
	return ret;
}
EXPORT_SYMBOL(pwr_mgr_register_event_handler);

int pwr_mgr_unregister_event_handler(u32 event_id)
{
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}
	if(unlikely(event_id >= PWR_MGR_NUM_EVENTS))
	{
		pwr_dbg("%s:invalid event id\n",__func__);
		return -EPERM;
	}
	spin_lock(&pwr_mgr_lock);
	pwr_mgr.event_cb[event_id].pwr_mgr_event_cb = NULL;
	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_unregister_event_handler);

int pwr_mgr_process_events(u32 event_start, u32 event_end, int clear_event)
{
	u32 reg_val = 0;
	int inx;
#if defined(CONFIG_KONA_PWRMGR_REV2)
	u32 offset;
	u32 bit_pos;
	u32 event_reg;
#endif
	pwr_dbg("%s\n",__func__);
	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	if(event_end == EVENT_ID_ALL)
	{
		event_end = PWR_MGR_NUM_EVENTS-1;
	}

	if(event_start == EVENT_ID_ALL)
	{
		event_start = 0;
	}

	if(unlikely(event_end >= PWR_MGR_NUM_EVENTS ||
			event_start > event_end))
	{
		pwr_dbg("%s:invalid event id\n",__func__);
		return -EPERM;
	}

	spin_lock(&pwr_mgr_lock);
#if defined(CONFIG_KONA_PWRMGR_REV2)
	offset = PWR_MGR_EVENT_ID_TO_BANK_REG_OFF(event_start);
	reg_val = readl(PWR_MGR_REG_ADDR(offset));
	bit_pos = PWR_MGR_EVENT_ID_TO_BIT_POS(event_start);

	for(inx = event_start; inx <= event_end;inx++)
	{
		if(reg_val & (1 << bit_pos))
		{
			pwr_dbg("%s:event id : %x\n",__func__,inx);
			if(pwr_mgr.event_cb[inx].pwr_mgr_event_cb)
				pwr_mgr.event_cb[inx].pwr_mgr_event_cb(inx,pwr_mgr.event_cb[inx].param);
			if(clear_event)
			{
				event_reg = readl(PWR_MGR_REG_ADDR(inx*4));
				event_reg &= ~PWRMGR_EVENT_CONDITION_ACTIVE_MASK;
				writel(event_reg,PWR_MGR_REG_ADDR(inx*4));
			}
		}
		bit_pos = (bit_pos+1) % 32;
		if(0 == bit_pos)
		{
			offset +=4;
			reg_val = readl(PWR_MGR_REG_ADDR(offset));
		}
	}
#else
	for(inx = event_start; inx <= event_end;inx++)
	{
		reg_val = readl(PWR_MGR_REG_ADDR(inx*4));
		if(reg_val & PWRMGR_EVENT_CONDITION_ACTIVE_MASK)
		{
			pwr_dbg("%s:event id : %x\n",__func__,inx);
			if(pwr_mgr.event_cb[inx].pwr_mgr_event_cb)
				pwr_mgr.event_cb[inx].pwr_mgr_event_cb(inx,pwr_mgr.event_cb[inx].param);
			if(clear_event)
			{
				reg_val &= ~PWRMGR_EVENT_CONDITION_ACTIVE_MASK;
				writel(reg_val,PWR_MGR_REG_ADDR(inx*4));

			}
		}
	}
#endif
	spin_unlock(&pwr_mgr_lock);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_process_events);

#if defined(CONFIG_KONA_PWRMGR_REV2)
static void pwr_mgr_work_handler(struct work_struct *work)
{
	pwr_mgr_process_events(EVENT_ID_ALL,EVENT_ID_ALL,false);
}

static irqreturn_t pwr_mgr_irq_handler(int irq, void *dev_id)
{
	u32 status, mask;

	pr_info("%s\n",__func__);

	BUG_ON(unlikely(!pwr_mgr.info));

	status = readl(PWR_MGR_REG_ADDR(PWRMGR_INTR_STATUS_OFFSET));
	mask = readl(PWR_MGR_REG_ADDR(PWRMGR_INTR_MASK_OFFSET));
	pr_info("%s: status : %x, mask =%x\n", __func__,status, mask);
	if(status & PWR_MGR_INTR_MASK(PWRMGR_INTR_I2C_SW_SEQ) &&
			mask & PWR_MGR_INTR_MASK(PWRMGR_INTR_I2C_SW_SEQ))
	{
		pr_info("%s:PWRMGR_INTR_I2C_SW_SEQ\n",__func__);
		complete(&pwr_mgr.i2c_seq_done);
	}
	if(pwr_mgr.info->flags & PROCESS_EVENTS_ON_INTR)
	{
		if(status & PWR_MGR_INTR_MASK(PWRMGR_INTR_EVENTS) &&
				mask & PWR_MGR_INTR_MASK(PWRMGR_INTR_EVENTS))
		{
			pr_info("%s:PWRMGR_INTR_EVENTS\n",__func__);
			schedule_work(&pwr_mgr.pwrmgr_work);
		}
	}
    return IRQ_HANDLED;
}

static void pwr_mgr_update_i2c_cmd_data(u32 cmd_offset, u8 cmd_data)
{
	u32 reg_val = 0;
	u32 mask, shift;

	pr_info("%s:cmd_offset = %d, cmd_data = %x reg_Addr = %x\n",__func__,
						cmd_offset,cmd_data,
						PWR_MGR_REG_ADDR(PWR_MGR_I2C_CMD_OFF_TO_REG_OFF(cmd_offset)));

	reg_val = readl(PWR_MGR_REG_ADDR(
						PWR_MGR_I2C_CMD_OFF_TO_REG_OFF(cmd_offset)));
	shift = PWR_MGR_I2C_CMD_OFF_TO_CMD_DATA_SHIFT(cmd_offset);
	mask = PWR_MGR_I2C_CMD_OFF_TO_CMD_DATA_MASK(cmd_offset);
	pr_info("%s:reg_val = %x, shift = %d, mask = %x\n",__func__,
						reg_val,shift,mask);
	reg_val &= ~mask;
	reg_val |= cmd_data << shift;
	pr_info("%s:new reg  = %x,\n",__func__, reg_val);
	writel(reg_val,PWR_MGR_REG_ADDR(
						PWR_MGR_I2C_CMD_OFF_TO_REG_OFF(cmd_offset)));

}

static int pwr_mgr_sw_i2c_seq_start(u32 action)
{
	u32 reg_val;
	int time;
	int ret = 0;

	pr_info("%s\n",__func__);
	spin_lock(&pwr_mgr_lock);

	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_I2C_SW_CMD_CTRL_OFFSET));
	reg_val &= ~(PWRMGR_I2C_REQ_TRG_MASK|PWRMGR_I2C_SW_START_ADDR_MASK);
	BUG_ON(reg_val & PWRMGR_I2C_REQ_BUSY_MASK);

	switch(action)
	{
	case I2C_SEQ_READ:
		reg_val |=
			(pwr_mgr.info->i2c_rd_off << PWRMGR_I2C_SW_START_ADDR_SHIFT) &
					PWRMGR_I2C_SW_START_ADDR_MASK;
		break;
	case I2C_SEQ_WRITE:
		reg_val |=
			(pwr_mgr.info->i2c_wr_off << PWRMGR_I2C_SW_START_ADDR_SHIFT) &
					PWRMGR_I2C_SW_START_ADDR_MASK;
		break;
	default:
		BUG();
		break;
	}
	pwr_mgr.i2c_seq_trg = (pwr_mgr.i2c_seq_trg+1) % \
					((PWRMGR_I2C_REQ_TRG_MASK >> PWRMGR_I2C_REQ_TRG_SHIFT) + 1);
	if(!pwr_mgr.i2c_seq_trg)
		pwr_mgr.i2c_seq_trg++;
	reg_val |= pwr_mgr.i2c_seq_trg << PWRMGR_I2C_REQ_TRG_SHIFT;

	spin_unlock(&pwr_mgr_lock);

	INIT_COMPLETION(pwr_mgr.i2c_seq_done);
	pwr_mgr_mask_intr(PWRMGR_INTR_I2C_SW_SEQ,false);
	writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_I2C_SW_CMD_CTRL_OFFSET));
	 time = wait_for_completion_interruptible_timeout(&pwr_mgr.i2c_seq_done,
                  msecs_to_jiffies(pwr_mgr.info->i2c_seq_timeout));
    if(time <= 0)
	{
		ret = -EAGAIN;
		pr_info("%s seq timedout !!\n",__func__);
	}
	pwr_mgr_mask_intr(PWRMGR_INTR_I2C_SW_SEQ,true);
	/*disable sw seq*/
	//reg_val &= ~PWRMGR_I2C_REQ_TRG_MASK;
	//writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_I2C_SW_CMD_CTRL_OFFSET));
	return ret;
}

int pwr_mgr_pmu_reg_read(u8 reg_addr,u8 slave_addr, u8* reg_val)
{
	int ret;
	u32 reg;

	pr_info("%s\n",__func__);
	if(unlikely(!pwr_mgr.info))
	{
		pr_info("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	spin_lock(&pwr_mgr_lock);

	pwr_mgr_update_i2c_cmd_data(pwr_mgr.info->i2c_rd_slv_addr_off,
									(slave_addr << 1));
	pwr_mgr_update_i2c_cmd_data(pwr_mgr.info->i2c_rd_reg_addr_off,
									reg_addr);
	spin_unlock(&pwr_mgr_lock);

	ret = pwr_mgr_sw_i2c_seq_start(I2C_SEQ_READ);
	spin_lock(&pwr_mgr_lock);
	if(!ret && reg_val)
	{
		reg = readl(PWR_MGR_REG_ADDR(PWRMGR_I2C_SW_CMD_CTRL_OFFSET));
		*reg_val = (reg & PWRMGR_I2C_READ_DATA_MASK) >>
							PWRMGR_I2C_READ_DATA_SHIFT;
	}
	spin_unlock(&pwr_mgr_lock);
	return ret;

}
EXPORT_SYMBOL(pwr_mgr_pmu_reg_read);

int pwr_mgr_pmu_reg_write(u8 reg_addr,u8 slave_addr, u8 reg_val)
{

	pr_info("%s\n",__func__);
	if(unlikely(!pwr_mgr.info))
	{
		pr_info("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	spin_lock(&pwr_mgr_lock);

	pwr_mgr_update_i2c_cmd_data(pwr_mgr.info->i2c_wr_slv_addr_off,
									(slave_addr << 1));
	pwr_mgr_update_i2c_cmd_data(pwr_mgr.info->i2c_wr_reg_addr_off,
									reg_addr);
	pwr_mgr_update_i2c_cmd_data(pwr_mgr.info->i2c_wr_val_addr_off,
									reg_val);

	spin_unlock(&pwr_mgr_lock);

	return pwr_mgr_sw_i2c_seq_start(I2C_SEQ_WRITE);

}
EXPORT_SYMBOL(pwr_mgr_pmu_reg_write);

int pwr_mgr_pmu_reg_read_mul(u8 reg_addr_start,u8 slave_addr, u8 count, u8* reg_val)
{
	int i;
	int ret = -EPERM;
	pr_info("%s\n",__func__);
	if(unlikely(!pwr_mgr.info))
	{
		pr_info("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}
	if(pwr_mgr.info->flags & I2C_SIMULATE_BURST_MODE)
	{
		for(i =0; i < count; i++)
		{
			ret = pwr_mgr_pmu_reg_read(reg_addr_start+i,slave_addr,&reg_val[i]);
			if(ret)
				return ret;
		}
	}
	return ret;
}
EXPORT_SYMBOL(pwr_mgr_pmu_reg_read_mul);

int pwr_mgr_pmu_reg_write_mul(u8 reg_addr_start,u8 slave_addr, u8 count, u8* reg_val)
{
	int i;
	int ret = -EPERM;
	pr_info("%s\n",__func__);
	if(unlikely(!pwr_mgr.info))
	{
		pr_info("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}
	if(pwr_mgr.info->flags & I2C_SIMULATE_BURST_MODE)
	{
		for(i =0; i < count; i++)
		{
			ret = pwr_mgr_pmu_reg_write(reg_addr_start+i,slave_addr,reg_val[i]);
			if(ret)
				return ret;
		}
	}
	return ret;
}
EXPORT_SYMBOL(pwr_mgr_pmu_reg_write_mul);

int pwr_mgr_mask_intr(u32 intr, bool mask)
{
	u32 reg_val = 0;
	unsigned long flgs;
	u32 reg_mask = 0;
	pr_info("%s\n",__func__);
	if(unlikely(!pwr_mgr.info))
	{
		pr_info("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	spin_lock_irqsave(&pwr_mgr_lock,flgs);
	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_INTR_MASK_OFFSET));

	BUG_ON(PWRMGR_INTR_ALL != intr &&
					intr >= PWRMGR_INTR_MAX);

	if(intr == PWRMGR_INTR_ALL)
		reg_mask = 0xFFFFFFFF >> (32 - PWRMGR_INTR_MAX);
	else
		reg_mask = 1 << intr;
	if(!mask)
		reg_val |= reg_mask;
	else
		reg_val &= ~reg_mask;
	writel(reg_val,PWR_MGR_REG_ADDR(PWRMGR_INTR_MASK_OFFSET));
	spin_unlock_irqrestore(&pwr_mgr_lock,flgs);
	return 0;
}
EXPORT_SYMBOL(pwr_mgr_mask_intr);

int pwr_mgr_get_intr_status(u32 intr)
{
	u32 reg_val = 0;
	unsigned long flgs;
	pr_info("%s\n",__func__);
	if(unlikely(!pwr_mgr.info))
	{
		pr_info("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	spin_lock_irqsave(&pwr_mgr_lock,flgs);
	BUG_ON(intr >= PWRMGR_INTR_MAX);
	reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_INTR_STATUS_OFFSET));
	spin_unlock_irqrestore(&pwr_mgr_lock,flgs);
	return !!(reg_val & (1 << intr));
}
EXPORT_SYMBOL(pwr_mgr_get_intr_status);

#endif

int pwr_mgr_init(struct pwr_mgr_info* info)
{
	int ret = 0;
	u32 v_set;
	pwr_mgr.info = info;
	pwr_mgr.sem_dfs_client = NULL;
#ifndef CONFIG_KONA_PWRMGR_ENABLE_HW_SEM_WORKAROUND
	pwr_mgr.i2c_sem_val = 0;
#endif
	pwr_mgr.sem_locked = false;

	/*I2C seq is disabled by default*/
	pwr_mgr_pm_i2c_enable(false);
	/*init I2C seq,var data & cmd ptr if valid data available*/
	if(info->i2c_cmds)
	{
		ret = pwr_mgr_pm_i2c_cmd_write(info->i2c_cmds,
				info->num_i2c_cmds);
	}
	if(info->i2c_var_data)
	{
		ret |= pwr_mgr_pm_i2c_var_data_write(info->i2c_var_data,
						info->num_i2c_var_data);
	}
	for(v_set = VOLT0; v_set < V_SET_MAX;v_set++)
	{
		if(info->i2c_cmd_ptr[v_set])
			ret |= pwr_mgr_set_v0x_specific_i2c_cmd_ptr(v_set,info->i2c_cmd_ptr[v_set]);
	}
#if defined(CONFIG_KONA_PWRMGR_REV2)

	pwr_mgr.i2c_seq_trg = 0;
	init_completion(&pwr_mgr.i2c_seq_done);
	INIT_WORK(&pwr_mgr.pwrmgr_work,
				pwr_mgr_work_handler);
	/*Mask all interrupts by default*/
	pwr_mgr_mask_intr(PWRMGR_INTR_ALL,true);

	ret = request_irq(info->pwrmgr_intr,pwr_mgr_irq_handler,
                                 IRQF_TRIGGER_RISING,
                                 "pwr_mgr",NULL);

#endif

	return ret;
}
EXPORT_SYMBOL(pwr_mgr_init);





#ifdef CONFIG_DEBUG_FS

static u32 bmdm_pwr_mgr_base;

__weak void pwr_mgr_mach_debug_fs_init(int type)
{
}

static int set_pm_mgr_dbg_bus(void *data, u64 val)
{
    u32 reg_val = 0;

    pwr_dbg("%s: val: %lld\n", __func__, val);
    pwr_mgr_mach_debug_fs_init(0);
    if(val > 0xA)
	return -EINVAL;
    reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PC_PIN_OVERRIDE_CONTROL_OFFSET));
    reg_val &= ~(0xF << 20);
    reg_val |= (val & 0x0F) << 20;
    pwr_dbg("reg_val to be written %08x\n", reg_val);
    writel(reg_val, PWR_MGR_REG_ADDR(PWRMGR_PC_PIN_OVERRIDE_CONTROL_OFFSET));

    reg_val = readl(PWR_MGR_REG_ADDR(PWRMGR_PC_PIN_OVERRIDE_CONTROL_OFFSET));
    pwr_dbg("PC_PIN_OVERRIDE_CONTROL Register: %08x\n", reg_val);

    return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(set_pm_dbg_bus_fops, NULL, set_pm_mgr_dbg_bus, "%llu\n");

static int set_bmdm_mgr_dbg_bus(void *data, u64 val)
{
    u32 reg_val = 0;
	u32 reg_addr = bmdm_pwr_mgr_base+BMDM_PWRMGR_DEBUG_AND_COUNTER_CONTROL_OFFSET;
    pwr_dbg("%s: val: %lld\n", __func__, val);
    pwr_mgr_mach_debug_fs_init(1);
    if(val > 0xA)
	return -EINVAL;
    reg_val = readl(reg_addr);
    reg_val &= ~(BMDM_PWRMGR_DEBUG_AND_COUNTER_CONTROL_DEBUG_BUS_SELECT_MASK);
    reg_val |= (val << BMDM_PWRMGR_DEBUG_AND_COUNTER_CONTROL_DEBUG_BUS_SELECT_SHIFT) &
					BMDM_PWRMGR_DEBUG_AND_COUNTER_CONTROL_DEBUG_BUS_SELECT_MASK;
    pwr_dbg("reg_val to be written %08x\n", reg_val);
    writel(reg_val, reg_addr);

    reg_val = readl(reg_addr);
    pwr_dbg("BMDM_PWRMGR_DEBUG_AND_COUNTER_CONTROL Register: %08x\n", reg_val);

    return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(set_bmdm_dbg_bus_fops, NULL, set_bmdm_mgr_dbg_bus, "%llu\n");


static int pwr_mgr_dbg_event_get_active(void *data, u64* val)
{
	u32 event_id = (u32)data;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS);

	*val = pwr_mgr_is_event_active(event_id);
	return 0;
}

static int pwr_mgr_dbg_event_set_active(void *data, u64 val)
{
	u32 event_id = (u32)data;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS);

	return pwr_mgr_event_set(event_id,(int)val);
}

DEFINE_SIMPLE_ATTRIBUTE(pwr_mgr_dbg_active_fops, pwr_mgr_dbg_event_get_active,
		pwr_mgr_dbg_event_set_active, "%llu\n");


static int pwr_mgr_dbg_event_get_trig(void *data, u64* val)
{
	u32 event_id = (u32)data;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS);

	*val = (u64)pwr_mgr_get_event_trg_type(event_id);
	return 0;
}

static int pwr_mgr_dbg_event_set_trig(void *data, u64 val)
{
	u32 event_id = (u32)data;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS);

	return pwr_mgr_event_trg_enable(event_id,(int)val);
}

DEFINE_SIMPLE_ATTRIBUTE(pwr_mgr_dbg_trig_fops, pwr_mgr_dbg_event_get_trig,
		pwr_mgr_dbg_event_set_trig, "%llu\n");

static int pwr_mgr_dbg_event_get_atl(void *data, u64* val)
{
	u32 event_id = ((u32)data & 0xFFFF0000) >> 16;
	u32 pid = (u32)data & 0xFFFF;
	int ret;
	struct pm_policy_cfg pm_policy_cfg;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS ||  pid >= pwr_mgr.info->num_pi);

	ret = pwr_mgr_event_get_pi_policy(event_id,pid,&pm_policy_cfg);

	if(!ret)
		*val = pm_policy_cfg.atl;
	return ret;
}
static int pwr_mgr_dbg_event_set_atl(void *data, u64 val)
{
	u32 event_id = ((u32)data & 0xFFFF0000) >> 16;
	u32 pid = (u32)data & 0xFFFF;
	int ret;
	struct pm_policy_cfg pm_policy_cfg;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS ||  pid >= pwr_mgr.info->num_pi);

	ret = pwr_mgr_event_get_pi_policy(event_id,pid,&pm_policy_cfg);
	if(!ret)
	{
		pm_policy_cfg.atl = (u32)val;
		ret = pwr_mgr_event_set_pi_policy(event_id,pid,&pm_policy_cfg);
	}

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(pwr_mgr_dbg_atl_ops, pwr_mgr_dbg_event_get_atl,
		pwr_mgr_dbg_event_set_atl, "%llu\n");


static int pwr_mgr_dbg_event_get_ac(void *data, u64* val)
{
	u32 event_id = ((u32)data & 0xFFFF0000) >> 16;
	u32 pid = (u32)data & 0xFFFF;
	int ret;
	struct pm_policy_cfg pm_policy_cfg;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS ||  pid >= pwr_mgr.info->num_pi);

	ret = pwr_mgr_event_get_pi_policy(event_id,pid,&pm_policy_cfg);

	if(!ret)
		*val = pm_policy_cfg.ac;
	return ret;
}
static int pwr_mgr_dbg_event_set_ac(void *data, u64 val)
{
	u32 event_id = ((u32)data & 0xFFFF0000) >> 16;
	u32 pid = (u32)data & 0xFFFF;
	int ret;
	struct pm_policy_cfg pm_policy_cfg;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS ||  pid >= pwr_mgr.info->num_pi);

	ret = pwr_mgr_event_get_pi_policy(event_id,pid,&pm_policy_cfg);
	if(!ret)
	{
		pm_policy_cfg.ac = (u32)val;
		ret = pwr_mgr_event_set_pi_policy(event_id,pid,&pm_policy_cfg);
	}

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(pwr_mgr_dbg_ac_ops, pwr_mgr_dbg_event_get_ac,
		pwr_mgr_dbg_event_set_ac, "%llu\n");


static int pwr_mgr_dbg_event_get_policy(void *data, u64* val)
{
	u32 event_id = ((u32)data & 0xFFFF0000) >> 16;
	u32 pid = (u32)data & 0xFFFF;
	int ret;
	struct pm_policy_cfg pm_policy_cfg;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS ||  pid >= pwr_mgr.info->num_pi);

	ret = pwr_mgr_event_get_pi_policy(event_id,pid,&pm_policy_cfg);

	if(!ret)
		*val = pm_policy_cfg.policy;
	return ret;
}
static int pwr_mgr_dbg_event_set_policy(void *data, u64 val)
{
	u32 event_id = ((u32)data & 0xFFFF0000) >> 16;
	u32 pid = (u32)data & 0xFFFF;
	int ret;
	struct pm_policy_cfg pm_policy_cfg;
	BUG_ON(event_id >= PWR_MGR_NUM_EVENTS ||  pid >= pwr_mgr.info->num_pi);

	ret = pwr_mgr_event_get_pi_policy(event_id,pid,&pm_policy_cfg);
	if(!ret)
	{
		pm_policy_cfg.policy = (u32)val;
		ret = pwr_mgr_event_set_pi_policy(event_id,pid,&pm_policy_cfg);
	}

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(pwr_mgr_dbg_policy_ops, pwr_mgr_dbg_event_get_policy,
		pwr_mgr_dbg_event_set_policy, "%llu\n");

#if defined(CONFIG_KONA_PWRMGR_REV2)

static int pwr_mgr_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t pwr_mgr_i2c_req(struct file *file, char const __user *buf,
					size_t count, loff_t *offset)
{
	u32 len = 0;
	u32 reg_addr = 0xFFFF;
	u32 slv_addr = 0xFFFF;
	u32 reg_val  = 0xFFFF;

	char input_str[100];

	if (count > 100)
		len = 100;
	else
		len = count;

	if (copy_from_user(input_str, buf, len))
		return -EFAULT;
    sscanf(input_str, "%x%x%x", &slv_addr, &reg_addr, &reg_val);
	if(reg_addr == 0xFFFF || slv_addr == 0xFFFF)
	{
		pr_info("invalid param\n");
		return count;
	}
	if(reg_val == 0xFFFF)
	{
		u8 val = 0xFF;
		pwr_mgr_pmu_reg_read((u8)reg_addr,(u8)slv_addr, &val);
		pr_info("[%x] = %x\n",reg_addr,val);
	}
	else
	{
		pwr_mgr_pmu_reg_write((u8)reg_addr,(u8)slv_addr,(u8)reg_val);
	}
	return count;
}

static struct file_operations i2c_sw_seq_ops =
{
	.open = pwr_mgr_debugfs_open,
	.write = pwr_mgr_i2c_req,
};
#endif

struct dentry *dent_pwr_root_dir = NULL;
int __init pwr_mgr_debug_init(u32 bmdm_pwr_base)
{
	struct dentry *dent_event_tbl;
	struct dentry *dent_pi;
	struct dentry *dent_event;
	struct pi* pi;
	int event;
	int i;

	if(unlikely(!pwr_mgr.info))
	{
		pwr_dbg("%s:ERROR - pwr mgr not initialized\n",__func__);
		return -EPERM;
	}

	dent_pwr_root_dir = debugfs_create_dir("power_mgr", 0);
	if (!dent_pwr_root_dir)
		return -ENOMEM;

	bmdm_pwr_mgr_base = bmdm_pwr_base;

    if(!debugfs_create_u32("debug", S_IWUSR|S_IRUSR, dent_pwr_root_dir, (int*)&pwr_debug))
		return -ENOMEM;

    if(!debugfs_create_u32("flags", S_IWUSR|S_IRUSR, dent_pwr_root_dir, (int*)&pwr_mgr.info->flags))
		return -ENOMEM;
    /* Debug Bus control via Debugfs */
    if(!debugfs_create_file("pm_debug_bus", S_IWUSR|S_IRUSR, dent_pwr_root_dir, NULL, &set_pm_dbg_bus_fops))
		return -ENOMEM;
	if(!debugfs_create_file("bmdm_debug_bus", S_IWUSR|S_IRUSR, dent_pwr_root_dir, NULL, &set_bmdm_dbg_bus_fops))
		return -ENOMEM;
#if defined(CONFIG_KONA_PWRMGR_REV2)
	if(!debugfs_create_file("i2c_sw_seq", S_IWUSR|S_IRUSR, dent_pwr_root_dir, NULL, &i2c_sw_seq_ops))
		return -ENOMEM;
#endif
	dent_event_tbl = debugfs_create_dir("event_table", dent_pwr_root_dir);
    if(!dent_event_tbl)
		return -ENOMEM;
	for(event = 0; event < PWR_MGR_NUM_EVENTS;event++)
	{
		dent_event = debugfs_create_dir(PWRMGR_EVENT_ID_TO_STR(event), dent_event_tbl);
		if(!dent_event)
			return -ENOMEM;
		if(!debugfs_create_file("active", S_IWUSR|S_IRUSR, dent_event,(void*)event,&pwr_mgr_dbg_active_fops))
			return -ENOMEM;

		if(!debugfs_create_file("trig_type", S_IWUSR|S_IRUSR, dent_event,(void*)event,&pwr_mgr_dbg_trig_fops))
			return -ENOMEM;

		for(i =0; i < pwr_mgr.info->num_pi; i++)
		{
			pi = pi_mgr_get(i);
			BUG_ON(pi == NULL);
			dent_pi = debugfs_create_dir(pi_get_name(pi), dent_event);
			if(!dent_pi)
				return -ENOMEM;

			if(!debugfs_create_file("policy", S_IWUSR|S_IRUSR, dent_pi,(void*)(((u32)event << 16) | (u16)i),
							&pwr_mgr_dbg_policy_ops))
			return -ENOMEM;

			if(!debugfs_create_file("ac", S_IWUSR|S_IRUSR, dent_pi,(void*)(((u32)event << 16) | (u16)i),
				&pwr_mgr_dbg_ac_ops))
			return -ENOMEM;

			if(!debugfs_create_file("atl", S_IWUSR|S_IRUSR, dent_pi,(void*)(((u32)event << 16) | (u16)i),
				&pwr_mgr_dbg_atl_ops))
				return -ENOMEM;
		}

	}



    return 0;
}

#endif /*  CONFIG_DEBUG_FS  */


