/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../pci.h"
#include "../ps.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "table.h"
#include "trx.h"

static u32 _rtl8723be_phy_rf_serial_read(struct ieee80211_hw *hw,
					 enum radio_path rfpath, u32 offset);
static void _rtl8723be_phy_rf_serial_write(struct ieee80211_hw *hw,
					   enum radio_path rfpath,
					   u32 offset, u32 data);
static u32 _rtl8723be_phy_calculate_bit_shift(u32 bitmask);
static bool _rtl8723be_phy_bb8723b_config_parafile(struct ieee80211_hw *hw);
static bool _rtl8723be_phy_config_mac_with_headerfile(struct ieee80211_hw *hw);
static bool _rtl8723be_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
						     u8 configtype);
static bool _rtl8723be_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
						       u8 configtype);
static void _rtl8723be_phy_init_bb_rf_register_definition(
						struct ieee80211_hw *hw);
static bool _rtl8723be_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
						u32 cmdtableidx,
						u32 cmdtablesz,
						enum swchnlcmd_id cmdid,
						u32 para1, u32 para2,
						u32 msdelay);
static bool _rtl8723be_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
						u8 channel, u8 *stage,
						u8 *step, u32 *delay);

static long _rtl8723be_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					    enum wireless_mode wirelessmode,
					    u8 txpwridx);
static void rtl8723be_phy_set_rf_on(struct ieee80211_hw *hw);
static void rtl8723be_phy_set_io(struct ieee80211_hw *hw);

u32 rtl8723be_phy_query_bb_reg(struct ieee80211_hw *hw,
			       u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 returnvalue, originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x)\n", regaddr, bitmask);
	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	bitshift = _rtl8723be_phy_calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "BBR MASK=0x%x Addr[0x%x]=0x%x\n",
		  bitmask, regaddr, originalvalue);

	return returnvalue;

}

void rtl8723be_phy_set_bb_reg(struct ieee80211_hw *hw, u32 regaddr,
			      u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		  regaddr, bitmask, data);

	if (bitmask != MASKDWORD) {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = _rtl8723be_phy_calculate_bit_shift(bitmask);
		data = ((originalvalue & (~bitmask)) | (data << bitshift));
	}

	rtl_write_dword(rtlpriv, regaddr, data);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		  regaddr, bitmask, data);

}

u32 rtl8723be_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			       u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		  regaddr, rfpath, bitmask);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);


	original_value = _rtl8723be_phy_rf_serial_read(hw, rfpath, regaddr);
	bitshift = _rtl8723be_phy_calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x), original_value(%#x)\n",
		 regaddr, rfpath, bitmask, original_value);

	return readback_value;
}

void rtl8723be_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path path,
			      u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		  regaddr, bitmask, data, path);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	if (bitmask != RFREG_OFFSET_MASK) {
			original_value = _rtl8723be_phy_rf_serial_read(hw, path,
								       regaddr);
			bitshift = _rtl8723be_phy_calculate_bit_shift(bitmask);
			data = ((original_value & (~bitmask)) |
				(data << bitshift));
		}

	_rtl8723be_phy_rf_serial_write(hw, path, regaddr, data);

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		  regaddr, bitmask, data, path);

}

static u32 _rtl8723be_phy_rf_serial_read(struct ieee80211_hw *hw,
					 enum radio_path rfpath, u32 offset)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 newoffset;
	u32 tmplong, tmplong2;
	u8 rfpi_enable = 0;
	u32 retvalue;

	offset &= 0xff;
	newoffset = offset;
	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "return all one\n");
		return 0xFFFFFFFF;
	}
	tmplong = rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD);
	if (rfpath == RF90_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = rtl_get_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD);
	tmplong2 = (tmplong2 & (~BLSSIREADADDRESS)) |
		   (newoffset << 23) | BLSSIREADEDGE;
	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD,
		      tmplong & (~BLSSIREADEDGE));
	mdelay(1);
	rtl_set_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD, tmplong2);
	mdelay(1);
	/*rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD,
		      tmplong | BLSSIREADEDGE);*/
	mdelay(1);
	if (rfpath == RF90_PATH_A)
		rfpi_enable = (u8) rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER1,
						 BIT(8));
	else if (rfpath == RF90_PATH_B)
		rfpi_enable = (u8) rtl_get_bbreg(hw, RFPGA0_XB_HSSIPARAMETER1,
						 BIT(8));
	if (rfpi_enable)
		retvalue = rtl_get_bbreg(hw, pphyreg->rflssi_readbackpi,
					 BLSSIREADBACKDATA);
	else
		retvalue = rtl_get_bbreg(hw, pphyreg->rflssi_readback,
					 BLSSIREADBACKDATA);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "RFR-%d Addr[0x%x]=0x%x\n",
		  rfpath, pphyreg->rflssi_readback, retvalue);
	return retvalue;
}

static void _rtl8723be_phy_rf_serial_write(struct ieee80211_hw *hw,
					   enum radio_path rfpath,
					   u32 offset, u32 data)
{
	u32 data_and_addr;
	u32 newoffset;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];

	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "stop\n");
		return;
	}
	offset &= 0xff;
	newoffset = offset;
	data_and_addr = ((newoffset << 20) | (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, MASKDWORD, data_and_addr);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "RFW-%d Addr[0x%x]=0x%x\n", rfpath,
		   pphyreg->rf3wire_offset, data_and_addr);
}

static u32 _rtl8723be_phy_calculate_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}
	return i;
}

bool rtl8723be_phy_mac_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool rtstatus = _rtl8723be_phy_config_mac_with_headerfile(hw);

	rtl_write_byte(rtlpriv, 0x04CA, 0x0B);
	return rtstatus;
}

bool rtl8723be_phy_bb_config(struct ieee80211_hw *hw)
{
	bool rtstatus = true;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 regval;
	u8 b_reg_hwparafile = 1;
	u32 tmp;
	u8 crystalcap = rtlpriv->efuse.crystalcap;
	_rtl8723be_phy_init_bb_rf_register_definition(hw);
	regval = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN,
		       regval | BIT(13) | BIT(0) | BIT(1));

	rtl_write_byte(rtlpriv, REG_RF_CTRL, RF_EN | RF_RSTB | RF_SDMRSTB);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN,
		       FEN_PPLL | FEN_PCIEA | FEN_DIO_PCIE |
		       FEN_BB_GLB_RSTn | FEN_BBRSTB);
	tmp = rtl_read_dword(rtlpriv, 0x4c);
	rtl_write_dword(rtlpriv, 0x4c, tmp | BIT(23));

	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL + 1, 0x80);

	if (b_reg_hwparafile == 1)
		rtstatus = _rtl8723be_phy_bb8723b_config_parafile(hw);

	crystalcap = crystalcap & 0x3F;
	rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0xFFF000,
		      (crystalcap | crystalcap << 6));

	return rtstatus;
}

bool rtl8723be_phy_rf_config(struct ieee80211_hw *hw)
{
	return rtl8723be_phy_rf6052_config(hw);
}

static bool _rtl8723be_check_condition(struct ieee80211_hw *hw,
				       const u32  condition)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u32 _board = rtlefuse->board_type; /*need efuse define*/
	u32 _interface = rtlhal->interface;
	u32 _platform = 0x08;/*SupportPlatform */
	u32 cond = condition;

	if (condition == 0xCDCDCDCD)
		return true;

	cond = condition & 0xFF;
	if ((_board & cond) == 0 && cond != 0x1F)
		return false;

	cond = condition & 0xFF00;
	cond = cond >> 8;
	if ((_interface & cond) == 0 && cond != 0x07)
		return false;

	cond = condition & 0xFF0000;
	cond = cond >> 16;
	if ((_platform & cond) == 0 && cond != 0x0F)
		return false;
	return true;
}

static void _rtl8723be_config_rf_reg(struct ieee80211_hw *hw, u32 addr,
				     u32 data, enum radio_path rfpath,
				     u32 regaddr)
{
	if (addr == 0xfe || addr == 0xffe) {
		/* In order not to disturb BT music
			when wifi init.(1ant NIC only) */
		/*mdelay(50);*/
		/* In order not to disturb BT music
			when wifi init.(1ant NIC only) */
		mdelay(50);
	} else {
		rtl_set_rfreg(hw, rfpath, regaddr, RFREG_OFFSET_MASK, data);
		udelay(1);
	}
}
static void _rtl8723be_config_rf_radio_a(struct ieee80211_hw *hw,
					 u32 addr, u32 data)
{
	u32 content = 0x1000; /*RF Content: radio_a_txt*/
	u32 maskforphyset = (u32)(content & 0xE000);

	_rtl8723be_config_rf_reg(hw, addr, data, RF90_PATH_A,
				 addr | maskforphyset);

}

static void _rtl8723be_phy_init_tx_power_by_rate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	u8 band, path, txnum, section;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band)
		for (path = 0; path < TX_PWR_BY_RATE_NUM_RF; ++path)
			for (txnum = 0; txnum < TX_PWR_BY_RATE_NUM_RF; ++txnum)
				for (section = 0;
				     section < TX_PWR_BY_RATE_NUM_SECTION;
				     ++section)
					rtlphy->tx_power_by_rate_offset[band][path][txnum][section] = 0;
}

static void _rtl8723be_config_bb_reg(struct ieee80211_hw *hw,
				     u32 addr, u32 data)
{
	if (addr == 0xfe) {
		mdelay(50);
	} else if (addr == 0xfd) {
		mdelay(5);
	} else if (addr == 0xfc) {
		mdelay(1);
	} else if (addr == 0xfb) {
		udelay(50);
	} else if (addr == 0xfa) {
		udelay(5);
	} else if (addr == 0xf9) {
		udelay(1);
	} else {
		rtl_set_bbreg(hw, addr, MASKDWORD, data);
		udelay(1);
	}
}

static void _rtl8723be_phy_set_txpower_by_rate_base(struct ieee80211_hw *hw, u8 band,
					     u8 path, u8 rate_section,
					     u8 txnum, u8 value)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Rf Path %d in phy_SetTxPowerByRatBase()\n",
			  path);
		return;
	}

	if (band == BAND_ON_2_4G) {
		switch (rate_section) {
		case CCK:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][0] = value;
			break;
		case OFDM:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][1] = value;
			break;
		case HT_MCS0_MCS7:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][2] = value;
			break;
		case HT_MCS8_MCS15:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][3] = value;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 2.4G, Rf Path %d, %dTx in PHY_SetTxPowerByRateBase()\n",
				 rate_section, path, txnum);
			break;
		};
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Band %d in PHY_SetTxPowerByRateBase()\n",
			 band);
	}

}

static u8 _rtl8723be_phy_get_txpower_by_rate_base(struct ieee80211_hw *hw, u8 band,
					   u8 path, u8 txnum, u8 rate_section)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 value = 0;
	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Rf Path %d in PHY_GetTxPowerByRateBase()\n",
			  path);
		return 0;
	}

	if (band == BAND_ON_2_4G) {
		switch (rate_section) {
		case CCK:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][0];
			break;
		case OFDM:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][1];
			break;
		case HT_MCS0_MCS7:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][2];
			break;
		case HT_MCS8_MCS15:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][3];
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 2.4G, Rf Path %d, %dTx in PHY_GetTxPowerByRateBase()\n",
				 rate_section, path, txnum);
			break;
		};
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Band %d in PHY_GetTxPowerByRateBase()\n",
			 band);
	}

	return value;
}

