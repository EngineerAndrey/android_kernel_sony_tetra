/*******************************************************************************
* Copyright 2011 Broadcom Corporation.  All rights reserved.
*
*	@file	drivers/spi/spi_sspi_kona.c
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

/*
 * Broadcom KONA SSPI based SPI master controller
 *
 * TODO:(Limitations in the current implementation)
 * 1. Only Tx0 and Rx0 are used
 * 2. Only Task0 and Sequence0 and Sequence1 of SSPI is used
 * 3. Full Duplex not supported
 * 4. Jumbo SPI can be handled differently according to SSPI spec
 * 5. CLK_RATE < 12MHz leaves CS asserted(need to check this?)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <plat/chal/chal_types.h>
#include <plat/chal/chal_sspi.h>
#include <plat/spi_kona.h>

#define SSPI_MAX_TASK_LOOP	1023
#define SSPI_TASK_TIME_OUT	500000
#define SSPI_FIFO_SIZE		128

struct spi_kona_config {
	uint32_t speed_hz;
	uint32_t bpw;
	uint32_t mode;
	int cs;
};

#define	CS_ACTIVE	1	/* normally nCS, active low */
#define	CS_INACTIVE	0

struct spi_kona_data {
	struct spi_master *master;	/* SPI framework hookup */

	CHAL_HANDLE chandle;	/* SSPI CHAL Handle */
	void __iomem *base;	/* SPI virtual base address */
	struct clk *ssp_clk;	/* SSPI bus clock */
	struct clk *ssp_apb_clk;	/* SSPI APB access clock */
	unsigned long spi_clk;	/* SPI controller clock speed */

	struct workqueue_struct *workqueue;	/* Driver message queue */
	struct work_struct work;	/* Message work */
	struct completion xfer_done;	/* Used to signal completion of xfer */

	spinlock_t lock;
	struct list_head queue;
	u8 busy;
	u8 use_dma;
	u8 flags;		/* extra spi->mode support */
	int irq;
	int enable_dma;

	/* Current Transfer details */
	int32_t count;
	void (*tx) (struct spi_kona_data *);
	void (*rx) (struct spi_kona_data *);
	void *rx_buf;
	const void *tx_buf;
	int32_t rxpend;	/* No. of frames pending from Rx FIFO
			   corresponding to Tx done */
	uint8_t bpw;
};

#define SPI_KONA_BUF_RX(type, type2)					\
static void spi_kona_buf_rx_##type(struct spi_kona_data *d)		\
{									\
	type val = read##type2(d->base +				\
		chal_sspi_rx0_get_dma_port_addr_offset());		\
									\
	if (d->rx_buf) {						\
		*(type *)d->rx_buf = val;				\
		d->rx_buf += sizeof(type);				\
	}								\
}

#define SPI_KONA_BUF_TX(type, type2)					\
static void spi_kona_buf_tx_##type(struct spi_kona_data *d)		\
{									\
	type val = 0;							\
									\
	if (d->tx_buf) {						\
		val = *(type *)d->tx_buf;				\
		d->tx_buf += sizeof(type);				\
		d->count -= sizeof(type);				\
	}								\
									\
	write##type2(val, d->base +					\
		chal_sspi_tx0_get_dma_port_addr_offset());		\
}

SPI_KONA_BUF_RX(u8, b)
SPI_KONA_BUF_TX(u8, b)
SPI_KONA_BUF_RX(u16, w)
SPI_KONA_BUF_TX(u16, w)
SPI_KONA_BUF_RX(u32, l)
SPI_KONA_BUF_TX(u32, l)

static void spi_kona_tx_data(struct spi_kona_data *spi_kona)
{
	while (spi_kona->rxpend < (SSPI_FIFO_SIZE / (spi_kona->bpw / 8))) {
		if (!spi_kona->count)
			break;
		spi_kona->tx(spi_kona);
		spi_kona->rxpend++;
	}
}
/*
 * The data flow is designed with the following conditions:
 *
 * 1) Every TX is gauranteed to follow by an RX
 * 2) An RX cannot start without a TX from SPI Master
 *
 * The driver assumes half duplex communication.
 */
