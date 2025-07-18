// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/relay.h>
#include "mt7996.h"
#include "eeprom.h"
#include "mcu.h"
#include "mac.h"

#define FW_BIN_LOG_MAGIC	0x44d9c99a

/** global debugfs **/

struct hw_queue_map {
	const char *name;
	u8 index;
	u8 pid;
	u8 qid;
};

static int
mt7996_implicit_txbf_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;

	/* The existing connected stations shall reconnect to apply
	 * new implicit txbf configuration.
	 */
	dev->ibf = !!val;

	return mt7996_mcu_set_txbf(dev, BF_HW_EN_UPDATE);
}

static int
mt7996_implicit_txbf_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;

	*val = dev->ibf;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_implicit_txbf, mt7996_implicit_txbf_get,
			 mt7996_implicit_txbf_set, "%lld\n");

/* test knob of system error recovery */
static ssize_t
mt7996_sys_recovery_set(struct file *file, const char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7996_phy *phy = file->private_data;
	struct mt7996_dev *dev = phy->dev;
	bool band = phy->mt76->band_idx;
	char buf[16];
	int ret = 0;
	u16 val;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (kstrtou16(buf, 0, &val))
		return -EINVAL;

	switch (val) {
	/*
	 * 0: grab firmware current SER state.
	 * 1: trigger & enable system error L1 recovery.
	 * 2: trigger & enable system error L2 recovery.
	 * 3: trigger & enable system error L3 rx abort.
	 * 4: trigger & enable system error L3 tx abort
	 * 5: trigger & enable system error L3 tx disable.
	 * 6: trigger & enable system error L3 bf recovery.
	 * 7: trigger & enable system error L4 mdp recovery.
	 * 8: trigger & enable system error full recovery.
	 * 9: trigger firmware crash.
	 * 10: trigger grab wa firmware coredump.
	 * 11: trigger grab wm firmware coredump.
	 * 12: hw bit detect only.
	 */
	case UNI_CMD_SER_QUERY:
		ret = mt7996_mcu_set_ser(dev, UNI_CMD_SER_QUERY, 0, band);
		break;
	case UNI_CMD_SER_SET_RECOVER_L1:
	case UNI_CMD_SER_SET_RECOVER_L2:
	case UNI_CMD_SER_SET_RECOVER_L3_RX_ABORT:
	case UNI_CMD_SER_SET_RECOVER_L3_TX_ABORT:
	case UNI_CMD_SER_SET_RECOVER_L3_TX_DISABLE:
	case UNI_CMD_SER_SET_RECOVER_L3_BF:
	case UNI_CMD_SER_SET_RECOVER_L4_MDP:
		ret = mt7996_mcu_set_ser(dev, UNI_CMD_SER_SET, BIT(val), band);
		if (ret)
			return ret;

		ret = mt7996_mcu_set_ser(dev, UNI_CMD_SER_TRIGGER, val, band);
		break;

	/* enable full chip reset */
	case UNI_CMD_SER_SET_RECOVER_FULL:
		mt76_set(dev, MT_WFDMA0_MCU_HOST_INT_ENA, MT_MCU_CMD_WDT_MASK);
		dev->recovery.state |= MT_MCU_CMD_WM_WDT;
		mt7996_reset(dev);
		break;

	/* WARNING: trigger firmware crash */
	case UNI_CMD_SER_SET_SYSTEM_ASSERT:
		// trigger wm assert exception
		mt76_wr(dev, 0x89018108, 0x20);
		mt76_wr(dev, 0x89018118, 0x20);
		// trigger wa assert exception
		if (mt7996_has_wa(dev)) {
			mt76_wr(dev, 0x89098108, 0x20);
			mt76_wr(dev, 0x89098118, 0x20);
		}
		break;
	case UNI_CMD_SER_FW_COREDUMP_WA:
		if (mt7996_has_wa(dev))
			mt7996_coredump(dev, MT7996_COREDUMP_MANUAL_WA);
		break;
	case UNI_CMD_SER_FW_COREDUMP_WM:
		mt7996_coredump(dev, MT7996_COREDUMP_MANUAL_WM);
		break;
	case UNI_CMD_SER_SET_HW_BIT_DETECT_ONLY:
		ret = mt7996_mcu_set_ser(dev, UNI_CMD_SER_SET, BIT(0), band);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	return ret ? ret : count;
}

static ssize_t
mt7996_sys_recovery_get(struct file *file, char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7996_phy *phy = file->private_data;
	struct mt7996_dev *dev = phy->dev;
	char *buff;
	int desc = 0;
	ssize_t ret;
	static const size_t bufsz = 1536;

	buff = kmalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	/* HELP */
	desc += scnprintf(buff + desc, bufsz - desc,
			  "Please echo the correct value ...\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: grab firmware transient SER state\n",
			  UNI_CMD_SER_QUERY);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L1 recovery\n",
			  UNI_CMD_SER_SET_RECOVER_L1);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L2 recovery\n",
			  UNI_CMD_SER_SET_RECOVER_L2);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L3 rx abort\n",
			  UNI_CMD_SER_SET_RECOVER_L3_RX_ABORT);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L3 tx abort\n",
			  UNI_CMD_SER_SET_RECOVER_L3_TX_ABORT);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L3 tx disable\n",
			  UNI_CMD_SER_SET_RECOVER_L3_TX_DISABLE);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L3 bf recovery\n",
			  UNI_CMD_SER_SET_RECOVER_L3_BF);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error L4 mdp recovery\n",
			  UNI_CMD_SER_SET_RECOVER_L4_MDP);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger system error full recovery\n",
			  UNI_CMD_SER_SET_RECOVER_FULL);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger firmware crash\n",
			  UNI_CMD_SER_SET_SYSTEM_ASSERT);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger grab wa firmware coredump\n",
			  UNI_CMD_SER_FW_COREDUMP_WA);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: trigger grab wm firmware coredump\n",
			  UNI_CMD_SER_FW_COREDUMP_WM);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "%2d: hw bit detect only\n",
			  UNI_CMD_SER_SET_HW_BIT_DETECT_ONLY);
	/* SER statistics */
	desc += scnprintf(buff + desc, bufsz - desc,
			  "\nlet's dump firmware SER statistics...\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_STATUS        = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_SER_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PLE_ERR       = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PLE_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PLE_ERR_1     = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PLE1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PLE_ERR_AMSDU = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PLE_AMSDU_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PSE_ERR       = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PSE_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PSE_ERR_1     = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PSE1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR6_B0 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR6_BN0_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR6_B1 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR6_BN1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR6_B2 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR6_BN2_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR7_B0 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR7_BN0_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR7_B1 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR7_BN1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR7_B2 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR7_BN2_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "\nSYS_RESET_COUNT: WM %d, WA %d\n",
			  dev->recovery.wm_reset_count,
			  dev->recovery.wa_reset_count);

	ret = simple_read_from_buffer(user_buf, count, ppos, buff, desc);
	kfree(buff);
	return ret;
}

static const struct file_operations mt7996_sys_recovery_ops = {
	.write = mt7996_sys_recovery_set,
	.read = mt7996_sys_recovery_get,
	.open = simple_open,
	.llseek = default_llseek,
};

static int
mt7996_radar_trigger(void *data, u64 val)
{
#define RADAR_MAIN_CHAIN	1
#define RADAR_BACKGROUND	2
	struct mt7996_phy *phy = data;
	struct mt7996_dev *dev = phy->dev;
	int rdd_idx;

	if (!val || val > RADAR_BACKGROUND)
		return -EINVAL;

	if (val == RADAR_BACKGROUND && !dev->rdd2_phy) {
		dev_err(dev->mt76.dev, "Background radar is not enabled\n");
		return -EINVAL;
	}

	rdd_idx = mt7996_get_rdd_idx(phy, val == RADAR_BACKGROUND);
	if (rdd_idx < 0) {
		dev_err(dev->mt76.dev, "No RDD found\n");
		return -EINVAL;
	}

	return mt7996_mcu_rdd_cmd(dev, RDD_RADAR_EMULATE, rdd_idx, 0);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_radar_trigger, NULL,
			 mt7996_radar_trigger, "%lld\n");

static int
mt7996_rdd_monitor(struct seq_file *s, void *data)
{
	struct mt7996_dev *dev = dev_get_drvdata(s->private);
	struct cfg80211_chan_def *chandef = &dev->rdd2_chandef;
	const char *bw;
	int ret = 0;

	mutex_lock(&dev->mt76.mutex);

	if (!cfg80211_chandef_valid(chandef)) {
		ret = -EINVAL;
		goto out;
	}

	if (!dev->rdd2_phy) {
		seq_puts(s, "not running\n");
		goto out;
	}

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		bw = "40";
		break;
	case NL80211_CHAN_WIDTH_80:
		bw = "80";
		break;
	case NL80211_CHAN_WIDTH_160:
		bw = "160";
		break;
	case NL80211_CHAN_WIDTH_80P80:
		bw = "80P80";
		break;
	default:
		bw = "20";
		break;
	}

	seq_printf(s, "channel %d (%d MHz) width %s MHz center1: %d MHz\n",
		   chandef->chan->hw_value, chandef->chan->center_freq,
		   bw, chandef->center_freq1);
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_fw_debug_wm_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;
	enum {
		DEBUG_TXCMD = 62,
		DEBUG_CMD_RPT_TX,
		DEBUG_CMD_RPT_TRIG,
		DEBUG_SPL,
		DEBUG_RPT_RX,
		DEBUG_IDS_SND = 84,
		DEBUG_IDS_BSRP,
		DEBUG_IDS_TPUT_MON,
	};
	enum mt7996_ids_idx {
		DEBUG_MT7996_IDS_PP = 93,
		DEBUG_MT7996_IDS_RA,
		DEBUG_MT7996_IDS_BF,
		DEBUG_MT7996_IDS_SR,
		DEBUG_MT7996_IDS_RU,
		DEBUG_MT7996_IDS_MUMIMO,
		DEBUG_MT7996_IDS_MLO = 100,
		DEBUG_MT7996_IDS_ERR_LOG,
	};
	enum mt7992_ids_idx {
		DEBUG_MT7992_IDS_PP = 94,
		DEBUG_MT7992_IDS_RA,
		DEBUG_MT7992_IDS_BF,
		DEBUG_MT7992_IDS_SR,
		DEBUG_MT7992_IDS_RU,
		DEBUG_MT7992_IDS_MUMIMO,
		DEBUG_MT7992_IDS_MLO = 101,
		DEBUG_MT7992_IDS_ERR_LOG,
	};

	u8 debug_category[] = {
		DEBUG_TXCMD,
		DEBUG_CMD_RPT_TX,
		DEBUG_CMD_RPT_TRIG,
		DEBUG_SPL,
		DEBUG_RPT_RX,
		DEBUG_IDS_SND,
		DEBUG_IDS_BSRP,
		DEBUG_IDS_TPUT_MON,
		is_mt7996(&dev->mt76) ? DEBUG_MT7996_IDS_PP : DEBUG_MT7992_IDS_PP,
		is_mt7996(&dev->mt76) ? DEBUG_MT7996_IDS_RA : DEBUG_MT7992_IDS_RA,
		is_mt7996(&dev->mt76) ? DEBUG_MT7996_IDS_BF : DEBUG_MT7992_IDS_BF,
		is_mt7996(&dev->mt76) ? DEBUG_MT7996_IDS_SR : DEBUG_MT7992_IDS_SR,
		is_mt7996(&dev->mt76) ? DEBUG_MT7996_IDS_RU : DEBUG_MT7992_IDS_RU,
		is_mt7996(&dev->mt76) ? DEBUG_MT7996_IDS_MUMIMO : DEBUG_MT7992_IDS_MUMIMO,
		is_mt7996(&dev->mt76) ? DEBUG_MT7996_IDS_MLO : DEBUG_MT7992_IDS_MLO,
		is_mt7996(&dev->mt76) ? DEBUG_MT7996_IDS_ERR_LOG : DEBUG_MT7992_IDS_ERR_LOG,
	};
	bool tx, rx, en;
	int ret;
	u8 i;

	dev->fw_debug_wm = val;

	if (dev->fw_debug_bin)
		val = MCU_FW_LOG_RELAY;
	else
		val = dev->fw_debug_wm;

	tx = dev->fw_debug_wm || (dev->fw_debug_bin & BIT(1));
	rx = dev->fw_debug_wm || (dev->fw_debug_bin & BIT(2));
	en = dev->fw_debug_wm || (dev->fw_debug_bin & BIT(0));

	ret = mt7996_mcu_fw_log_2_host(dev, MCU_FW_LOG_WM, val);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(debug_category); i++) {
		if (debug_category[i] == DEBUG_RPT_RX)
			val = en && rx;
		else
			val = en && tx;

		ret = mt7996_mcu_fw_dbg_ctrl(dev, debug_category[i], val);
		if (ret)
			return ret;

		if ((debug_category[i] == DEBUG_TXCMD ||
		     debug_category[i] == DEBUG_IDS_SND) && en) {
			ret = mt7996_mcu_fw_dbg_ctrl(dev, debug_category[i], 2);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int
mt7996_fw_debug_wm_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;

	*val = dev->fw_debug_wm;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_wm, mt7996_fw_debug_wm_get,
			 mt7996_fw_debug_wm_set, "%lld\n");

static int
mt7996_fw_debug_wa_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;
	int ret;

	dev->fw_debug_wa = val ? MCU_FW_LOG_TO_HOST : 0;

	ret = mt7996_mcu_fw_log_2_host(dev, MCU_FW_LOG_WA, dev->fw_debug_wa);
	if (ret)
		return ret;

	return mt7996_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(SET), MCU_WA_PARAM_PDMA_RX,
				 !!dev->fw_debug_wa, 0);
}

