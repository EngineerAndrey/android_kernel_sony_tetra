//*********************************************************************
//
//	Copyright � 2000-2010 Broadcom Corporation
//
//	This program is the proprietary software of Broadcom Corporation
//	and/or its licensors, and may only be used, duplicated, modified
//	or distributed pursuant to the terms and conditions of a separate,
//	written license agreement executed between you and Broadcom (an
//	"Authorized License").  Except as set forth in an Authorized
//	License, Broadcom grants no license (express or implied), right
//	to use, or waiver of any kind with respect to the Software, and
//	Broadcom expressly reserves all rights in and to the Software and
//	all intellectual property rights therein.  IF YOU HAVE NO
//	AUTHORIZED LICENSE, THEN YOU HAVE NO RIGHT TO USE THIS SOFTWARE
//	IN ANY WAY, AND SHOULD IMMEDIATELY NOTIFY BROADCOM AND DISCONTINUE
//	ALL USE OF THE SOFTWARE.
//
//	Except as expressly set forth in the Authorized License,
//
//	1.	This program, including its structure, sequence and
//		organization, constitutes the valuable trade secrets
//		of Broadcom, and you shall use all reasonable efforts
//		to protect the confidentiality thereof, and to use
//		this information only in connection with your use
//		of Broadcom integrated circuit products.
//
//	2.	TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE
//		IS PROVIDED "AS IS" AND WITH ALL FAULTS AND BROADCOM
//		MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES,
//		EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE,
//		WITH RESPECT TO THE SOFTWARE.  BROADCOM SPECIFICALLY
//		DISCLAIMS ANY AND ALL IMPLIED WARRANTIES OF TITLE,
//		MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A
//		PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR
//		COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
//		CORRESPONDENCE TO DESCRIPTION. YOU ASSUME THE ENTIRE
//		RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE.
//
//	3.	TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT
//		SHALL BROADCOM OR ITS LICENSORS BE LIABLE FOR
//		(i) CONSEQUENTIAL, INCIDENTAL, SPECIAL, INDIRECT, OR
//		EXEMPLARY DAMAGES WHATSOEVER ARISING OUT OF OR IN ANY
//		WAY RELATING TO YOUR USE OF OR INABILITY TO USE THE
//		SOFTWARE EVEN IF BROADCOM HAS BEEN ADVISED OF THE
//		POSSIBILITY OF SUCH DAMAGES; OR (ii) ANY AMOUNT IN
//		EXCESS OF THE AMOUNT ACTUALLY PAID FOR THE SOFTWARE
//		ITSELF OR U.S. $1, WHICHEVER IS GREATER. THESE
//		LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE
//		OF ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.
//
//***************************************************************************
/**
*
*   @file   osdw_caph_drv.c
*
*   @brief  This file initializes the CAPH interrupt and defines the ISR
*
****************************************************************************/

#include "mobcom_types.h"
#include "chip_version.h"

#include <linux/sched.h>
#include <linux/interrupt.h>
#include "consts.h"
#include "msconsts.h"
#include "log.h"
#ifdef CONFIG_AUDIO_BUILD
#include "brcm_rdb_sysmap.h"
#include "nandsdram_memmap.h"
#include "osdw_caph_drv.h"
#endif
#include "auddrv_def.h"
#include "assert.h"
#include "chal_caph_intc.h"
#include "chip_irq.h"
#include "csl_caph_dma.h"
#include "drv_caph.h"
#include "irqs.h"

//****************************************************************************
// local variable definitions
//****************************************************************************
static struct work_struct audio_play;

//******************************************************************************
// local function declarations
//******************************************************************************

static void worker_audio_playback(struct work_struct *work);
static irqreturn_t caph_audio_isr(int irq, void *dev_id);

//******************************************************************************
//
// Function Name:	CAPHIRQ_Init
//
// Description: Initialize CAPH IRQ driver
//
// Notes:
//
//******************************************************************************

void CAPHIRQ_Init( void )
{
    int rc;
    Log_DebugPrintf(LOGID_AUDIO, " CAPHIRQ_Init:  \n");

    INIT_WORK(&audio_play,worker_audio_playback);
    //Plug in the ISR
    rc = request_irq(CAPH_NORM_IRQ, caph_audio_isr, IRQF_DISABLED,
			 "caph-interrupt", NULL);

    if (rc < 0) {
	Log_DebugPrintf(LOGID_AUDIO,"CAPHIRQ_INIT: %s failed to attach interrupt, rc = %d\n",
		       __FUNCTION__, rc);
		return;
    }
}


//******************************************************************************
//
// Function Name:	caph_audio_isr
//
// Description:		This function is the Low Level ISR for the CAPH interrupt.
//					It simply schedules the worker thread.
//
// Notes:
//
//******************************************************************************
static irqreturn_t caph_audio_isr(int irq, void *dev_id)
{
	disable_irq_nosync(BCM_INT_ID_CAPH);
	schedule_work(&audio_play);
	return IRQ_HANDLED;
}

//******************************************************************************
//
// Function Name:	worker_audio_playback
//
// Description:		This function is the CAPH interrupt service routine.
//
// Notes:
//
//******************************************************************************
static void worker_audio_playback(struct work_struct *work)
{
    	csl_caph_dma_process_interrupt();
	enable_irq(BCM_INT_ID_CAPH);
}