static irqreturn_t spi_kona_isr(int irq, void *dev_id)
{
	struct spi_kona_data *spi_kona = dev_id;
	uint16_t fifo_level = 0;
	uint32_t status = 0, dstat = 0;

	chal_sspi_get_intr_status(spi_kona->chandle, &status, &dstat);

	chal_sspi_get_fifo_level(spi_kona->chandle, SSPI_FIFO_ID_RX0,
							&fifo_level);
	while (fifo_level) {
		spi_kona->rx(spi_kona);
		spi_kona->rxpend--;
		chal_sspi_get_fifo_level(spi_kona->chandle,
				SSPI_FIFO_ID_RX0, &fifo_level);
	}

	if (spi_kona->count > 0) {
		spi_kona_tx_data(spi_kona);

		return IRQ_HANDLED;
	}

	if (spi_kona->rxpend) {
		/* No data left to Tx, but still waiting for rx data */
		/* No need to re-enable Rx interrupt */
		return IRQ_HANDLED;
	}

	/* Disable all Interrupt */
	chal_sspi_enable_intr(spi_kona->chandle, 0);

	complete(&spi_kona->xfer_done);

	return IRQ_HANDLED;
}

static int spi_kona_config_clk(struct spi_kona_data *spi_kona,
					uint32_t clk_rate)
{
	CHAL_HANDLE chandle = spi_kona->chandle;
	uint32_t clk_src = 1000000, clk_pdiv = 0;

	if (clk_rate < (12000))
		return -EINVAL;
	clk_src *= clk_rate % (12000) ? 52 : 48;
	clk_disable(spi_kona->ssp_clk);
	do {
		clk_set_rate(spi_kona->ssp_clk, clk_src);
		spi_kona->spi_clk = clk_get_rate(spi_kona->ssp_clk);
		clk_pdiv = (spi_kona->spi_clk / clk_rate) - 1;
		clk_src >>= 1;
	} while (clk_pdiv > 16);
	chal_sspi_set_clk_src_select(chandle, SSPI_CLK_SRC_INTCLK);
	chal_sspi_set_clk_divider(chandle, SSPI_CLK_DIVIDER0, clk_pdiv);
	chal_sspi_set_clk_divider(chandle, SSPI_CLK_REF_DIVIDER, clk_pdiv);
	clk_enable(spi_kona->ssp_clk);
	return 0;
}

static void spi_kona_configure(struct spi_kona_data *spi_kona,
			       struct spi_kona_config *config)
{
	CHAL_HANDLE chandle = spi_kona->chandle;
	CHAL_SSPI_FIFO_DATA_PACK_t bpw = SSPI_FIFO_DATA_PACK_NONE;
	uint32_t frame_mask = 1;

	if (spi_kona->bpw == 8)
		bpw = SSPI_FIFO_DATA_PACK_8BIT;
	else if (spi_kona->bpw == 16)
		bpw = SSPI_FIFO_DATA_PACK_16BIT;

	/* Set FIFO data size */
	chal_sspi_set_fifo_pack(chandle, SSPI_FIFO_ID_TX0, bpw);
	chal_sspi_set_fifo_pack(chandle, SSPI_FIFO_ID_RX0, bpw);

	/* Configure the clock speed */
	spi_kona_config_clk(spi_kona, config->speed_hz);

	/* Set frame data size */
	chal_sspi_set_frame(chandle, &frame_mask, config->mode & SPI_MODE_3,
			    config->bpw, 0);
}