static int
mt7996_fw_debug_wa_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;

	*val = dev->fw_debug_wa;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_wa, mt7996_fw_debug_wa_get,
			 mt7996_fw_debug_wa_set, "%lld\n");

static struct dentry *
create_buf_file_cb(const char *filename, struct dentry *parent, umode_t mode,
		   struct rchan_buf *buf, int *is_global)
{
	struct dentry *f;

	f = debugfs_create_file(filename[0] == 'f' ? "fwlog_data" : "idxlog_data",
	                        mode, parent, buf, &relay_file_operations);
	if (IS_ERR(f))
		return NULL;

	*is_global = 1;

	return f;
}

static int
remove_buf_file_cb(struct dentry *f)
{
	debugfs_remove(f);

	return 0;
}

static int
mt7996_fw_debug_muru_set(void *data)
{
	struct mt7996_dev *dev = data;
	enum {
		DEBUG_BSRP_STATUS = 256,
		DEBUG_TX_DATA_BYTE_CONUT,
		DEBUG_RX_DATA_BYTE_CONUT,
		DEBUG_RX_TOTAL_BYTE_CONUT,
		DEBUG_INVALID_TID_BSR,
		DEBUG_UL_LONG_TERM_PPDU_TYPE,
		DEBUG_DL_LONG_TERM_PPDU_TYPE,
		DEBUG_PPDU_CLASS_TRIG_ONOFF,
		DEBUG_AIRTIME_BUSY_STATUS,
		DEBUG_UL_OFDMA_MIMO_STATUS,
		DEBUG_RU_CANDIDATE,
		DEBUG_MEC_UPDATE_AMSDU,
	} debug;
	int ret;

	if (dev->fw_debug_muru_disable)
		return 0;

	for (debug = DEBUG_BSRP_STATUS; debug <= DEBUG_MEC_UPDATE_AMSDU; debug++) {
		ret = mt7996_mcu_muru_dbg_info(dev, debug,
					       dev->fw_debug_bin & BIT(0));
		if (ret)
			return ret;
	}

	return 0;
}

static int
mt7996_fw_debug_bin_set(void *data, u64 val)
{
	static struct rchan_callbacks relay_cb = {
		.create_buf_file = create_buf_file_cb,
		.remove_buf_file = remove_buf_file_cb,
	};
	struct mt7996_dev *dev = data;
	int ret;

	if (!dev->relay_fwlog) {
		dev->relay_fwlog = relay_open("fwlog_data", dev->debugfs_dir,
					      1500, 512, &relay_cb, NULL);
		if (!dev->relay_fwlog)
			return -ENOMEM;
	}

	dev->fw_debug_bin = val;

	relay_reset(dev->relay_fwlog);

	ret = mt7996_fw_debug_muru_set(dev);
	if (ret)
		return ret;

	dev->dbg.dump_mcu_pkt = !!(val & BIT(4));
	dev->dbg.dump_txd = !!(val & BIT(5));
	dev->dbg.dump_tx_pkt = !!(val & BIT(6));
	dev->dbg.dump_rx_pkt = !!(val & BIT(7));
	dev->dbg.dump_rx_raw = !!(val & BIT(8));
	dev->dbg.dump_mcu_event = !!(val & BIT(9));

	return mt7996_fw_debug_wm_set(dev, dev->fw_debug_wm);
}

static int
mt7996_fw_debug_bin_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;

	*val = dev->fw_debug_bin;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_bin, mt7996_fw_debug_bin_get,
			 mt7996_fw_debug_bin_set, "%lld\n");

static int
mt7996_idxlog_enable_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;

	*val = dev->idxlog_enable;

	return 0;
}

static int
mt7996_idxlog_enable_set(void *data, u64 val)
{
	static struct rchan_callbacks relay_cb = {
		.create_buf_file = create_buf_file_cb,
		.remove_buf_file = remove_buf_file_cb,
	};
	struct mt7996_dev *dev = data;

	if (dev->idxlog_enable == !!val)
		return 0;

	if (!dev->relay_idxlog) {
		dev->relay_idxlog = relay_open("idxlog_data", dev->debugfs_dir,
		                               1500, 512, &relay_cb, NULL);
		if (!dev->relay_idxlog)
			return -ENOMEM;
	}

	dev->idxlog_enable = !!val;

	if (val) {
		int ret = mt7996_mcu_fw_time_sync(&dev->mt76);
		if (ret)
			return ret;

		/* Reset relay channel only when it is not being written to. */
		relay_reset(dev->relay_idxlog);
	}

	return mt7996_mcu_fw_log_2_host(dev, MCU_FW_LOG_WM,
	                                val ? MCU_FW_LOG_RELAY_IDX : 0);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_idxlog_enable, mt7996_idxlog_enable_get,
	                 mt7996_idxlog_enable_set, "%llu\n");

void mt7996_packet_log_to_host(struct mt7996_dev *dev, const void *data, int len, int type, int des_len)
{
	struct bin_debug_hdr *hdr;
	char *buf;

	if (len > 1500 - sizeof(*hdr))
		len = 1500 - sizeof(*hdr);

	buf = kzalloc(sizeof(*hdr) + len, GFP_KERNEL);
	if (!buf)
		return;

	hdr = (struct bin_debug_hdr *)buf;
	hdr->magic_num = cpu_to_le32(PKT_BIN_DEBUG_MAGIC);
	hdr->serial_id = cpu_to_le16(dev->fw_debug_seq++);
	hdr->msg_type = cpu_to_le16(type);
	hdr->len = cpu_to_le16(len);
	hdr->des_len = cpu_to_le16(des_len);

	memcpy(buf + sizeof(*hdr), data, len);

	mt7996_debugfs_rx_log(dev, buf, sizeof(*hdr) + len);
	kfree(buf);
}

static int
mt7996_fw_util_wa_show(struct seq_file *file, void *data)
{
	struct mt7996_dev *dev = file->private;

	if (dev->fw_debug_wa)
		return mt7996_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(QUERY),
					 MCU_WA_PARAM_CPU_UTIL, 0, 0);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7996_fw_util_wa);

static void
mt7996_ampdu_stat_read_phy(struct mt7996_phy *phy, struct seq_file *file)
{
	struct mt7996_dev *dev = phy->dev;
	int bound[15], range[8], i;
	u8 band_idx = phy->mt76->band_idx;

	/* Tx ampdu stat */
	for (i = 0; i < ARRAY_SIZE(range); i++)
		range[i] = mt76_rr(dev, MT_MIB_ARNG(band_idx, i));

	for (i = 0; i < ARRAY_SIZE(bound); i++)
		bound[i] = MT_MIB_ARNCR_RANGE(range[i / 2], i % 2) + 1;

	seq_printf(file, "\nPhy %s, Phy band %d\n",
		   wiphy_name(phy->mt76->hw->wiphy), band_idx);

	seq_printf(file, "Length: %8d | ", bound[0]);
	for (i = 0; i < ARRAY_SIZE(bound) - 1; i++)
		seq_printf(file, "%3d -%3d | ",
			   bound[i] + 1, bound[i + 1]);

	seq_puts(file, "\nCount:  ");
	for (i = 0; i < ARRAY_SIZE(bound); i++)
		seq_printf(file, "%8d | ", phy->mt76->aggr_stats[i]);
	seq_puts(file, "\n");

	seq_printf(file, "BA miss count: %d\n", phy->mib.ba_miss_cnt);
}

static void
mt7996_txbf_stat_read_phy(struct mt7996_phy *phy, struct seq_file *s)
{
	struct mt76_mib_stats *mib = &phy->mib;
	static const char * const bw[] = {
		"BW20", "BW40", "BW80", "BW160", "BW320"
	};

	/* Tx Beamformer monitor */
	seq_puts(s, "\nTx Beamformer applied PPDU counts: ");

	seq_printf(s, "iBF: %d, eBF: %d\n",
		   mib->tx_bf_ibf_ppdu_cnt,
		   mib->tx_bf_ebf_ppdu_cnt);

	/* Tx Beamformer Rx feedback monitor */
	seq_puts(s, "Tx Beamformer Rx feedback statistics: ");

	seq_printf(s, "All: %d, EHT: %d, HE: %d, VHT: %d, HT: %d, ",
		   mib->tx_bf_rx_fb_all_cnt,
		   mib->tx_bf_rx_fb_eht_cnt,
		   mib->tx_bf_rx_fb_he_cnt,
		   mib->tx_bf_rx_fb_vht_cnt,
		   mib->tx_bf_rx_fb_ht_cnt);

	seq_printf(s, "%s, NC: %d, NR: %d\n",
		   bw[mib->tx_bf_rx_fb_bw],
		   mib->tx_bf_rx_fb_nc_cnt,
		   mib->tx_bf_rx_fb_nr_cnt);

	/* Tx Beamformee Rx NDPA & Tx feedback report */
	seq_printf(s, "Tx Beamformee successful feedback frames: %d\n",
		   mib->tx_bf_fb_cpl_cnt);
	seq_printf(s, "Tx Beamformee feedback triggered counts: %d\n",
		   mib->tx_bf_fb_trig_cnt);

	/* Tx SU & MU counters */
	seq_printf(s, "Tx multi-user Beamforming counts: %d\n",
		   mib->tx_mu_bf_cnt);
	seq_printf(s, "Tx multi-user MPDU counts: %d\n", mib->tx_mu_mpdu_cnt);
	seq_printf(s, "Tx multi-user successful MPDU counts: %d\n",
		   mib->tx_mu_acked_mpdu_cnt);
	seq_printf(s, "Tx single-user successful MPDU counts: %d\n",
		   mib->tx_su_acked_mpdu_cnt);

	seq_puts(s, "\n");
}

static void
mt7996_tx_stats_show_phy(struct seq_file *file, struct mt7996_phy *phy)
{
	struct mt76_mib_stats *mib = &phy->mib;
	u32 attempts, success, per;
	int i;

	mt7996_mac_update_stats(phy);
	mt7996_ampdu_stat_read_phy(phy, file);

	attempts = mib->tx_mpdu_attempts_cnt;
	success = mib->tx_mpdu_success_cnt;
	per = attempts ? 100 - success * 100 / attempts : 100;
	seq_printf(file, "Tx attempts: %8u (MPDUs)\n", attempts);
	seq_printf(file, "Tx success: %8u (MPDUs)\n", success);
	seq_printf(file, "Tx PER: %u%%\n", per);

	mt7996_txbf_stat_read_phy(phy, file);

	/* Tx amsdu info */
	seq_puts(file, "Tx MSDU statistics:\n");
	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		seq_printf(file, "AMSDU pack count of %d MSDU in TXD: %8d ",
			   i + 1, mib->tx_amsdu[i]);
		if (mib->tx_amsdu_cnt)
			seq_printf(file, "(%3d%%)\n",
				   mib->tx_amsdu[i] * 100 / mib->tx_amsdu_cnt);
		else
			seq_puts(file, "\n");
	}
}

static int
mt7996_tx_stats_show(struct seq_file *file, void *data)
{
	struct mt7996_phy *phy = file->private;
	struct mt7996_dev *dev = phy->dev;

	mutex_lock(&dev->mt76.mutex);

	mt7996_tx_stats_show_phy(file, phy);

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7996_tx_stats);