static void _rtl8723be_phy_store_txpower_by_rate_base(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u16 rawValue = 0;
	u8 base = 0, path = 0;

	for (path = RF90_PATH_A; path <= RF90_PATH_B; ++path) {
		if (path == RF90_PATH_A) {
			rawValue = (u16) (rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][3] >> 24) & 0xFF;
			base = (rawValue >> 4) * 10 + (rawValue & 0xF);
			_rtl8723be_phy_set_txpower_by_rate_base(hw,
				BAND_ON_2_4G, path, CCK, RF_1TX, base);
		} else if (path == RF90_PATH_B) {
			rawValue = (u16) (rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][3] >> 0) & 0xFF;
			base = (rawValue >> 4) * 10 + (rawValue & 0xF);
			_rtl8723be_phy_set_txpower_by_rate_base(hw,
								BAND_ON_2_4G,
								path, CCK,
								RF_1TX, base);
		}
		rawValue = (u16) (rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][1] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8723be_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G,
							path, OFDM, RF_1TX,
							base);

		rawValue = (u16) (rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][5] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8723be_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G,
							path, HT_MCS0_MCS7,
							RF_1TX, base);

		rawValue = (u16) (rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_2TX][7] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8723be_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G,
							path, HT_MCS8_MCS15,
							RF_2TX, base);
	}
}

static void _phy_convert_txpower_dbm_to_relative_value(u32 *data, u8 start,
						u8 end, u8 base_val)
{
	char i = 0;
	u8 temp_value = 0;
	u32 temp_data = 0;

	for (i = 3; i >= 0; --i) {
		if (i >= start && i <= end) {
			/* Get the exact value */
			temp_value = (u8) (*data >> (i * 8)) & 0xF;
			temp_value += ((u8) ((*data >> (i*8 + 4)) & 0xF)) * 10;

			/* Change the value to a relative value */
			temp_value = (temp_value > base_val) ?
				     temp_value - base_val :
				     base_val - temp_value;
		} else {
			temp_value = (u8) (*data >> (i * 8)) & 0xFF;
		}
		temp_data <<= 8;
		temp_data |= temp_value;
	}
	*data = temp_data;
}

static void _rtl8723be_phy_convert_txpower_dbm_to_relative_value(
							struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 base = 0, rfPath = RF90_PATH_A;

	base = _rtl8723be_phy_get_txpower_by_rate_base(hw,
			BAND_ON_2_4G, rfPath, RF_1TX, CCK);
	_phy_convert_txpower_dbm_to_relative_value(
	    &(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][2]),
	    1, 1, base);
	_phy_convert_txpower_dbm_to_relative_value(
	    &(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][3]),
	    1, 3, base);

	base = _rtl8723be_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath,
						       RF_1TX, OFDM);
	_phy_convert_txpower_dbm_to_relative_value(
	    &(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][0]),
	    0, 3, base);
	_phy_convert_txpower_dbm_to_relative_value(
	    &(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][1]),
	    0, 3, base);

	base = _rtl8723be_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G,
						rfPath, RF_1TX, HT_MCS0_MCS7);
	_phy_convert_txpower_dbm_to_relative_value(
	    &(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][4]),
	    0, 3, base);
	_phy_convert_txpower_dbm_to_relative_value(
	    &(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][5]),
	    0, 3, base);

	base = _rtl8723be_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G,
						rfPath, RF_2TX, HT_MCS8_MCS15);
	_phy_convert_txpower_dbm_to_relative_value(
	    &(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][6]),
	    0, 3, base);

	_phy_convert_txpower_dbm_to_relative_value(
	    &(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][7]),
	    0, 3, base);

	RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
	    "<===_rtl8723be_phy_convert_txpower_dbm_to_relative_value()\n");

}

static void _rtl8723be_phy_txpower_by_rate_configuration(
							struct ieee80211_hw *hw)
{
	_rtl8723be_phy_store_txpower_by_rate_base(hw);
	_rtl8723be_phy_convert_txpower_dbm_to_relative_value(hw);
}

static bool _rtl8723be_phy_bb8723b_config_parafile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus;

	rtstatus = _rtl8723be_phy_config_bb_with_headerfile(hw,
						BASEBAND_CONFIG_PHY_REG);
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Write BB Reg Fail!!");
		return false;
	}
	_rtl8723be_phy_init_tx_power_by_rate(hw);
	if (rtlefuse->autoload_failflag == false) {
		rtlphy->pwrgroup_cnt = 0;
		rtstatus = _rtl8723be_phy_config_bb_with_pgheaderfile(hw,
						BASEBAND_CONFIG_PHY_REG);
	}
	_rtl8723be_phy_txpower_by_rate_configuration(hw);
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "BB_PG Reg Fail!!");
		return false;
	}
	rtstatus = _rtl8723be_phy_config_bb_with_headerfile(hw,
						BASEBAND_CONFIG_AGC_TAB);
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "AGC Table Fail\n");
		return false;
	}
	rtlphy->cck_high_power = (bool) (rtl_get_bbreg(hw,
						       RFPGA0_XA_HSSIPARAMETER2,
						       0x200));

	return true;
}

static bool _rtl8723be_phy_config_mac_with_headerfile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;
	u32 arraylength;
	u32 *ptrarray;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Read rtl8723beMACPHY_Array\n");
	arraylength = RTL8723BEMAC_1T_ARRAYLEN;
	ptrarray = RTL8723BEMAC_1T_ARRAY;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Img:RTL8723bEMAC_1T_ARRAY LEN %d\n", arraylength);
	for (i = 0; i < arraylength; i = i + 2)
		rtl_write_byte(rtlpriv, ptrarray[i], (u8) ptrarray[i + 1]);
	return true;
}

static bool _rtl8723be_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
						     u8 configtype)
{
	#define READ_NEXT_PAIR(v1, v2, i) \
		do { \
			i += 2; \
			v1 = array_table[i];\
			v2 = array_table[i+1]; \
		} while (0)

	int i;
	u32 *array_table;
	u16 arraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 v1 = 0, v2 = 0;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		arraylen = RTL8723BEPHY_REG_1TARRAYLEN;
		array_table = RTL8723BEPHY_REG_1TARRAY;

		for (i = 0; i < arraylen; i = i + 2) {
			v1 = array_table[i];
			v2 = array_table[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl8723be_config_bb_reg(hw, v1, v2);
			} else {/*This line is the start line of branch.*/
				/* to protect READ_NEXT_PAIR not overrun */
				if (i >= arraylen - 2)
					break;

				if (!_rtl8723be_check_condition(hw,
						array_table[i])) {
					/*Discard the following
					(offset, data) pairs*/
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(v1, v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				/*Configure matched pairs and
				 *skip to end of if-else.*/
				} else {
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						_rtl8723be_config_bb_reg(hw,
								    v1, v2);
						READ_NEXT_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD && i < arraylen - 2)
						READ_NEXT_PAIR(v1, v2, i);

				}
			}
		}
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		arraylen = RTL8723BEAGCTAB_1TARRAYLEN;
		array_table = RTL8723BEAGCTAB_1TARRAY;

		for (i = 0; i < arraylen; i = i + 2) {
			v1 = array_table[i];
			v2 = array_table[i+1];
			if (v1 < 0xCDCDCDCD) {
				rtl_set_bbreg(hw, array_table[i],
					      MASKDWORD,
					      array_table[i + 1]);
				udelay(1);
				continue;
			} else {/*This line is the start line of branch.*/
				/* to protect READ_NEXT_PAIR not overrun */
				if (i >= arraylen - 2)
					break;

				if (!_rtl8723be_check_condition(hw,
					array_table[i])) {
					/*Discard the following
					 *(offset, data) pairs*/
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(v1, v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				/*Configure matched pairs and
				 *skip to end of if-else.*/
				} else {
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						rtl_set_bbreg(hw, array_table[i],
							MASKDWORD,
							array_table[i + 1]);
						udelay(1);
						READ_NEXT_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD && i < arraylen - 2)
						READ_NEXT_PAIR(v1, v2, i);

				}
			}
			RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
				 "The agctab_array_table[0] is %x Rtl818EEPHY_REGArray[1] is %x\n",
				 array_table[i], array_table[i + 1]);
		}
	}
	return true;
}

static u8 _rtl8723be_get_rate_section_index(u32 regaddr)
{
	u8 index = 0;

	switch (regaddr) {
	case RTXAGC_A_RATE18_06:
		index = 0;
	break;
	case RTXAGC_A_RATE54_24:
		index = 1;
	break;
	case RTXAGC_A_CCK1_MCS32:
		index = 2;
	break;
	case RTXAGC_B_CCK11_A_CCK2_11:
		index = 3;
	break;
	case RTXAGC_A_MCS03_MCS00:
		index = 4;
	break;
	case RTXAGC_A_MCS07_MCS04:
		index = 5;
	break;
	case RTXAGC_A_MCS11_MCS08:
		index = 6;
	break;
	case RTXAGC_A_MCS15_MCS12:
		index = 7;
	break;
	case RTXAGC_B_RATE18_06:
		index = 0;
	break;
	case RTXAGC_B_RATE54_24:
		index = 1;
	break;
	case RTXAGC_B_CCK1_55_MCS32:
		index = 2;
	break;
	case RTXAGC_B_MCS03_MCS00:
		index = 4;
	break;
	case RTXAGC_B_MCS07_MCS04:
		index = 5;
	break;
	case RTXAGC_B_MCS11_MCS08:
		index = 6;
	break;
	case RTXAGC_B_MCS15_MCS12:
		index = 7;
	break;
	default:
		regaddr &= 0xFFF;
		if (regaddr >= 0xC20 && regaddr <= 0xC4C)
			index = (u8) ((regaddr - 0xC20) / 4);
		else if (regaddr >= 0xE20 && regaddr <= 0xE4C)
			index = (u8) ((regaddr - 0xE20) / 4);
		break;
	};
	return index;
}

static void _rtl8723be_store_tx_power_by_rate(struct ieee80211_hw *hw,
					      u32 band, u32 rfpath,
					      u32 txnum, u32 regaddr,
					      u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 rate_section = _rtl8723be_get_rate_section_index(regaddr);

	if (band != BAND_ON_2_4G && band != BAND_ON_5G) {
		RT_TRACE(rtlpriv, FPHY, PHY_TXPWR, "Invalid Band %d\n", band);
		return;
	}
	if (rfpath > MAX_RF_PATH - 1) {
		RT_TRACE(rtlpriv, FPHY, PHY_TXPWR, "Invalid RfPath %d\n", rfpath);
		return;
	}
	if (txnum > MAX_RF_PATH - 1) {
		RT_TRACE(rtlpriv, FPHY, PHY_TXPWR, "Invalid TxNum %d\n", txnum);
		return;
	}

	rtlphy->tx_power_by_rate_offset[band][rfpath][txnum][rate_section] =
									data;

}

static bool _rtl8723be_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
						       u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i;
	u32 *phy_regarray_table_pg;
	u16 phy_regarray_pg_len;
	u32 v1 = 0, v2 = 0, v3 = 0, v4 = 0, v5 = 0, v6 = 0;

	phy_regarray_pg_len = RTL8723BEPHY_REG_ARRAY_PGLEN;
	phy_regarray_table_pg = RTL8723BEPHY_REG_ARRAY_PG;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_regarray_pg_len; i = i + 6) {
			v1 = phy_regarray_table_pg[i];
			v2 = phy_regarray_table_pg[i+1];
			v3 = phy_regarray_table_pg[i+2];
			v4 = phy_regarray_table_pg[i+3];
			v5 = phy_regarray_table_pg[i+4];
			v6 = phy_regarray_table_pg[i+5];

			if (v1 < 0xcdcdcdcd) {
				if (phy_regarray_table_pg[i] == 0xfe ||
				    phy_regarray_table_pg[i] == 0xffe)
					mdelay(50);
				else
					_rtl8723be_store_tx_power_by_rate(hw,
							v1, v2, v3, v4, v5, v6);
				continue;
			}
		}
	} else {

		RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
			 "configtype != BaseBand_Config_PHY_REG\n");
	}
	return true;
}