static int spi_kona_setupxfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct spi_kona_data *spi_kona = spi_master_get_devdata(spi->master);
	struct spi_kona_config config;

	config.bpw = t ? t->bits_per_word : spi->bits_per_word;
	config.speed_hz = t ? t->speed_hz : spi->max_speed_hz;
	config.mode = spi->mode;
	config.cs = spi->chip_select;

	if (!config.speed_hz)
		config.speed_hz = spi->max_speed_hz;
	if (!config.bpw)
		config.bpw = spi->bits_per_word;

	/* Initialize the functions for transfer */
	if (config.bpw <= 8) {
		config.bpw = spi_kona->bpw = 8;
		spi_kona->rx = spi_kona_buf_rx_u8;
		spi_kona->tx = spi_kona_buf_tx_u8;
	} else if (config.bpw <= 16) {
		config.bpw = spi_kona->bpw = 16;
		spi_kona->rx = spi_kona_buf_rx_u16;
		spi_kona->tx = spi_kona_buf_tx_u16;
	} else if (config.bpw <= 32) {
		config.bpw = spi_kona->bpw = 32;
		spi_kona->rx = spi_kona_buf_rx_u32;
		spi_kona->tx = spi_kona_buf_tx_u32;
	} else
		BUG();

	spi_kona_configure(spi_kona, &config);

	return 0;
}

/* TODO Need to find a better way to stop sequence in continuous mode */
static int spi_kona_end_task(struct spi_device *spi,
				struct spi_transfer *transfer)
{
	struct spi_kona_data *spi_kona = spi_master_get_devdata(spi->master);
	CHAL_HANDLE chandle = spi_kona->chandle;
	chal_sspi_seq_conf_t seq_conf;
	chal_sspi_task_conf_t task_conf;
	uint32_t timeout = SSPI_TASK_TIME_OUT;

	/* task_conf struct initialization */
	memset(&task_conf, 0, sizeof(task_conf));
	/* disable scheduler operation */
	if (chal_sspi_enable_scheduler(chandle, 0))
		return -EIO;

	/* task_conf struct configuration */
	task_conf.chan_sel = SSPI_CHAN_SEL_CHAN0;
	task_conf.cs_sel = SSPI_CS_SEL_CS0;
	task_conf.tx_sel = SSPI_TX_SEL_TX0;
	task_conf.rx_sel = (spi->mode & SPI_LOOP) ? SSPI_RX_SEL_COPY_TX0
							: SSPI_RX_SEL_RX0;
	task_conf.div_sel = SSPI_CLK_DIVIDER0;
	task_conf.seq_ptr = 0;

	task_conf.loop_cnt = 0;
	task_conf.continuous = 0;
	task_conf.init_cond_mask = (transfer->tx_buf) ?
			SSPI_TASK_INIT_COND_THRESHOLD_TX0 : 0;
	task_conf.wait_before_start = 0;

	if (chal_sspi_set_task(chandle, 0, spi->mode & SPI_MODE_3, &task_conf))
		return -EIO;

	seq_conf.tx_enable = FALSE;
	seq_conf.rx_enable = FALSE;
	seq_conf.cs_activate = 0;
	seq_conf.cs_deactivate = 1;
	seq_conf.pattern_mode = 0;
	seq_conf.rep_cnt = 0;
	seq_conf.opcode = SSPI_SEQ_OPCODE_STOP;
	seq_conf.rx_fifo_sel = 0;
	seq_conf.tx_fifo_sel = 0;
	seq_conf.frm_sel = 0;
	seq_conf.rx_sidetone_on = 0;
	seq_conf.tx_sidetone_on = 0;
	seq_conf.next_pc = 0;
	if (chal_sspi_set_sequence(chandle, 0, spi->mode & SPI_MODE_3,
				   &seq_conf))
		return -EIO;

	/* enable scheduler operation */
	if (chal_sspi_enable_scheduler(chandle, 1))
		return -EIO;

	while (timeout--) {
		int status;
		chal_sspi_get_intr_status(chandle, &status, NULL);
		if (status & SSPIL_INTERRUPT_STATUS_SCHEDULER_STATUS_MASK) {
			chal_sspi_clear_intr(chandle, status, 0);
			return 0;
		}
		udelay(1);
	}
	return -EIO;
}