static int
mt7996_rxfilter_show(struct seq_file *file, void *data)
{
	struct mt7996_phy *phy = file->private;
	struct mt7996_dev *dev = phy->dev;
	u32 cr, cr1;

	mutex_lock(&phy->dev->mt76.mutex);

	cr = mt76_rr(dev, MT_WF_RFCR(phy->mt76->band_idx));
	cr1 = mt76_rr(dev, MT_WF_RFCR1(phy->mt76->band_idx));

#define __MT7996_RXFILTER_PRINT(reg, flag) do {		\
		if ((reg) & (flag))			\
			seq_printf(file, #flag "\n");	\
	} while (0)
#define MT7996_RFCR_PRINT(flag) __MT7996_RXFILTER_PRINT(cr, MT_WF_RFCR_##flag)
#define MT7996_RFCR1_PRINT(flag) __MT7996_RXFILTER_PRINT(cr1, MT_WF_RFCR1_##flag)

	seq_printf(file, "CR: 0x%08x (configured: 0x%08x)\n", cr, phy->rxfilter.cr);
	MT7996_RFCR_PRINT(DROP_STBC_MULTI);
	MT7996_RFCR_PRINT(DROP_FCSFAIL);
	MT7996_RFCR_PRINT(DROP_PROBEREQ);
	MT7996_RFCR_PRINT(DROP_MCAST);
	MT7996_RFCR_PRINT(DROP_BCAST);
	MT7996_RFCR_PRINT(DROP_MCAST_FILTERED);
	MT7996_RFCR_PRINT(DROP_A3_MAC);
	MT7996_RFCR_PRINT(DROP_A3_BSSID);
	MT7996_RFCR_PRINT(DROP_A2_BSSID);
	MT7996_RFCR_PRINT(DROP_OTHER_BEACON);
	MT7996_RFCR_PRINT(DROP_FRAME_REPORT);
	MT7996_RFCR_PRINT(DROP_CTL_RSV);
	MT7996_RFCR_PRINT(DROP_CTS);
	MT7996_RFCR_PRINT(DROP_RTS);
	MT7996_RFCR_PRINT(DROP_DUPLICATE);
	MT7996_RFCR_PRINT(DROP_OTHER_BSS);
	MT7996_RFCR_PRINT(DROP_OTHER_UC);
	MT7996_RFCR_PRINT(DROP_OTHER_TIM);
	MT7996_RFCR_PRINT(DROP_NDPA);
	MT7996_RFCR_PRINT(DROP_UNWANTED_CTL);

	seq_printf(file, "\nCR1: 0x%08x (configured: 0x%08x)\n", cr1, phy->rxfilter.cr1);
	MT7996_RFCR1_PRINT(DROP_ACK);
	MT7996_RFCR1_PRINT(DROP_BF_POLL);
	MT7996_RFCR1_PRINT(DROP_BA);
	MT7996_RFCR1_PRINT(DROP_CFEND);
	MT7996_RFCR1_PRINT(DROP_CFACK);

	mutex_unlock(&phy->dev->mt76.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7996_rxfilter);

static int
mt7996_phy_info_show(struct seq_file *file, void *data)
{
	struct mt7996_dev *dev = file->private;
	struct mt7996_phy *phy;

	mutex_lock(&dev->mt76.mutex);

	mt7996_for_each_phy(dev, phy) {
		seq_printf(file, "MAC: %pM\n", phy->mt76->macaddr);
		seq_printf(file, "Band: %d\n", phy->mt76->band_idx);
	}

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7996_phy_info);

struct mt7996_txo_worker_info {
	char *buf;
	int sofar;
	int size;
};

// TODO:  Set txo per link instead of assuming it is in deflink
//  but maybe first, see if mtk firmware has what we need for txo
//  as it may not ever support what we want. --Ben
static void mt7996_txo_worker(void *wi_data, struct ieee80211_sta *sta)
{
	struct mt7996_txo_worker_info *wi = wi_data;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt76_testmode_data *td = &msta->deflink.test;
	struct ieee80211_vif *vif;
	struct wireless_dev *wdev;

	if (wi->sofar >= wi->size)
		return; /* buffer is full */

	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	wdev = ieee80211_vif_to_wdev(vif);

	wi->sofar += scnprintf(wi->buf + wi->sofar, wi->size - wi->sofar,
			       "vdev (%s) active=%d tpc=%d sgi=%d mcs=%d nss=%d"
			       " pream=%d retries=%d dynbw=%d bw=%d stbc=%d ldpc=%d\n",
			       wdev->netdev->name,
			       td->txo_active, td->tx_power[0],
			       td->tx_rate_sgi, td->tx_rate_idx,
			       td->tx_rate_nss, td->tx_rate_mode,
			       td->tx_xmit_count, td->tx_dynbw,
			       td->txbw, td->tx_rate_stbc, td->tx_rate_ldpc);
}

static ssize_t mt7996_read_set_rate_override(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct mt7996_dev *dev = file->private_data;
	struct ieee80211_hw *hw = dev->mphy.hw;
	char *buf2;
	int size = 8000;
	int rv, sofar;
	struct mt7996_txo_worker_info wi;
	const char buf[] =
		"This allows specify specif tx rate parameters for all DATA"
		" frames on a vdev\n"
		"To set a value, you specify the dev-name and key-value pairs:\n"
		"tpc=10 sgi=1 mcs=x nss=x pream=x retries=x dynbw=0|1 bw=x enable=0|1\n"
		"pream: 0=cck, 1=ofdm, 2=HT, 3=VHT, 4=HE_SU\n"
		"cck-mcs: 0=1Mbps, 1=2Mbps, 3=5.5Mbps, 3=11Mbps\n"
		"ofdm-mcs: 0=6Mbps, 1=9Mbps, 2=12Mbps, 3=18Mbps, 4=24Mbps, 5=36Mbps,"
		" 6=48Mbps, 7=54Mbps\n"
		"sgi: HT/VHT: 0 | 1, HE 0: 1xLTF+0.8us, 1: 2xLTF+0.8us, 2: 2xLTF+1.6us, 3: 4xLTF+3.2us, 4: 4xLTF+0.8us\n"
		"tpc: adjust power from defaults, in 1/2 db units 0 - 31, 16 is default\n"
		"bw is 0-3 for 20-160\n"
		"stbc: 0 off, 1 on\n"
		"ldpc: 0 off, 1 on\n"
		" For example, wlan0:\n"
		"echo \"wlan0 tpc=255 sgi=1 mcs=0 nss=1 pream=3 retries=1 dynbw=0 bw=0"
		" active=1\" > ...mt76/set_rate_override\n";

	buf2 = kzalloc(size, GFP_KERNEL);
	if (!buf2)
		return -ENOMEM;
	strcpy(buf2, buf);
	sofar = strlen(buf2);

	wi.sofar = sofar;
	wi.buf = buf2;
	wi.size = size;

	ieee80211_iterate_stations_atomic(hw, mt7996_txo_worker, &wi);

	rv = simple_read_from_buffer(user_buf, count, ppos, buf2, wi.sofar);
	kfree(buf2);
	return rv;
}

/* Set the rates for specific types of traffic.
 */
static ssize_t mt7996_write_set_rate_override(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct mt7996_dev *dev = file->private_data;
	struct mt7996_sta *msta;
	struct ieee80211_vif *vif;
	struct mt76_testmode_data *td = NULL;
	struct wireless_dev *wdev;
	struct mt76_wcid *wcid;
	struct mt7996_sta_link *link;
	struct mt76_phy *mphy = &dev->mt76.phy;
	char buf[180];
	char tmp[20];
	char *tok;
	int ret, i, j;
	unsigned int vdev_id = 0xFFFF;
	char *bufptr = buf;
	long rc;
	char dev_name_match[IFNAMSIZ + 2];

	memset(buf, 0, sizeof(buf));

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = 0;

#define MT7996_PARSE_LTOK(a, b)						\
	do {								\
		tok = strstr(bufptr, " " #a "=");			\
		if (tok) {						\
			char *tspace;					\
			tok += 1; /* move past initial space */		\
			strncpy(tmp, tok + strlen(#a "="), sizeof(tmp) - 1); \
			tmp[sizeof(tmp) - 1] = 0;			\
			tspace = strstr(tmp, " ");			\
			if (tspace)					\
				*tspace = 0;				\
			if (kstrtol(tmp, 0, &rc) != 0)			\
				dev_info(dev->mt76.dev,			\
					 "mt7996: set-rate-override: " #a \
					 "= could not be parsed, tmp: %s\n", \
					 tmp);				\
			else						\
				td->b = rc;				\
		}							\
	} while (0)

	/* drop the possible '\n' from the end */
	if (buf[count - 1] == '\n')
		buf[count - 1] = 0;

	mutex_lock(&mphy->dev->mutex);

	/* Ignore empty lines, 'echo' appends them sometimes at least. */
	if (buf[0] == 0) {
		ret = count;
		goto exit;
	}

	/* String starts with vdev name, ie 'wlan0'  Find the proper vif that
	 * matches the name.
	 */
	for (i = 0; i < ARRAY_SIZE(dev->mt76.wcid_mask); i++) {
		u32 mask = dev->mt76.wcid_mask[i];

		if (!mask)
			continue;

		for (j = i * 32; mask; j++, mask >>= 1) {
			if (!(mask & 1))
				continue;

			rcu_read_lock();
			wcid = rcu_dereference(dev->mt76.wcid[j]);
			if (!wcid) {
				rcu_read_unlock();
				continue;
			}

			link = container_of(wcid, struct mt7996_sta_link, wcid);
			msta = link->sta;

			if (!msta->vif) {
				rcu_read_unlock();
				continue;
			}

			vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);

			wdev = ieee80211_vif_to_wdev(vif);

			if (!wdev || !wdev->netdev) {
				rcu_read_unlock();
				continue;
			}

			snprintf(dev_name_match, sizeof(dev_name_match) - 1, "%s ",
				 wdev->netdev->name);

			if (strncmp(dev_name_match, buf, strlen(dev_name_match)) == 0) {
				vdev_id = j;
				td = &msta->deflink.test;
				bufptr = buf + strlen(dev_name_match) - 1;

				MT7996_PARSE_LTOK(tpc, tx_power[0]);
				MT7996_PARSE_LTOK(sgi, tx_rate_sgi);
				MT7996_PARSE_LTOK(mcs, tx_rate_idx);
				MT7996_PARSE_LTOK(nss, tx_rate_nss);
				MT7996_PARSE_LTOK(pream, tx_rate_mode);
				MT7996_PARSE_LTOK(retries, tx_xmit_count);
				MT7996_PARSE_LTOK(dynbw, tx_dynbw);
				MT7996_PARSE_LTOK(bw, txbw);
				MT7996_PARSE_LTOK(active, txo_active);
				MT7996_PARSE_LTOK(ldpc, tx_rate_ldpc);
				MT7996_PARSE_LTOK(stbc, tx_rate_stbc);

				/* To match Intel's API
				 * HE 0: 1xLTF+0.8us, 1: 2xLTF+0.8us, 2: 2xLTF+1.6us, 3: 4xLTF+3.2us, 4: 4xLTF+0.8us
				 */
				if (td->tx_rate_mode >= 4) {
					if (td->tx_rate_sgi == 0) {
						td->tx_rate_sgi = 0;
						td->tx_ltf = 0;
					} else if (td->tx_rate_sgi == 1) {
						td->tx_rate_sgi = 0;
						td->tx_ltf = 1;
					} else if (td->tx_rate_sgi == 2) {
						td->tx_rate_sgi = 1;
						td->tx_ltf = 1;
					} else if (td->tx_rate_sgi == 3) {
						td->tx_rate_sgi = 2;
						td->tx_ltf = 2;
					}
					else {
						td->tx_rate_sgi = 0;
						td->tx_ltf = 2;
					}
				}
				//td->tx_ltf = 1; /* 0: HTLTF 3.2us, 1: HELTF, 6.4us, 2 HELTF 12,8us */

				dev_info(dev->mt76.dev,
					 "mt7996: set-rate-overrides, vdev %i(%s) active=%d tpc=%d sgi=%d ltf=%d mcs=%d"
					 " nss=%d pream=%d retries=%d dynbw=%d bw=%d ldpc=%d stbc=%d\n",
					 vdev_id, dev_name_match,
					 td->txo_active, td->tx_power[0], td->tx_rate_sgi, td->tx_ltf, td->tx_rate_idx,
					 td->tx_rate_nss, td->tx_rate_mode, td->tx_xmit_count, td->tx_dynbw,
					 td->txbw, td->tx_rate_ldpc, td->tx_rate_stbc);
			}

			rcu_read_unlock();
		}
	}

	if (vdev_id == 0xFFFF) {
		if (strstr(buf, "active=0")) {
			/* Ignore, we are disabling it anyway */
			ret = count;
			goto exit;
		} else {
			dev_info(dev->mt76.dev,
				 "mt7996: set-rate-override, unknown netdev name: %s\n", buf);
		}
		ret = -EINVAL;
		goto exit;
	}

	ret = count;

exit:
	mutex_unlock(&mphy->dev->mutex);
	return ret;
}

static const struct file_operations fops_set_rate_override = {
	.read = mt7996_read_set_rate_override,
	.write = mt7996_write_set_rate_override,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int
mt7996_sr_pp_enable_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;

	*val = dev->sr_pp_enable;

	return 0;
}

static int
mt7996_sr_pp_enable_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;
	int ret;
	bool en = !!val;

	if (en == dev->sr_pp_enable)
		return 0;

	ret = mt7996_mcu_set_sr_pp_en(dev, en);
	if (ret)
		return ret;

	dev->sr_pp_enable = en;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_sr_pp_enable, mt7996_sr_pp_enable_get,
			 mt7996_sr_pp_enable_set, "%lld\n");

static int
mt7996_uba_enable_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;

	*val = dev->uba_enable;

	return 0;
}

static int
mt7996_uba_enable_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;
	int ret;
	bool en = !!val;

	if (en == dev->uba_enable)
		return 0;

	ret = mt7996_mcu_set_uba_en(dev, en);
	if (ret)
		return ret;

	dev->uba_enable = en;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_uba_enable, mt7996_uba_enable_get,
			 mt7996_uba_enable_set, "%lld\n");

