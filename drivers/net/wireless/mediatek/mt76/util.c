// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#include <linux/module.h>
#include "mt76.h"

bool __mt76_poll(struct mt76_dev *dev, u32 offset, u32 mask, u32 val,
		 int timeout)
{
	u32 cur;

	timeout /= 10;
	do {
		cur = __mt76_rr(dev, offset) & mask;
		if (cur == val)
			return true;

		udelay(10);
	} while (timeout-- > 0);

	return false;
}
EXPORT_SYMBOL_GPL(__mt76_poll);

bool ____mt76_poll_msec(struct mt76_dev *dev, u32 offset, u32 mask, u32 val,
			int timeout, int tick)
{
	u32 cur;

	timeout /= tick;
	do {
		cur = __mt76_rr(dev, offset) & mask;
		if (cur == val)
			return true;

		usleep_range(1000 * tick, 2000 * tick);
	} while (timeout-- > 0);

	return false;
}
EXPORT_SYMBOL_GPL(____mt76_poll_msec);

int mt76_wcid_alloc(u32 *mask, int size)
{
	int i, idx = 0, cur;

	for (i = 0; i < DIV_ROUND_UP(size, 32); i++) {
		idx = ffs(~mask[i]);
		if (!idx)
			continue;

		idx--;
		cur = i * 32 + idx;
		if (cur >= size)
			break;

		mask[i] |= BIT(idx);
		return cur;
	}

	return -1;
}
EXPORT_SYMBOL_GPL(mt76_wcid_alloc);

int mt76_get_min_avg_rssi(struct mt76_dev *dev, u8 phy_idx)
{
	struct mt76_wcid *wcid;
	int i, j, min_rssi = 0;
	s8 cur_rssi;

	local_bh_disable();
	rcu_read_lock();

	for (i = 0; i < ARRAY_SIZE(dev->wcid_mask); i++) {
		u32 mask = dev->wcid_mask[i];

		if (!mask)
			continue;

		for (j = i * 32; mask; j++, mask >>= 1) {
			if (!(mask & 1))
				continue;

			wcid = __mt76_wcid_ptr(dev, j);
			if (!wcid || wcid->phy_idx != phy_idx)
				continue;

			spin_lock(&dev->rx_lock);
			if (wcid->inactive_count++ < 5)
				cur_rssi = -ewma_signal_read(&wcid->rssi);
			else
				cur_rssi = 0;
			spin_unlock(&dev->rx_lock);

			if (cur_rssi < min_rssi)
				min_rssi = cur_rssi;
		}
	}

	rcu_read_unlock();
	local_bh_enable();

	return min_rssi;
}
EXPORT_SYMBOL_GPL(mt76_get_min_avg_rssi);

int __mt76_worker_fn(void *ptr)
{
	struct mt76_worker *w = ptr;

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (kthread_should_park()) {
			kthread_parkme();
			continue;
		}

		if (!test_and_clear_bit(MT76_WORKER_SCHEDULED, &w->state)) {
			schedule();
			continue;
		}

		set_bit(MT76_WORKER_RUNNING, &w->state);
		set_current_state(TASK_RUNNING);
		w->fn(w);
		cond_resched();
		clear_bit(MT76_WORKER_RUNNING, &w->state);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(__mt76_worker_fn);

void mt76_copy_rate_overrides(struct mt76_testmode_data *dest, struct mt76_testmode_data *src)
{
	dest->tx_power[0]   = src->tx_power[0];
	dest->tx_rate_sgi   = src->tx_rate_sgi;
	dest->tx_rate_idx   = src->tx_rate_idx;
	dest->tx_rate_nss   = src->tx_rate_nss;
	dest->tx_rate_mode  = src->tx_rate_mode;
	dest->tx_xmit_count = src->tx_xmit_count;
	dest->tx_dynbw      = src->tx_dynbw;
	dest->tx_rate_ldpc  = src->tx_rate_ldpc;
	dest->tx_rate_stbc  = src->tx_rate_stbc;
	dest->txbw          = src->txbw;
	dest->txo_active    = src->txo_active;
}
EXPORT_SYMBOL_GPL(mt76_copy_rate_overrides);

MODULE_DESCRIPTION("MediaTek MT76x helpers");
MODULE_LICENSE("Dual BSD/GPL");
