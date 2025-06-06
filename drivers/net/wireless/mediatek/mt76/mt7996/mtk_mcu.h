/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2023 MediaTek Inc.
 */

#ifndef __MT7996_MTK_MCU_H
#define __MT7996_MTK_MCU_H

#include "../mt76_connac_mcu.h"

#ifdef CONFIG_MTK_DEBUG

struct bf_pfmu_tag {
	__le16 tag;
	__le16 len;

	u8 pfmu_id;
	bool bfer;
	u8 band_idx;
	u8 __rsv[5];
	u8 buf[56];
} __packed;

struct bf_starec_read {
	__le16 tag;
	__le16 len;

	__le16 wlan_idx;
	u8 __rsv[2];
} __packed;

struct bf_txsnd_info {
	__le16 tag;
	__le16 len;

	u8 action;
	u8 read_clr;
	u8 vht_opt;
	u8 he_opt;
	__le16 wlan_idx;
	u8 glo_opt;
	u8 snd_intv;
	u8 snd_stop;
	u8 max_snd_stas;
	u8 tx_time;
	u8 mcs;
	u8 ldpc;
	u8 inf;
	u8 man;
	u8 ac_queue;
	u8 sxn_protect;
	u8 direct_fbk;
	u8 __rsv[2];
} __packed;

#define MAX_PHASE_GROUP_NUM	13

struct bf_phase_comp {
	__le16 tag;
	__le16 len;

	u8 bw;
	u8 jp_band;
	u8 band_idx;
	bool read_from_e2p;
	bool disable;
	u8 group;
	u8 rsv[2];
	u8 buf[44];
} __packed;

struct bf_tx_apply {
	__le16 tag;
	__le16 len;

	__le16 wlan_idx;
	bool ebf;
	bool ibf;
	bool mu_txbf;
	bool phase_cal;
	u8 rsv[2];
} __packed;

struct bf_phase_cal {
	__le16 tag;
	__le16 len;

	u8 group_l_m_n;
	u8 group;
	u8 sx2;
	u8 cal_type;
	u8 lna_gain_level;
	u8 band_idx;
	u8 version;
	u8 rsv[1];
} __packed;

struct bf_txcmd {
	__le16 tag;
	__le16 len;

	u8 action;
	u8 bf_manual;
	u8 bf_bit;
	u8 rsv[5];
} __packed;

struct bf_pfmu_data_all {
	__le16 tag;
	__le16 len;

	u8 pfmu_id;
	u8 band_idx;
	u8 rsv[2];

	u8 buf[640];
} __packed;

#define TXBF_DUT_MAC_SUBADDR		0x22
#define TXBF_GOLDEN_MAC_SUBADDR		0x11

struct mt7996_tm_bf_req {
	u8 _rsv[4];

	union {
		struct bf_sounding_on sounding;
		struct bf_tx_apply tx_apply;
		struct bf_pfmu_tag pfmu_tag;
		struct bf_pfmu_data_all pfmu_data_all;
		struct bf_phase_cal phase_cal;
		struct bf_phase_comp phase_comp;
		struct bf_txcmd txcmd;
	};
} __packed;

enum tm_trx_mac_type {
	TM_TRX_MAC_TX = 1,
	TM_TRX_MAC_RX,
	TM_TRX_MAC_TXRX,
	TM_TRX_MAC_TXRX_RXV,
	TM_TRX_MAC_RXV,
	TM_TRX_MAC_RX_RXV,
};

struct mt7996_pfmu_data {
	__le16 subc_idx;
	__le16 phi11;
	__le16 phi21;
	__le16 phi31;
};

struct mt7996_pfmu_data_5x5 {
	__le16 subc_idx;
	__le16 phi11;
	__le16 phi21;
	__le16 phi31;
	__le16 phi41;
};

enum {
	CAPI_SU,
	CAPI_MU,
	CAPI_ER_SU,
	CAPI_TB,
	CAPI_LEGACY
};

enum {
	CAPI_BASIC,
	CAPI_BRP,
	CAPI_MU_BAR,
	CAPI_MU_RTS,
	CAPI_BSRP,
	CAPI_GCR_MU_BAR,
	CAPI_BQRP,
	CAPI_NDP_FRP,
};

enum {
	MU_DL_ACK_POLICY_MU_BAR = 3,
	MU_DL_ACK_POLICY_TF_FOR_ACK = 4,
	MU_DL_ACK_POLICY_SU_BAR = 5,
};

#endif

#endif