static int
mt7996_mru_probe_enable_get(void *data, u64 *val)
{
	struct mt7996_phy *phy = data;

	*val = phy->mru_probe_enable;

	return 0;
}

static int
mt7996_mru_probe_enable_set(void *data, u64 val)
{
#define MRU_PROBE_ENABLE 1
	struct mt7996_phy *phy = data;
	int ret;
	bool en = !!val;

	if (en == phy->mru_probe_enable)
		return 0;

	if (en != MRU_PROBE_ENABLE)
		return 0;

	ret = mt7996_mcu_set_mru_probe_en(phy);
	if (ret)
		return ret;

	phy->mru_probe_enable = en;
	/* When enabling MRU probe, PP would also enter FW mode */
	phy->pp_mode = PP_FW_MODE;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_mru_probe_enable, mt7996_mru_probe_enable_get,
			 mt7996_mru_probe_enable_set, "%lld\n");

static int
mt7996_rx_group_5_enable_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;

	mutex_lock(&dev->mt76.mutex);

	dev->rx_group_5_enable = !!val;

	/* Enabled if we requested enabled OR if monitor mode is enabled. */
	mt76_rmw_field(dev, MT_DMA_DCR0(0), MT_DMA_DCR0_RXD_G5_EN,
		       dev->rx_group_5_enable);
	mt76_testmode_reset(dev->phy.mt76, true);

	mutex_unlock(&dev->mt76.mutex);
	return 0;
}

static int
mt7996_rx_group_5_enable_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;

	*val = dev->rx_group_5_enable;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_rx_group_5_enable, mt7996_rx_group_5_enable_get,
			 mt7996_rx_group_5_enable_set, "%lld\n");

static void
mt7996_hw_queue_read(struct seq_file *s, u32 size,
		     const struct hw_queue_map *map)
{
	struct mt7996_phy *phy = s->private;
	struct mt7996_dev *dev = phy->dev;
	u32 i, val;

	val = mt76_rr(dev, MT_FL_Q_EMPTY);
	for (i = 0; i < size; i++) {
		u32 ctrl, head, tail, queued;

		if (val & BIT(map[i].index))
			continue;

		ctrl = BIT(31) | (map[i].pid << 10) | ((u32)map[i].qid << 24);
		mt76_wr(dev, MT_FL_Q0_CTRL, ctrl);

		head = mt76_get_field(dev, MT_FL_Q2_CTRL,
				      GENMASK(11, 0));
		tail = mt76_get_field(dev, MT_FL_Q2_CTRL,
				      GENMASK(27, 16));
		queued = mt76_get_field(dev, MT_FL_Q3_CTRL,
					GENMASK(11, 0));

		seq_printf(s, "\t%s: ", map[i].name);
		seq_printf(s, "queued:0x%03x head:0x%03x tail:0x%03x\n",
			   queued, head, tail);
	}
}

static void
mt7996_sta_hw_queue_read(void *data, struct ieee80211_sta *sta)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_vif *mvif = msta->vif;
	struct mt7996_dev *dev = mvif->deflink.phy->dev;
	struct ieee80211_link_sta *link_sta;
	struct seq_file *s = data;
	struct ieee80211_vif *vif;
	unsigned int link_id;

	vif = container_of((void *)mvif, struct ieee80211_vif, drv_priv);

	rcu_read_lock();

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7996_sta_link *msta_link;
		struct mt76_vif_link *mlink;
		u8 ac;

		mlink = rcu_dereference(mvif->mt76.link[link_id]);
		if (!mlink)
			continue;

		msta_link = rcu_dereference(msta->link[link_id]);
		if (!msta_link)
			continue;

		for (ac = 0; ac < 4; ac++) {
			u32 idx = msta_link->wcid.idx >> 5, qlen, ctrl, val;
			u8 offs = msta_link->wcid.idx & GENMASK(4, 0);

			ctrl = BIT(31) | BIT(11) | (ac << 24);
			val = mt76_rr(dev, MT_PLE_AC_QEMPTY(ac, idx));

			if (val & BIT(offs))
				continue;

			mt76_wr(dev,
				MT_FL_Q0_CTRL, ctrl | msta_link->wcid.idx);
			qlen = mt76_get_field(dev, MT_FL_Q3_CTRL,
					      GENMASK(11, 0));
			seq_printf(s, "\tSTA %pM wcid %d: AC%d%d queued:%d\n",
				   sta->addr, msta_link->wcid.idx,
				   mlink->wmm_idx, ac, qlen);
		}
	}

	rcu_read_unlock();
}

static int
mt7996_hw_queues_show(struct seq_file *file, void *data)
{
	struct mt7996_phy *phy = file->private;
	struct mt7996_dev *dev = phy->dev;
	static const struct hw_queue_map ple_queue_map[] = {
		{ "CPU_Q0",  0,  1, MT_CTX0	      },
		{ "CPU_Q1",  1,  1, MT_CTX0 + 1	      },
		{ "CPU_Q2",  2,  1, MT_CTX0 + 2	      },
		{ "CPU_Q3",  3,  1, MT_CTX0 + 3	      },
		{ "ALTX_Q0", 8,  2, MT_LMAC_ALTX0     },
		{ "BMC_Q0",  9,  2, MT_LMAC_BMC0      },
		{ "BCN_Q0",  10, 2, MT_LMAC_BCN0      },
		{ "PSMP_Q0", 11, 2, MT_LMAC_PSMP0     },
		{ "ALTX_Q1", 12, 2, MT_LMAC_ALTX0 + 4 },
		{ "BMC_Q1",  13, 2, MT_LMAC_BMC0  + 4 },
		{ "BCN_Q1",  14, 2, MT_LMAC_BCN0  + 4 },
		{ "PSMP_Q1", 15, 2, MT_LMAC_PSMP0 + 4 },
	};
	static const struct hw_queue_map pse_queue_map[] = {
		{ "CPU Q0",  0,  1, MT_CTX0	      },
		{ "CPU Q1",  1,  1, MT_CTX0 + 1	      },
		{ "CPU Q2",  2,  1, MT_CTX0 + 2	      },
		{ "CPU Q3",  3,  1, MT_CTX0 + 3	      },
		{ "HIF_Q0",  8,  0, MT_HIF0	      },
		{ "HIF_Q1",  9,  0, MT_HIF0 + 1	      },
		{ "HIF_Q2",  10, 0, MT_HIF0 + 2	      },
		{ "HIF_Q3",  11, 0, MT_HIF0 + 3	      },
		{ "HIF_Q4",  12, 0, MT_HIF0 + 4	      },
		{ "HIF_Q5",  13, 0, MT_HIF0 + 5	      },
		{ "LMAC_Q",  16, 2, 0		      },
		{ "MDP_TXQ", 17, 2, 1		      },
		{ "MDP_RXQ", 18, 2, 2		      },
		{ "SEC_TXQ", 19, 2, 3		      },
		{ "SEC_RXQ", 20, 2, 4		      },
	};
	u32 val, head, tail;

	/* ple queue */
	val = mt76_rr(dev, MT_PLE_FREEPG_CNT);
	head = mt76_get_field(dev, MT_PLE_FREEPG_HEAD_TAIL, GENMASK(11, 0));
	tail = mt76_get_field(dev, MT_PLE_FREEPG_HEAD_TAIL, GENMASK(27, 16));
	seq_puts(file, "PLE page info:\n");
	seq_printf(file,
		   "\tTotal free page: 0x%08x head: 0x%03x tail: 0x%03x\n",
		   val, head, tail);

	val = mt76_rr(dev, MT_PLE_PG_HIF_GROUP);
	head = mt76_get_field(dev, MT_PLE_HIF_PG_INFO, GENMASK(11, 0));
	tail = mt76_get_field(dev, MT_PLE_HIF_PG_INFO, GENMASK(27, 16));
	seq_printf(file, "\tHIF free page: 0x%03x res: 0x%03x used: 0x%03x\n",
		   val, head, tail);

	seq_puts(file, "PLE non-empty queue info:\n");
	mt7996_hw_queue_read(file, ARRAY_SIZE(ple_queue_map),
			     &ple_queue_map[0]);

	/* iterate per-sta ple queue */
	ieee80211_iterate_stations_atomic(phy->mt76->hw,
					  mt7996_sta_hw_queue_read, file);
	phy = mt7996_phy2(dev);
	if (phy)
		ieee80211_iterate_stations_atomic(phy->mt76->hw,
						  mt7996_sta_hw_queue_read, file);
	phy = mt7996_phy3(dev);
	if (phy)
		ieee80211_iterate_stations_atomic(phy->mt76->hw,
						  mt7996_sta_hw_queue_read, file);

	/* pse queue */
	seq_puts(file, "PSE non-empty queue info:\n");
	mt7996_hw_queue_read(file, ARRAY_SIZE(pse_queue_map),
			     &pse_queue_map[0]);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7996_hw_queues);

static int
mt7996_xmit_queues_show(struct seq_file *file, void *data)
{
	struct mt7996_phy *phy = file->private;
	struct mt7996_dev *dev = phy->dev;
	struct {
		struct mt76_queue *q;
		char *queue;
	} queue_map[] = {
		{ dev->mphy.q_tx[MT_TXQ_BE],	 "  MAIN0"  },
		{ NULL,				 "  MAIN1"  },
		{ NULL,				 "  MAIN2"  },
		{ dev->mt76.q_mcu[MT_MCUQ_WM],	 "  MCUWM"  },
		{ dev->mt76.q_mcu[MT_MCUQ_WA],	 "  MCUWA"  },
		{ dev->mt76.q_mcu[MT_MCUQ_FWDL], "MCUFWDL" },
	};
	int i;

	phy = mt7996_phy2(dev);
	if (phy)
		queue_map[1].q = phy->mt76->q_tx[MT_TXQ_BE];

	phy = mt7996_phy3(dev);
	if (phy)
		queue_map[2].q = phy->mt76->q_tx[MT_TXQ_BE];

	seq_puts(file, "     queue | hw-queued |      head |      tail |\n");
	for (i = 0; i < ARRAY_SIZE(queue_map); i++) {
		struct mt76_queue *q = queue_map[i].q;

		if (!q)
			continue;

		seq_printf(file, "   %s | %9d | %9d | %9d |\n",
			   queue_map[i].queue, q->queued, q->head,
			   q->tail);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7996_xmit_queues);

static int
mt7996_twt_stats(struct seq_file *s, void *data)
{
	struct mt7996_dev *dev = dev_get_drvdata(s->private);
	struct mt7996_twt_flow *iter;

	rcu_read_lock();

	seq_puts(s, "     wcid |       id |    flags |      exp | mantissa");
	seq_puts(s, " | duration |            tsf |\n");
	list_for_each_entry_rcu(iter, &dev->twt_list, list)
		seq_printf(s,
			   "%9d | %8d | %5c%c%c%c | %8d | %8d | %8d | %14lld |\n",
			   iter->wcid, iter->id,
			   iter->sched ? 's' : 'u',
			   iter->protection ? 'p' : '-',
			   iter->trigger ? 't' : '-',
			   iter->flowtype ? '-' : 'a',
			   iter->exp, iter->mantissa,
			   iter->duration, iter->tsf);

	rcu_read_unlock();

	return 0;
}

/* The index of RF registers use the generic regidx, combined with two parts:
 * WF selection [31:24] and offset [23:0].
 */
static int
mt7996_rf_regval_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;
	u32 regval;
	int ret;

	ret = mt7996_mcu_rf_regval(dev, dev->mt76.debugfs_reg, &regval, false);
	if (ret)
		return ret;

	*val = regval;

	return 0;
}

static int
mt7996_rf_regval_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;
	u32 val32 = val;

	return mt7996_mcu_rf_regval(dev, dev->mt76.debugfs_reg, &val32, true);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_rf_regval, mt7996_rf_regval_get,
			 mt7996_rf_regval_set, "0x%08llx\n");

static int
mt7996_fw_debug_muru_disable_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;

	dev->fw_debug_muru_disable = !!val;

	return 0;
}

static int
mt7996_fw_debug_muru_disable_get(void *data, u64 *val)
{
	struct mt7996_dev *dev = data;

	*val = dev->fw_debug_muru_disable;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_muru_disable,
			 mt7996_fw_debug_muru_disable_get,
			 mt7996_fw_debug_muru_disable_set, "%lld\n");

static int
mt7996_txpower_level_set(void *data, u64 val)
{
	struct mt7996_phy *phy = data;
	int ret;

	if (val > 100)
		return -EINVAL;

	ret = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_PERCENTAGE_CTRL, !!val);
	if (ret)
		return ret;

	return mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_PERCENTAGE_DROP_CTRL, val);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_txpower_level, NULL,
			 mt7996_txpower_level_set, "%lld\n");

static int
mt7996_scs_enable_set(void *data, u64 val)
{
	struct mt7996_phy *phy = data;
	return mt7996_mcu_set_scs(phy, (u8) val);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_scs_enable, NULL,
			 mt7996_scs_enable_set, "%lld\n");

