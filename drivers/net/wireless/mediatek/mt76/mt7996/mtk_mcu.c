// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2023 MediaTek Inc.
 */

#include <linux/firmware.h>
#include <linux/fs.h>
#include "mt7996.h"
#include "mcu.h"
#include "mac.h"
#include "mtk_mcu.h"

#ifdef CONFIG_MTK_DEBUG

int mt7996_mcu_set_dup_wtbl(struct mt7996_dev *dev)
{
#define CHIP_CONFIG_DUP_WTBL	4
#define DUP_WTBL_NUM	80
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		__le16 base;
		__le16 num;
		u8 _rsv2[4];
	} __packed req = {
		.tag = cpu_to_le16(CHIP_CONFIG_DUP_WTBL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.base = cpu_to_le16(MT7996_WTBL_STA - DUP_WTBL_NUM + 1),
		.num = cpu_to_le16(DUP_WTBL_NUM),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(CHIP_CONFIG), &req,
				 sizeof(req), true);
}

static struct tlv *
__mt7996_mcu_add_uni_tlv(struct sk_buff *skb, u16 tag, u16 len)
{
	struct tlv *ptlv, tlv = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(len),
	};

	ptlv = skb_put(skb, len);
	memcpy(ptlv, &tlv, sizeof(tlv));

	return ptlv;
}

int mt7996_mcu_set_txbf_internal(struct mt7996_phy *phy, u8 action, int idx, bool bfer)
{
	struct mt7996_dev *dev = phy->dev;
#define MT7996_MTK_BF_MAX_SIZE	sizeof(struct bf_starec_read)
	struct uni_header hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + MT7996_MTK_BF_MAX_SIZE;

	memset(&hdr, 0, sizeof(hdr));

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	switch (action) {
	case BF_PFMU_TAG_READ: {
		struct bf_pfmu_tag *req;

		tlv = __mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req));
		req = (struct bf_pfmu_tag *)tlv;
		req->pfmu_id = idx;
		req->bfer = bfer;
		req->band_idx = phy->mt76->band_idx;
		break;
	}
	case BF_STA_REC_READ: {
		struct bf_starec_read *req;

		tlv = __mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req));
		req = (struct bf_starec_read *)tlv;
		req->wlan_idx = idx;
		break;
	}
	case BF_FBRPT_DBG_INFO_READ: {
		struct bf_fbk_rpt_info *req;

		if (idx != 0) {
			dev_info(dev->mt76.dev, "Invalid input");
			return 0;
		}

		tlv = __mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req));
		req = (struct bf_fbk_rpt_info *)tlv;
		req->action = idx;
		req->band_idx = phy->mt76->band_idx;
		break;
	}
	default:
		return -EINVAL;
	}

	return mt76_mcu_skb_send_msg(&phy->dev->mt76, skb, MCU_WM_UNI_CMD(BF), false);
}

int mt7996_mcu_set_txbf_snd_info(struct mt7996_dev *dev, void *para)
{
	char *buf = (char *)para;
	__le16 input[5] = {0};
	u8 recv_arg = 0;
	struct bf_txsnd_info *req;
	struct uni_header hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + MT7996_MTK_BF_MAX_SIZE;

	memset(&hdr, 0, sizeof(hdr));

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	recv_arg = sscanf(buf, "%hx:%hx:%hx:%hx:%hx", &input[0], &input[1], &input[2],
						      &input[3], &input[4]);

	if (!recv_arg)
		return -EINVAL;

	tlv = __mt7996_mcu_add_uni_tlv(skb, BF_TXSND_INFO, sizeof(*req));
	req = (struct bf_txsnd_info *)tlv;
	req->action = input[0];

	switch (req->action) {
	case BF_SND_READ_INFO: {
		req->read_clr = input[1];
		break;
	}
	case BF_SND_CFG_OPT: {
		req->vht_opt = input[1];
		req->he_opt = input[2];
		req->glo_opt = input[3];
		break;
	}
	case BF_SND_CFG_INTV: {
		req->wlan_idx = input[1];
		req->snd_intv = input[2];
		break;
	}
	case BF_SND_STA_STOP: {
		req->wlan_idx = input[1];
		req->snd_stop = input[2];
		break;
	}
	case BF_SND_CFG_MAX_STA: {
		req->max_snd_stas = input[1];
		break;
	}
	case BF_SND_CFG_BFRP: {
		req->man = input[1];
		req->tx_time = input[2];
		req->mcs = input[3];
		req->ldpc = input[4];
		break;
	}
	case BF_SND_CFG_INF: {
		req->inf = input[1];
		break;
	}
	case BF_SND_CFG_TXOP_SND: {
		req->man = input[1];
		req->ac_queue = input[2];
		req->sxn_protect = input[3];
		req->direct_fbk = input[4];
		break;
	}
	default:
		return -EINVAL;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(BF), false);
}

int mt7996_mcu_set_bypass_smthint(struct mt7996_dev *dev, u8 band_idx, u8 val)
{
#define BF_PHY_SMTH_INT_BYPASS 0
#define BYPASS_VAL 1
	struct {
		u8 _rsv[4];

		u16 tag;
		u16 len;

		u8 action;
		u8 band_idx;
		u8 smthintbypass;
		u8 __rsv2[5];
	} __packed data = {
		.tag = cpu_to_le16(BF_CFG_PHY),
		.len = cpu_to_le16(sizeof(data) - 4),
		.action = BF_PHY_SMTH_INT_BYPASS,
		.band_idx = band_idx,
		.smthintbypass = val,
	};

	if (val != BYPASS_VAL || !mt7996_band_valid(dev, band_idx))
		return -EINVAL;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(BF), &data, sizeof(data),
				 true);
}

