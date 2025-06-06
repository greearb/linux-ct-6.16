#include <linux/inet.h>
#include "mt7996.h"
#include "../mt76.h"
#include "mcu.h"
#include "mac.h"
#include "eeprom.h"
#include "mtk_debug.h"
#include "mtk_debug_i.h"
#include "mtk_mcu.h"

#ifdef CONFIG_MTK_DEBUG

#define info_or_seq_printf(seq, fmt, ...)	do {	\
	if (seq)					\
		seq_printf(seq, fmt, ##__VA_ARGS__);	\
	else						\
		pr_info(fmt, ##__VA_ARGS__);		\
} while (0)

static void info_or_seq_hex_dump(struct seq_file *seq, int prefix_type,
				 int rowsize, int groupsize, const void *buf,
				 size_t len, bool ascii)
{
	if (seq)
		seq_hex_dump(seq, "", prefix_type, rowsize, groupsize,
			     buf, len, ascii);
	else
		print_hex_dump(KERN_INFO, "", prefix_type,
			       rowsize, groupsize, buf, len, ascii);
}

//bmac dump mac txp
static void mt7996_dump_bmac_mac_txp_info(struct seq_file *s, struct mt7996_dev *dev,
					  __le32 *txp)
{
	struct mt7996_txp_token {
		__le16 msdu[4];
	} *msdu;
	struct mt7996_txp_ptr {
		__le32 addr1;
		__le32 addr_info;
		__le32 addr2;
	} *ptr;
	int i = 0;

	for (i = 0; i < 12; i = i+2 ) {
		if (i == 0 || i == 4) {
			msdu = (struct mt7996_txp_token *) txp;
			info_or_seq_printf(s, "msdu token(%d-%d)=%ld %ld %ld %ld (0x%08x-0x%08x)\n", i, i+3,
				(msdu->msdu[0] & GENMASK(14, 0)),
				(msdu->msdu[1] & GENMASK(14, 0)),
				(msdu->msdu[2] & GENMASK(14, 0)),
				(msdu->msdu[3] & GENMASK(14, 0)), *txp, *(txp+1));
			txp = txp + 2;
		}
		ptr = (struct mt7996_txp_ptr *) txp;
		info_or_seq_printf(s, "ptr%02d : addr(0x%08x) len(%ld) addr_h(%02lx) SRC(%d) ML(%d) \n",
			i, ptr->addr1,
			FIELD_GET(GENMASK(11, 0), ptr->addr_info),
			FIELD_GET(GENMASK(13, 12), ptr->addr_info),
			!!(ptr->addr_info & BIT(14)),
			!!(ptr->addr_info & BIT(15)));
		info_or_seq_printf(s, "ptr%02d : addr(0x%08x) len(%ld) addr_h(%02lx) SRC(%d) ML(%d) \n",
			i+1, ptr->addr2,
			FIELD_GET(GENMASK(27, 16), ptr->addr_info),
			FIELD_GET(GENMASK(29, 28), ptr->addr_info),
			!!(ptr->addr_info & BIT(30)),
			!!(ptr->addr_info & BIT(31)));
		txp = txp + 3;
	}
}

//bmac dump hif txp
static void
mt7996_dump_bmac_hif_txp_info(struct seq_file *s, struct mt7996_dev *dev,
			      __le32 *txp, u32 hif_txp_ver)
{
	int i, j = 0;
	u32 dw;

	info_or_seq_printf(s, "txp raw data: size=%d\n", HIF_TXP_V2_SIZE);
	info_or_seq_hex_dump(s, DUMP_PREFIX_OFFSET, 16, 1, (u8 *)txp, HIF_TXP_V2_SIZE, false);

	info_or_seq_printf(s, "BMAC_TXP Fields:\n");

	/* dw0 */
	if (hif_txp_ver == 2) {
		dw = le32_to_cpu(txp[0]);
		info_or_seq_printf(s, "HIF_TXP_PRIORITY = %d\n",
				GET_FIELD(HIF_TXP_PRIORITY, dw));
		info_or_seq_printf(s, "HIF_TXP_FIXED_RATE = %d\n",
				GET_FIELD(HIF_TXP_FIXED_RATE, dw));
		info_or_seq_printf(s, "HIF_TXP_TCP = %d\n",
				GET_FIELD(HIF_TXP_TCP, dw));
		info_or_seq_printf(s, "HIF_TXP_NON_CIPHER = %d\n",
				GET_FIELD(HIF_TXP_NON_CIPHER, dw));
		info_or_seq_printf(s, "HIF_TXP_VLAN = %d\n",
				GET_FIELD(HIF_TXP_VLAN, dw));
		info_or_seq_printf(s, "HIF_TXP_BC_MC_FLAG = %d\n",
				GET_FIELD(HIF_TXP_BC_MC_FLAG, dw));
		info_or_seq_printf(s, "HIF_TXP_FR_HOST = %d\n",
				GET_FIELD(HIF_TXP_FR_HOST, dw));
		info_or_seq_printf(s, "HIF_TXP_ETYPE = %d\n",
				GET_FIELD(HIF_TXP_ETYPE, dw));
		info_or_seq_printf(s, "HIF_TXP_TXP_AMSDU = %d\n",
				GET_FIELD(HIF_TXP_TXP_AMSDU, dw));
		info_or_seq_printf(s, "HIF_TXP_TXP_MC_CLONE = %d\n",
				GET_FIELD(HIF_TXP_TXP_MC_CLONE, dw));
		info_or_seq_printf(s, "HIF_TXP_TOKEN_ID = %d\n",
				GET_FIELD(HIF_TXP_TOKEN_ID, dw));

		/* dw1 */
		dw = le32_to_cpu(txp[1]);
		info_or_seq_printf(s, "HIF_TXP_BSS_IDX = %d\n",
				GET_FIELD(HIF_TXP_BSS_IDX, dw));
		info_or_seq_printf(s, "HIF_TXP_USER_PRIORITY = %d\n",
				GET_FIELD(HIF_TXP_USER_PRIORITY, dw));
		info_or_seq_printf(s, "HIF_TXP_BUF_NUM = %d\n",
				GET_FIELD(HIF_TXP_BUF_NUM, dw));
		info_or_seq_printf(s, "HIF_TXP_MSDU_CNT = %d\n",
				GET_FIELD(HIF_TXP_MSDU_CNT, dw));
		info_or_seq_printf(s, "HIF_TXP_SRC = %d\n",
				GET_FIELD(HIF_TXP_SRC, dw));

		/* dw2 */
		dw = le32_to_cpu(txp[2]);
		info_or_seq_printf(s, "HIF_TXP_ETH_TYPE(network-endian) = 0x%x\n",
				GET_FIELD(HIF_TXP_ETH_TYPE, dw));
		info_or_seq_printf(s, "HIF_TXP_WLAN_IDX = %d\n",
				GET_FIELD(HIF_TXP_WLAN_IDX, dw));

		/* dw3 */
		dw = le32_to_cpu(txp[3]);
		info_or_seq_printf(s, "HIF_TXP_PPE_INFO = 0x%x\n",
				GET_FIELD(HIF_TXP_PPE_INFO, dw));

		for (i = 0; i < 13; i++) {
			if (i % 2 == 0) {
				info_or_seq_printf(s, "HIF_TXP_BUF_PTR%d_L = 0x%x\n",
						i, GET_FIELD(HIF_TXP_BUF_PTR0_L,
						le32_to_cpu(txp[4 + j])));
				j++;
				info_or_seq_printf(s, "HIF_TXP_BUF_LEN%d = %d\n",
						i, GET_FIELD(HIF_TXP_BUF_LEN0, le32_to_cpu(txp[4 + j])));
				info_or_seq_printf(s, "HIF_TXP_BUF_PTR%d_H = 0x%x\n",
						i, GET_FIELD(HIF_TXP_BUF_PTR0_H, le32_to_cpu(txp[4 + j])));
				if (i <= 10) {
					info_or_seq_printf(s, "HIF_TXP_BUF_LEN%d = %d\n",
							   i + 1, GET_FIELD(HIF_TXP_BUF_LEN1, le32_to_cpu(txp[4 + j])));
					info_or_seq_printf(s, "HIF_TXP_BUF_PTR%d_H = 0x%x\n",
							   i + 1, GET_FIELD(HIF_TXP_BUF_PTR1_H, le32_to_cpu(txp[4 + j])));
				}
				j++;
			} else {
				info_or_seq_printf(s, "HIF_TXP_BUF_PTR%d_L = 0x%x\n",
					i, GET_FIELD(HIF_TXP_BUF_PTR1_L,
					le32_to_cpu(txp[4 + j])));
				j++;
			}
		}

		info_or_seq_printf(s, "ml = 0x%x\n",
			GET_FIELD(HIF_TXP_ML, le32_to_cpu(txp[23])));
	} else {
		struct mt76_connac_txp_common *txp_v1 = (struct mt76_connac_txp_common *)txp;

		info_or_seq_printf(s, "FLAGS = (%04x)\n", txp_v1->fw.flags);

		info_or_seq_printf(s, "MSDU = %d\n", txp_v1->fw.token);

		info_or_seq_printf(s, "BSS_IDX = %d\n", txp_v1->fw.bss_idx);

		info_or_seq_printf(s, "WCID = %d\n",txp_v1->fw.rept_wds_wcid);

		info_or_seq_printf(s, "MSDU_CNT = %d\n", txp_v1->fw.nbuf);

		for (i = 0; i < MT_TXP_MAX_BUF_NUM; i++)
			info_or_seq_printf(s, "ptr%02d : addr(0x%08x) len(%d)\n", i, le32_to_cpu(txp_v1->fw.buf[i]),
				le16_to_cpu(txp_v1->fw.len[i]));
	}
}

/* bmac txd dump */
void mt7996_dump_bmac_txd_info(struct seq_file *s, struct mt7996_dev *dev,
			       __le32 *txd, bool is_hif_txd, bool dump_txp)
{
	u32 hif_txp_ver = 0;

	/* dump stop */
	if (!dev->dbg.txd_read_cnt)
		return;

	/* force dump */
	if (dev->dbg.txd_read_cnt > 8)
		dev->dbg.txd_read_cnt = 8;

	/* dump txd_read_cnt times */
	if (dev->dbg.txd_read_cnt != 8)
		dev->dbg.txd_read_cnt--;

	info_or_seq_printf(s, "txd raw data: size=%d\n", MT_TXD_SIZE);
	info_or_seq_hex_dump(s, DUMP_PREFIX_OFFSET, 16, 1, (u8 *)txd, MT_TXD_SIZE, false);

	info_or_seq_printf(s, "BMAC_TXD Fields:\n");
	/* dw0 */
	if (is_hif_txd) {
		hif_txp_ver = FIELD_GET(GENMASK(22, 19), txd[0]);
		info_or_seq_printf(s, "HIF TXD VER = %d\n", hif_txp_ver);
	}
	info_or_seq_printf(s, "TX_BYTE_COUNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TX_BYTE_COUNT, txd[0]));
	info_or_seq_printf(s, "ETHER_TYPE_OFFSET(word) = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_ETHER_TYPE_OFFSET, txd[0]));
	info_or_seq_printf(s, "PKT_FT = %d%s%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]),
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]) == 0 ? "(ct)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]) == 1 ? "(s&f)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]) == 2 ? "(cmd)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_PKT_FT, txd[0]) == 3 ? "(redirect)" : "");
	info_or_seq_printf(s, "Q_IDX = %d%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_Q_IDX, txd[0]),
			GET_FIELD(WF_TX_DESCRIPTOR_Q_IDX, txd[0]) == 0x10 ? "(ALTX)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_Q_IDX, txd[0]) == 0x11 ? "(BMC)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_Q_IDX, txd[0]) == 0x12 ? "(BCN)" : "");

	/* dw1 */
	info_or_seq_printf(s, "MLD_ID = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_MLD_ID, txd[1]));
	info_or_seq_printf(s, "TGID = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TGID, txd[1]));
	info_or_seq_printf(s, "HF = %d%s%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]),
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ? "(eth/802.3)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 1 ? "(cmd)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 2 ? "(802.11)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 3 ? "(802.11 enhanced" : "");
	info_or_seq_printf(s, "802.11 HEADER_LENGTH = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 2 ?
			GET_FIELD(WF_TX_DESCRIPTOR_HEADER_LENGTH, txd[1]) : 0);
	info_or_seq_printf(s, "MRD = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_MRD, txd[1]) : 0);
	info_or_seq_printf(s, "EOSP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_EOSP, txd[1]) : 0);
	info_or_seq_printf(s, "AMS = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 3 ?
			GET_FIELD(WF_TX_DESCRIPTOR_AMS, txd[1]) : 0);
	info_or_seq_printf(s, "RMVL = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_RMVL, txd[1]): 0);
	info_or_seq_printf(s, "VLAN = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_VLAN, txd[1]) : 0);
	info_or_seq_printf(s, "ETYP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HF, txd[1]) == 0 ?
			GET_FIELD(WF_TX_DESCRIPTOR_ETYP, txd[1]) : 0);
	info_or_seq_printf(s, "TID_MGMT_TYPE = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TID_MGMT_TYPE, txd[1]));
	info_or_seq_printf(s, "OM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_OM, txd[1]));
	info_or_seq_printf(s, "FR = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FR, txd[1]));

	/* dw2 */
	info_or_seq_printf(s, "SUBTYPE = %d%s%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]),
			(GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 0) &&
			(GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]) == 13) ?
			"(action)" : "",
			(GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 1) &&
			(GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]) == 8) ?
			"(bar)" : "",
			(GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 2) &&
			(GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]) == 4) ?
			"(null)" : "",
			(GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 2) &&
			(GET_FIELD(WF_TX_DESCRIPTOR_SUBTYPE, txd[2]) == 12) ?
			"(qos null)" : "");

	info_or_seq_printf(s, "FTYPE = %d%s%s%s\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]),
			GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 0 ? "(mgmt)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 1 ? "(ctl)" : "",
			GET_FIELD(WF_TX_DESCRIPTOR_FTYPE, txd[2]) == 2 ? "(data)" : "");
	info_or_seq_printf(s, "BF_TYPE = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BF_TYPE, txd[2]));
	info_or_seq_printf(s, "OM_MAP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_OM_MAP, txd[2]));
	info_or_seq_printf(s, "RTS = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_RTS, txd[2]));
	info_or_seq_printf(s, "HEADER_PADDING = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HEADER_PADDING, txd[2]));
	info_or_seq_printf(s, "DU = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_DU, txd[2]));
	info_or_seq_printf(s, "HE = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HE, txd[2]));
	info_or_seq_printf(s, "FRAG = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FRAG, txd[2]));
	info_or_seq_printf(s, "REMAINING_TX_TIME = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_REMAINING_TX_TIME, txd[2]));
	info_or_seq_printf(s, "POWER_OFFSET = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_POWER_OFFSET, txd[2]));

	/* dw3 */
	info_or_seq_printf(s, "NA = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_NA, txd[3]));
	info_or_seq_printf(s, "PF = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PF, txd[3]));
	info_or_seq_printf(s, "EMRD = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_EMRD, txd[3]));
	info_or_seq_printf(s, "EEOSP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_EEOSP, txd[3]));
	info_or_seq_printf(s, "BM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BM, txd[3]));
	info_or_seq_printf(s, "HW_AMSDU_CAP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HW_AMSDU_CAP, txd[3]));
	info_or_seq_printf(s, "TX_COUNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TX_COUNT, txd[3]));
	info_or_seq_printf(s, "REMAINING_TX_COUNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_REMAINING_TX_COUNT, txd[3]));
	info_or_seq_printf(s, "SN = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SN, txd[3]));
	info_or_seq_printf(s, "BA_DIS = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BA_DIS, txd[3]));
	info_or_seq_printf(s, "PM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PM, txd[3]));
	info_or_seq_printf(s, "PN_VLD = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PN_VLD, txd[3]));
	info_or_seq_printf(s, "SN_VLD = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SN_VLD, txd[3]));

	/* dw4 */
	info_or_seq_printf(s, "PN_31_0 = 0x%x\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PN_31_0_, txd[4]));

	/* dw5 */
	info_or_seq_printf(s, "PID = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PID, txd[5]));
	info_or_seq_printf(s, "TXSFM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TXSFM, txd[5]));
	info_or_seq_printf(s, "TXS2M = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TXS2M, txd[5]));
	info_or_seq_printf(s, "TXS2H = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TXS2H, txd[5]));
	info_or_seq_printf(s, "FBCZ = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FBCZ, txd[5]));
	info_or_seq_printf(s, "BYPASS_RBB = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BYPASS_RBB, txd[5]));

	info_or_seq_printf(s, "FL = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FL, txd[5]));
	info_or_seq_printf(s, "PN_47_32 = 0x%x\n",
			GET_FIELD(WF_TX_DESCRIPTOR_PN_47_32_, txd[5]));

	/* dw6 */
	info_or_seq_printf(s, "AMSDU_CAP_UTXB = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_AMSDU_CAP_UTXB, txd[6]));
	info_or_seq_printf(s, "DAS = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_DAS, txd[6]));
	info_or_seq_printf(s, "DIS_MAT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_DIS_MAT, txd[6]));
	info_or_seq_printf(s, "MSDU_COUNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_MSDU_COUNT, txd[6]));
	info_or_seq_printf(s, "TIMESTAMP_OFFSET = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_IDX, txd[6]));
	info_or_seq_printf(s, "FIXED_RATE_IDX = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_FIXED_RATE_IDX, txd[6]));
	info_or_seq_printf(s, "BW = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_BW, txd[6]));
	info_or_seq_printf(s, "VTA = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_VTA, txd[6]));
	info_or_seq_printf(s, "SRC = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SRC, txd[6]));

	/* dw7 */
	info_or_seq_printf(s, "SW_TX_TIME(unit:65536ns) = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_SW_TX_TIME , txd[7]));
	info_or_seq_printf(s, "UT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_UT, txd[7]));
	info_or_seq_printf(s, "CTXD_CNT = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_CTXD_CNT, txd[7]));
	info_or_seq_printf(s, "HM = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_HM, txd[7]));
	info_or_seq_printf(s, "DP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_DP, txd[7]));
	info_or_seq_printf(s, "IP = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_IP, txd[7]));
	info_or_seq_printf(s, "TXD_LEN = %d\n",
			GET_FIELD(WF_TX_DESCRIPTOR_TXD_LEN, txd[7]));

	if (dump_txp) {
		__le32 *txp = txd + 8;

		if (is_hif_txd)
			mt7996_dump_bmac_hif_txp_info(s, dev, txp, hif_txp_ver);
		else
			mt7996_dump_bmac_mac_txp_info(s, dev, txp);
	}
}

static void
mt7996_dump_mac_fid(struct seq_file *s, struct mt7996_dev *dev, u32 fid, bool is_ple)
{
#define PLE_MEM_SIZE	 128
#define PSE_MEM_SIZE	 256
	 u8 data[PSE_MEM_SIZE] = {0};
	 u32 addr = 0;
	 int i = 0, cr_cnt = PSE_MEM_SIZE;
	 u32 *ptr = (u32 *) data;

	 if (is_ple) {
		cr_cnt = PLE_MEM_SIZE;
		seq_printf(s, "dump ple: fid = 0x%08x\n", fid);
	 } else {
		seq_printf(s, "dump pse: fid = 0x%08x\n", fid);
	 }

	 for (i = 0; i < cr_cnt; i = i + 4) {
		if (is_ple)
			addr = (0xa << 28 | fid << 15) + i;
		else
			addr = (0xb << 28 | fid << 15) + i;
		*ptr = mt76_rr(dev, addr);
		ptr++;
	 }

	 seq_printf(s, "raw data: size=%d\n", cr_cnt);

	 seq_hex_dump(s, "", DUMP_PREFIX_OFFSET, 16, 1, (u8 *)data, cr_cnt, false);
	 /* dump one txd info */
	 if (is_ple) {
		 dev->dbg.txd_read_cnt = 1;
		 mt7996_dump_bmac_txd_info(s, dev, (__le32 *)&data[0], false, true);
	 }
}

static int
mt7996_ple_fid_read(struct seq_file *s, void *data) {
	 struct mt7996_dev *dev = dev_get_drvdata(s->private);

	 mt7996_dump_mac_fid(s, dev, dev->dbg.fid_idx, true);
	 return 0;
}

static int
mt7996_pse_fid_read(struct seq_file *s, void *data) {
	 struct mt7996_dev *dev = dev_get_drvdata(s->private);

	 mt7996_dump_mac_fid(s, dev, dev->dbg.fid_idx, false);
	 return 0;
}

void mt7996_dump_bmac_rxd_info(struct mt7996_dev *dev, __le32 *rxd)
{
	/* dump stop */
	if (!dev->dbg.rxd_read_cnt)
		return;

	/* force dump */
	if (dev->dbg.rxd_read_cnt > 8)
		dev->dbg.rxd_read_cnt = 8;

	/* dump txd_read_cnt times */
	if (dev->dbg.rxd_read_cnt != 8)
		dev->dbg.rxd_read_cnt--;

	printk("rxd raw data: size=%d\n", MT_TXD_SIZE);
	print_hex_dump(KERN_ERR , "", DUMP_PREFIX_OFFSET, 16, 1, (u8 *)rxd, 96, false);

	printk("BMAC_RXD Fields:\n");

	/* group0 */
	/* dw0 */
	printk("RX_BYTE_COUNT = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_RX_BYTE_COUNT, le32_to_cpu(rxd[0])));
	printk("PACKET_TYPE = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_PACKET_TYPE, le32_to_cpu(rxd[0])));

	/* dw1 */
	printk("MLD_ID = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_MLD_ID, le32_to_cpu(rxd[1])));
	printk("GROUP_VLD = 0x%x%s%s%s%s%s\n",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1])),
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_1 ? "[group1]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_2 ? "[group2]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_3 ? "[group3]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_4 ? "[group4]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_5 ? "[group5]" : "");
	printk("KID = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_KID, le32_to_cpu(rxd[1])));
	printk("CM = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_CM, le32_to_cpu(rxd[1])));
	printk("CLM = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_CLM, le32_to_cpu(rxd[1])));
	printk("I = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_I, le32_to_cpu(rxd[1])));
	printk("T = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_T, le32_to_cpu(rxd[1])));
	printk("BN = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_BN, le32_to_cpu(rxd[1])));
	printk("BIPN_FAIL = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_BIPN_FAIL, le32_to_cpu(rxd[1])));

	/* dw2 */
	printk("BSSID = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_BSSID, le32_to_cpu(rxd[2])));
	printk("H = %d%s\n",
			GET_FIELD(WF_RX_DESCRIPTOR_H, le32_to_cpu(rxd[2])),
			GET_FIELD(WF_RX_DESCRIPTOR_H, le32_to_cpu(rxd[2])) == 0 ?
			"802.11 frame" : "eth/802.3 frame");
	printk("HEADER_LENGTH(word) = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_HEADER_LENGTH, le32_to_cpu(rxd[2])));
	printk("HO(word) = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_HO, le32_to_cpu(rxd[2])));
	printk("SEC_MODE = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_SEC_MODE, le32_to_cpu(rxd[2])));
	printk("MUBAR = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_MUBAR, le32_to_cpu(rxd[2])));
	printk("SWBIT = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_SWBIT, le32_to_cpu(rxd[2])));
	printk("DAF = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_DAF, le32_to_cpu(rxd[2])));
	printk("EL = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_EL, le32_to_cpu(rxd[2])));
	printk("HTF = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_HTF, le32_to_cpu(rxd[2])));
	printk("INTF = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_INTF, le32_to_cpu(rxd[2])));
	printk("FRAG = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_FRAG, le32_to_cpu(rxd[2])));
	printk("NUL = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_NUL, le32_to_cpu(rxd[2])));
	printk("NDATA = %d%s\n",
			GET_FIELD(WF_RX_DESCRIPTOR_NDATA, le32_to_cpu(rxd[2])),
			GET_FIELD(WF_RX_DESCRIPTOR_NDATA, le32_to_cpu(rxd[2])) == 0 ?
			"[data frame]" : "[mgmt/ctl frame]");
	printk("NAMP = %d%s\n",
			GET_FIELD(WF_RX_DESCRIPTOR_NAMP, le32_to_cpu(rxd[2])),
			GET_FIELD(WF_RX_DESCRIPTOR_NAMP, le32_to_cpu(rxd[2])) == 0 ?
			"[ampdu frame]" : "[mpdu frame]");
	printk("BF_RPT = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_BF_RPT, le32_to_cpu(rxd[2])));

	/* dw3 */
	printk("RXV_SN = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_RXV_SN, le32_to_cpu(rxd[3])));
	printk("CH_FREQUENCY = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_CH_FREQUENCY, le32_to_cpu(rxd[3])));
	printk("A1_TYPE = %d%s%s%s%s\n",
			GET_FIELD(WF_RX_DESCRIPTOR_A1_TYPE, le32_to_cpu(rxd[3])),
			GET_FIELD(WF_RX_DESCRIPTOR_A1_TYPE, le32_to_cpu(rxd[3])) == 0 ?
			"[reserved]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_A1_TYPE, le32_to_cpu(rxd[3])) == 1 ?
			"[uc2me]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_A1_TYPE, le32_to_cpu(rxd[3])) == 2 ?
			"[mc]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_A1_TYPE, le32_to_cpu(rxd[3])) == 3 ?
			"[bc]" : "");
	printk("HTC = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_HTC, le32_to_cpu(rxd[3])));
	printk("TCL = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_TCL, le32_to_cpu(rxd[3])));
	printk("BBM = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_BBM, le32_to_cpu(rxd[3])));
	printk("BU = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_BU, le32_to_cpu(rxd[3])));
	printk("CO_ANT = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_CO_ANT, le32_to_cpu(rxd[3])));
	printk("BF_CQI = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_BF_CQI, le32_to_cpu(rxd[3])));
	printk("FC = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_FC, le32_to_cpu(rxd[3])));
	printk("VLAN = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_VLAN, le32_to_cpu(rxd[3])));

	/* dw4 */
	printk("PF = %d%s%s%s%s\n",
			GET_FIELD(WF_RX_DESCRIPTOR_PF, le32_to_cpu(rxd[4])),
			GET_FIELD(WF_RX_DESCRIPTOR_PF, le32_to_cpu(rxd[4])) == 0 ?
			"[msdu]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_PF, le32_to_cpu(rxd[4])) == 1 ?
			"[final amsdu]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_PF, le32_to_cpu(rxd[4])) == 2 ?
			"[middle amsdu]" : "",
			GET_FIELD(WF_RX_DESCRIPTOR_PF, le32_to_cpu(rxd[4])) == 3 ?
			"[first amsdu]" : "");
	printk("MAC = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_MAC, le32_to_cpu(rxd[4])));
	printk("TID = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_TID, le32_to_cpu(rxd[4])));
	printk("ETHER_TYPE_OFFSET = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_ETHER_TYPE_OFFSET, le32_to_cpu(rxd[4])));
	printk("IP = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_IP, le32_to_cpu(rxd[4])));
	printk("UT = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_UT, le32_to_cpu(rxd[4])));
	printk("PSE_FID = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_PSE_FID, le32_to_cpu(rxd[4])));

	/* group4 */
	/* dw0 */
	printk("FRAME_CONTROL_FIELD = 0x%x\n",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_4 ?
			GET_FIELD(WF_RX_DESCRIPTOR_FRAME_CONTROL_FIELD, le32_to_cpu(rxd[8])) : 0);
	printk("PEER_MLD_ADDRESS_15_0 = 0x%x\n",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_4 ?
			GET_FIELD(WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_15_0_,
			le32_to_cpu(rxd[8])) : 0);

	/* dw1 */
	printk("PEER_MLD_ADDRESS_47_16 = 0x%x\n",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_4 ?
			GET_FIELD(WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_47_16_,
			le32_to_cpu(rxd[9])) : 0);

	/* dw2 */
	printk("FRAGMENT_NUMBER = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_4 ?
			GET_FIELD(WF_RX_DESCRIPTOR_FRAGMENT_NUMBER,
			le32_to_cpu(rxd[10])) : 0);
	printk("SEQUENCE_NUMBER = %d\n",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_4 ?
			GET_FIELD(WF_RX_DESCRIPTOR_SEQUENCE_NUMBER,
			le32_to_cpu(rxd[10])) : 0);
	printk("QOS_CONTROL_FIELD = 0x%x\n",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_4 ?
			GET_FIELD(WF_RX_DESCRIPTOR_QOS_CONTROL_FIELD,
			le32_to_cpu(rxd[10])) : 0);

	/* dw3 */
	printk("HT_CONTROL_FIELD = 0x%x\n",
			GET_FIELD(WF_RX_DESCRIPTOR_GROUP_VLD, le32_to_cpu(rxd[1]))
			& BMAC_GROUP_VLD_4 ?
			GET_FIELD(WF_RX_DESCRIPTOR_HT_CONTROL_FIELD,
			le32_to_cpu(rxd[11])) : 0);
}

static int mt7996_token_txd_read(struct seq_file *s, void *data)
{
	struct mt7996_dev *dev = dev_get_drvdata(s->private);
	struct mt76_txwi_cache *t;
	u8* txwi;

	seq_printf(s, "\n");
	spin_lock_bh(&dev->mt76.token_lock);

	t = idr_find(&dev->mt76.token, dev->dbg.token_idx);
	if (t != NULL) {
		struct mt76_dev *mdev = &dev->mt76;
		txwi = ((u8*)(t)) - (mdev->drv->txwi_size);
		/* dump one txd info */
		dev->dbg.txd_read_cnt = 1;
		mt7996_dump_bmac_txd_info(s, dev, (__le32 *)txwi, true, true);
		seq_printf(s, "\n");
		seq_printf(s, "[SKB]\n");
		seq_hex_dump(s, "", DUMP_PREFIX_OFFSET, 16, 1, (u8 *)t->skb->data, t->skb->len, false);
		seq_printf(s, "\n");
	}
	spin_unlock_bh(&dev->mt76.token_lock);
	return 0;
}

#if 0
// TODO:  Re-enable
static int mt7996_rx_msdu_pg_read(struct seq_file *s, void *data)
{
	struct mt7996_dev *dev = dev_get_drvdata(s->private);
	struct list_head *p;
	int i, count = 0, total = 0;

	seq_printf(s, "Rx Msdu page:\n");
	spin_lock(&dev->wed_rro.lock);
	for (i = 0; i < MT7996_RRO_MSDU_PG_HASH_SIZE; i++) {
		list_for_each(p, &dev->wed_rro.pg_hash_head[i]) {
			count++;
		}
	}

	total = count;
	list_for_each(p, &dev->wed_rro.pg_addr_cache) {
		total++;
	}
	seq_printf(s, "\ttotal:%8d used:%8d\n", total, count);
	spin_unlock(&dev->wed_rro.lock);

	return 0;
}
#endif

static int
mt7996_mat_table_show(struct seq_file *s, void *data)
{
#define MT_MAX_MAT_TABLE_SIZE	63
	struct mt7996_dev *dev = s->private;
	int i;

	for (i = 0; i < MT_MAX_MAT_TABLE_SIZE; i++) {
		u32 req = MT_WF_UWTBL_ITCR_SET |
			  u32_encode_bits(i, MT_WF_UWTBL_ITCR_INDEX);
		u32 dw[2];
		u8 *addr = (u8 *)dw;

		mt76_wr(dev, MT_WF_UWTBL_ITCR, req);
		dw[0] = mt76_rr(dev, MT_WF_UWTBL_ITCR0);
		dw[1] = mt76_rr(dev, MT_WF_UWTBL_ITCR1);

		if (dw[0] || dw[1])
			seq_printf(s, "own_mld_id%d\tAddr: %pM\n", i, addr);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_mat_table);

static int
mt7996_rmac_table_show(struct seq_file *s, void *data)
{
	struct mt7996_phy *phy = s->private;
	struct mt7996_dev *dev = phy->dev;
	unsigned long usage_bitmap[2] = {0};
	int i, j;
	u8 band = phy->mt76->band_idx;

	usage_bitmap[0] = (unsigned long)mt76_rr(dev, MT_WF_RMAC_SRAM_BITMAP0(band));
	usage_bitmap[1] = (unsigned long)mt76_rr(dev, MT_WF_RMAC_SRAM_BITMAP1(band));

	for (i = 0; i < 2; i++) {
		for_each_set_bit(j, &usage_bitmap[i], 32) {
			u32 req = MT_WF_RMAC_MEM_CRTL_TRIG |
				  u32_encode_bits(i * 32 + j, MT_WF_RMAC_MEM_CRTL_TDX);
			u32 dw[2];
			u8 *addr = (u8 *)dw;

			mt76_wr(dev, MT_WF_RMAC_MEM_CTRL(band), req);
			dw[0] = mt76_rr(dev, MT_WF_RMAC_SRAM_DATA0(band));
			dw[1] = mt76_rr(dev, MT_WF_RMAC_SRAM_DATA1(band));

			seq_printf(s, "omac_idx%d\tAddr: %pM\n", i * 32 + j, addr);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_rmac_table);

static int
mt7996_agg_table_show(struct seq_file *s, void *data)
{
	struct mt7996_phy *phy = s->private;
	struct mt7996_dev *dev = phy->dev;
	int i, j;
	u8 band = phy->mt76->band_idx;

	for (i = 0; i < 4; i++) {
		u32 value = mt76_rr(dev, MT_AGG_REMAP_CTRL(band) + 4 * i);

		for (j = 0; j < 4; j++) {
			u8 shift = 8 * j;
			u32 mask = MT_AGG_REMAP_CTRL_OM_REMAP << shift;

			seq_printf(s, "idx%d: %d\n", i * 4 + j,
				      (value & mask) >> shift);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt7996_agg_table);

static ssize_t mt7996_mlo_agc_tx_set(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct mt7996_dev *dev = file->private_data;
	struct mt7996_mlo_agc_set req;
	char buf[100];
	int ret;
	u16 mgf;

	memset(&req, 0, sizeof(req));

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hhu %hhu %hhu %hhu %hu %hhu %hhu",
		   &req.mld_id, &req.link_id, &req.ac, &req.disp_pol,
		   &mgf, &req.ratio, &req.order) != 7) {
		dev_warn(dev->mt76.dev,
			 "format: [MldRecIdx] [Link] [Ac] [DispPol] [MGF] [Ratio] [Order]\n");
		goto out;
	}

	req.tag = cpu_to_le16(UNI_CMD_MLO_AGC_TX);
	req.len = cpu_to_le16(sizeof(req) - 4);
	req.mgf = cpu_to_le16(mgf);

	ret = mt7996_mcu_mlo_agc(dev, &req, sizeof(req));
	if (ret)
		return -EFAULT;

out:
	return count;
}

static const struct file_operations fops_mlo_agc_tx = {
	.write = mt7996_mlo_agc_tx_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t mt7996_be_txop_set(struct file *file,
			       const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct mt7996_dev *dev = file->private_data;
	char buf[100], role[4];
	u32 ofs;
	u16 txop, decimal;
	int i = 0;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%3s %hu.%hu", role, &txop, &decimal) != 3)
		goto err;

	if (!strncmp(role, "ap", 2))
		ofs = MT_WF_TMAC_WMM0_OFFSET;
	else if (!strncmp(role, "sta", 3))
		ofs = MT_WF_TMAC_WMM3_OFFSET;
	else
		goto err;

	/* Change unit to 32 us */
	txop = (txop * 1000 + decimal * 100 + 16) >> 5;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		if (!dev->mt76.phys[i])
			continue;

		mt76_rmw(dev, MT_WF_TMAC(i, ofs), MT_WF_TMAC_WMM_TXOP_MASK,
			 txop << MT_WF_TMAC_WMM_TXOP_SHIFT);
	}

	return count;
err:
	dev_warn(dev->mt76.dev,
		 "format: [ap|sta] [tx_queue_data2_burst]\n");
	return -EINVAL;
}

static ssize_t mt7996_be_txop_dump(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct mt7996_dev *dev = file->private_data;
	static const size_t size = 2048;
	int len = 0, i, ret;
	char *buf;
	enum {
		AP,
		STA,
		MAX_IF_TYPE,
	};

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, size - len, "Band\tAP (WMM0)\t\tSTA (WMM3)\n");

	for (i = 0; i < __MT_MAX_BAND; i++) {
		u32 txop[MAX_IF_TYPE], tx_burst[MAX_IF_TYPE];

		if (!dev->mt76.phys[i])
			continue;

#define MT7996_READ_TXOP(role, base)						\
do {										\
	txop[role] = mt76_rr(dev, MT_WF_TMAC(i, base));				\
	tx_burst[role] = u32_get_bits(txop[role], MT_WF_TMAC_WMM_TXOP_MASK);	\
	tx_burst[role] = tx_burst[role] ? ((tx_burst[role] << 5) - 16) / 100 : 0;\
} while (0)
		MT7996_READ_TXOP(AP, MT_WF_TMAC_WMM0_OFFSET);
		MT7996_READ_TXOP(STA, MT_WF_TMAC_WMM3_OFFSET);
#undef MT7996_READ_TXOP

		len += scnprintf(buf + len, size - len,
			"%d\t0x%08x (%1u.%1u) \t0x%08x (%1u.%1u)\n", i,
			txop[AP], tx_burst[AP] / 10, tx_burst[AP] % 10,
			txop[STA], tx_burst[STA] / 10, tx_burst[STA] % 10);
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret;
}

static const struct file_operations fops_mt7996_txop = {
	.write = mt7996_be_txop_set,
	.read = mt7996_be_txop_dump,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t mt7996_mlo_agc_trig_set(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct mt7996_dev *dev = file->private_data;
	struct mt7996_mlo_agc_set req;
	char buf[100];
	int ret;
	u16 mgf;

	memset(&req, 0, sizeof(req));

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%hhu %hhu %hhu %hhu %hu %hhu",
		   &req.mld_id, &req.link_id, &req.ac, &req.disp_pol,
		   &mgf, &req.ratio) != 6) {
		dev_warn(dev->mt76.dev,
			 "format: [MldRecIdx] [Link] [Ac] [DispPol] [MGF] [Ratio]\n");
		goto out;
	}

	req.tag = cpu_to_le16(UNI_CMD_MLO_AGC_TRIG);
	req.len = cpu_to_le16(sizeof(req) - 4);
	req.mgf = cpu_to_le16(mgf);

	ret = mt7996_mcu_mlo_agc(dev, &req, sizeof(req));
	if (ret)
		return -EFAULT;

out:
	return count;
}

static const struct file_operations fops_mlo_agc_trig = {
	.write = mt7996_mlo_agc_trig_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int mt7996_mtk_init_dev_debugfs_internal(struct mt7996_phy *phy, struct dentry *dir)
{
	struct mt7996_dev *dev = phy->dev;

	debugfs_create_devm_seqfile(dev->mt76.dev, "token_txd", dir,
				    mt7996_token_txd_read);
	debugfs_create_u32("txd_dump", 0600, dir, &dev->dbg.txd_read_cnt);
	debugfs_create_u32("rxd_dump", 0600, dir, &dev->dbg.rxd_read_cnt);
	//debugfs_create_devm_seqfile(dev->mt76.dev, "rx_msdu_pg", dir,
	//			    mt7996_rx_msdu_pg_read);

	/* ple/pse fid raw data dump */
	debugfs_create_u32("fid_idx", 0600, dir, &dev->dbg.fid_idx);
	debugfs_create_devm_seqfile(dev->mt76.dev, "ple_fid", dir,
				    mt7996_ple_fid_read);
	debugfs_create_devm_seqfile(dev->mt76.dev, "pse_fid", dir,
				    mt7996_pse_fid_read);

	debugfs_create_u8("dump_ple_txd", 0600, dir, &dev->dbg.dump_ple_txd);
	debugfs_create_file("txop", 0600, dir, dev, &fops_mt7996_txop);

	/* MLO related Table */
	debugfs_create_file("mat_table", 0400, dir, dev, &mt7996_mat_table_fops);
	debugfs_create_file("mlo_agc_tx", 0200, dir, dev, &fops_mlo_agc_tx);
	debugfs_create_file("mlo_agc_trig", 0200, dir, dev, &fops_mlo_agc_trig);

	return 0;
}

int mt7996_mtk_init_band_debugfs_internal(struct mt7996_phy *phy, struct dentry *dir)
{
	/* MLO related Table */
	debugfs_create_file("rmac_table", 0400, dir, phy, &mt7996_rmac_table_fops);
	debugfs_create_file("agg_table", 0400, dir, phy, &mt7996_agg_table_fops);

	return 0;
}
#endif
