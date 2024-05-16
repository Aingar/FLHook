// BountyScan Plugin by Alex. Just looks up the target's ID (tractor). For convenience on Discovery.
// 
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include "Main.h"
#include "headers/zlib.h"

PLUGIN_RETURNCODE returncode;

DWORD* dsac_update_infocard_cmd = 0;
DWORD dsac_update_infocard_cmd_len = 0;

DWORD* dsac_update_econ_cmd = 0;
DWORD dsac_update_econ_cmd_len = 0;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		HkLoadStringDLLs();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		HkUnloadStringDLLs();
	}
	return true;
}

EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

void LoadMarketGoodsIni(const string& scPath, map<uint, market_map_t >& mapBaseMarket)
{
	INI_Reader ini;
	if (!ini.open(scPath.c_str(), false))
	{
		return;
	}
	while (ini.read_header())
	{
		if (!ini.is_header("BaseGood"))
		{
			continue;
		}
		if (!ini.read_value() || !ini.is_value("base"))
		{
			continue;
		}
		market_map_t mapMarket;
		uint baseId = CreateID(ini.get_value_string());
		while (ini.read_value())
		{
			if (!ini.is_value("MarketGood"))
			{
				continue;
			}
			MarketGoodInfo mgi;
			mgi.iGoodID = CreateID(ini.get_value_string(0));
			mgi.iMin = ini.get_value_int(3);
			mgi.iQuantity = ini.get_value_int(4);
			mgi.iTransType = (TransactionType)ini.get_value_int(5);
			mgi.fRep = 0.0f;
			mgi.fRank = 0.0f;
			const GoodInfo* gi = GoodList::find_by_id(mgi.iGoodID);
			mgi.fPrice = (gi) ? gi->fPrice * ini.get_value_float(6) : 0.0f;
			mapMarket.insert(market_map_t::value_type(mgi.iGoodID, mgi));
		}
		mapBaseMarket.insert(map<uint, market_map_t >::value_type(baseId, mapMarket));
	}
	ini.close();
}

struct VC7_MAP
{
	VC7_MAP* left;
	VC7_MAP* parent;
	VC7_MAP* right;
	DWORD first;
	DWORD second;
};

static DWORD* stl_map_find(DWORD* map, DWORD item)
{
	DWORD* start = (DWORD*)map[1];
	VC7_MAP* curr = (VC7_MAP*)start[1];
	VC7_MAP* end = (VC7_MAP*)map[2];

	VC7_MAP* res = 0;
	while (curr != end)
	{
		if (curr->first >= item)
		{
			res = curr;
			curr = curr->left;
		}
		else
		{
			curr = curr->right;
		}
	}
	if (res && item == res->first)
		return (DWORD*)&(res->second);
	else
		return 0;
}
/// Return a infocard update packet with the size of the packet in the cmdsize parameter.
DWORD* BuildDSACEconUpdateCmd(DWORD* cmdsize, bool bReset, map<uint, market_map_t>& mapMarket)
{
	// Calculate the size of the data to compress and allocate a buffer
	DWORD number_of_updates = 0;
	DWORD srclen = 8; // header
	for (map<uint, market_map_t>::iterator i = mapMarket.begin(); i != mapMarket.end(); ++i)
	{
		srclen += i->second.size() * 20;
		number_of_updates += i->second.size();
	}
	DWORD* src = (DWORD*)new byte[srclen];

	// Build the infocard packet contents
	uint pos = 0;
	src[pos++] = bReset; // reset econ
	src[pos++] = number_of_updates; // number of updates;
	for (map<uint, market_map_t>::const_iterator iterBase = mapMarket.begin();
		iterBase != mapMarket.end(); ++iterBase)
	{
		uint baseId = iterBase->first;
		for (market_map_t::const_iterator iterMarketDelta = iterBase->second.begin();
			iterMarketDelta != iterBase->second.end(); iterMarketDelta++)
		{
			const MarketGoodInfo& mgi = iterMarketDelta->second;
			src[pos++] = baseId;
			src[pos++] = mgi.iGoodID;
			*((float*)&(src[pos++])) = mgi.fPrice;
			src[pos++] = mgi.iQuantity; // sell price
			src[pos++] = mgi.iTransType;
		}
	}

	// Compress it into a buffer with sufficient room for the packet header
	uint destlen = compressBound(srclen);
	DWORD* dest = (DWORD*)new byte[16 + destlen];
	compress((Bytef*)(&dest[4]), (uLongf*)&destlen, (const Bytef*)src, (uLong)srclen);

	// Add the packet header.
	dest[0] = 0xD5AC;
	dest[1] = 0x02;
	dest[2] = destlen;
	dest[3] = srclen;

	// Clean up and save the buffered infocards.
	delete src;
	*cmdsize = 16 + destlen;
	return dest;
}

