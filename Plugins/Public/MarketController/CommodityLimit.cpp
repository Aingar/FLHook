// CommodityLimit for FLHookPlugin
// February 2016 by Alley
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Main.h"

#include "../hookext_plugin/hookext_exports.h"

/// A return code to indicate to FLHook if we want the hook processing to continue.

struct CommodityLimitStruct
{
	list<wstring> TagRestrictions;
	unordered_set<uint> IDRestrictions;
};

unordered_map<uint, CommodityLimitStruct> mapCommodityRestrictions;
unordered_map<uint, bool> mapBuySuppression;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommodityLimit::LoadSettings()
{
	string File_FLHook = "..\\exe\\flhook_plugins\\commodity_restrictions.cfg";
	int iLoaded = 0;
	int iLoaded2 = 0;

	INI_Reader ini;
	vector<uint> buyBackRestrictedGoods;
	if (ini.open(File_FLHook.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("commodity"))
			{
				uint commodity;
				CommodityLimitStruct cls;
				while (ini.read_value())
				{
					if (ini.is_value("commodity"))
					{
						pub::GetGoodID(commodity, ini.get_value_string(0));
					}
					else if (ini.is_value("tag"))
					{
						cls.TagRestrictions.emplace_back(stows(ini.get_value_string(0)));
					}
					else if (ini.is_value("id"))
					{
						cls.IDRestrictions.insert(CreateID(ini.get_value_string(0)));
					}
				}
				mapCommodityRestrictions[commodity] = cls;
				++iLoaded;
			}
		}
		ini.close();
	}

	ConPrint(L"CL: Loaded %u Limited Commodities\n", iLoaded);
}


/** Clean up when a client disconnects */
void CommodityLimit::ClearClientInfo(uint iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	mapBuySuppression.erase(iClientID);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommodityLimit::GFGoodBuy(struct SGFGoodBuyInfo const& gbi, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;

	//Check to ensure this ship has been undocked at least once and the character has an hookext ID value stored
	uint pID = ClientInfo[iClientID].playerID;

	static const uint recruitID = CreateID("dsy_license_military");
	if (!pID || pID == recruitID)
	{
		auto good = GoodList_get()->find_by_id(gbi.iGoodID);
		if (good && good->iType == GOODINFO_TYPE_COMMODITY)
		{
			PrintUserCmdText(iClientID, L"ERR You cannot buy commodities as Recruit ID holder");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			mapBuySuppression[iClientID] = true;
			return false;
		}
	}

	if (Players[iClientID].equipDescList.equip.size() >= 127)
	{
		PrintUserCmdText(iClientID, L"ERR Too many individual items in hold, aborting purchase to prevent character corruption");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		mapBuySuppression[iClientID] = true;
		return false;
	}
	//Check if this a purchase this plugin must handle
	auto commodityRestriction = mapCommodityRestrictions.find(gbi.iGoodID);
	if (commodityRestriction == mapCommodityRestrictions.end())
	{
		return true;
	}

	if (pID != 0)
	{
		bool valid = false;
		//Check the ID to begin with, it's the most likely type of restriction
		if (commodityRestriction->second.IDRestrictions.count(pID))
		{
			//Allow the purchase
			valid = true;
		}
		else
		{
			//If the ID doesn't match, check for the tag
			wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
			for (wstring& tag : commodityRestriction->second.TagRestrictions)
			{
				if (wscCharname.find(tag) != string::npos)
				{
					valid = true;
					break;
				}
			}
		}

		//If none of the conditions have been met, deny the purchase
		if (!valid)
		{
			//deny the purchase
			PrintUserCmdText(iClientID, L"Sorry, you do not have permission to buy this item.");
			mapBuySuppression[iClientID] = true;
			return false;
		}

	}
	else
	{
		//deny the purchase
		PrintUserCmdText(iClientID, L"Your ship is not initialized. Please undock once to initialize your server variables.");
		mapBuySuppression[iClientID] = true;
		return false;
	}
	return true;
}

/// Suppress the buying of goods.
void __stdcall CommodityLimit::ReqAddItem(uint& goodID, char const* hardpoint, int count, float status, bool& mounted, uint iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	if (mapBuySuppression[iClientID])
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
}

void CommodityLimit::ReqChangeCash(int iMoneyDiff, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	if (mapBuySuppression[iClientID])
	{
		mapBuySuppression[iClientID] = false;
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
}