static ssize_t
mt7996_get_txpower_info(struct file *file, char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7996_phy *phy = file->private_data;
	struct mt7996_mcu_txpower_event *event;
	struct txpower_basic_info *basic_info;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_hw *hw = mphy->hw;
	static const size_t size = 2048;
	int len = 0;
	ssize_t ret;
	char *buf;
	s8 single_nss_txpower;

	buf = kzalloc(size, GFP_KERNEL);
	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!buf || !event) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mt7996_mcu_get_tx_power_info(phy, BASIC_INFO, event);
	if (ret ||
	    le32_to_cpu(event->basic_info.category) != UNI_TXPOWER_BASIC_INFO)
		goto out;

	basic_info = &event->basic_info;

	len += scnprintf(buf + len, size - len,
			 "======================== BASIC INFO ========================\n");
	len += scnprintf(buf + len, size - len, "    Band Index: %d, Channel Band: %d\n",
			 basic_info->band_idx, basic_info->band);
	len += scnprintf(buf + len, size - len, "    PA Type: %s\n",
			 basic_info->is_epa ? "ePA" : "iPA");
	len += scnprintf(buf + len, size - len, "    LNA Type: %s\n",
			 basic_info->is_elna ? "eLNA" : "iLNA");

	len += scnprintf(buf + len, size - len,
			 "------------------------------------------------------------\n");
	len += scnprintf(buf + len, size - len, "    SKU: %s\n",
			 basic_info->sku_enable ? "enable" : "disable");
	len += scnprintf(buf + len, size - len, "    Percentage Control: %s\n",
			 basic_info->percentage_ctrl_enable ? "enable" : "disable");
	len += scnprintf(buf + len, size - len, "    Power Drop: %d [dBm]\n",
			 basic_info->power_drop_level >> 1);
	len += scnprintf(buf + len, size - len, "    Backoff: %s\n",
			 basic_info->bf_backoff_enable ? "enable" : "disable");
	len += scnprintf(buf + len, size - len, "    TX Front-end Loss:  %d, %d, %d, %d\n",
			 basic_info->front_end_loss_tx[0], basic_info->front_end_loss_tx[1],
			 basic_info->front_end_loss_tx[2], basic_info->front_end_loss_tx[3]);
	len += scnprintf(buf + len, size - len, "    RX Front-end Loss:  %d, %d, %d, %d\n",
			 basic_info->front_end_loss_rx[0], basic_info->front_end_loss_rx[1],
			 basic_info->front_end_loss_rx[2], basic_info->front_end_loss_rx[3]);
	len += scnprintf(buf + len, size - len,
			 "    MU TX Power Mode:  %s\n",
			 basic_info->mu_tx_power_manual_enable ? "manual" : "auto");
	len += scnprintf(buf + len, size - len,
			 "    MU TX Power (Auto / Manual): %d / %d [0.5 dBm]\n",
			 basic_info->mu_tx_power_auto, basic_info->mu_tx_power_manual);
	len += scnprintf(buf + len, size - len,
			 "    Thermal Compensation:  %s\n",
			 basic_info->thermal_compensate_enable ? "enable" : "disable");
	len += scnprintf(buf + len, size - len,
			 "    Thermal Compensation Value: %d\n",
			 basic_info->thermal_compensate_value);

	len += scnprintf(buf + len, size - len,
			 "    PHY Power Bound: %d\n",
			 mt7996_get_power_bound(mphy, hw->conf.power_level, &single_nss_txpower));
	len += scnprintf(buf + len, size - len,
			 "    HW Conf Power Level: %d\n",
			 hw->conf.power_level);
	len += scnprintf(buf + len, size - len,
			 "    Per-Chain TX-Power Cur: %d 1/2dB\n",
			 mphy->txpower_cur);
	len += scnprintf(buf + len, size - len,
			 "    PHY tx-front-end-loss: %d\n",
			 phy->tx_front_end_loss);
	len += scnprintf(buf + len, size - len,
			 "    PHY tx-front-end-loss-acquired: %d\n",
			 phy->tx_front_end_loss_acquired);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

out:
	kfree(buf);
	kfree(event);
	return ret;
}