bool rtl8723be_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					     enum radio_path rfpath)
{
	#define READ_NEXT_RF_PAIR(v1, v2, i) \
		do { \
			i += 2; \
			v1 = radioa_array_table[i]; \
			v2 = radioa_array_table[i+1]; \
		} while (0)

	int i;
	bool rtstatus = true;
	u32 *radioa_array_table;
	u16 radioa_arraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 v1 = 0, v2 = 0;

	radioa_arraylen = RTL8723BE_RADIOA_1TARRAYLEN;
	radioa_array_table = RTL8723BE_RADIOA_1TARRAY;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Radio_A:RTL8723BE_RADIOA_1TARRAY %d\n", radioa_arraylen);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
	rtstatus = true;
	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radioa_arraylen; i = i + 2) {
			v1 = radioa_array_table[i];
			v2 = radioa_array_table[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl8723be_config_rf_radio_a(hw, v1, v2);
			} else {/*This line is the start line of branch.*/
				/* to protect READ_NEXT_PAIR not overrun */
				if (i >= radioa_arraylen - 2)
					break;

				if (!_rtl8723be_check_condition(hw,
						radioa_array_table[i])) {
					/*Discard the following
					 *(offset, data) pairs*/
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < radioa_arraylen - 2) {
						READ_NEXT_RF_PAIR(v1, v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				} else {
					/*Configure matched pairs
					 *and skip to end of if-else.*/
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < radioa_arraylen - 2) {
						_rtl8723be_config_rf_radio_a(hw,
									v1, v2);
						READ_NEXT_RF_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD &&
					       i < radioa_arraylen - 2) {
						READ_NEXT_RF_PAIR(v1, v2, i);
					}
				}
			}
		}

		if (rtlhal->oem_id == RT_CID_819x_HP)
			_rtl8723be_config_rf_radio_a(hw, 0x52, 0x7E4BD);

		break;
	case RF90_PATH_B:
	case RF90_PATH_C:
		break;
	case RF90_PATH_D:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case not process\n");
		break;
	}
	return true;
}

void rtl8723be_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	rtlphy->default_initialgain[0] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[1] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[2] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XCAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[3] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XDAGCCORE1, MASKBYTE0);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x\n",
		 rtlphy->default_initialgain[0],
		 rtlphy->default_initialgain[1],
		 rtlphy->default_initialgain[2],
		 rtlphy->default_initialgain[3]);

	rtlphy->framesync = (u8) rtl_get_bbreg(hw, ROFDM0_RXDETECTOR3,
					       MASKBYTE0);
	rtlphy->framesync_c34 = rtl_get_bbreg(hw, ROFDM0_RXDETECTOR2,
					      MASKDWORD);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default framesync (0x%x) = 0x%x\n",
		  ROFDM0_RXDETECTOR3, rtlphy->framesync);
}

static void _rtl8723be_phy_init_bb_rf_register_definition(
							struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	rtlphy->phyreg_def[RF90_PATH_A].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_C].rfintfs = RFPGA0_XCD_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_D].rfintfs = RFPGA0_XCD_RFINTERFACESW;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfi = RFPGA0_XAB_RFINTERFACERB;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfi = RFPGA0_XAB_RFINTERFACERB;
	rtlphy->phyreg_def[RF90_PATH_C].rfintfi = RFPGA0_XCD_RFINTERFACERB;
	rtlphy->phyreg_def[RF90_PATH_D].rfintfi = RFPGA0_XCD_RFINTERFACERB;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfo = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfo = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfe = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfe = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rf3wire_offset =
						RFPGA0_XA_LSSIPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rf3wire_offset =
						RFPGA0_XB_LSSIPARAMETER;

	rtlphy->phyreg_def[RF90_PATH_A].rflssi_select = rFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_select = rFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_C].rflssi_select = rFPGA0_XCD_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_D].rflssi_select = rFPGA0_XCD_RFPARAMETER;

	rtlphy->phyreg_def[RF90_PATH_A].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxgain_stage = RFPGA0_TXGAINSTAGE;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para1 = RFPGA0_XA_HSSIPARAMETER1;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para1 = RFPGA0_XB_HSSIPARAMETER1;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RFPGA0_XA_HSSIPARAMETER2;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RFPGA0_XB_HSSIPARAMETER2;

	rtlphy->phyreg_def[RF90_PATH_A].rfswitch_control =
						RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_B].rfswitch_control =
						RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_C].rfswitch_control =
						RFPGA0_XCD_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_D].rfswitch_control =
						RFPGA0_XCD_SWITCHCONTROL;

	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control1 = ROFDM0_XAAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control1 = ROFDM0_XBAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control1 = ROFDM0_XCAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control1 = ROFDM0_XDAGCCORE1;

	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control2 = ROFDM0_XAAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control2 = ROFDM0_XBAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control2 = ROFDM0_XCAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control2 = ROFDM0_XDAGCCORE2;

	rtlphy->phyreg_def[RF90_PATH_A].rfrxiq_imbalance =
						ROFDM0_XARXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrxiq_imbalance =
						ROFDM0_XBRXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrxiq_imbalance =
						ROFDM0_XCRXIQIMBANLANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrxiq_imbalance =
						ROFDM0_XDRXIQIMBALANCE;

	rtlphy->phyreg_def[RF90_PATH_A].rfrx_afe = ROFDM0_XARXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrx_afe = ROFDM0_XBRXAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrx_afe = ROFDM0_XCRXAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrx_afe = ROFDM0_XDRXAFE;

	rtlphy->phyreg_def[RF90_PATH_A].rftxiq_imbalance =
						ROFDM0_XATXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxiq_imbalance =
						ROFDM0_XBTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxiq_imbalance =
						ROFDM0_XCTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxiq_imbalance =
						ROFDM0_XDTXIQIMBALANCE;

	rtlphy->phyreg_def[RF90_PATH_A].rftx_afe = ROFDM0_XATXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rftx_afe = ROFDM0_XBTXAFE;

	rtlphy->phyreg_def[RF90_PATH_A].rflssi_readback =
						RFPGA0_XA_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_readback =
						RFPGA0_XB_LSSIREADBACK;

	rtlphy->phyreg_def[RF90_PATH_A].rflssi_readbackpi =
						TRANSCEIVEA_HSPI_READBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_readbackpi =
						TRANSCEIVEB_HSPI_READBACK;

}

void rtl8723be_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 txpwr_level;
	long txpwr_dbm;

	txpwr_level = rtlphy->cur_cck_txpwridx;
	txpwr_dbm = _rtl8723be_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_B,
						    txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl8723be_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G, txpwr_level) >
	    txpwr_dbm)
		txpwr_dbm =
		    _rtl8723be_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G,
						    txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl8723be_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
					    txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    _rtl8723be_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
						    txpwr_level);
	*powerlevel = txpwr_dbm;
}

static u8 _rtl8723be_phy_get_ratesection_intxpower_byrate(enum radio_path path,
							  u8 rate)
{
	u8 rate_section = 0;

	switch (rate) {
	case DESC92C_RATE1M:
		rate_section = 2;
		break;

	case DESC92C_RATE2M:
	case DESC92C_RATE5_5M:
		if (path == RF90_PATH_A)
			rate_section = 3;
		else if (path == RF90_PATH_B)
			rate_section = 2;
		break;

	case DESC92C_RATE11M:
		rate_section = 3;
		break;

	case DESC92C_RATE6M:
	case DESC92C_RATE9M:
	case DESC92C_RATE12M:
	case DESC92C_RATE18M:
		rate_section = 0;
		break;

	case DESC92C_RATE24M:
	case DESC92C_RATE36M:
	case DESC92C_RATE48M:
	case DESC92C_RATE54M:
		rate_section = 1;
		break;

	case DESC92C_RATEMCS0:
	case DESC92C_RATEMCS1:
	case DESC92C_RATEMCS2:
	case DESC92C_RATEMCS3:
		rate_section = 4;
		break;

	case DESC92C_RATEMCS4:
	case DESC92C_RATEMCS5:
	case DESC92C_RATEMCS6:
	case DESC92C_RATEMCS7:
		rate_section = 5;
		break;

	case DESC92C_RATEMCS8:
	case DESC92C_RATEMCS9:
	case DESC92C_RATEMCS10:
	case DESC92C_RATEMCS11:
		rate_section = 6;
		break;

	case DESC92C_RATEMCS12:
	case DESC92C_RATEMCS13:
	case DESC92C_RATEMCS14:
	case DESC92C_RATEMCS15:
		rate_section = 7;
		break;

	default:
		RT_ASSERT(true, "Rate_Section is Illegal\n");
		break;
	}

	return rate_section;
}

static u8 _rtl8723be_get_txpower_by_rate(struct ieee80211_hw *hw,
				  enum band_type band,
				  enum radio_path rfpath, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 shift = 0, rate_section, tx_num;
	char tx_pwr_diff = 0;

	rate_section = _rtl8723be_phy_get_ratesection_intxpower_byrate(rfpath,
									rate);
	tx_num = RF_TX_NUM_NONIMPLEMENT;

	if (tx_num == RF_TX_NUM_NONIMPLEMENT) {
		if (rate >= DESC92C_RATEMCS8 && rate <= DESC92C_RATEMCS15)
			tx_num = RF_2TX;
		else
			tx_num = RF_1TX;
	}

	switch (rate) {
	case DESC92C_RATE1M:
		shift = 8;
	break;
	case DESC92C_RATE2M:
		shift = 8;
	break;
	case DESC92C_RATE5_5M:
		shift = 16;
	break;
	case DESC92C_RATE11M:
		shift = 24;
	break;

	case DESC92C_RATE6M:
		shift = 0;
	break;
	case DESC92C_RATE9M:
		shift = 8;
	break;
	case DESC92C_RATE12M:
		shift = 16;
	break;
	case DESC92C_RATE18M:
		shift = 24;
	break;

	case DESC92C_RATE24M:
		shift = 0;
	break;
	case DESC92C_RATE36M:
		shift = 8;
	break;
	case DESC92C_RATE48M:
		shift = 16;
	break;
	case DESC92C_RATE54M:
		shift = 24;
	break;

	case DESC92C_RATEMCS0:
		shift = 0;
	break;
	case DESC92C_RATEMCS1:
		shift = 8;
	break;
	case DESC92C_RATEMCS2:
		shift = 16;
	break;
	case DESC92C_RATEMCS3:
		shift = 24;
	break;

	case DESC92C_RATEMCS4:
		shift = 0;
	break;
	case DESC92C_RATEMCS5:
		shift = 8;
	break;
	case DESC92C_RATEMCS6:
		shift = 16;
	break;
	case DESC92C_RATEMCS7:
		shift = 24;
	break;

	case DESC92C_RATEMCS8:
		shift = 0;
	break;
	case DESC92C_RATEMCS9:
		shift = 8;
	break;
	case DESC92C_RATEMCS10:
		shift = 16;
	break;
	case DESC92C_RATEMCS11:
		shift = 24;
	break;

	case DESC92C_RATEMCS12:
		shift = 0;
	break;
	case DESC92C_RATEMCS13:
		shift = 8;
	break;
	case DESC92C_RATEMCS14:
		shift = 16;
	break;
	case DESC92C_RATEMCS15:
		shift = 24;
	break;

	default:
		RT_ASSERT(true, "Rate_Section is Illegal\n");
		break;
	}

	tx_pwr_diff =
		(u8) (rtlphy->tx_power_by_rate_offset[
			band][rfpath][tx_num][rate_section] >> shift) & 0xff;

	return	tx_pwr_diff;
}