static int
mt7996_mcu_set_bsrp_ctrl(struct mt7996_dev *dev, u8 band_idx, u16 interval,
			 u16 ru_alloc, u32 trig_type, u8 trig_flow, u8 ext_cmd)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le16 interval;
		__le16 ru_alloc;
		__le32 trigger_type;
		u8 trigger_flow;
		u8 ext_cmd_bsrp;
		u8 band_bitmap;
		u8 _rsv2;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_MURU_BSRP_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.interval = cpu_to_le16(interval),
		.ru_alloc = cpu_to_le16(ru_alloc),
		.trigger_type = cpu_to_le32(trig_type),
		.trigger_flow = trig_flow,
		.ext_cmd_bsrp = ext_cmd,
		.band_bitmap = mt7996_band_valid(dev, MT_BAND2) ?
			       GENMASK(2, 0) : GENMASK(1, 0),
	};

	if (!mt7996_band_valid(dev, band_idx))
		return -EINVAL;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &req,
				 sizeof(req), false);
}

int mt7996_mcu_set_rfeature_trig_type(struct mt7996_dev *dev, u8 band_idx,
				      u8 enable, u8 trig_type)
{
	int ret = 0;
	char buf[] = "01:00:00:1B";

	if (enable) {
		ret = mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SET_TRIG_TYPE, trig_type);
		if (ret)
			return ret;
	}

	switch (trig_type) {
	case CAPI_BASIC:
		return mt7996_mcu_set_bsrp_ctrl(dev, band_idx, 5, 67, 0, 0, enable);
	case CAPI_BRP:
		return mt7996_mcu_set_txbf_snd_info(dev, buf);
	case CAPI_MU_BAR:
		return mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SET_MUDL_ACK_POLICY,
					       MU_DL_ACK_POLICY_MU_BAR);
	case CAPI_BSRP:
		return mt7996_mcu_set_bsrp_ctrl(dev, band_idx, 5, 67, 4, 0, enable);
	default:
		return 0;
	}
}

void mt7996_mcu_set_ppdu_tx_type(struct mt7996_dev *dev, u8 ppdu_type)
{
	int enable_su;

	switch (ppdu_type) {
	case CAPI_SU:
		enable_su = 1;
		mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SUTX_CTRL, enable_su);
		mt7996_set_muru_cfg(dev, MU_CTRL_DL_USER_CNT, 0);
		break;
	case CAPI_MU:
		enable_su = 0;
		mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SUTX_CTRL, enable_su);
		break;
	default:
		break;
	}
}

void mt7996_mcu_set_nusers_ofdma(struct mt7996_dev *dev, u8 band_idx, u8 user_cnt)
{
	struct mt76_phy *mphy;
	struct mt7996_phy *phy;
	int enable_su = 0;
	u8 type;

	if (!mt7996_band_valid(dev, band_idx)) {
		dev_err(dev->mt76.dev, "Invalid band_idx\n");
		return;
	}

	mphy = dev->mt76.phys[band_idx];
	if (!mphy)
		return;

	phy = (struct mt7996_phy *)mphy->priv;

	mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SUTX_CTRL, enable_su);
	mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SET_MUDL_ACK_POLICY,
				MU_DL_ACK_POLICY_SU_BAR);
	mt7996_mcu_muru_set_prot_frame_thr(dev, 9999);

	if (phy->muru_onoff & OFDMA_UL)
		type = MU_CTRL_UL_USER_CNT;
	else
		type = MU_CTRL_DL_USER_CNT;

	mt7996_set_muru_cfg(dev, type, user_cnt);
}

void mt7996_mcu_set_mimo(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int disable_ra = 1;
	char buf[] = "2 134 0 1 0 1 2 2 2";
	int force_mu = 1;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		strscpy(buf, "2 122 0 1 0 1 2 2 2", sizeof(buf));
		break;
	case NL80211_CHAN_WIDTH_80:
		break;
	case NL80211_CHAN_WIDTH_160:
		strscpy(buf, "2 137 0 1 0 1 2 2 2", sizeof(buf));
		break;
	default:
		break;
	}

	mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SET_MUDL_ACK_POLICY, MU_DL_ACK_POLICY_SU_BAR);
	mt7996_mcu_set_muru_fixed_rate_enable(dev, UNI_CMD_MURU_FIXED_RATE_CTRL, disable_ra);
	mt7996_mcu_set_muru_fixed_rate_parameter(dev, UNI_CMD_MURU_FIXED_GROUP_RATE_CTRL, buf);
	mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SET_FORCE_MU, force_mu);
}

void mt7996_mcu_set_cert(struct mt7996_dev *dev)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 action;
		u8 _rsv2[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_CERT_CFG),
		.len = cpu_to_le16(sizeof(req) - 4),
		.action = !!dev->cert_mode, /* 1: CAPI Enable */
	};

	mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(WSYS_CONFIG), &req,
			  sizeof(req), false);
}

int mt7996_mcu_thermal_debug(struct mt7996_dev *dev, u8 mode, u8 action)
{
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;

		u8 mode;
		u8 action;
		u8 __rsv2[2];
	} __packed req = {
		.tag = cpu_to_le16(mode),
		.len = cpu_to_le16(sizeof(req) - 4),
		.mode = mode,
		.action = action,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(THERMAL_CAL), &req,
	                         sizeof(req), true);
}

int mt7996_mcu_mlo_agc(struct mt7996_dev *dev, const void *data, int len)
{
	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MLO), data,
	                        len, true);
}
#endif