static int spi_kona_config_task(struct spi_device *spi,
				struct spi_transfer *transfer)
{
	struct spi_kona_data *spi_kona = spi_master_get_devdata(spi->master);
	CHAL_HANDLE chandle = spi_kona->chandle;
	chal_sspi_seq_conf_t seq_conf;
	chal_sspi_task_conf_t task_conf;
	uint32_t timeout = SSPI_TASK_TIME_OUT;

	/* task_conf struct initialization */
	memset(&task_conf, 0, sizeof(task_conf));
	/* disable scheduler operation */
	if (chal_sspi_enable_scheduler(chandle, 0))
		return -EIO;

	/* task_conf struct configuration */
	task_conf.chan_sel = SSPI_CHAN_SEL_CHAN0;
	task_conf.cs_sel = SSPI_CS_SEL_CS0;
	task_conf.tx_sel = SSPI_TX_SEL_TX0;
	task_conf.rx_sel = (spi->mode & SPI_LOOP) ? SSPI_RX_SEL_COPY_TX0
							: SSPI_RX_SEL_RX0;
	task_conf.div_sel = SSPI_CLK_DIVIDER0;
	task_conf.seq_ptr = 0;

	if (spi_kona->bpw == 8)
		task_conf.loop_cnt = transfer->len - 1;
	else if (spi_kona->bpw == 16)
		task_conf.loop_cnt = (transfer->len >> 1) - 1;
	else		/* spi_kona->bpw == 32 */
		task_conf.loop_cnt = (transfer->len >> 2) - 1;
	if (task_conf.loop_cnt > SSPI_MAX_TASK_LOOP) {
		/* Care needs to be taken to stop this sequence */
		task_conf.loop_cnt = 0;
		task_conf.continuous = 1;
	} else {
		task_conf.continuous = 0;
	}

	task_conf.init_cond_mask = (transfer->tx_buf) ?
			SSPI_TASK_INIT_COND_THRESHOLD_TX0 : 0;
	task_conf.wait_before_start = 1;

	if (chal_sspi_set_task(chandle, 0, spi->mode & SPI_MODE_3, &task_conf))
		return -EIO;

	/* configure sequence */
	seq_conf.tx_enable = (transfer->tx_buf) ? TRUE : FALSE;
	seq_conf.rx_enable = (transfer->rx_buf) ? TRUE : FALSE;
	seq_conf.cs_activate = 1;
	seq_conf.cs_deactivate = 0;
	seq_conf.pattern_mode = 0;
	seq_conf.rep_cnt = 0;
	seq_conf.opcode = SSPI_SEQ_OPCODE_COND_JUMP;
	seq_conf.rx_fifo_sel = SSPI_FIFO_ID_RX0;
	seq_conf.tx_fifo_sel = 0;	/* SSPI_FIFO_ID_TX0 */
	seq_conf.frm_sel = 0;
	seq_conf.rx_sidetone_on = 0;
	seq_conf.tx_sidetone_on = 0;
	seq_conf.next_pc = 0;
	if (chal_sspi_set_sequence(chandle, 0, spi->mode & SPI_MODE_3,
				   &seq_conf))
		return -EIO;

	seq_conf.tx_enable = FALSE;
	seq_conf.rx_enable = FALSE;
	seq_conf.cs_activate = 0;
	seq_conf.cs_deactivate = 1;
	seq_conf.pattern_mode = 0;
	seq_conf.rep_cnt = 0;
	seq_conf.opcode = SSPI_SEQ_OPCODE_STOP;
	seq_conf.rx_fifo_sel = 0;
	seq_conf.tx_fifo_sel = 0;
	seq_conf.frm_sel = 0;
	seq_conf.rx_sidetone_on = 0;
	seq_conf.tx_sidetone_on = 0;
	seq_conf.next_pc = 0;
	if (chal_sspi_set_sequence(chandle, 1, spi->mode & SPI_MODE_3,
				   &seq_conf))
		return -EIO;

	/* enable scheduler operation */
	if (chal_sspi_enable_scheduler(chandle, 1))
		return -EIO;