static u8 _rtl8723be_get_txpower_index(struct ieee80211_hw *hw, u8 path,
				       u8 rate, u8 bandwidth, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = (channel - 1);
	u8 txpower;
	u8 power_diff_byrate = 0;

	if (channel > 14 || channel < 1) {
		index = 0;
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD, "Illegal channel!\n");
	}
	if (RX_HAL_IS_CCK_RATE(rate))
		txpower = rtlefuse->txpwrlevel_cck[path][index];
	else if (DESC92C_RATE6M <= rate)
		txpower = rtlefuse->txpwrlevel_ht40_1s[path][index];
	else
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD, "invalid rate\n");

	if (DESC92C_RATE6M <= rate && rate <= DESC92C_RATE54M &&
	    !RX_HAL_IS_CCK_RATE(rate))
		txpower += rtlefuse->txpwr_legacyhtdiff[0][TX_1S];

	if (bandwidth == HT_CHANNEL_WIDTH_20) {
		if (DESC92C_RATEMCS0 <= rate && rate <= DESC92C_RATEMCS15)
			txpower += rtlefuse->txpwr_ht20diff[0][TX_1S];
		if (DESC92C_RATEMCS8 <= rate && rate <= DESC92C_RATEMCS15)
			txpower += rtlefuse->txpwr_ht20diff[0][TX_2S];
	} else if (bandwidth == HT_CHANNEL_WIDTH_20_40) {
		if (DESC92C_RATEMCS0 <= rate && rate <= DESC92C_RATEMCS15)
			txpower += rtlefuse->txpwr_ht40diff[0][TX_1S];
		if (DESC92C_RATEMCS8 <= rate && rate <= DESC92C_RATEMCS15)
			txpower += rtlefuse->txpwr_ht40diff[0][TX_2S];
	}

	if (rtlefuse->eeprom_regulatory != 2)
		power_diff_byrate = _rtl8723be_get_txpower_by_rate(hw,
								   BAND_ON_2_4G,
								   path, rate);

	txpower += power_diff_byrate;

	if (txpower > MAX_POWER_INDEX)
		txpower = MAX_POWER_INDEX;

	return txpower;
}


static void _rtl8723be_phy_set_txpower_index(struct ieee80211_hw *hw,
					     u8 power_index, u8 path, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (path == RF90_PATH_A) {
		switch (rate) {
		case DESC92C_RATE1M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_CCK1_MCS32,
						 MASKBYTE1, power_index);
			break;
		case DESC92C_RATE2M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_B_CCK11_A_CCK2_11,
						 MASKBYTE1, power_index);
			break;
		case DESC92C_RATE5_5M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_B_CCK11_A_CCK2_11,
						 MASKBYTE2, power_index);
			break;
		case DESC92C_RATE11M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_B_CCK11_A_CCK2_11,
						 MASKBYTE3, power_index);
			break;

		case DESC92C_RATE6M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_RATE18_06,
						 MASKBYTE0, power_index);
			break;
		case DESC92C_RATE9M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_RATE18_06,
						 MASKBYTE1, power_index);
			break;
		case DESC92C_RATE12M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_RATE18_06,
						 MASKBYTE2, power_index);
			break;
		case DESC92C_RATE18M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_RATE18_06,
						 MASKBYTE3, power_index);
			break;

		case DESC92C_RATE24M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_RATE54_24,
						 MASKBYTE0, power_index);
			break;
		case DESC92C_RATE36M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_RATE54_24,
						 MASKBYTE1, power_index);
			break;
		case DESC92C_RATE48M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_RATE54_24,
						 MASKBYTE2, power_index);
			break;
		case DESC92C_RATE54M:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_RATE54_24,
						 MASKBYTE3, power_index);
			break;

		case DESC92C_RATEMCS0:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS03_MCS00,
						 MASKBYTE0, power_index);
			break;
		case DESC92C_RATEMCS1:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS03_MCS00,
						 MASKBYTE1, power_index);
			break;
		case DESC92C_RATEMCS2:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS03_MCS00,
						 MASKBYTE2, power_index);
			break;
		case DESC92C_RATEMCS3:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS03_MCS00,
						 MASKBYTE3, power_index);
			break;

		case DESC92C_RATEMCS4:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS07_MCS04,
						 MASKBYTE0, power_index);
			break;
		case DESC92C_RATEMCS5:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS07_MCS04,
						 MASKBYTE1, power_index);
			break;
		case DESC92C_RATEMCS6:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS07_MCS04,
						 MASKBYTE2, power_index);
			break;
		case DESC92C_RATEMCS7:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS07_MCS04,
						 MASKBYTE3, power_index);
			break;

		case DESC92C_RATEMCS8:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS11_MCS08,
						 MASKBYTE0, power_index);
			break;
		case DESC92C_RATEMCS9:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS11_MCS08,
						 MASKBYTE1, power_index);
			break;
		case DESC92C_RATEMCS10:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS11_MCS08,
						 MASKBYTE2, power_index);
			break;
		case DESC92C_RATEMCS11:
			rtl8723be_phy_set_bb_reg(hw, RTXAGC_A_MCS11_MCS08,
						 MASKBYTE3, power_index);
			break;

		default:
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "Invalid Rate!!\n");
			break;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "Invalid RFPath!!\n");
	}
}

void rtl8723be_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 cck_rates[]  = {DESC92C_RATE1M, DESC92C_RATE2M,
			   DESC92C_RATE5_5M, DESC92C_RATE11M};
	u8 ofdm_rates[]  = {DESC92C_RATE6M, DESC92C_RATE9M,
			    DESC92C_RATE12M, DESC92C_RATE18M,
			    DESC92C_RATE24M, DESC92C_RATE36M,
			    DESC92C_RATE48M, DESC92C_RATE54M};
	u8 ht_rates_1t[]  = {DESC92C_RATEMCS0, DESC92C_RATEMCS1,
			     DESC92C_RATEMCS2, DESC92C_RATEMCS3,
			     DESC92C_RATEMCS4, DESC92C_RATEMCS5,
			     DESC92C_RATEMCS6, DESC92C_RATEMCS7};
	u8 i, size;
	u8 power_index;
	if (rtlefuse->txpwr_fromeprom == false)
		return;

	size = sizeof(cck_rates) / sizeof(u8);
	for (i = 0; i < size; i++) {
		power_index = _rtl8723be_get_txpower_index(hw, RF90_PATH_A,
					cck_rates[i],
					rtl_priv(hw)->phy.current_chan_bw,
					channel);
		_rtl8723be_phy_set_txpower_index(hw, power_index, RF90_PATH_A,
						 cck_rates[i]);
	}
	size = sizeof(ofdm_rates) / sizeof(u8);
	for (i = 0; i < size; i++) {
		power_index = _rtl8723be_get_txpower_index(hw, RF90_PATH_A,
					ofdm_rates[i],
					rtl_priv(hw)->phy.current_chan_bw,
					channel);
		_rtl8723be_phy_set_txpower_index(hw, power_index, RF90_PATH_A,
						 ofdm_rates[i]);
	}
	size = sizeof(ht_rates_1t) / sizeof(u8);
	for (i = 0; i < size; i++) {
		power_index = _rtl8723be_get_txpower_index(hw, RF90_PATH_A,
					ht_rates_1t[i],
					rtl_priv(hw)->phy.current_chan_bw,
					channel);
		_rtl8723be_phy_set_txpower_index(hw, power_index, RF90_PATH_A,
						 ht_rates_1t[i]);
	}
}

static long _rtl8723be_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					    enum wireless_mode wirelessmode,
					    u8 txpwridx)
{
	long offset;
	long pwrout_dbm;

	switch (wirelessmode) {
	case WIRELESS_MODE_B:
		offset = -7;
		break;
	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		offset = -8;
		break;
	default:
		offset = -8;
		break;
	}
	pwrout_dbm = txpwridx / 2 + offset;
	return pwrout_dbm;
}

void rtl8723be_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum io_type iotype;

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP_BAND0:
			iotype = IO_CMD_PAUSE_BAND0_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_IO_CMD,
						      (u8 *) &iotype);

			break;
		case SCAN_OPT_RESTORE:
			iotype = IO_CMD_RESUME_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_IO_CMD,
						      (u8 *) &iotype);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Unknown Scan Backup operation.\n");
			break;
		}
	}
}

void rtl8723be_phy_set_bw_mode_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 reg_bw_opmode;
	u8 reg_prsr_rsc;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "Switch to %s bandwidth\n",
		  rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20 ?
		  "20MHz" : "40MHz");

	if (is_hal_stop(rtlhal)) {
		rtlphy->set_bwmode_inprogress = false;
		return;
	}

	reg_bw_opmode = rtl_read_byte(rtlpriv, REG_BWOPMODE);
	reg_prsr_rsc = rtl_read_byte(rtlpriv, REG_RRSR + 2);

	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		reg_bw_opmode |= BW_OPMODE_20MHZ;
		rtl_write_byte(rtlpriv, REG_BWOPMODE, reg_bw_opmode);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		reg_bw_opmode &= ~BW_OPMODE_20MHZ;
		rtl_write_byte(rtlpriv, REG_BWOPMODE, reg_bw_opmode);
		reg_prsr_rsc = (reg_prsr_rsc & 0x90) |
			       (mac->cur_40_prime_sc << 5);
		rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_prsr_rsc);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;
	}

	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x0);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x0);
	/*	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10), 1);*/
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x1);

		rtl_set_bbreg(hw, RCCK0_SYSTEM, BCCK_SIDEBAND,
			      (mac->cur_40_prime_sc >> 1));
		rtl_set_bbreg(hw, ROFDM1_LSTF, 0xC00, mac->cur_40_prime_sc);
		/*rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10), 0);*/

		rtl_set_bbreg(hw, 0x818, (BIT(26) | BIT(27)),
			      (mac->cur_40_prime_sc ==
			       HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;
	}
	rtl8723be_phy_rf6052_set_bandwidth(hw, rtlphy->current_chan_bw);
	rtlphy->set_bwmode_inprogress = false;
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD, "\n");
}

void rtl8723be_phy_set_bw_mode(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_bw = rtlphy->current_chan_bw;

	if (rtlphy->set_bwmode_inprogress)
		return;
	rtlphy->set_bwmode_inprogress = true;
	if ((!is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw))) {
		rtl8723be_phy_set_bw_mode_callback(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "false driver sleep or unload\n");
		rtlphy->set_bwmode_inprogress = false;
		rtlphy->current_chan_bw = tmp_bw;
	}
}

void rtl8723be_phy_sw_chnl_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 delay;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "switch to channel%d\n", rtlphy->current_channel);
	if (is_hal_stop(rtlhal))
		return;
	do {
		if (!rtlphy->sw_chnl_inprogress)
			break;
		if (!_rtl8723be_phy_sw_chnl_step_by_step(hw,
							rtlphy->current_channel,
							&rtlphy->sw_chnl_stage,
							&rtlphy->sw_chnl_step,
							&delay)) {
			if (delay > 0)
				mdelay(delay);
			else
				continue;
		} else {
			rtlphy->sw_chnl_inprogress = false;
		}
		break;
	} while (true);
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "\n");
}