static const struct file_operations mt7996_txpower_info_fops = {
	.read = mt7996_get_txpower_info,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define mt7996_txpower_puts(rate)							\
({											\
	len += scnprintf(buf + len, size - len, "%-21s:", #rate " (TMAC)");		\
	for (i = 0; i < mt7996_sku_group_len[SKU_##rate]; i++, offs++)			\
		len += scnprintf(buf + len, size - len, " %6d",				\
				 event->phy_rate_info.frame_power[offs][band_idx]);	\
	len += scnprintf(buf + len, size - len, "\n");					\
})

static ssize_t
__mt7996_get_txpower_sku(struct file *file, char __user *user_buf,
			 size_t count, loff_t *ppos, struct mt7996_mcu_txpower_event *event,
			 char* buf, size_t size)
{
	struct mt7996_phy *phy = file->private_data;
	struct mt7996_dev *dev = phy->dev;
	u8 band_idx = phy->mt76->band_idx;
	int i, offs = 0, len = 0;
	ssize_t ret;
	u32 reg;

	len += scnprintf(buf + len, size - len,
			 "\nPhy %d TX Power Table (Channel %d)\n",
			 band_idx, phy->mt76->chandef.chan->hw_value);
	len += scnprintf(buf + len, size - len, "%-21s  %6s %6s %6s %6s\n",
			 " ", "1m", "2m", "5m", "11m");
	mt7996_txpower_puts(CCK);

	len += scnprintf(buf + len, size - len,
			 "%-21s  %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 " ", "6m", "9m", "12m", "18m", "24m", "36m", "48m",
			 "54m");
	mt7996_txpower_puts(OFDM);

	len += scnprintf(buf + len, size - len,
			 "%-21s  %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4",
			 "mcs5", "mcs6", "mcs7");
	mt7996_txpower_puts(HT20);

	len += scnprintf(buf + len, size - len,
			 "%-21s  %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
			 "mcs6", "mcs7", "mcs32");
	mt7996_txpower_puts(HT40);

	len += scnprintf(buf + len, size - len,
			 "%-21s  %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
			 "mcs6", "mcs7", "mcs8", "mcs9", "mcs10", "mcs11");
	mt7996_txpower_puts(VHT20);
	mt7996_txpower_puts(VHT40);
	mt7996_txpower_puts(VHT80);
	mt7996_txpower_puts(VHT160);
	mt7996_txpower_puts(HE26);
	mt7996_txpower_puts(HE52);
	mt7996_txpower_puts(HE106);
	mt7996_txpower_puts(HE242);
	mt7996_txpower_puts(HE484);
	mt7996_txpower_puts(HE996);
	mt7996_txpower_puts(HE2x996);

	len += scnprintf(buf + len, size - len,
			 "%-21s  %6s %6s %6s %6s %6s %6s %6s %6s ",
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5", "mcs6", "mcs7");
	len += scnprintf(buf + len, size - len,
			 "%6s %6s %6s %6s %6s %6s %6s %6s\n",
			 "mcs8", "mcs9", "mcs10", "mcs11", "mcs12", "mcs13", "mcs14", "mcs15");
	mt7996_txpower_puts(EHT26);
	mt7996_txpower_puts(EHT52);
	mt7996_txpower_puts(EHT106);
	mt7996_txpower_puts(EHT242);
	mt7996_txpower_puts(EHT484);
	mt7996_txpower_puts(EHT996);
	mt7996_txpower_puts(EHT2x996);
	mt7996_txpower_puts(EHT4x996);
	mt7996_txpower_puts(EHT26_52);
	mt7996_txpower_puts(EHT26_106);
	mt7996_txpower_puts(EHT484_242);
	mt7996_txpower_puts(EHT996_484);
	mt7996_txpower_puts(EHT996_484_242);
	mt7996_txpower_puts(EHT2x996_484);
	mt7996_txpower_puts(EHT3x996);
	mt7996_txpower_puts(EHT3x996_484);

	len += scnprintf(buf + len, size - len, "\nePA Gain: %d\n",
			 event->phy_rate_info.epa_gain);
	len += scnprintf(buf + len, size - len, "Max Power Bound: %d\n",
			 event->phy_rate_info.max_power_bound);
	len += scnprintf(buf + len, size - len, "Min Power Bound: %d\n",
			 event->phy_rate_info.min_power_bound);

	reg = MT_WF_PHYDFE_BAND_TPC_CTRL_STAT0(band_idx);
	len += scnprintf(buf + len, size - len,
			 "BBP TX Power (target power from TMAC)  : %6ld [0.5 dBm]\n",
			 mt76_get_field(dev, reg, MT_WF_PHY_TPC_POWER_TMAC));
	len += scnprintf(buf + len, size - len,
			 "BBP TX Power (target power from RMAC)  : %6ld [0.5 dBm]\n",
			 mt76_get_field(dev, reg, MT_WF_PHY_TPC_POWER_RMAC));
	len += scnprintf(buf + len, size - len,
			 "BBP TX Power (TSSI module power input)  : %6ld [0.5 dBm]\n",
			 mt76_get_field(dev, reg, MT_WF_PHY_TPC_POWER_TSSI));

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	return ret;
}

static ssize_t
mt7996_get_txpower_sku(struct file *file, char __user *user_buf,
		       size_t count, loff_t *ppos)
{
	struct mt7996_phy *phy = file->private_data;
	struct mt7996_mcu_txpower_event *event;
	static const size_t size = 5120;
	char *buf;
	int ret;

	buf = kzalloc(size, GFP_KERNEL);
	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!buf || !event) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mt7996_mcu_get_tx_power_info(phy, PHY_RATE_INFO, event);
	if (ret ||
	    le32_to_cpu(event->phy_rate_info.category) != UNI_TXPOWER_PHY_RATE_INFO)
		goto out;

	ret = __mt7996_get_txpower_sku(file, user_buf, count, ppos, event, buf, size);

out:
	kfree(buf);
	kfree(event);

	return ret;
}

static const struct file_operations mt7996_txpower_sku_fops = {
	.read = mt7996_get_txpower_sku,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t
mt7996_get_txpower_default(struct file *file, char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct mt7996_phy *phy = file->private_data;
	static const size_t size = 5120;
	char *buf;
	int ret;
	int len = 0;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	if (phy->default_txpower) {
		ret = __mt7996_get_txpower_sku(file, user_buf, count, ppos, phy->default_txpower, buf, size);
	}
	else {
		len += scnprintf(buf + len, size - len, "ERROR:  default_txpower is NULL\n");
		ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	}

out:
	kfree(buf);

	return ret;
}

static const struct file_operations mt7996_txpower_default_fops = {
	.read = mt7996_get_txpower_default,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define mt7996_txpower_path_puts(rate, arr_length)					\
({											\
	len += scnprintf(buf + len, size - len, "%-23s:", #rate " (TMAC)");		\
	for (i = 0; i < arr_length; i++, offs++)					\
		len += scnprintf(buf + len, size - len, " %4d",				\
				 event->backoff_table_info.frame_power[offs]);		\
	len += scnprintf(buf + len, size - len, "\n");					\
})

static ssize_t
mt7996_get_txpower_path(struct file *file, char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7996_phy *phy = file->private_data;
	struct mt7996_mcu_txpower_event *event;
	static const size_t size = 5120;
	int i, offs = 0, len = 0;
	ssize_t ret;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!buf || !event) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mt7996_mcu_get_tx_power_info(phy, BACKOFF_TABLE_INFO, event);
	if (ret ||
	    le32_to_cpu(event->phy_rate_info.category) != UNI_TXPOWER_BACKOFF_TABLE_SHOW_INFO)
		goto out;

	len += scnprintf(buf + len, size - len, "\n%*c", 25, ' ');
	len += scnprintf(buf + len, size - len, "1T1S/2T1S/3T1S/4T1S/5T1S/2T2S/3T2S/4T2S/5T2S/"
			 "3T3S/4T3S/5T3S/4T4S/5T4S/5T5S\n");

	mt7996_txpower_path_puts(CCK, 5);
	mt7996_txpower_path_puts(OFDM, 5);
	mt7996_txpower_path_puts(BF-OFDM, 4);

	mt7996_txpower_path_puts(RU26, 15);
	mt7996_txpower_path_puts(BF-RU26, 15);
	mt7996_txpower_path_puts(RU52, 15);
	mt7996_txpower_path_puts(BF-RU52, 15);
	mt7996_txpower_path_puts(RU26_52, 15);
	mt7996_txpower_path_puts(BF-RU26_52, 15);
	mt7996_txpower_path_puts(RU106, 15);
	mt7996_txpower_path_puts(BF-RU106, 15);
	mt7996_txpower_path_puts(RU106_52, 15);
	mt7996_txpower_path_puts(BF-RU106_52, 15);

	mt7996_txpower_path_puts(BW20/RU242, 15);
	mt7996_txpower_path_puts(BF-BW20/RU242, 15);
	mt7996_txpower_path_puts(BW40/RU484, 15);
	mt7996_txpower_path_puts(BF-BW40/RU484, 15);
	mt7996_txpower_path_puts(RU242_484, 15);
	mt7996_txpower_path_puts(BF-RU242_484, 15);
	mt7996_txpower_path_puts(BW80/RU996, 15);
	mt7996_txpower_path_puts(BF-BW80/RU996, 15);
	mt7996_txpower_path_puts(RU484_996, 15);
	mt7996_txpower_path_puts(BF-RU484_996, 15);
	mt7996_txpower_path_puts(RU242_484_996, 15);
	mt7996_txpower_path_puts(BF-RU242_484_996, 15);
	mt7996_txpower_path_puts(BW160/RU996x2, 15);
	mt7996_txpower_path_puts(BF-BW160/RU996x2, 15);
	mt7996_txpower_path_puts(RU484_996x2, 15);
	mt7996_txpower_path_puts(BF-RU484_996x2, 15);
	mt7996_txpower_path_puts(RU996x3, 15);
	mt7996_txpower_path_puts(BF-RU996x3, 15);
	mt7996_txpower_path_puts(RU484_996x3, 15);
	mt7996_txpower_path_puts(BF-RU484_996x3, 15);
	mt7996_txpower_path_puts(BW320/RU996x4, 15);
	mt7996_txpower_path_puts(BF-BW320/RU996x4, 15);

	len += scnprintf(buf + len, size - len, "\nBackoff table: %s\n",
			 event->backoff_table_info.backoff_en ? "enable" : "disable");

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

out:
	kfree(buf);
	kfree(event);
	return ret;
}

static const struct file_operations mt7996_txpower_path_fops = {
	.read = mt7996_get_txpower_path,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int
mt7996_sr_enable_get(void *data, u64 *val)
{
	struct mt7996_phy *phy = data;

	*val = phy->sr_enable;

	return 0;
}

static int
mt7996_sr_enable_set(void *data, u64 val)
{
	struct mt7996_phy *phy = data;
	int ret;

	if (!!val == phy->sr_enable)
		return 0;

	ret = mt7996_mcu_set_sr_enable(phy, UNI_CMD_SR_CFG_SR_ENABLE, val, true);
	if (ret)
		return ret;

	return mt7996_mcu_set_sr_enable(phy, UNI_CMD_SR_CFG_SR_ENABLE, 0, false);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_sr_enable, mt7996_sr_enable_get,
			 mt7996_sr_enable_set, "%lld\n");

static int
mt7996_adjust_txp_by_loss_get(void *data, u64 *val)
{
	struct mt7996_phy *phy = data;

	*val = phy->adjust_txp_by_loss;

	return 0;
}

static int
mt7996_adjust_txp_by_loss_set(void *data, u64 val)
{
	struct mt7996_phy *phy = data;

	if (!!val == phy->adjust_txp_by_loss)
		return 0;

	phy->adjust_txp_by_loss = val;
	return mt7996_mcu_set_txpower_sku(phy);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_adjust_txp_by_loss, mt7996_adjust_txp_by_loss_get,
			 mt7996_adjust_txp_by_loss_set, "%lld\n");

static int
mt7996_sr_enhanced_enable_get(void *data, u64 *val)
{
	struct mt7996_phy *phy = data;

	*val = phy->enhanced_sr_enable;

	return 0;
}

static int
mt7996_sr_enhanced_enable_set(void *data, u64 val)
{
	struct mt7996_phy *phy = data;
	int ret;

	if (!!val == phy->enhanced_sr_enable)
		return 0;

	ret = mt7996_mcu_set_sr_enable(phy, UNI_CMD_SR_HW_ENHANCE_SR_ENABLE, val, true);
	if (ret)
		return ret;

	return mt7996_mcu_set_sr_enable(phy, UNI_CMD_SR_HW_ENHANCE_SR_ENABLE, 0, false);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_sr_enhanced_enable, mt7996_sr_enhanced_enable_get,
			 mt7996_sr_enhanced_enable_set, "%lld\n");

static int
mt7996_sr_stats_show(struct seq_file *file, void *data)
{
	struct mt7996_phy *phy = file->private;

	mt7996_mcu_set_sr_enable(phy, UNI_CMD_SR_HW_IND, 0, false);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_sr_stats);

static int
mt7996_sr_scene_cond_show(struct seq_file *file, void *data)
{
	struct mt7996_phy *phy = file->private;

	return mt7996_mcu_set_sr_enable(phy, UNI_CMD_SR_SW_SD, 0, false);
}
DEFINE_SHOW_ATTRIBUTE(mt7996_sr_scene_cond);

static int
mt7996_vow_info_read(struct seq_file *s, void *data)
{
	struct mt7996_dev *dev = dev_get_drvdata(s->private);
	struct mt7996_vow_ctrl *vow = &dev->vow;
	int i;

	seq_printf(s, "VoW ATF Configuration:\n");
	seq_printf(s, "ATF: %s\n", vow->atf_enable ? "enabled" : "disabled");
	seq_printf(s, "WATF: %s\n", vow->watf_enable ? "enabled" : "disabled");
	seq_printf(s, "Airtime Quantums (unit: 256 us)\n");
	for (i = 0; i < VOW_DRR_QUANTUM_NUM; ++i)
		seq_printf(s, "\tL%d: %hhu\n", i, vow->drr_quantum[i]);
	seq_printf(s, "Max Airtime Deficit: %hhu (unit: 256 us)\n", vow->max_deficit);

	return 0;
}

static int
mt7996_atf_enable_get(void *data, u64 *val)
{
	struct mt7996_phy *phy = data;

	*val = phy->dev->vow.atf_enable;

	return 0;
}

static int
mt7996_atf_enable_set(void *data, u64 val)
{
	struct mt7996_phy *phy = data;
	struct mt7996_vow_ctrl *vow = &phy->dev->vow;
	int ret;

	vow->max_deficit = val ? 64 : 1;
	ret = mt7996_mcu_set_vow_drr_ctrl(phy, NULL, NULL, VOW_DRR_CTRL_AIRTIME_DEFICIT_BOUND);
	if (ret)
		return ret;

	vow->atf_enable = !!val;
	return mt7996_mcu_set_vow_feature_ctrl(phy);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_atf_enable, mt7996_atf_enable_get,
	                 mt7996_atf_enable_set, "%llu\n");

static int
mt7996_red_config_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;

	return mt7996_mcu_red_config(dev, !!val);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_red_config, NULL,
			 mt7996_red_config_set, "%lld\n");

static int
mt7996_airtime_read(struct seq_file *s, void *data)
{
	struct mt7996_dev *dev = dev_get_drvdata(s->private);
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_sta_stats *stats;
	struct ieee80211_sta *sta;
	struct mt7996_sta_link *msta_link;
	struct mt76_wcid *wcid;
	struct mt76_vif_link *vif;
	u16 i;

	seq_printf(s, "VoW Airtime Information:\n");
	rcu_read_lock();
	for (i = 1; i < MT7996_WTBL_STA; ++i) {
		wcid = rcu_dereference(mdev->wcid[i]);
		if (!wcid || !wcid->sta)
			continue;

		msta_link = container_of(wcid, struct mt7996_sta_link, wcid);
		sta = container_of((void *)msta_link->sta, struct ieee80211_sta, drv_priv);
		vif = &msta_link->sta->vif->deflink.mt76;
		stats = &wcid->stats;

		seq_printf(s, "%pM WCID: %hu BandIdx: %hhu OmacIdx: 0x%hhx\t"
		              "TxAirtime: %llu\tRxAirtime: %llu\n",
		              sta->addr, i, vif->band_idx, vif->omac_idx,
		              stats->tx_airtime, stats->rx_airtime);

		stats->tx_airtime = 0;
		stats->rx_airtime = 0;
	}
	rcu_read_unlock();

	return 0;
}

static int
mt7996_vow_drr_dbg(void *data, u64 val)
{
	struct mt7996_dev *dev = data;

	return mt7996_mcu_set_vow_drr_dbg(dev, (u32)val);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_vow_drr_dbg, NULL,
			 mt7996_vow_drr_dbg, "%lld\n");

static int
mt7996_muru_fixed_rate_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;

	return mt7996_mcu_set_muru_fixed_rate_enable(dev, UNI_CMD_MURU_FIXED_RATE_CTRL,
						     val);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_muru_fixed_rate_enable, NULL,
			 mt7996_muru_fixed_rate_set, "%lld\n");

static ssize_t
mt7996_muru_fixed_rate_parameter_set(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct mt7996_dev *dev = file->private_data;
	char buf[40];
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';


	ret = mt7996_mcu_set_muru_fixed_rate_parameter(dev, UNI_CMD_MURU_FIXED_GROUP_RATE_CTRL,
						       buf);

	if (ret) return -EFAULT;

	return count;
}

static const struct file_operations fops_muru_fixed_group_rate = {
	.write = mt7996_muru_fixed_rate_parameter_set,
	.read = NULL,
	.open = simple_open,
	.llseek = default_llseek,
};

static int mt7996_muru_prot_thr_set(void *data, u64 val)
{
	struct mt7996_dev *dev = data;

	return mt7996_mcu_muru_set_prot_frame_thr(dev, (u32)val);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_muru_prot_thr, NULL,
			 mt7996_muru_prot_thr_set, "%lld\n");


static ssize_t mt7996_muru_dbg_info_set(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct mt7996_dev *dev = file->private_data;
	char buf[10];
	u16 item;
	u8 val;
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hu-%hhu", &item, &val) != 2) {
		dev_warn(dev->mt76.dev,"format: item-value\n");
		return -EINVAL;
	}

	ret = mt7996_mcu_muru_dbg_info(dev, item, val);
	if (ret) {
		dev_warn(dev->mt76.dev, "Fail to send mcu cmd.\n");
		return -EFAULT;
	}

	return count;
}

static const struct file_operations fops_muru_dbg_info = {
	.write = mt7996_muru_dbg_info_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int
mt7996_thermal_enable_get(void *data, u64 *enable)
{
	struct mt7996_phy *phy = data;

	*enable = phy->thermal_protection_enable;

	return 0;
}

static int
mt7996_thermal_enable_set(void *data, u64 action)
{
	struct mt7996_phy *phy = data;
	int ret;
	u8 throttling;

	if (action > 1)
		return -EINVAL;

	if (!!action == phy->thermal_protection_enable)
		return 0;

	ret = mt7996_mcu_set_thermal_protect(phy, !!action);
	if (ret)
		return ret;

	if (!!!action)
		goto out;

	throttling = MT7996_THERMAL_THROTTLE_MAX - phy->cdev_state;
	ret = mt7996_mcu_set_thermal_throttling(phy, throttling);
	if (ret)
		return ret;

out:
	phy->thermal_protection_enable = !!action;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_thermal_enable, mt7996_thermal_enable_get,
			 mt7996_thermal_enable_set, "%lld\n");

static int mt7996_pp_alg_show(struct seq_file *s, void *data)
{
	struct mt7996_phy *phy = s->private;
	struct mt7996_dev *dev = phy->dev;

	dev_info(dev->mt76.dev, "pp_mode = %d\n", phy->pp_mode);
	mt7996_mcu_set_pp_alg_ctrl(phy, PP_ALG_GET_STATISTICS);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_pp_alg);

int mt7996_init_band_debugfs(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	struct dentry *dir;
	char dir_name[10];

	if (!dev->debugfs_dir)
		return -EINVAL;

	snprintf(dir_name, sizeof(dir_name), "band%d", phy->mt76->band_idx);

	dir = debugfs_create_dir(dir_name, dev->debugfs_dir);
	if (!dir)
		return -ENOMEM;

	debugfs_create_file("hw-queues", 0400, dir, phy,
			    &mt7996_hw_queues_fops);
	debugfs_create_file("xmit-queues", 0400, dir, phy,
			    &mt7996_xmit_queues_fops);
	debugfs_create_file("sys_recovery", 0600, dir, phy,
			    &mt7996_sys_recovery_ops);
	debugfs_create_file("atf_enable", 0600, dir, phy, &fops_atf_enable);
	debugfs_create_file("tx_stats", 0400, dir, phy, &mt7996_tx_stats_fops);
	debugfs_create_file("red", 0200, dir, dev, &fops_red_config);
	debugfs_create_file("vow_drr_dbg", 0200, dir, dev, &fops_vow_drr_dbg);
	debugfs_create_file("muru_prot_thr", 0200, dir, dev, &fops_muru_prot_thr);
	debugfs_create_file("muru_fixed_rate_enable", 0600, dir, dev,
			    &fops_muru_fixed_rate_enable);
	debugfs_create_file("muru_fixed_group_rate", 0600, dir, dev,
			    &fops_muru_fixed_group_rate);
	debugfs_create_file("muru_dbg", 0200, dir, dev, &fops_muru_dbg_info);
	debugfs_create_file("rxfilter", 0400, dir, phy, &mt7996_rxfilter_fops);
	debugfs_create_file("sr_enable", 0600, dir, phy, &fops_sr_enable);
	debugfs_create_file("sr_stats", 0400, dir, phy, &mt7996_sr_stats_fops);
	debugfs_create_file("sr_enhanced_enable", 0600, dir, phy, &fops_sr_enhanced_enable);
	debugfs_create_file("sr_scene_cond", 0400, dir, phy, &mt7996_sr_scene_cond_fops);
	if (phy->mt76->cap.has_5ghz) {
		debugfs_create_u32("dfs_hw_pattern", 0400, dir,
				   &dev->hw_pattern);
		debugfs_create_file("radar_trigger", 0200, dir, dev,
				    &fops_radar_trigger);
		debugfs_create_devm_seqfile(dev->mt76.dev, "rdd_monitor", dir,
					    mt7996_rdd_monitor);
	}

	debugfs_create_file("txpower_level", 0600, dir, phy, &fops_txpower_level);
	debugfs_create_file("txpower_info", 0600, dir, phy, &mt7996_txpower_info_fops);
	debugfs_create_file("txpower_sku", 0600, dir, phy, &mt7996_txpower_sku_fops);
	debugfs_create_file("txpower_default", 0600, dir, phy, &mt7996_txpower_default_fops);
	debugfs_create_file("txpower_path", 0600, dir, phy, &mt7996_txpower_path_fops);
	debugfs_create_file("adjust_txp_by_loss", 0600, dir, phy, &fops_adjust_txp_by_loss);

	debugfs_create_file("thermal_enable", 0600, dir, phy, &fops_thermal_enable);
	debugfs_create_file("scs_enable", 0200, dir, phy, &fops_scs_enable);

	if (!is_mt7996(&dev->mt76)) {
		debugfs_create_file("mru_probe_enable", 0600, dir, phy,
				    &fops_mru_probe_enable);
	}

	debugfs_create_file("pp_alg", 0200, dir, phy, &mt7996_pp_alg_fops);
	debugfs_create_file("radar_trigger", 0200, dir, phy,
			    &fops_radar_trigger);

#ifdef CONFIG_MTK_DEBUG
	mt7996_mtk_init_band_debugfs(phy, dir);
	mt7996_mtk_init_band_debugfs_internal(phy, dir);
#endif
	return 0;
}

int mt7996_init_dev_debugfs(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	struct dentry *dir;

	dir = mt76_register_debugfs_fops(phy->mt76, NULL);
	if (!dir)
		return -ENOMEM;
	debugfs_create_file("fw_debug_wm", 0600, dir, dev, &fops_fw_debug_wm);
	debugfs_create_file("fw_debug_wa", 0600, dir, dev, &fops_fw_debug_wa);
	debugfs_create_file("fw_debug_bin", 0600, dir, dev, &fops_fw_debug_bin);
	debugfs_create_file("idxlog_enable", 0600, dir, dev, &fops_idxlog_enable);

	if (!is_mt7996(&dev->mt76)) {
		debugfs_create_file("sr_pp_enable", 0600, dir, dev,
				    &fops_sr_pp_enable);
		debugfs_create_file("uba_enable", 0600, dir, dev, &fops_uba_enable);
	}
	/* TODO: wm fw cpu utilization */
	debugfs_create_file("fw_util_wa", 0400, dir, dev,
			    &mt7996_fw_util_wa_fops);
	debugfs_create_file("rx_group_5_enable", 0600, dir, dev, &fops_rx_group_5_enable);
	debugfs_create_file("implicit_txbf", 0600, dir, dev,
			    &fops_implicit_txbf);
	debugfs_create_devm_seqfile(dev->mt76.dev, "twt_stats", dir,
				    mt7996_twt_stats);
	debugfs_create_file("rf_regval", 0600, dir, dev, &fops_rf_regval);
	debugfs_create_devm_seqfile(dev->mt76.dev, "vow_info", dir,
	                            mt7996_vow_info_read);
	debugfs_create_devm_seqfile(dev->mt76.dev, "airtime", dir,
	                            mt7996_airtime_read);
	debugfs_create_file("fw_debug_muru_disable", 0600, dir, dev,
			    &fops_fw_debug_muru_disable);

	debugfs_create_u32("ignore_radar", 0600, dir,
			   &dev->ignore_radar);
	debugfs_create_file("set_rate_override", 0600, dir,
			    dev, &fops_set_rate_override);

	debugfs_create_file("phy_info", 0400, dir, dev, &mt7996_phy_info_fops);

	debugfs_create_bool("mgmt_pwr_enhance", 0600, dir, &dev->mt76.mgmt_pwr_enhance);

	debugfs_create_u32("dfs_hw_pattern", 0400, dir, &dev->hw_pattern);
	debugfs_create_devm_seqfile(dev->mt76.dev, "rdd_monitor", dir,
				    mt7996_rdd_monitor);

	if (phy == &dev->phy) {
		dev->debugfs_dir = dir;
#ifdef CONFIG_MTK_DEBUG
		mt7996_mtk_init_dev_debugfs_internal(dev, dir);
#endif
	}
#ifdef CONFIG_MTK_DEBUG
	debugfs_create_u16("wlan_idx", 0600, dir, &dev->wlan_idx);
	mt7996_mtk_init_dev_debugfs(dev, dir);
#endif

	return 0;
}

static void
mt7996_debugfs_write_fwlog(struct mt7996_dev *dev, const void *hdr, int hdrlen,
			   const void *data, int len)
{
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;
	void *dest;

	if (!dev->relay_fwlog)
		return;

	spin_lock_irqsave(&lock, flags);
	dest = relay_reserve(dev->relay_fwlog, hdrlen + len + 4);
	if (dest) {
		*(u32 *)dest = hdrlen + len;
		dest += 4;

		if (hdrlen) {
			memcpy(dest, hdr, hdrlen);
			dest += hdrlen;
		}

		memcpy(dest, data, len);
		relay_flush(dev->relay_fwlog);
	}
	spin_unlock_irqrestore(&lock, flags);
}

static void
mt7996_debugfs_write_idxlog(struct mt7996_dev *dev, const void *data, int len)
{
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;
	void *dest;

	if (!dev->relay_idxlog)
		return;

	spin_lock_irqsave(&lock, flags);

	dest = relay_reserve(dev->relay_idxlog, len + 4);
	if (!dest)
		dev_err(dev->mt76.dev, "Failed to reserve slot in %s\n",
		        dev->relay_idxlog->base_filename);
	else {
		*(u32 *)dest = len;
		dest += 4;
		memcpy(dest, data, len);
		relay_flush(dev->relay_idxlog);
	}

	spin_unlock_irqrestore(&lock, flags);
}

void mt7996_debugfs_rx_fw_monitor(struct mt7996_dev *dev, const void *data, int len)
{
	struct {
		__le32 magic;
		u8 version;
		u8 _rsv;
		__le16 serial_id;
		__le32 timestamp;
		__le16 msg_type;
		__le16 len;
	} hdr = {
		.version = 0x1,
		.magic = cpu_to_le32(FW_BIN_LOG_MAGIC),
		.msg_type = cpu_to_le16(PKT_TYPE_RX_FW_MONITOR),
	};

	if (!dev->relay_fwlog)
		return;

	hdr.serial_id = cpu_to_le16(dev->fw_debug_seq++);
	hdr.timestamp = cpu_to_le32(mt76_rr(dev, MT_LPON_FRCR(0)));
	hdr.len = *(__le16 *)data;
	mt7996_debugfs_write_fwlog(dev, &hdr, sizeof(hdr), data, len);
}

bool mt7996_debugfs_rx_log(struct mt7996_dev *dev, const void *data, int len)
{
	bool is_fwlog = get_unaligned_le32(data) == FW_BIN_LOG_MAGIC;
	is_fwlog |= get_unaligned_le32(data) == PKT_BIN_DEBUG_MAGIC;

	if (is_fwlog) {
		if (dev->relay_fwlog)
			mt7996_debugfs_write_fwlog(dev, NULL, 0, data, len);
	} else if (dev->relay_idxlog)
		mt7996_debugfs_write_idxlog(dev, data, len);
	else
		return false;

	return true;
}

#ifdef CONFIG_MAC80211_DEBUGFS
/** per-station debugfs **/

static ssize_t mt7996_sta_fixed_rate_set(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
#define SHORT_PREAMBLE 0
#define LONG_PREAMBLE 1
	struct ieee80211_sta *sta = file->private_data;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = msta->vif->deflink.phy->dev;
	struct mt7996_sta_link *msta_link = &msta->deflink;
	struct ra_rate phy = {};
	char buf[100];
	int ret;
	u16 gi, ltf;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	/* mode - cck: 0, ofdm: 1, ht: 2, gf: 3, vht: 4, he_su: 8, he_er: 9 EHT: 15
	 * bw - bw20: 0, bw40: 1, bw80: 2, bw160: 3, BW320: 4
	 * nss - vht: 1~4, he: 1~4, eht: 1~4, others: ignore
	 * mcs - cck: 0~4, ofdm: 0~7, ht: 0~32, vht: 0~9, he_su: 0~11, he_er: 0~2, eht: 0~13
	 * gi - (ht/vht) lgi: 0, sgi: 1; (he) 0.8us: 0, 1.6us: 1, 3.2us: 2
	 * preamble - short: 1, long: 0
	 * ldpc - off: 0, on: 1
	 * stbc - off: 0, on: 1
	 * ltf - 1xltf: 0, 2xltf: 1, 4xltf: 2
	 */
	if (sscanf(buf, "%hhu %hhu %hhu %hhu %hu %hhu %hhu %hhu %hhu %hu",
		   &phy.mode, &phy.bw, &phy.mcs, &phy.nss, &gi,
		   &phy.preamble, &phy.stbc, &phy.ldpc, &phy.spe, &ltf) != 10) {
		dev_warn(dev->mt76.dev,
			 "format: Mode BW MCS NSS GI Preamble STBC LDPC SPE ltf\n");
		goto out;
	}

	phy.wlan_idx = cpu_to_le16(msta_link->wcid.idx);
	phy.gi = cpu_to_le16(gi);
	phy.ltf = cpu_to_le16(ltf);
	phy.ldpc = phy.ldpc ? 7 : 0;
	phy.preamble = phy.preamble ? SHORT_PREAMBLE : LONG_PREAMBLE;

	ret = mt7996_mcu_set_fixed_rate_ctrl(dev, &phy, 0);
	if (ret)
		return -EFAULT;

out:
	return count;
}

static const struct file_operations fops_fixed_rate = {
	.write = mt7996_sta_fixed_rate_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int
mt7996_queues_show(struct seq_file *s, void *data)
{
	struct ieee80211_sta *sta = s->private;

	mt7996_sta_hw_queue_read(s, sta);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_queues);

static int
mt7996_sta_links_info_show(struct seq_file *s, void *data)
{
	struct ieee80211_sta *sta = s->private;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	u64 tx_cnt = 0, tx_fails = 0, tx_retries = 0, rx_cnt = 0;
	struct mt7996_dev *dev = msta->vif->dev;
	unsigned long valid_links;
	u8 link_id;

	seq_printf(s, "primary link, link ID = %d\n", msta->deflink_id);
	seq_printf(s, "secondary link, link ID = %d\n", msta->sec_link);
	seq_printf(s, "valid links = 0x%x\n", sta->valid_links);

	mutex_lock(&dev->mt76.mutex);
	valid_links = sta->valid_links ?: BIT(0);
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_sta_link *msta_link =
			mt76_dereference(msta->link[link_id], &dev->mt76);
		struct mt76_wcid *wcid;

		if (!msta_link)
			continue;

		wcid = &msta_link->wcid;

		tx_cnt += wcid->stats.tx_attempts;
		tx_fails += wcid->stats.tx_failed;
		tx_retries += wcid->stats.tx_retries;
		rx_cnt += wcid->stats.rx_packets;

		seq_printf(s, "link%d: wcid=%d, phy=%d, link_valid=%d, ampdu_state=0x%lx\n",
			    wcid->link_id, wcid->idx, wcid->phy_idx, wcid->link_valid,
			    wcid->ampdu_state);
	}
	mutex_unlock(&dev->mt76.mutex);

	/* PER may be imprecise, because MSDU total and failed counts
	 * are updated at different times.
	 */
	seq_printf(s, "TX MSDU Count: %llu\n", tx_cnt);
	seq_printf(s, "TX MSDU Fails: %llu (PER: %llu.%llu%%)\n", tx_fails,
		   tx_cnt ? tx_fails * 1000 / tx_cnt / 10 : 0,
		   tx_cnt ? tx_fails * 1000 / tx_cnt % 10 : 0);
	seq_printf(s, "TX MSDU Retries: %llu\n", tx_retries);
	seq_printf(s, "RX MSDU Count: %llu\n", rx_cnt);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_sta_links_info);

void mt7996_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir)
{
	debugfs_create_file("fixed_rate", 0600, dir, sta, &fops_fixed_rate);
	debugfs_create_file("hw-queues", 0400, dir, sta, &mt7996_queues_fops);
	debugfs_create_file("mt76_links_info", 0400, dir, sta,
			    &mt7996_sta_links_info_fops);
}

static int
mt7996_vif_links_info_show(struct seq_file *s, void *data)
{
	struct ieee80211_vif *vif = s->private;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = mvif->dev;
	struct mt7996_vif_link *mconf;
	struct mt7996_sta_link *msta_link;
	unsigned long valid_links;
	u8 link_id, i;

	static const char* width_to_bw[] = {
		[NL80211_CHAN_WIDTH_40] = "40",
		[NL80211_CHAN_WIDTH_80] = "80",
		[NL80211_CHAN_WIDTH_80P80] = "80+80",
		[NL80211_CHAN_WIDTH_160] = "160",
		[NL80211_CHAN_WIDTH_5] = "5",
		[NL80211_CHAN_WIDTH_10] = "10",
		[NL80211_CHAN_WIDTH_20] = "20",
		[NL80211_CHAN_WIDTH_20_NOHT] = "20_NOHT",
		[NL80211_CHAN_WIDTH_320] = "320",
	};

	seq_printf(s, "master link id = %d\n", mvif->mt76.deflink_id);
	seq_printf(s, "group mld id = %d\n", mvif->group_mld_id);
	seq_printf(s, "mld remap id = %d\n", mvif->mld_remap_id);

	seq_printf(s, "valid links = 0x%x\n", vif->valid_links);
	for (i = 0; i < __MT_MAX_BAND; i++)
		seq_printf(s, "band%d_link_id = %d\n", i, mvif->mt76.band_to_link[i]);

	mutex_lock(&dev->mt76.mutex);
	valid_links = vif->valid_links ?: BIT(0);
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		mconf = mt7996_vif_link(dev, vif, link_id);
		msta_link = mt76_dereference(mvif->sta.link[link_id], &dev->mt76);

		if (!mconf || !msta_link)
			continue;

		seq_printf(s, "- link[%02d]: bss_idx = %d, wcid = %d\n",
			   msta_link->wcid.link_id, mconf->mt76.idx, msta_link->wcid.idx);
		seq_printf(s, "            omac_idx = %d, own_mld_id=%d\n",
			   mconf->mt76.omac_idx, mconf->own_mld_id);

		if (!mconf->phy->mt76->chanctx)
			continue;

		seq_printf(s, "            band_idx=%d, radio_idx=%d, channel=%d, bw%s\n",
			   mconf->mt76.band_idx,
			   mconf->mt76.ctx->radio_idx,
			   mconf->phy->mt76->chanctx->chandef.chan->hw_value,
			   width_to_bw[mconf->phy->mt76->chanctx->chandef.width]);
	}
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_vif_links_info);

void mt7996_vif_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	debugfs_create_file("mt76_links_info", 0400, vif->debugfs_dir, vif,
			    &mt7996_vif_links_info_fops);
}

static void
mt7996_parse_rate(struct rate_info *rate, char *buf, size_t size)
{
	u32 bitrate = cfg80211_calculate_bitrate(rate);
	bool legacy = false;
	char *pos = buf;
	enum {
		GI_0_4,
		GI_0_8,
		GI_1_6,
		GI_3_2
	} gi = GI_0_8;

	if (bitrate)
		pos += snprintf(pos, size - (pos - buf), "%u.%u Mbit/s",
				bitrate / 10, bitrate % 10);

	if (rate->flags & RATE_INFO_FLAGS_MCS) {
		pos += snprintf(pos, size - (pos - buf), " HT");

		if (rate->flags & RATE_INFO_FLAGS_SHORT_GI)
			gi = GI_0_4;
	} else if (rate->flags & RATE_INFO_FLAGS_VHT_MCS) {
		pos += snprintf(pos, size - (pos - buf), " VHT");

		if (rate->flags & RATE_INFO_FLAGS_SHORT_GI)
			gi = GI_0_4;
	} else if (rate->flags & RATE_INFO_FLAGS_HE_MCS) {
		pos += snprintf(pos, size - (pos - buf), " HE");

		if (rate->he_gi == NL80211_RATE_INFO_HE_GI_1_6)
			gi = GI_1_6;
		else if (rate->he_gi == NL80211_RATE_INFO_HE_GI_3_2)
			gi = GI_3_2;
	} else if (rate->flags & RATE_INFO_FLAGS_EHT_MCS) {
		pos += snprintf(pos, size - (pos - buf), " EHT");

		if (rate->eht_gi == NL80211_RATE_INFO_EHT_GI_1_6)
			gi = GI_1_6;
		else if (rate->eht_gi == NL80211_RATE_INFO_EHT_GI_3_2)
			gi = GI_3_2;
	} else if (rate->legacy == 0) {
		pos += snprintf(pos, size - (pos - buf), "Proprietary Long Range");
	} else {
		pos += snprintf(pos, size - (pos - buf), " Legacy");
		legacy = true;
	}

	switch (rate->bw) {
	case RATE_INFO_BW_20:
		pos += snprintf(pos, size - (pos - buf), " 20MHz");
		break;
	case RATE_INFO_BW_40:
		pos += snprintf(pos, size - (pos - buf), " 40MHz");
		break;
	case RATE_INFO_BW_80:
		pos += snprintf(pos, size - (pos - buf), " 80MHz");
		break;
	case RATE_INFO_BW_160:
		pos += snprintf(pos, size - (pos - buf), " 160MHz");
		break;
	case RATE_INFO_BW_320:
		pos += snprintf(pos, size - (pos - buf), " 320MHz");
		break;
	case RATE_INFO_BW_HE_RU:
		if (rate->he_ru_alloc == NL80211_RATE_INFO_HE_RU_ALLOC_106) {
			pos += snprintf(pos, size - (pos - buf), " 106-tone RU");
			break;
		}
		fallthrough;
	default:
		pos += snprintf(pos, size - (pos - buf), " (Unknown BW)");
	}

	if (!legacy) {
		pos += snprintf(pos, size - (pos - buf), " MCS %hhu", rate->mcs);
		pos += snprintf(pos, size - (pos - buf), " NSS %hhu", rate->nss);
	}

	switch (gi) {
	case GI_0_4:
		pos += snprintf(pos, size - (pos - buf), " GI 0.4us");
		break;
	case GI_0_8:
		pos += snprintf(pos, size - (pos - buf), " GI 0.8us");
		break;
	case GI_1_6:
		pos += snprintf(pos, size - (pos - buf), " GI 1.6us");
		break;
	default:
		pos += snprintf(pos, size - (pos - buf), " GI 3.2us");
		break;
	}
}

static const char *ac_to_str(enum ieee80211_ac_numbers ac)
{
	static const char *ac_str[] = {"VO", "VI", "BE", "BK"};
	return ac_str[ac];
}

static int
mt7996_link_sta_info_show(struct seq_file *file, void *data)
{
	struct ieee80211_link_sta *link_sta = file->private;
	struct mt7996_sta *msta = (struct mt7996_sta *)link_sta->sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct mt76_sta_stats *stats;
	struct mt76_wcid *wcid;
	char buf[100];
	u8 ac;

	mutex_lock(&msta->vif->dev->mt76.mutex);

	msta_link = mt76_dereference(msta->link[link_sta->link_id], &msta->vif->dev->mt76);
	if (!msta_link) {
		mutex_unlock(&msta->vif->dev->mt76.mutex);
		return -EINVAL;
	}
	wcid = &msta_link->wcid;
	stats = &wcid->stats;

	seq_printf(file, "WCID: %hu\n", wcid->idx);
	seq_printf(file, "Link ID: %hhu\n", link_sta->link_id);
	seq_printf(file, "Link Address: %pM\n", link_sta->addr);
	seq_printf(file, "Status:\n");
	seq_printf(file, "\tRSSI: %d [%hhd, %hhd, %hhd, %hhd] dBm\n",
		   msta_link->signal, msta_link->chain_signal[0], msta_link->chain_signal[1],
		   msta_link->chain_signal[2], msta_link->chain_signal[3]);
	seq_printf(file, "\tACK RSSI: %d [%hhd, %hhd, %hhd, %hhd] dBm\n",
		   msta_link->ack_signal, msta_link->chain_ack_signal[0],
		   msta_link->chain_ack_signal[1], msta_link->chain_ack_signal[2],
		   msta_link->chain_ack_signal[3]);
	seq_printf(file, "\tACK SNR: [%hhd, %hhd, %hhd, %hhd] dBm\n",
		   msta_link->chain_ack_snr[0], msta_link->chain_ack_snr[1],
		   msta_link->chain_ack_snr[2], msta_link->chain_ack_snr[3]);
	seq_printf(file, "Rate:\n");

	mt7996_parse_rate(&wcid->rate, buf, sizeof(buf));
	seq_printf(file, "\tTX: %s\n", buf);
	mt7996_parse_rate(&wcid->rx_rate, buf, sizeof(buf));
	seq_printf(file, "\tRX: %s\n", buf);

	seq_printf(file, "Statistics:\n");
	seq_printf(file, "\tTX:\n");
	seq_printf(file, "\t\tByte Count: %llu\n", stats->tx_bytes);
	for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ++ac)
		seq_printf(file, "\t\t\t%s: %llu\n", ac_to_str(ac), stats->tx_bytes_per_ac[ac]);
	seq_printf(file, "\t\tByte Fails: %llu\n", stats->tx_bytes_failed);
	for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ++ac)
		seq_printf(file, "\t\t\t%s: %llu\n", ac_to_str(ac), stats->tx_bytes_failed_per_ac[ac]);
	seq_printf(file, "\t\tMPDU OK: %lu\n", stats->tx_mpdu_ok);
	seq_printf(file, "\t\tMPDU Attempts: %lu\n", stats->tx_attempts);
	seq_printf(file, "\t\tMPDU Fails: %lu (PER: %lu.%lu%%)\n", stats->tx_failed,
		   stats->tx_mpdu_ok ? stats->tx_failed * 1000 / stats->tx_mpdu_ok / 10 : 0,
		   stats->tx_mpdu_ok ? stats->tx_failed * 1000 / stats->tx_mpdu_ok % 10 : 0);
	seq_printf(file, "\t\tMPDU Retries: %lu\n", stats->tx_retries);
	seq_printf(file, "\t\tAirtime: %llu (unit: 1.024 us)\n", stats->tx_airtime);
	seq_printf(file, "\tRX:\n");
	seq_printf(file, "\t\tByte Count: %llu\n", stats->rx_bytes);
	for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ++ac)
		seq_printf(file, "\t\t\t%s: %llu\n", ac_to_str(ac), stats->rx_bytes_per_ac[ac]);
	seq_printf(file, "\t\tMPDU Count: %u\n", stats->rx_mpdus);
	seq_printf(file, "\t\tMPDU FCS Errors: %u (PER: %u.%u%%)\n", stats->rx_fcs_err,
		   stats->rx_mpdus ? stats->rx_fcs_err * 1000 / stats->rx_mpdus / 10 : 0,
		   stats->rx_mpdus ? stats->rx_fcs_err * 1000 / stats->rx_mpdus % 10 : 0);
	seq_printf(file, "\t\tAirtime: %llu (unit: 1.024 us)\n", stats->rx_airtime);

	mutex_unlock(&msta->vif->dev->mt76.mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_link_sta_info);