	while (timeout--) {
		int status;
		chal_sspi_get_intr_status(chandle, &status, NULL);
		if (status & SSPIL_INTERRUPT_STATUS_SCHEDULER_STATUS_MASK) {
			chal_sspi_clear_intr(chandle, status, 0);
			return 0;
		}
		udelay(1);
	}
	return -EIO;
}

static int spi_kona_txrxfer_bufs(struct spi_device *spi,
				 struct spi_transfer *transfer)
{
	struct spi_kona_data *spi_kona = spi_master_get_devdata(spi->master);
	spi_kona->tx_buf = transfer->tx_buf;
	spi_kona->rx_buf = transfer->rx_buf;
	spi_kona->count = transfer->len;
	spi_kona->rxpend = 0;

	init_completion(&spi_kona->xfer_done);

	spi_kona_config_task(spi, transfer);

	spi_kona_tx_data(spi_kona);

	chal_sspi_enable_intr(spi_kona->chandle,
		SSPIL_INTERRUPT_ENABLE_PIO_TX_START_INTERRUPT_ENB_MASK
		| SSPIL_INTERRUPT_ENABLE_PIO_TX_STOP_INTERRUPT_ENB_MASK
		| SSPIL_INTERRUPT_ENABLE_PIO_RX_START_INTERRUPT_ENB_MASK
		| SSPIL_INTERRUPT_ENABLE_PIO_RX_STOP_INTERRUPT_ENB_MASK);

	wait_for_completion(&spi_kona->xfer_done);

	spi_kona_end_task(spi, transfer);

	/* Reset FIFO before configuring */
	chal_sspi_fifo_reset(spi_kona->chandle, SSPI_FIFO_ID_TX0);
	chal_sspi_fifo_reset(spi_kona->chandle, SSPI_FIFO_ID_RX0);
	chal_sspi_enable_scheduler(spi_kona->chandle, 0);

	return transfer->len;
}

static void spi_kona_chipselect(struct spi_device *spi, int is_active)
{
	/* TODO
	 * Need to do CS functionality here, can be platform specific
	 */
	return;
}

/*
 * This costs a task context per controller, running the queue by
 * performing each transfer in sequence.
 */
static void spi_kona_work(struct work_struct *work)
{
	struct spi_kona_data *spi_kona =
	    container_of(work, struct spi_kona_data, work);
	unsigned long flags;
	int do_setup = -1;

	spin_lock_irqsave(&spi_kona->lock, flags);
	spi_kona->busy = 1;
	while (!list_empty(&spi_kona->queue)) {
		struct spi_message *m;
		struct spi_device *spi;
		struct spi_transfer *t = NULL;
		unsigned cs_change;
		int status;

		m = container_of(spi_kona->queue.next, struct spi_message,
				 queue);
		list_del_init(&m->queue);
		spin_unlock_irqrestore(&spi_kona->lock, flags);

		spi = m->spi;
		cs_change = 1;
		status = 0;

		list_for_each_entry(t, &m->transfers, transfer_list) {

			/* override speed or wordsize? */
			if (t->speed_hz || t->bits_per_word)
				do_setup = 1;

			/* init (-1) or override (1) transfer params */
			if (do_setup != 0) {
				status = spi_kona_setupxfer(spi, t);
				if (status < 0)
					break;
			}

			if (cs_change)
				spi_kona_chipselect(spi, CS_ACTIVE);
			cs_change = t->cs_change;
			if (!t->tx_buf && !t->rx_buf && t->len) {
				status = -EINVAL;
				break;
			}

			if (t->len) {
				if (!m->is_dma_mapped)
					t->rx_dma = t->tx_dma = 0;
				status = spi_kona_txrxfer_bufs(spi, t);
			}
			if (status > 0)
				m->actual_length += status;
			if (status != t->len) {
				/* always report some kind of error */
				if (status >= 0)
					status = -EREMOTEIO;
				break;
			}
			status = 0;

			/* protocol tweaks before next transfer */
			if (t->delay_usecs)
				udelay(t->delay_usecs);

			if (!cs_change)
				continue;
			if (t->transfer_list.next == &m->transfers)
				break;

			/* sometimes a short mid-message deselect of the chip
			 * may be needed to terminate a mode or command
			 */
			spi_kona_chipselect(spi, CS_INACTIVE);
		}

		m->status = status;
		m->complete(m->context);

		/* restore speed and wordsize if it was overridden */
		if (do_setup == 1)
			spi_kona_setupxfer(spi, NULL);
		do_setup = 0;

		/* normally deactivate chipselect ... unless no error and
		 * cs_change has hinted that the next message will probably
		 * be for this chip too.
		 */
		if (!(status == 0 && cs_change))
			spi_kona_chipselect(spi, CS_INACTIVE);

		spin_lock_irqsave(&spi_kona->lock, flags);
	}
	spi_kona->busy = 0;
	spin_unlock_irqrestore(&spi_kona->lock, flags);
}