void SendD5ACCmd(uint client, DWORD* pData, uint iSize)
{
	HkFMsgSendChat(client, (char*)pData, iSize);
}

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	AlleyMF::LoadSettings();

	map<uint, market_map_t> mapBaseMarketDelta;

	// Load the default prices for goods on sale at each base.
	map<uint, market_map_t > mapOldBaseMarket;
	LoadMarketGoodsIni("..\\data\\equipment\\market_commodities.ini", mapOldBaseMarket);
	LoadMarketGoodsIni("..\\data\\equipment\\market_misc.ini", mapOldBaseMarket);

	// Read the prices.ini and add all goods in [Price] section in this file to the market.
	// Before this remove all goods in the [NoSale] section from the market.
	INI_Reader ini;
	if (ini.open("flhook_plugins\\prices.cfg", false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("NoSale"))
			{
				while (ini.read_value())
				{
					uint iGoodID = CreateID(ini.get_name_ptr());

					// For each base that sells this good and an item to the new market map to
					// set this item to buy only (buy only by the dealer).
					for (map<uint, market_map_t >::const_iterator iterOldBaseMarket = mapOldBaseMarket.begin();
						iterOldBaseMarket != mapOldBaseMarket.end(); iterOldBaseMarket++)
					{
						const uint& baseId = iterOldBaseMarket->first;
						const market_map_t& mapOldMarket = iterOldBaseMarket->second;
						if (mapOldMarket.find(iGoodID) != mapOldMarket.end())
						{
							const GoodInfo* gi = GoodList::find_by_id(iGoodID);
							if (gi && gi->iType == GOODINFO_TYPE_COMMODITY)
							{
								mapBaseMarketDelta[baseId][iGoodID].iGoodID = iGoodID;
								mapBaseMarketDelta[baseId][iGoodID].fPrice = gi->fPrice;
								mapBaseMarketDelta[baseId][iGoodID].iTransType = TransactionType_Buy;
							}
						}
					}
				}
			}
			else if (ini.is_header("Price"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("MarketGood"))
					{
						uint baseId = CreateID(ini.get_value_string(0));
						uint iGoodID = CreateID(ini.get_value_string(1));
						float fPrice = ini.get_value_float(2);
						uint iSellPrice = ini.get_value_int(3);
						bool bBaseBuys = (ini.get_value_int(4) == 1);

						mapBaseMarketDelta[baseId][iGoodID].iGoodID = iGoodID;
						mapBaseMarketDelta[baseId][iGoodID].fPrice = fPrice;
						mapBaseMarketDelta[baseId][iGoodID].iQuantity = iSellPrice;
						mapBaseMarketDelta[baseId][iGoodID].iTransType = (bBaseBuys) ? TransactionType_Buy : TransactionType_Sell;
					}
				}
			}
		}
		ini.close();
	}


	// Reset the commodities and load the price changes.
	BaseDataList_load_market_data("..\\data\\equipment\\market_commodities.ini");

	for (map<uint, market_map_t>::const_iterator iterBase = mapBaseMarketDelta.begin(); iterBase != mapBaseMarketDelta.end(); iterBase++)
	{
		const uint& baseId = iterBase->first;
		BaseData* bd = BaseDataList_get()->get_base_data(baseId);
		if (!bd)
		{
			continue;
		}
		for (market_map_t::const_iterator iterMarketDelta = iterBase->second.begin();
			iterMarketDelta != iterBase->second.end();
			iterMarketDelta++)
		{
			const MarketGoodInfo& mgi = iterMarketDelta->second;
			const GoodInfo* gi = GoodList::find_by_id(mgi.iGoodID);
			if (!gi)
			{
				continue;
			}
			// The multiplier is the new price / old good (base) price
			float fMultiplier = mgi.fPrice / gi->fPrice;

			bd->set_market_good(mgi.iGoodID, mgi.iMin, !mgi.iTransType, (TransactionType)mgi.iTransType, fMultiplier, 0.0f, -1.0f);
		}
	}

	// Build dsace command for version 6 dsace clients
	if (dsac_update_econ_cmd)
		delete dsac_update_econ_cmd;
	dsac_update_econ_cmd = BuildDSACEconUpdateCmd(&dsac_update_econ_cmd_len, 1, mapBaseMarketDelta);
	
	// For any players in a base, update them.
	struct PlayerData* pPD = 0;
	while (pPD = Players.traverse_active(pPD))
	{
		SendD5ACCmd(pPD->iOnlineID, dsac_update_econ_cmd, dsac_update_econ_cmd_len);
	}
}