u8 rtl8723be_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (rtlphy->sw_chnl_inprogress)
		return 0;
	if (rtlphy->set_bwmode_inprogress)
		return 0;
	RT_ASSERT((rtlphy->current_channel <= 14),
		  "WIRELESS_MODE_G but channel>14");
	rtlphy->sw_chnl_inprogress = true;
	rtlphy->sw_chnl_stage = 0;
	rtlphy->sw_chnl_step = 0;
	if (!(is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw))) {
		rtl8723be_phy_sw_chnl_callback(hw);
		RT_TRACE(rtlpriv, COMP_CHAN, DBG_LOUD,
			 "sw_chnl_inprogress false schdule workitem current channel %d\n",
			 rtlphy->current_channel);
		rtlphy->sw_chnl_inprogress = false;
	} else {
		RT_TRACE(rtlpriv, COMP_CHAN, DBG_LOUD,
			 "sw_chnl_inprogress false driver sleep or unload\n");
		rtlphy->sw_chnl_inprogress = false;
	}
	return 1;
}

static bool _rtl8723be_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
						u8 channel, u8 *stage,
						u8 *step, u32 *delay)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct swchnlcmd precommoncmd[MAX_PRECMD_CNT];
	u32 precommoncmdcnt;
	struct swchnlcmd postcommoncmd[MAX_POSTCMD_CNT];
	u32 postcommoncmdcnt;
	struct swchnlcmd rfdependcmd[MAX_RFDEPENDCMD_CNT];
	u32 rfdependcmdcnt;
	struct swchnlcmd *currentcmd = NULL;
	u8 rfpath;
	u8 num_total_rfpath = rtlphy->num_total_rfpath;

	precommoncmdcnt = 0;
	_rtl8723be_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					    MAX_PRECMD_CNT,
					    CMDID_SET_TXPOWEROWER_LEVEL,
					    0, 0, 0);
	_rtl8723be_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					    MAX_PRECMD_CNT, CMDID_END, 0, 0, 0);

	postcommoncmdcnt = 0;

	_rtl8723be_phy_set_sw_chnl_cmdarray(postcommoncmd, postcommoncmdcnt++,
					    MAX_POSTCMD_CNT, CMDID_END,
					    0, 0, 0);

	rfdependcmdcnt = 0;

	RT_ASSERT((channel >= 1 && channel <= 14),
		  "illegal channel for Zebra: %d\n", channel);

	_rtl8723be_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					    MAX_RFDEPENDCMD_CNT,
					    CMDID_RF_WRITEREG,
					    RF_CHNLBW, channel, 10);

	_rtl8723be_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					    MAX_RFDEPENDCMD_CNT,
					    CMDID_END, 0, 0, 0);

	do {
		switch (*stage) {
		case 0:
			currentcmd = &precommoncmd[*step];
			break;
		case 1:
			currentcmd = &rfdependcmd[*step];
			break;
		case 2:
			currentcmd = &postcommoncmd[*step];
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Invalid 'stage' = %d, Check it!\n", *stage);
			return true;
			break;
		}

		if (currentcmd->cmdid == CMDID_END) {
			if ((*stage) == 2) {
				return true;
			} else {
				(*stage)++;
				(*step) = 0;
				continue;
			}
		}

		switch (currentcmd->cmdid) {
		case CMDID_SET_TXPOWEROWER_LEVEL:
			rtl8723be_phy_set_txpower_level(hw, channel);
			break;
		case CMDID_WRITEPORT_ULONG:
			rtl_write_dword(rtlpriv, currentcmd->para1,
					currentcmd->para2);
			break;
		case CMDID_WRITEPORT_USHORT:
			rtl_write_word(rtlpriv, currentcmd->para1,
				       (u16) currentcmd->para2);
			break;
		case CMDID_WRITEPORT_UCHAR:
			rtl_write_byte(rtlpriv, currentcmd->para1,
				       (u8) currentcmd->para2);
			break;
		case CMDID_RF_WRITEREG:
			for (rfpath = 0; rfpath < num_total_rfpath; rfpath++) {
				rtlphy->rfreg_chnlval[rfpath] =
				    ((rtlphy->rfreg_chnlval[rfpath] &
				      0xfffffc00) | currentcmd->para2);

				rtl_set_rfreg(hw, (enum radio_path)rfpath,
					      currentcmd->para1,
					      RFREG_OFFSET_MASK,
					      rtlphy->rfreg_chnlval[rfpath]);
			}
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
				 "switch case not process\n");
			break;
		}

		break;
	} while (true);

	(*delay) = currentcmd->msdelay;
	(*step)++;
	return false;
}

static bool _rtl8723be_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
						u32 cmdtableidx,
						u32 cmdtablesz,
						enum swchnlcmd_id cmdid,
						u32 para1, u32 para2,
						u32 msdelay)
{
	struct swchnlcmd *pcmd;

	if (cmdtable == NULL) {
		RT_ASSERT(false, "cmdtable cannot be NULL.\n");
		return false;
	}

	if (cmdtableidx >= cmdtablesz)
		return false;

	pcmd = cmdtable + cmdtableidx;
	pcmd->cmdid = cmdid;
	pcmd->para1 = para1;
	pcmd->para2 = para2;
	pcmd->msdelay = msdelay;
	return true;
}

static u8 _rtl8723be_phy_path_a_iqk(struct ieee80211_hw *hw)
{
	u32 reg_eac, reg_e94, reg_e9c, tmp;
	u8 result = 0x00;

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	/* switch to path A */
	rtl_set_bbreg(hw, 0x948, MASKDWORD, 0x00000000);
	/* enable path A PA in TXIQK mode */
	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, RFREG_OFFSET_MASK, 0x800a0);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK_OS, RFREG_OFFSET_MASK, 0x20000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0003f);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xc7f87);

	/* 1. TX IQK */
	/* path-A IQK setting */
	/* IQK setting */
	rtl_set_bbreg(hw, RTx_IQK, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, RRx_IQK, MASKDWORD, 0x01004800);
	/* path-A IQK setting */
	rtl_set_bbreg(hw, RTx_IQK_Tone_A, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RTx_IQK_Tone_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTx_IQK_PI_A, MASKDWORD, 0x821403ea);
	rtl_set_bbreg(hw, RRx_IQK_PI_A, MASKDWORD, 0x28160000);
	rtl_set_bbreg(hw, RTx_IQK_PI_B, MASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, RRx_IQK_PI_B, MASKDWORD, 0x28110000);
	/* LO calibration setting */
	rtl_set_bbreg(hw, RIQK_AGC_Rsp, MASKDWORD, 0x00462911);
	/* enter IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/* One shot, path A LOK & IQK */
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);

	/* Check failed */
	reg_eac = rtl_get_bbreg(hw, 0xeac, MASKDWORD);
	reg_e94 = rtl_get_bbreg(hw, 0xe94, MASKDWORD);
	reg_e9c = rtl_get_bbreg(hw, 0xe9c, MASKDWORD);

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else /* if Tx not OK, ignore Rx */
		return result;

	/* Allen 20131125 */
	tmp = (reg_e9c & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) < 0x110) &&
	    (((reg_e94 & 0x03FF0000) >> 16) > 0xf0) &&
	    (tmp < 0xf))
		result |= 0x01;
	else /* if Tx not OK, ignore Rx */
		return result;

	return result;
}

/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl8723be_phy_path_a_rx_iqk(struct ieee80211_hw *hw)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, u32tmp, tmp;
	u8 result = 0x00;

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);

	/* switch to path A */
	rtl_set_bbreg(hw, 0x948, MASKDWORD, 0x00000000);

	/* 1 Get TXIMR setting */
	/* modify RXIQK mode table */
	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0001f);
	/* LNA2 off, PA on for Dcut */
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf7fb7);
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/* IQK setting */
	rtl_set_bbreg(hw, RTx_IQK, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, RRx_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	rtl_set_bbreg(hw, RTx_IQK_Tone_A, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RTx_IQK_Tone_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTx_IQK_PI_A, MASKDWORD, 0x82160ff0);
	rtl_set_bbreg(hw, RRx_IQK_PI_A, MASKDWORD, 0x28110000);
	rtl_set_bbreg(hw, RTx_IQK_PI_B, MASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, RRx_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	rtl_set_bbreg(hw, RIQK_AGC_Rsp, MASKDWORD, 0x0046a911);

	/* enter IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/* One shot, path A LOK & IQK */
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);

	/* Check failed */
	reg_eac = rtl_get_bbreg(hw, RRx_Power_After_IQK_A_2, MASKDWORD);
	reg_e94 = rtl_get_bbreg(hw, RTx_Power_Before_IQK_A, MASKDWORD);
	reg_e9c = rtl_get_bbreg(hw, RTx_Power_After_IQK_A, MASKDWORD);

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else /* if Tx not OK, ignore Rx */
		return result;

	/* Allen 20131125 */
	tmp = (reg_e9c & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) < 0x110) &&
	    (((reg_e94 & 0x03FF0000) >> 16) > 0xf0) &&
	    (tmp < 0xf))
		result |= 0x01;
	else /* if Tx not OK, ignore Rx */
		return result;

	u32tmp = 0x80007C00 | (reg_e94 & 0x3FF0000) |
		 ((reg_e9c & 0x3FF0000) >> 16);
	rtl_set_bbreg(hw, RTx_IQK, MASKDWORD, u32tmp);

	/* 1 RX IQK */
	/* modify RXIQK mode table */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0001f);
	/* LAN2 on, PA off for Dcut */
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf7d77);

	/* PA, PAD setting */
	rtl_set_rfreg(hw, RF90_PATH_A, 0xdf, RFREG_OFFSET_MASK, 0xf80);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x55, RFREG_OFFSET_MASK, 0x4021f);

	/* IQK setting */
	rtl_set_bbreg(hw, RRx_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	rtl_set_bbreg(hw, RTx_IQK_Tone_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_A, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RTx_IQK_Tone_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTx_IQK_PI_A, MASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, RRx_IQK_PI_A, MASKDWORD, 0x2816001f);
	rtl_set_bbreg(hw, RTx_IQK_PI_B, MASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, RRx_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	rtl_set_bbreg(hw, RIQK_AGC_Rsp, MASKDWORD, 0x0046a8d1);

	/* enter IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/* One shot, path A LOK & IQK */
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);

	/* Check failed */
	reg_eac = rtl_get_bbreg(hw, RRx_Power_After_IQK_A_2, MASKDWORD);
	reg_ea4 = rtl_get_bbreg(hw, RRx_Power_Before_IQK_A_2, MASKDWORD);

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_A, 0xdf, RFREG_OFFSET_MASK, 0x780);

	/* Allen 20131125 */
	tmp = (reg_eac & 0x03FF0000) >> 16;
		if ((tmp & 0x200) > 0)
			tmp = 0x400 - tmp;
	/* if Tx is OK, check whether Rx is OK */
	if (!(reg_eac & BIT(27)) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else if (!(reg_eac & BIT(27)) &&
		(((reg_ea4 & 0x03FF0000) >> 16) < 0x110) &&
		(((reg_ea4 & 0x03FF0000) >> 16) > 0xf0) &&
		(tmp < 0xf))
		result |= 0x02;

	return result;
}