static int spi_kona_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct spi_kona_data *spi_kona;
	unsigned long flags;
	int status = 0;

	m->actual_length = 0;
	m->status = -EINPROGRESS;

	spi_kona = spi_master_get_devdata(spi->master);

	spin_lock_irqsave(&spi_kona->lock, flags);
	if (!spi->max_speed_hz)
		status = -ENETDOWN;
	else {
		list_add_tail(&m->queue, &spi_kona->queue);
		queue_work(spi_kona->workqueue, &spi_kona->work);
	}
	spin_unlock_irqrestore(&spi_kona->lock, flags);

	return status;
}

static int spi_kona_setup(struct spi_device *spi)
{
	dev_dbg(&spi->dev, "%s: mode %d, %u bpw, %d hz\n", __func__,
		spi->mode, spi->bits_per_word, spi->max_speed_hz);

	spi_kona_chipselect(spi, CS_INACTIVE);

	return 0;
}

static void spi_kona_cleanup(struct spi_device *spi)
{
	/* Any SPI device cleanup needs to be done here */
	return;
}

static int spi_kona_config_spi_hw(struct spi_kona_data *spi_kona)
{
	CHAL_HANDLE chandle;

	chandle = chal_sspi_init((uint32_t) spi_kona->base);
	if (!chandle) {
		pr_err("%s: invalid CHAL handler\n", __func__);
		return -ENXIO;
	}
	chal_sspi_set_type(chandle, SSPI_TYPE_LITE);
	/* Soft Reset SSPI */
	chal_sspi_soft_reset(chandle);
	/* Driver supports only Master Mode */
	chal_sspi_set_mode(chandle, SSPI_MODE_MASTER);
	/*
	 * Set SSPI IDLE State in Mode 0 SPI
	 * Currently only Mode 0 SPI supported by the driver
	 */
	chal_sspi_set_idle_state(chandle, SSPI_PROT_SPI_MODE0);

	/* Set SSPI FIFO Size: Rx0/Tx0 - Full, other Rx/Tx to zero */
	chal_sspi_set_fifo_size(chandle, SSPI_FIFO_ID_RX0, SSPI_FIFO_SIZE_FULL);
	chal_sspi_set_fifo_size(chandle, SSPI_FIFO_ID_RX1, SSPI_FIFO_SIZE_NONE);
	chal_sspi_set_fifo_size(chandle, SSPI_FIFO_ID_RX2, SSPI_FIFO_SIZE_NONE);
	chal_sspi_set_fifo_size(chandle, SSPI_FIFO_ID_RX3, SSPI_FIFO_SIZE_NONE);
	chal_sspi_set_fifo_size(chandle, SSPI_FIFO_ID_TX0, SSPI_FIFO_SIZE_FULL);
	chal_sspi_set_fifo_size(chandle, SSPI_FIFO_ID_TX1, SSPI_FIFO_SIZE_NONE);
	chal_sspi_set_fifo_size(chandle, SSPI_FIFO_ID_TX2, SSPI_FIFO_SIZE_NONE);
	chal_sspi_set_fifo_size(chandle, SSPI_FIFO_ID_TX3, SSPI_FIFO_SIZE_NONE);

	/* Set default FIFO threshold to 1 byte */
	chal_sspi_set_fifo_threshold(chandle, SSPI_FIFO_ID_TX0, 0x1);
	chal_sspi_set_fifo_threshold(chandle, SSPI_FIFO_ID_RX0, 0x1);

	chal_sspi_set_fifo_pio_threshhold(chandle, SSPI_FIFO_ID_TX0, 0x0,
							SSPI_FIFO_SIZE);
	chal_sspi_set_fifo_pio_threshhold(chandle, SSPI_FIFO_ID_RX0, 0x1,
							SSPI_FIFO_SIZE);
	chal_sspi_enable_fifo_pio_start_stop_intr(chandle, SSPI_FIFO_ID_TX0,
									1, 1);
	chal_sspi_enable_fifo_pio_start_stop_intr(chandle, SSPI_FIFO_ID_RX0,
									1, 1);
	chal_sspi_enable_intr(chandle, 0);
	chal_sspi_enable_error_intr(chandle,
				~SSPIL_INTERRUPT_ERROR_ENABLE_RESERVED_MASK);
	chal_sspi_enable(chandle, 1);

	spi_kona->chandle = chandle;

	return 0;
}

