
#ifndef __POWER_ISLAND_MGR_H__
#define __POWER_ISLAND_MGR_H__

#include <linux/plist.h>
#include <linux/notifier.h>
#include <linux/pm_qos_params.h>
#include <mach/pi_mgr.h>

#define PI_MGR_QOS_DEFAULT_VALUE 	0xFFFFFFFF
#define PI_OPP_UNSUPPORTED		0xFFFF

#define PI_MGR_ACTIVE_STATE_INX		0

#ifndef PI_MGR_MAX_STATE_ALLOWED
#define PI_MGR_MAX_STATE_ALLOWED 10
#endif

#if defined(DEBUG)
#define pi_dbg printk
#else
#define pi_dbg(format...)              \
	do {                            \
	    if (pi_debug)          	\
		printk(format); 	\
	} while(0)
#endif



struct pi_ops;
struct pi_mgr_qos_node;
struct pi_mgr_dfs_node;


enum
{
	POLICY_QOS = 	(1 << 0),
#ifdef CONFIG_CHANGE_POLICY_FOR_DFS
	POLICY_DFS = 	(1 << 1),
	POLICY_BOTH = 	(POLICY_QOS | POLICY_DFS),
#endif
};


enum
{
	PI_ENABLE_ON_INIT  		= (1 << 0),
	PI_ARM_CORE				= (1 << 1),
	PI_NO_QOS				= (1 << 2),
	PI_NO_DFS				= (1 << 3),
	PI_NO_STATE_CHANGE		= (1 << 5),
	UPDATE_PM_QOS			= (1 << 5),
};

struct pm_pi_info
{
	u32 policy_reg_offset;
	u32 ac_shift;
	u32 atl_shift;
	u32 pm_policy_shift;

	u32 fixed_vol_map_mask;
	u32 vi_to_vOx_map_mask;
	u32 wakeup_overide_mask;

	u32 counter_reg_offset;
	u32 rtn_clmp_dis_mask;
};

struct pi_state
{
	u32 id;
	u32 state_policy;
	u32 hw_wakeup_latency;
};

struct pi_opp
{
	u32	opp[PI_OPP_MAX];
};

struct pi
{
	char* name;
	u32 flags;
	char** ccu_id;
	u32 num_ccu_id;
	u32 id;
	u32 state_allowed;
	bool init;
	u32 usg_cnt;
	u32 opp_active;
	u32 qos_sw_event_id;
#ifdef CONFIG_CHANGE_POLICY_FOR_DFS
	u32 dfs_sw_event_id;
#endif /*CONFIG_CHANGE_POLICY_FOR_DFS*/
	struct pi_opp* pi_opp;
	u32 num_opp;
	struct pi_state* pi_state;
	u32 num_states;
	struct pm_pi_info pi_info;
	u32* dep_pi;
	u32 num_dep_pi;
	struct pm_qos_request_list* pm_qos;
	struct pi_ops* ops;
};

struct pi_ops
{
	int	(*init)(struct pi *pi);
	int	(*enable)(struct pi *pi, int enable);
	int (*change_notify)(struct pi *pi, int policy);
};


extern struct pi_ops gen_pi_ops;

struct pi_mgr_qos_node* pi_mgr_qos_add_request(char* client_name, u32 pi_id, u32 lat_value);
int pi_mgr_qos_request_update(struct pi_mgr_qos_node* node, u32 lat_value);
int pi_mgr_qos_request_remove(struct pi_mgr_qos_node* node);
int pi_mgr_qos_add_notifier(u32 pi_id, struct notifier_block *notifier);
int pi_mgr_qos_remove_notifier(u32 pi_id, struct notifier_block *notifier);
int pi_set_policy(const struct pi *pi, u32 policy,int type);

struct pi_mgr_dfs_node* pi_mgr_dfs_add_request(char* client_name, u32 pi_id, u32 opp);
int pi_mgr_dfs_request_update(struct pi_mgr_dfs_node* node, u32 opp);
int pi_mgr_dfs_request_remove(struct pi_mgr_dfs_node* node);
int pi_mgr_dfs_add_notifier(u32 pi_id, struct notifier_block *notifier);
int pi_mgr_dfs_remove_notifier(u32 pi_id, struct notifier_block *notifier);

int pi_state_allowed(int pi_id);
int pi_mgr_register(struct pi* pi);
struct pi* pi_mgr_get(int pi_id);
int pi_mgr_init(void);
u32 pi_get_active_qos(int pi_id);
u32 pi_get_active_opp(int pi_id);

#ifdef CONFIG_DEBUG_FS
int __init pi_debug_init(void);
int __init pi_debug_add_pi(struct pi *pi);
#endif /* CONFIG_DEBUG_FS */

#define pi_init(pi) if((pi)->ops && (pi)->ops->init) (pi)->ops->init(pi)
#define pi_is_enabled(pi)	(pi->usg_cnt != 0)
#define pi_enable(pi,en)  if((pi)->ops && (pi)->ops->enable) (pi)->ops->enable(pi,en)
#define pi_change_notify(pi,p) if((pi)->ops && (pi)->ops->change_notify) (pi)->ops->change_notify(pi,p)

#define pi_get_name(pi)	(pi)->name

#endif /*__POWER_ISLAND_MGR_H__*/