static u8 _rtl8723be_phy_path_b_iqk(struct ieee80211_hw *hw)
{
	u32 reg_eac, reg_e94, reg_e9c, tmp;
	u8 result = 0x00;

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	/* switch to path B */
	rtl_set_bbreg(hw, 0x948, MASKDWORD, 0x00000280);

	/* enable path B PA in TXIQK mode */
	rtl_set_rfreg(hw, RF90_PATH_A, 0xed, RFREG_OFFSET_MASK, 0x00020);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x43, RFREG_OFFSET_MASK, 0x40fc1);

	/* 1 Tx IQK */
	/* IQK setting */
	rtl_set_bbreg(hw, RTx_IQK, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, RRx_IQK, MASKDWORD, 0x01004800);
	/* path-A IQK setting */
	rtl_set_bbreg(hw, RTx_IQK_Tone_A, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RTx_IQK_Tone_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTx_IQK_PI_A, MASKDWORD, 0x821403ea);
	rtl_set_bbreg(hw, RRx_IQK_PI_A, MASKDWORD, 0x28110000);
	rtl_set_bbreg(hw, RTx_IQK_PI_B, MASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, RRx_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	rtl_set_bbreg(hw, RIQK_AGC_Rsp, MASKDWORD, 0x00462911);

	/* enter IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/* One shot, path B LOK & IQK */
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);

	/* Check failed */
	reg_eac = rtl_get_bbreg(hw, RRx_Power_After_IQK_A_2, MASKDWORD);
	reg_e94 = rtl_get_bbreg(hw, RTx_Power_Before_IQK_A, MASKDWORD);
	reg_e9c = rtl_get_bbreg(hw, RTx_Power_After_IQK_A, MASKDWORD);

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;

	/* Allen 20131125 */
	tmp = (reg_e9c & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) < 0x110) &&
	    (((reg_e94 & 0x03FF0000) >> 16) > 0xf0) &&
	    (tmp < 0xf))
		result |= 0x01;
	else
		return result;

	return result;
}


/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl8723be_phy_path_b_rx_iqk(struct ieee80211_hw *hw)
{
	u32 reg_e94, reg_e9c, reg_ea4, reg_eac, u32tmp, tmp;
	u8 result = 0x00;

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	/* switch to path B */
	rtl_set_bbreg(hw, 0x948, MASKDWORD, 0x00000280);

	/* 1 Get TXIMR setting */
	/* modify RXIQK mode table */
	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, RFREG_OFFSET_MASK, 0x800a0);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0001f);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf7ff7);

	/* open PA S1 & SMIXER */
	rtl_set_rfreg(hw, RF90_PATH_A, 0xed, RFREG_OFFSET_MASK, 0x00020);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x43, RFREG_OFFSET_MASK, 0x60fed);

	/* IQK setting */
	rtl_set_bbreg(hw, RTx_IQK, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, RRx_IQK, MASKDWORD, 0x01004800);

	/* path-B IQK setting */
	rtl_set_bbreg(hw, RTx_IQK_Tone_A, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RTx_IQK_Tone_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTx_IQK_PI_A, MASKDWORD, 0x82160ff0);
	rtl_set_bbreg(hw, RRx_IQK_PI_A, MASKDWORD, 0x28110000);
	rtl_set_bbreg(hw, RTx_IQK_PI_B, MASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, RRx_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	rtl_set_bbreg(hw, RIQK_AGC_Rsp, MASKDWORD, 0x0046a911);
	/* enter IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/* One shot, path B TXIQK @ RXIQK */
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	/* Check failed */
	reg_eac = rtl_get_bbreg(hw, RRx_Power_After_IQK_A_2, MASKDWORD);
	reg_e94 = rtl_get_bbreg(hw, RTx_Power_Before_IQK_A, MASKDWORD);
	reg_e9c = rtl_get_bbreg(hw, RTx_Power_After_IQK_A, MASKDWORD);

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else	/* if Tx not OK, ignore Rx */
		return result;

	/* Allen 20131125 */
	tmp = (reg_e9c & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) < 0x110) &&
	    (((reg_e94 & 0x03FF0000) >> 16) > 0xf0) &&
	    (tmp < 0xf))
		result |= 0x01;
	else
		return result;

	u32tmp = 0x80007C00 | (reg_e94 & 0x3FF0000)  |
		 ((reg_e9c & 0x3FF0000) >> 16);
	rtl_set_bbreg(hw, RTx_IQK, MASKDWORD, u32tmp);

	/* 1 RX IQK */

	/* <20121009, Kordan> RF Mode = 3 */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0001f);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf7d77);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, 0x80000, 0x0);

	/* open PA S1 & close SMIXER */
	rtl_set_rfreg(hw, RF90_PATH_A, 0xed, RFREG_OFFSET_MASK, 0x00020);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x43, RFREG_OFFSET_MASK, 0x60fbd);

	/* IQK setting */
	rtl_set_bbreg(hw, RRx_IQK, MASKDWORD, 0x01004800);

	/* path-B IQK setting */
	rtl_set_bbreg(hw, RTx_IQK_Tone_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_A, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RTx_IQK_Tone_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRx_IQK_Tone_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTx_IQK_PI_A, MASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, RRx_IQK_PI_A, MASKDWORD, 0x2816001f);
	rtl_set_bbreg(hw, RTx_IQK_PI_B, MASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, RRx_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	rtl_set_bbreg(hw, RIQK_AGC_Rsp, MASKDWORD, 0x0046a8d1);
	/* enter IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/* One shot, path B LOK & IQK */
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_Pts, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	/* leave IQK mode */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	/* Check failed */
	reg_eac = rtl_get_bbreg(hw, RRx_Power_After_IQK_A_2, MASKDWORD);
	reg_ea4 = rtl_get_bbreg(hw, RRx_Power_Before_IQK_A_2, MASKDWORD);

	/* Allen 20131125 */
	tmp = (reg_eac & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	/* if Tx is OK, check whether Rx is OK */
	if (!(reg_eac & BIT(27)) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else if (!(reg_eac & BIT(27)) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) < 0x110) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) > 0xf0) &&
	    (tmp < 0xf))
		result |= 0x02;
	else
		return result;

	return result;
}

static void _rtl8723be_phy_path_a_fill_iqk_matrix(struct ieee80211_hw *hw,
						  bool b_iqk_ok,
						  long result[][8],
						  u8 final_candidate,
						  bool btxonly)
{
	u32 oldval_0, x, tx0_a, reg;
	long y, tx0_c;

	if (final_candidate == 0xFF) {
		return;
	} else if (b_iqk_ok) {
		oldval_0 = (rtl_get_bbreg(hw, ROFDM0_XATXIQIMBALANCE,
					  MASKDWORD) >> 22) & 0x3FF;
		x = result[final_candidate][0];
		if ((x & 0x00000200) != 0)
			x = x | 0xFFFFFC00;
		tx0_a = (x * oldval_0) >> 8;
		rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, 0x3FF, tx0_a);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(31),
			      ((x * oldval_0 >> 7) & 0x1));
		y = result[final_candidate][1];
		if ((y & 0x00000200) != 0)
			y = y | 0xFFFFFC00;
		tx0_c = (y * oldval_0) >> 8;
		rtl_set_bbreg(hw, ROFDM0_XCTXAFE, 0xF0000000,
			      ((tx0_c & 0x3C0) >> 6));
		rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, 0x003F0000,
			      (tx0_c & 0x3F));
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(29),
			      ((y * oldval_0 >> 7) & 0x1));
		if (btxonly)
			return;
		reg = result[final_candidate][2];
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0x3FF, reg);
		reg = result[final_candidate][3] & 0x3F;
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0xFC00, reg);
		reg = (result[final_candidate][3] >> 6) & 0xF;
		rtl_set_bbreg(hw, 0xca0, 0xF0000000, reg);
	}
}

static void _rtl8723be_phy_path_b_fill_iqk_matrix(struct ieee80211_hw *hw,
						  bool b_iqk_ok,
						  long result[][8],
						  u8 final_candidate,
						  bool btxonly)
{
	u32 oldval_1, x, tx1_a, reg;
	long y, tx1_c;

	if (final_candidate == 0xFF) {
		return;
	} else if (b_iqk_ok) {
		oldval_1 = (rtl_get_bbreg(hw, ROFDM0_XBTXIQIMBALANCE,
					  MASKDWORD) >> 22) & 0x3FF;
		x = result[final_candidate][4];
		if ((x & 0x00000200) != 0)
			x = x | 0xFFFFFC00;
		tx1_a = (x * oldval_1) >> 8;
		rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE, 0x3FF, tx1_a);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(27),
			      ((x * oldval_1 >> 7) & 0x1));
		y = result[final_candidate][5];
		if ((y & 0x00000200) != 0)
			y = y | 0xFFFFFC00;
		tx1_c = (y * oldval_1) >> 8;
		rtl_set_bbreg(hw, ROFDM0_XDTXAFE, 0xF0000000,
			      ((tx1_c & 0x3C0) >> 6));
		rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE, 0x003F0000,
			      (tx1_c & 0x3F));
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(25),
			      ((y * oldval_1 >> 7) & 0x1));
		if (btxonly)
			return;
		reg = result[final_candidate][6];
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0x3FF, reg);
		reg = result[final_candidate][7] & 0x3F;
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0xFC00, reg);
		reg = (result[final_candidate][7] >> 6) & 0xF;
		/* rtl_set_bbreg(hw, 0xca0, 0xF0000000, reg); */
	}
}

static void _rtl8723be_phy_save_adda_registers(struct ieee80211_hw *hw,
					       u32 *addareg, u32 *addabackup,
					       u32 registernum)
{
	u32 i;

	for (i = 0; i < registernum; i++)
		addabackup[i] = rtl_get_bbreg(hw, addareg[i], MASKDWORD);
}

static void _rtl8723be_phy_save_mac_registers(struct ieee80211_hw *hw,
					      u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		macbackup[i] = rtl_read_byte(rtlpriv, macreg[i]);
	macbackup[i] = rtl_read_dword(rtlpriv, macreg[i]);
}

static void _rtl8723be_phy_reload_adda_registers(struct ieee80211_hw *hw,
						 u32 *addareg, u32 *addabackup,
						 u32 regiesternum)
{
	u32 i;

	for (i = 0; i < regiesternum; i++)
		rtl_set_bbreg(hw, addareg[i], MASKDWORD, addabackup[i]);
}

static void _rtl8723be_phy_reload_mac_registers(struct ieee80211_hw *hw,
						u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i], (u8) macbackup[i]);
	rtl_write_dword(rtlpriv, macreg[i], macbackup[i]);
}

static void _rtl8723be_phy_path_adda_on(struct ieee80211_hw *hw, u32 *addareg,
					bool is_patha_on, bool is2t)
{
	u32 pathon;
	u32 i;

	pathon = is_patha_on ? 0x01c00014 : 0x01c00014;
	if (false == is2t) {
		pathon = 0x01c00014;
		rtl_set_bbreg(hw, addareg[0], MASKDWORD, 0x01c00014);
	} else {
		rtl_set_bbreg(hw, addareg[0], MASKDWORD, pathon);
	}

	for (i = 1; i < IQK_ADDA_REG_NUM; i++)
		rtl_set_bbreg(hw, addareg[i], MASKDWORD, pathon);
}

static void _rtl8723be_phy_mac_setting_calibration(struct ieee80211_hw *hw,
						   u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i = 0;

	rtl_write_byte(rtlpriv, macreg[i], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i],
			       (u8) (macbackup[i] & (~BIT(3))));
	rtl_write_byte(rtlpriv, macreg[i], (u8) (macbackup[i] & (~BIT(5))));
}