static int spi_kona_probe(struct platform_device *pdev)
{
	struct spi_kona_platform_data *platform_info;
	struct resource *res;
	struct spi_master *master;
	struct spi_kona_data *spi_kona = 0;
	uint8_t clk_name[32];
	int status = 0;

	platform_info = dev_get_platdata(&pdev->dev);
	if (!platform_info) {
		dev_err(&pdev->dev, "can't get the platform data\n");
		return -EINVAL;
	}

	/* Allocate master with space for spi_kona and null dma buffer */
	master = spi_alloc_master(&pdev->dev, sizeof(struct spi_kona_data));
	if (!master) {
		dev_err(&pdev->dev, "can not alloc spi_master\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, master);

	master->bus_num = pdev->id;
	master->num_chipselect = platform_info->cs_line;
	master->mode_bits = platform_info->mode;

	spi_kona = spi_master_get_devdata(master);
	spi_kona->master = spi_master_get(master);
	spi_kona->enable_dma = platform_info->enable_dma;

	master->setup = spi_kona_setup;
	master->cleanup = spi_kona_cleanup;
	master->transfer = spi_kona_transfer;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("%s: SPI: No resource for memory\n", __func__);
		status = -ENXIO;
		goto out_master_put;
	}

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		status = -EBUSY;
		goto out_master_put;
	}

	spi_kona->base = (void __iomem *)HW_IO_PHYS_TO_VIRT(res->start);
	/* spi_kona->base = ioremap(res->start, resource_size(res)); */
	if (!spi_kona->base) {
		status = -EINVAL;
		goto out_release_mem;
	}

	status = spi_kona_config_spi_hw(spi_kona);
	if (status) {
		pr_err("Error configuring SPI hardware\n");
		goto out_iounmap;
	}

	spi_kona->irq = platform_get_irq(pdev, 0);
	if (!spi_kona->irq) {
		pr_err("%s: No resource for IRQ\n", __func__);
		status = -ENXIO;
		goto out_iounmap;
	}

	status = request_irq(spi_kona->irq, spi_kona_isr, IRQF_SHARED,
			     "spi_irq", spi_kona);
	if (status) {
		pr_err("%s:Error registering spi irq %d %d\n",
		       __func__, status, spi_kona->irq);
		goto out_iounmap;
	}

	sprintf(clk_name, "ssp%d_clk", master->bus_num);
	spi_kona->ssp_clk = clk_get(NULL, clk_name);
	if (IS_ERR_OR_NULL(spi_kona->ssp_clk)) {
		dev_err(&pdev->dev, "unable to get %s clock\n", clk_name);
		status = PTR_ERR(spi_kona->ssp_clk);
		goto out_free_irq;
	}
	sprintf(clk_name, "ssp%d_apb_clk", master->bus_num);
	spi_kona->ssp_apb_clk = clk_get(NULL, clk_name);
	if (IS_ERR_OR_NULL(spi_kona->ssp_apb_clk)) {
		dev_err(&pdev->dev, "unable to get %s clock\n", clk_name);
		status = PTR_ERR(spi_kona->ssp_apb_clk);
		clk_put(spi_kona->ssp_clk);
		goto out_free_irq;
	}
	clk_enable(spi_kona->ssp_apb_clk);
	clk_enable(spi_kona->ssp_clk);

	INIT_WORK(&spi_kona->work, spi_kona_work);
	spin_lock_init(&spi_kona->lock);
	INIT_LIST_HEAD(&spi_kona->queue);

	/* this task is the only thing to touch the SPI bits */
	spi_kona->busy = 0;
	spi_kona->workqueue =
	    create_singlethread_workqueue(dev_name
					  (spi_kona->master->dev.parent));
	if (spi_kona->workqueue == NULL) {
		status = -EBUSY;
		goto out_clk_put;
	}

	/* Register with the SPI framework */
	status = spi_register_master(master);
	if (status != 0) {
		dev_err(&pdev->dev, "problem registering spi master\n");
		goto out_clk_put;
	}

	pr_info("%s: SSP %d done\n", __func__, master->bus_num);
	return status;

out_clk_put:
	clk_disable(spi_kona->ssp_clk);
	clk_disable(spi_kona->ssp_apb_clk);
	clk_put(spi_kona->ssp_clk);
	clk_put(spi_kona->ssp_apb_clk);
out_free_irq:
	free_irq(spi_kona->irq, spi_kona);
out_iounmap:
	/* iounmap(spi_kona->base); */
	chal_sspi_deinit(spi_kona->chandle);
out_release_mem:
	release_mem_region(res->start, resource_size(res));
out_master_put:
	spi_master_put(master);
	platform_set_drvdata(pdev, NULL);
	return status;
}