void __stdcall Login(struct SLoginInfo const& li, uint client)
{
	returncode = DEFAULT_RETURNCODE;
	SendD5ACCmd(client, dsac_update_econ_cmd, dsac_update_econ_cmd_len);
}

void __stdcall GFGoodSell(struct SGFGoodSellInfo const& gsi, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	AlleyMF::GFGoodSell(gsi, client);

	const GoodInfo* gi = GoodList_get()->find_by_id(gsi.iArchID);
	if (gi->iType != GOODINFO_TYPE_COMMODITY)
	{
		return;
	}

	BaseData* bd = BaseDataList_get()->get_base_data(Players[client].iBaseID);
	MarketGoodInfo* info = (MarketGoodInfo*)stl_map_find((DWORD*)&(bd->market_map), gsi.iArchID);
	int sellPrice = info->iQuantity;
	int currPrice = static_cast<int>(info->fPrice);
	pub::Player::AdjustCash(client, gsi.iCount * (sellPrice - currPrice));
}

void __stdcall GFGoodBuy(struct SGFGoodBuyInfo const& gbi, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	if (!CommodityLimit::GFGoodBuy(gbi, client) 
		|| !AlleyMF::GFGoodBuy(gbi, client))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
}

void __stdcall PlayerLaunch(unsigned int iShip, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	AlleyMF::PlayerLaunch(iShip, client);
}

void __stdcall BaseEnter_AFTER(unsigned int baseId, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	AlleyMF::BaseEnter_AFTER(baseId, client);
}

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "MarketController by Aingar";
	p_PI->sShortName = "MarketController";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Login, PLUGIN_HkIServerImpl_Login, 0));
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodSell, PLUGIN_HkIServerImpl_GFGoodSell, 0));
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodBuy, PLUGIN_HkIServerImpl_GFGoodBuy, 0));
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch, PLUGIN_HkIServerImpl_PlayerLaunch, 0));
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter_AFTER, PLUGIN_HkIServerImpl_BaseEnter_AFTER, 0));
	//
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CommodityLimit::ClearClientInfo, PLUGIN_ClearClientInfo, 0));
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CommodityLimit::ReqAddItem, PLUGIN_HkIServerImpl_ReqAddItem, 0));
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CommodityLimit::ReqChangeCash, PLUGIN_HkIServerImpl_ReqChangeCash, 0));

	return p_PI;
}