void mt7996_link_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				 struct ieee80211_link_sta *link_sta,
				 struct dentry *dir)
{
	debugfs_create_file("link_sta_info", 0400, dir, link_sta,
			    &mt7996_link_sta_info_fops);
}

static int
mt7996_link_info_show(struct seq_file *file, void *data)
{
	struct ieee80211_bss_conf *conf = file->private;
	struct mt7996_vif *mvif = (struct mt7996_vif *)conf->vif->drv_priv;
	struct mt7996_sta *msta = &mvif->sta;
	struct mt7996_vif_link *mconf;
	struct mt7996_sta_link *msta_link;
	struct mt7996_dev *dev = mvif->dev;
	struct rate_info *r;

	mutex_lock(&dev->mt76.mutex);

	mconf = mt7996_vif_link(dev, conf->vif, conf->link_id);
	msta_link = mt76_dereference(msta->link[conf->link_id], &dev->mt76);
	if (!mconf || !msta_link) {
		mutex_unlock(&dev->mt76.mutex);
		return -EINVAL;
	}

	r = &msta_link->wcid.rate;
	seq_printf(file, "band mapping=%u\n", mconf->phy->mt76->band_idx);
	seq_printf(file, "tx rate: flags=0x%x,legacy=%u,mcs=%u,nss=%u,bw=%u,he_gi=%u,he_dcm=%u,he_ru_alloc=%u,eht_gi=%u,eht_ru_alloc=%u\n",
		   r->flags, r->legacy, r->mcs, r->nss, r->bw, r->he_gi, r->he_dcm, r->he_ru_alloc, r->eht_gi, r->eht_ru_alloc);

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_link_info);

void mt7996_link_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf, struct dentry *dir)
{
	debugfs_create_file("link_info", 0600, dir, link_conf, &mt7996_link_info_fops);
}

#endif