static bool _rtl8723be_phy_simularity_compare(struct ieee80211_hw *hw,
					      long result[][8], u8 c1, u8 c2)
{
	u32 i, j, diff, simularity_bitmap, bound = 0;

	u8 final_candidate[2] = {0xFF, 0xFF}; /* for path A and path B */
	bool bresult = true; /* is2t = true*/
	s32 tmp1 = 0, tmp2 = 0;

	bound = 8;

	simularity_bitmap = 0;

	for (i = 0; i < bound; i++) {
		if ((i == 1) || (i == 3) || (i == 5) || (i == 7)) {
			if ((result[c1][i] & 0x00000200) != 0)
				tmp1 = result[c1][i] | 0xFFFFFC00;
			else
				tmp1 = result[c1][i];

			if ((result[c2][i] & 0x00000200) != 0)
				tmp2 = result[c2][i] | 0xFFFFFC00;
			else
				tmp2 = result[c2][i];
		} else {
			tmp1 = result[c1][i];
			tmp2 = result[c2][i];
		}

		diff = (tmp1 > tmp2) ? (tmp1 - tmp2) : (tmp2 - tmp1);

		if (diff > MAX_TOLERANCE) {
			if ((i == 2 || i == 6) && !simularity_bitmap) {
				if (result[c1][i] + result[c1][i + 1] == 0)
					final_candidate[(i / 4)] = c2;
				else if (result[c2][i] + result[c2][i + 1] == 0)
					final_candidate[(i / 4)] = c1;
				else
					simularity_bitmap |= (1 << i);
			} else
				simularity_bitmap |= (1 << i);
		}
	}

	if (simularity_bitmap == 0) {
		for (i = 0; i < (bound / 4); i++) {
			if (final_candidate[i] != 0xFF) {
				for (j = i * 4; j < (i + 1) * 4 - 2; j++)
					result[3][j] =
						result[final_candidate[i]][j];
				bresult = false;
			}
		}
		return bresult;
	} else {
		if (!(simularity_bitmap & 0x03)) { /* path A TX OK */
			for (i = 0; i < 2; i++)
				result[3][i] = result[c1][i];
		}
		if (!(simularity_bitmap & 0x0c)) { /* path A RX OK */
			for (i = 2; i < 4; i++)
				result[3][i] = result[c1][i];
		}
		if (!(simularity_bitmap & 0x30)) { /* path B TX OK */
			for (i = 4; i < 6; i++)
				result[3][i] = result[c1][i];
		}
		if (!(simularity_bitmap & 0xc0)) { /* path B RX OK */
			for (i = 6; i < 8; i++)
				result[3][i] = result[c1][i];
		}
		return false;
	}
}

static void _rtl8723be_phy_iq_calibrate(struct ieee80211_hw *hw,
					long result[][8], u8 t, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 i;
	u8 patha_ok, pathb_ok;
	u32 adda_reg[IQK_ADDA_REG_NUM] = {
		0x85c, 0xe6c, 0xe70, 0xe74,
		0xe78, 0xe7c, 0xe80, 0xe84,
		0xe88, 0xe8c, 0xed0, 0xed4,
		0xed8, 0xedc, 0xee0, 0xeec
	};

	u32 iqk_mac_reg[IQK_MAC_REG_NUM] = {
		0x522, 0x550, 0x551, 0x040
	};
	u32 iqk_bb_reg[IQK_BB_REG_NUM] = {
		ROFDM0_TRXPATHENABLE, ROFDM0_TRMUXPAR,
		RFPGA0_XCD_RFINTERFACESW, 0xb68, 0xb6c,
		0x870, 0x860,
		0x864, 0xa04
	};
	const u32 retrycount = 2;

	u32 path_sel_bb;/* path_sel_rf */

	u8 tmp_reg_c50, tmp_reg_c58;

	tmp_reg_c50 = rtl_get_bbreg(hw, 0xc50, MASKBYTE0);
	tmp_reg_c58 = rtl_get_bbreg(hw, 0xc58, MASKBYTE0);


	if (t == 0) {

		_rtl8723be_phy_save_adda_registers(hw, adda_reg,
						   rtlphy->adda_backup, 16);
		_rtl8723be_phy_save_mac_registers(hw, iqk_mac_reg,
						  rtlphy->iqk_mac_backup);
		_rtl8723be_phy_save_adda_registers(hw, iqk_bb_reg,
						   rtlphy->iqk_bb_backup,
						   IQK_BB_REG_NUM);
	}
	_rtl8723be_phy_path_adda_on(hw, adda_reg, true, is2t);
	if (t == 0) {
		rtlphy->rfpi_enable = (u8) rtl_get_bbreg(hw,
						RFPGA0_XA_HSSIPARAMETER1,
						BIT(8));
	}

/*	if (!rtlphy->rfpi_enable)
		_rtl8723be_phy_pi_mode_switch(hw, true); */

	path_sel_bb = rtl_get_bbreg(hw, 0x948, MASKDWORD);
	/* path_sel_rf = rtl_get_rfreg(hw, RF90_PATH_A, 0xb0, 0xfffff); */

	_rtl8723be_phy_mac_setting_calibration(hw, iqk_mac_reg,
					       rtlphy->iqk_mac_backup);
	/*BB Setting*/
	/*rtl_set_bbreg(hw, 0x800, BIT(24), 0x00);*/
	rtl_set_bbreg(hw, 0xa04, 0x0f000000, 0xf);
	rtl_set_bbreg(hw, 0xc04, MASKDWORD, 0x03a05600);
	rtl_set_bbreg(hw, 0xc08, MASKDWORD, 0x000800e4);
	rtl_set_bbreg(hw, 0x874, MASKDWORD, 0x22204000);

	/* path A TX IQK */
	for (i = 0; i < retrycount; i++) {
		patha_ok = _rtl8723be_phy_path_a_iqk(hw);
		if (patha_ok == 0x01) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				"Path A Tx IQK Success!!\n");
			result[t][0] = (rtl_get_bbreg(hw, 0xe94, MASKDWORD) &
					0x3FF0000) >> 16;
			result[t][1] = (rtl_get_bbreg(hw, 0xe9c, MASKDWORD) &
					0x3FF0000) >> 16;
			break;
		} else {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Path A Tx IQK Fail!!\n");
		}
	}
	/* path A RX IQK */
	for (i = 0; i < retrycount; i++) {
		patha_ok = _rtl8723be_phy_path_a_rx_iqk(hw);
		if (patha_ok == 0x03) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Path A Rx IQK Success!!\n");
			result[t][2] = (rtl_get_bbreg(hw, 0xea4, MASKDWORD) &
					0x3FF0000) >> 16;
			result[t][3] = (rtl_get_bbreg(hw, 0xeac, MASKDWORD) &
					0x3FF0000) >> 16;
			break;
		} else {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Path A Rx IQK Fail!!\n");
		}
	}

	if (0x00 == patha_ok)
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Path A IQK Fail!!\n");

	if (is2t) {
		/* path B TX IQK */
		for (i = 0; i < retrycount; i++) {
			pathb_ok = _rtl8723be_phy_path_b_iqk(hw);
			if (pathb_ok == 0x01) {
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "Path B Tx IQK Success!!\n");
				result[t][4] = (rtl_get_bbreg(hw, 0xe94, MASKDWORD) &
						0x3FF0000) >> 16;
				result[t][5] = (rtl_get_bbreg(hw, 0xe9c, MASKDWORD) &
						0x3FF0000) >> 16;
				break;
			} else {
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "Path B Tx IQK Fail!!\n");
			}
		}
		/* path B RX IQK */
		for (i = 0; i < retrycount; i++) {
			pathb_ok = _rtl8723be_phy_path_b_rx_iqk(hw);
			if (pathb_ok == 0x03) {
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "Path B Rx IQK Success!!\n");
				result[t][6] = (rtl_get_bbreg(hw, 0xea4, MASKDWORD) &
						0x3FF0000) >> 16;
				result[t][7] = (rtl_get_bbreg(hw, 0xeac, MASKDWORD) &
						0x3FF0000) >> 16;
				break;
			} else {
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "Path B Rx IQK Fail!!\n");
			}
		}
	}

	/* Back to BB mode, load original value */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0);

	if (t != 0) {
		_rtl8723be_phy_reload_adda_registers(hw, adda_reg,
						     rtlphy->adda_backup, 16);
		_rtl8723be_phy_reload_mac_registers(hw, iqk_mac_reg,
						    rtlphy->iqk_mac_backup);
		_rtl8723be_phy_reload_adda_registers(hw, iqk_bb_reg,
						     rtlphy->iqk_bb_backup,
						     IQK_BB_REG_NUM);

		rtl_set_bbreg(hw, 0x948, MASKDWORD, path_sel_bb);
		/*rtl_set_rfreg(hw, RF90_PATH_B, 0xb0, 0xfffff, path_sel_rf);*/

		rtl_set_bbreg(hw, 0xc50, MASKBYTE0, 0x50);
		rtl_set_bbreg(hw, 0xc50, MASKBYTE0, tmp_reg_c50);
		if (is2t) {
			rtl_set_bbreg(hw, 0xc58, MASKBYTE0, 0x50);
			rtl_set_bbreg(hw, 0xc58, MASKBYTE0, tmp_reg_c58);
		}
		rtl_set_bbreg(hw, 0xe30, MASKDWORD, 0x01008c00);
		rtl_set_bbreg(hw, 0xe34, MASKDWORD, 0x01008c00);
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "8723be IQK Finish!!\n");
}

static u8 _get_right_chnl_place_for_iqk(u8 chnl)
{
	u8 channel_all[TARGET_CHNL_NUM_2G_5G] = {
			1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
			13, 14, 36, 38, 40, 42, 44, 46,
			48, 50, 52, 54, 56, 58, 60, 62, 64,
			100, 102, 104, 106, 108, 110,
			112, 114, 116, 118, 120, 122,
			124, 126, 128, 130, 132, 134, 136,
			138, 140, 149, 151, 153, 155, 157,
			159, 161, 163, 165};
	u8 place = chnl;


	if (chnl > 14) {
		for (place = 14; place < sizeof(channel_all); place++) {
			if (channel_all[place] == chnl)
				return place - 13;
		}
	}
	return 0;
}

static void _rtl8723be_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t)
{
	u8 tmpreg;
	u32 rf_a_mode = 0, rf_b_mode = 0, lc_cal;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	tmpreg = rtl_read_byte(rtlpriv, 0xd03);

	if ((tmpreg & 0x70) != 0)
		rtl_write_byte(rtlpriv, 0xd03, tmpreg & 0x8F);
	else
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);

	if ((tmpreg & 0x70) != 0) {
		rf_a_mode = rtl_get_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS);

		if (is2t)
			rf_b_mode = rtl_get_rfreg(hw, RF90_PATH_B, 0x00,
						  MASK12BITS);

		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS,
			      (rf_a_mode & 0x8FFFF) | 0x10000);

		if (is2t)
			rtl_set_rfreg(hw, RF90_PATH_B, 0x00, MASK12BITS,
				      (rf_b_mode & 0x8FFFF) | 0x10000);
	}
	lc_cal = rtl_get_rfreg(hw, RF90_PATH_A, 0x18, MASK12BITS);

	rtl_set_rfreg(hw, RF90_PATH_A, 0xb0, RFREG_OFFSET_MASK, 0xdfbe0);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x18, MASK12BITS, 0x8c0a);

	/* In order not to disturb BT music when wifi init.(1ant NIC only) */
	/*mdelay(100);*/
	/* In order not to disturb BT music when wifi init.(1ant NIC only) */
	mdelay(50);

	rtl_set_rfreg(hw, RF90_PATH_A, 0xb0, RFREG_OFFSET_MASK, 0xdffe0);

	if ((tmpreg & 0x70) != 0) {
		rtl_write_byte(rtlpriv, 0xd03, tmpreg);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS, rf_a_mode);

		if (is2t)
			rtl_set_rfreg(hw, RF90_PATH_B, 0x00,
				      MASK12BITS, rf_b_mode);
	} else {
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
	}
RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "\n");

}

static void _rtl8723be_phy_set_rfpath_switch(struct ieee80211_hw *hw,
					     bool bmain, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "\n");

	if (bmain) /* left antenna */
		rtl_set_bbreg(hw, 0x92C, MASKDWORD, 0x1);
	else
		rtl_set_bbreg(hw, 0x92C, MASKDWORD, 0x2);
}

