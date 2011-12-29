/******************************************************************************/
/* (c) 2011 Broadcom Corporation                                              */
/*                                                                            */
/* Unless you and Broadcom execute a separate written software license        */
/* agreement governing use of this software, this software is licensed to you */
/* under the terms of the GNU General Public License version 2, available at  */
/* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").                    */
/*                                                                            */
/******************************************************************************/

#ifndef _KONA_PM_DBG_H_
#define _KONA_PM_DBG_H_

/* Types of snapshot parms */
enum {
	SNAPSHOT_SIMPLE,
	SNAPSHOT_CLK,
	SNAPSHOT_AHB_REG,
};

struct snapshot {
	void *data;	/* data for snapshot handler        */
	u32 type;	/* snapshot type                    */
	u32 reg;	/* register to be read              */
	u32 mask;	/* mask for data of interest        */
	u32 good;	/* expected value for sleep entry   */
	char *name;
	u32 curr;	/* actual value after applying mask */

	/* Fields used internally */
	u32 (*handler)(struct snapshot *);
};

/* Helpers for constructing the snapshot table */
#define SIMPLE_PARM(_reg, _good, _mask)		\
	{					\
		.reg = _reg,			\
		.good = _good,			\
		.mask = _mask,			\
		.type = SNAPSHOT_SIMPLE,	\
		.data = NULL,			\
		.name = #_reg,			\
	}

#define CLK_PARM(clk)				\
	{					\
		.data = (void *)clk,		\
		.type = SNAPSHOT_CLK,		\
		.mask = 0,			\
		.good = 0,			\
		.name = clk,			\
	}

#define AHB_REG_PARM(_reg, _good, _mask, _clk)	\
	{					\
		.reg = _reg,			\
		.good = _good,			\
		.mask = _mask,			\
		.type = SNAPSHOT_AHB_REG,	\
		.data = _clk,			\
		.name = #_reg,			\
	}

extern void snapshot_get(void);
extern void snapshot_show(void);
extern void snapshot_table_register(struct snapshot *table, size_t len);

/* Callbacks into machine code for instrumentation */
extern void instrument_idle_entry(void);
extern void instrument_idle_exit(void);

#endif /* _KONA_PM_DBG_H_ */