static int spi_kona_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct spi_kona_data *spi_kona = spi_master_get_devdata(master);
	int status = 0;

	if (!spi_kona)
		return 0;

	spi_unregister_master(master);
	WARN_ON(!list_empty(&spi_kona->queue));
	destroy_workqueue(spi_kona->workqueue);

	status = chal_sspi_deinit(spi_kona->chandle);
	if (status != CHAL_SSPI_STATUS_SUCCESS)
		status = -EBUSY;

	clk_disable(spi_kona->ssp_clk);
	clk_disable(spi_kona->ssp_apb_clk);
	clk_put(spi_kona->ssp_clk);
	clk_put(spi_kona->ssp_apb_clk);
	free_irq(spi_kona->irq, spi_kona);

	spi_master_put(master);

	release_mem_region(res->start, resource_size(res));

	platform_set_drvdata(pdev, NULL);

	return status;
}

static void spi_kona_shutdown(struct platform_device *pdev)
{
	int status = spi_kona_remove(pdev);

	if (status != 0)
		dev_err(&pdev->dev, "shutdown failed with %d\n", status);

}

#ifdef CONFIG_PM
static int spi_kona_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int spi_kona_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define spi_kona_suspend     NULL
#define spi_kona_resume      NULL
#endif

static struct platform_driver spi_kona_sspi_driver = {
	.driver = {
		   .name = "kona_sspi_spi",
		   .owner = THIS_MODULE,
		   },
	.probe = spi_kona_probe,
	.remove = spi_kona_remove,
	.shutdown = spi_kona_shutdown,
	.suspend = spi_kona_suspend,
	.resume = spi_kona_resume
};

static int __init kona_sspi_spi_init(void)
{
	return platform_driver_register(&spi_kona_sspi_driver);
}

module_init(kona_sspi_spi_init);

static void __exit kona_sspi_spi_exit(void)
{
	platform_driver_unregister(&spi_kona_sspi_driver);
}

module_exit(kona_sspi_spi_exit);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("KONA SSPI based SPI Contoller");
MODULE_LICENSE("GPL");