#undef IQK_ADDA_REG_NUM
#undef IQK_DELAY_TIME
/* IQK is merge from Merge Temp */
void rtl8723be_phy_iq_calibrate(struct ieee80211_hw *hw, bool b_recovery)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	long result[4][8];
	u8 i, final_candidate, idx;
	bool b_patha_ok, b_pathb_ok;
	long reg_e94, reg_e9c, reg_ea4, reg_eac, reg_eb4, reg_ebc, reg_ec4;
	long reg_ecc, reg_tmp = 0;
	bool is12simular, is13simular, is23simular;
	u32 iqk_bb_reg[9] = {
		ROFDM0_XARXIQIMBALANCE,
		ROFDM0_XBRXIQIMBALANCE,
		ROFDM0_ECCATHRESHOLD,
		ROFDM0_AGCRSSITABLE,
		ROFDM0_XATXIQIMBALANCE,
		ROFDM0_XBTXIQIMBALANCE,
		ROFDM0_XCTXAFE,
		ROFDM0_XDTXAFE,
		ROFDM0_RXIQEXTANTA
	};
	u32 path_sel_bb = 0; /* path_sel_rf = 0 */

	if (rtlphy->iqk_inprogress)
		return;

	spin_lock(&rtlpriv->locks.iqk_lock);
	rtlphy->iqk_inprogress = true;
	spin_unlock(&rtlpriv->locks.iqk_lock);

	if (b_recovery) {
		_rtl8723be_phy_reload_adda_registers(hw, iqk_bb_reg,
						     rtlphy->iqk_bb_backup, 9);
		return;
	}
	/* Save RF Path */
	path_sel_bb = rtl_get_bbreg(hw, 0x948, MASKDWORD);
	/* path_sel_rf = rtl_get_rfreg(hw, RF90_PATH_A, 0xb0, 0xfffff); */

	for (i = 0; i < 8; i++) {
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	final_candidate = 0xff;
	b_patha_ok = false;
	b_pathb_ok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;
	for (i = 0; i < 3; i++) {
		_rtl8723be_phy_iq_calibrate(hw, result, i, true);
		if (i == 1) {
			is12simular = _rtl8723be_phy_simularity_compare(hw,
									result,
									0, 1);
			if (is12simular) {
				final_candidate = 0;
				break;
			}
		}
		if (i == 2) {
			is13simular = _rtl8723be_phy_simularity_compare(hw,
									result,
									0, 2);
			if (is13simular) {
				final_candidate = 0;
				break;
			}
			is23simular = _rtl8723be_phy_simularity_compare(hw,
									result,
									1, 2);
			if (is23simular) {
				final_candidate = 1;
			} else {
				for (i = 0; i < 8; i++)
					reg_tmp += result[3][i];

				if (reg_tmp != 0)
					final_candidate = 3;
				else
					final_candidate = 0xFF;
			}
		}
	}
	for (i = 0; i < 4; i++) {
		reg_e94 = result[i][0];
		reg_e9c = result[i][1];
		reg_ea4 = result[i][2];
		reg_eac = result[i][3];
		reg_eb4 = result[i][4];
		reg_ebc = result[i][5];
		reg_ec4 = result[i][6];
		reg_ecc = result[i][7];
	}
	if (final_candidate != 0xff) {
		rtlphy->reg_e94 = reg_e94 = result[final_candidate][0];
		rtlphy->reg_e9c = reg_e9c = result[final_candidate][1];
		reg_ea4 = result[final_candidate][2];
		reg_eac = result[final_candidate][3];
		rtlphy->reg_eb4 = reg_eb4 = result[final_candidate][4];
		rtlphy->reg_ebc = reg_ebc = result[final_candidate][5];
		reg_ec4 = result[final_candidate][6];
		reg_ecc = result[final_candidate][7];
		b_patha_ok = b_pathb_ok = true;
	} else {
		rtlphy->reg_e94 = rtlphy->reg_eb4 = 0x100;
		rtlphy->reg_e9c = rtlphy->reg_ebc = 0x0;
	}
	if (reg_e94 != 0) /*&&(reg_ea4 != 0) */
		_rtl8723be_phy_path_a_fill_iqk_matrix(hw, b_patha_ok, result,
						      final_candidate,
						      (reg_ea4 == 0));
	if (reg_eb4 != 0) /*&&(reg_ec4 != 0) */
		_rtl8723be_phy_path_b_fill_iqk_matrix(hw, b_pathb_ok, result,
						      final_candidate,
						      (reg_ec4 == 0));

	idx = _get_right_chnl_place_for_iqk(rtlphy->current_channel);

	if (final_candidate < 4) {
		for (i = 0; i < IQK_MATRIX_REG_NUM; i++)
			rtlphy->iqk_matrix_regsetting[idx].value[0][i] =
						result[final_candidate][i];
		rtlphy->iqk_matrix_regsetting[idx].iqk_done = true;

	}
	_rtl8723be_phy_save_adda_registers(hw, iqk_bb_reg,
					   rtlphy->iqk_bb_backup, 9);

	rtl_set_bbreg(hw, 0x948, MASKDWORD, path_sel_bb);
	/* rtl_set_rfreg(hw, RF90_PATH_A, 0xb0, 0xfffff, path_sel_rf); */

	spin_lock(&rtlpriv->locks.iqk_lock);
	rtlphy->iqk_inprogress = false;
	spin_unlock(&rtlpriv->locks.iqk_lock);
}

void rtl8723be_phy_lc_calibrate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u32 timeout = 2000, timecount = 0;


	while (rtlpriv->mac80211.act_scanning && timecount < timeout) {
		udelay(50);
		timecount += 50;
	}

	rtlphy->lck_inprogress = true;
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"LCK:Start!!! currentband %x delay %d ms\n",
		 rtlhal->current_bandtype, timecount);

	_rtl8723be_phy_lc_calibrate(hw, false);

	rtlphy->lck_inprogress = false;
}

void rtl92c_phy_ap_calibrate(struct ieee80211_hw *hw, char delta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	if (rtlphy->apk_done)
		return;

	return;
}

void rtl8723be_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool bmain)
{
	_rtl8723be_phy_set_rfpath_switch(hw, bmain, true);
}

bool rtl8723be_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	bool b_postprocessing = false;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "-->IO Cmd(%#x), set_io_inprogress(%d)\n",
		  iotype, rtlphy->set_io_inprogress);
	do {
		switch (iotype) {
		case IO_CMD_RESUME_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 "[IO CMD] Resume DM after scan.\n");
			b_postprocessing = true;
			break;
		case IO_CMD_PAUSE_BAND0_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 "[IO CMD] Pause DM before scan.\n");
			b_postprocessing = true;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
				 "switch case not process\n");
			break;
		}
	} while (false);
	if (b_postprocessing && !rtlphy->set_io_inprogress) {
		rtlphy->set_io_inprogress = true;
		rtlphy->current_io_type = iotype;
	} else {
		return false;
	}
	rtl8723be_phy_set_io(hw);
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE, "IO Type(%#x)\n", iotype);
	return true;
}

static void rtl8723be_phy_set_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "--->Cmd(%#x), set_io_inprogress(%d)\n",
		  rtlphy->current_io_type, rtlphy->set_io_inprogress);
	switch (rtlphy->current_io_type) {
	case IO_CMD_RESUME_DM_BY_SCAN:
		dm_digtable->cur_igvalue = rtlphy->initgain_backup.xaagccore1;
		/*rtl92c_dm_write_dig(hw);*/
		rtl8723be_phy_set_txpower_level(hw, rtlphy->current_channel);
		rtl_set_bbreg(hw, RCCK0_CCA, 0xff0000, 0x83);
		break;
	case IO_CMD_PAUSE_BAND0_DM_BY_SCAN:
		rtlphy->initgain_backup.xaagccore1 = dm_digtable->cur_igvalue;
		dm_digtable->cur_igvalue = 0x17;
		rtl_set_bbreg(hw, RCCK0_CCA, 0xff0000, 0x40);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case not process\n");
		break;
	}
	rtlphy->set_io_inprogress = false;
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "(%#x)\n", rtlphy->current_io_type);
}

static void rtl8723be_phy_set_rf_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
}

static void _rtl8723be_phy_set_rf_sleep(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x22);
}

static bool _rtl8723be_phy_set_rf_power_state(struct ieee80211_hw *hw,
					      enum rf_pwrstate rfpwr_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool bresult = true;
	u8 i, queue_id;
	struct rtl8192_tx_ring *ring = NULL;

	switch (rfpwr_state) {
	case ERFON:
		if ((ppsc->rfpwr_state == ERFOFF) &&
		     RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC)) {
			bool rtstatus;
			u32 InitializeCount = 0;
			do {
				InitializeCount++;
				RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
					 "IPS Set eRf nic enable\n");
				rtstatus = rtl_ps_enable_nic(hw);
			} while ((rtstatus != true) && (InitializeCount < 10));
				RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "Set ERFON sleeped:%d ms\n",
				  jiffies_to_msecs(jiffies -
						   ppsc->last_sleep_jiffies));
			ppsc->last_awake_jiffies = jiffies;
			rtl8723be_phy_set_rf_on(hw);
		}
		if (mac->link_state == MAC80211_LINKED)
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_LINK);
		else
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_NO_LINK);

		break;

	case ERFOFF:
		for (queue_id = 0, i = 0;
		     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
			ring = &pcipriv->dev.tx_ring[queue_id];
			/* Don't check BEACON Q.
			 * BEACON Q is always not empty,
			 * because 'rtl_cmd_send_packet' */
			if (queue_id == BEACON_QUEUE ||
			    skb_queue_len(&ring->queue) == 0) {
				queue_id++;
				continue;
			} else {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
					 (i + 1), queue_id,
					 skb_queue_len(&ring->queue));

				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "ERFSLEEP: %d times TcbBusyQueue[%d] = %d !\n",
					  MAX_DOZE_WAITING_TIMES_9x,
					  queue_id,
					  skb_queue_len(&ring->queue));
				break;
			}
		}

		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC) {
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "IPS Set eRf nic disable\n");
			rtl_ps_disable_nic(hw);
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS) {
				rtlpriv->cfg->ops->led_control(hw,
							       LED_CTL_NO_LINK);
			} else {
				rtlpriv->cfg->ops->led_control(hw,
							     LED_CTL_POWER_OFF);
			}
		}
		break;

	case ERFSLEEP:
		if (ppsc->rfpwr_state == ERFOFF)
			break;
		for (queue_id = 0, i = 0;
		     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
			ring = &pcipriv->dev.tx_ring[queue_id];
			if (skb_queue_len(&ring->queue) == 0) {
				queue_id++;
				continue;
			} else {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
					 (i + 1), queue_id,
					 skb_queue_len(&ring->queue));

				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "ERFSLEEP: %d times TcbBusyQueue[%d] = %d !\n",
					 MAX_DOZE_WAITING_TIMES_9x,
					 queue_id,
					 skb_queue_len(&ring->queue));
				break;
			}
		}
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "Set ERFSLEEP awaked:%d ms\n",
			  jiffies_to_msecs(jiffies -
					   ppsc->last_awake_jiffies));
		ppsc->last_sleep_jiffies = jiffies;
		_rtl8723be_phy_set_rf_sleep(hw);
		break;

	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case not process\n");
		bresult = false;
		break;
	}
	if (bresult)
		ppsc->rfpwr_state = rfpwr_state;
	return bresult;
}

bool rtl8723be_phy_set_rf_power_state(struct ieee80211_hw *hw,
				      enum rf_pwrstate rfpwr_state)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	bool bresult = false;

	if (rfpwr_state == ppsc->rfpwr_state)
		return bresult;
	bresult = _rtl8723be_phy_set_rf_power_state(hw, rfpwr_state);
	return bresult;
}
