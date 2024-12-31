// AlleyPlugin for FLHookPlugin
// March 2015 by Alley
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Main.h"
#include <sstream>
#include <iostream>


// For ships, we go the easy way and map each ship belonging to each base
static unordered_map <uint, unordered_set<uint>> mapACShips;
// For items, we create a list of all bases with market possibilities.
// And a list of the items we keep under watch
static unordered_set<uint> mapACBases;
static unordered_set<uint> mapACItems;

// map we'll use to keep track of watched item sales.
static unordered_map <uint, unordered_map <uint, int>> mapACSales;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AlleyMF::LoadSettings()
{
	// The path to the configuration file.
	string marketshipsfile = "..\\data\\equipment\\market_ships.ini";
	string marketcommoditiesfile = "..\\data\\equipment\\market_commodities.ini";
	string flhookitems = "..\\exe\\flhook_plugins\\alley_mf.cfg";
	int shipamount1 = 0;
	int shipamount2 = 0;
	int commodamount1 = 0;
	int commodamount2 = 0;

	INI_Reader ini;
	if (ini.open(marketshipsfile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (!ini.is_header("BaseGood"))
			{
				continue;
			}
			while (ini.read_value())
			{
				uint currentbase;
				if (ini.is_value("base"))
				{
					currentbase = CreateID(ini.get_value_string(0));
					shipamount1++;
				}
				else if (ini.is_value("marketgood"))
				{
					mapACShips[currentbase].insert(CreateID(ini.get_value_string(0)));
					shipamount2++;
				}
			}
		}
		ini.close();
	}
	if (ini.open(marketcommoditiesfile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (!ini.is_header("BaseGood"))
			{
				continue;
			}
			while (ini.read_value())
			{
				if (!ini.is_value("base"))
				{
					continue;
				}
				uint currentbase = CreateID(ini.get_value_string(0));
				mapACBases.insert(currentbase);
				commodamount1++;
			}
		}
		ini.close();
	}
	if (ini.open(flhookitems.c_str(), false))
	{
		while (ini.read_header())
		{
			if (!ini.is_header("items"))
			{
				continue;
			}
			while (ini.read_value())
			{
				uint currentitem;
				if (!ini.is_value("item"))
				{
					continue;
				}
				mapACItems.insert(currentitem);
				commodamount2++;
			}
		}
		ini.close();
	}

	ConPrint(L"MARKETFUCKER: Loaded %u ships for %u bases \n", shipamount2, shipamount1);
	ConPrint(L"MARKETFUCKER: Loaded %u items for %u bases \n", commodamount2, commodamount1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

FILE* Logfile = fopen("./flhook_logs/marketfucker.log", "at");

void Logging(const char* szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	char szBuf[64];
	time_t tNow = time(0);
	struct tm* t = localtime(&tNow);
	strftime(szBuf, sizeof(szBuf), "%Y/%m/%d %H:%M:%S", t);
	fprintf(Logfile, "%s %s\n", szBuf, szBufString);
	fflush(Logfile);
	fclose(Logfile);
	Logfile = fopen("./flhook_logs/marketfucker.log", "at");
}

void LogCheater(uint client, const wstring& reason)
{
	CAccount* acc = Players.FindAccountFromClientID(client);

	if (!HkIsValidClientID(client) || !acc)
	{
		AddLog("ERROR: invalid parameter in log cheater, clientid=%u acc=%08x reason=%s", client, acc, wstos(reason).c_str());
		return;
	}

	//internal log
	string scText = wstos(reason);
	Logging("%s", scText.c_str());

	// Set the kick timer to kick this player. We do this to break potential
	// stack corruption.
	HkDelayedKick(client, 1);

	// Ban the account.
	flstr* flStr = CreateWString(acc->wszAccID);
	Players.BanAccount(*flStr, true);
	FreeWString(flStr);

	// Overwrite the ban file so that it contains the ban reason
	wstring wscDir;
	HkGetAccountDirName(acc, wscDir);
	string scBanPath = scAcctPath + wstos(wscDir) + "\\banned";
	FILE* file = fopen(scBanPath.c_str(), "wb");
	if (file)
	{
		fprintf(file, "Autobanned by MarketController\n");
		fclose(file);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Actual Code
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Clean up when a client disconnects */
void ClearClientInfo(uint clientId)
{
	mapACSales.erase(clientId);
}


void AlleyMF::GFGoodSell(struct SGFGoodSellInfo const& gsi, unsigned int clientId)
{
	// check for equipments only
	const GoodInfo* packageInfo = GoodList::find_by_id(gsi.iArchID);
	if (packageInfo->iType != 1)
	{
		return;
	}
	uint iBase = Players[clientId].iBaseID;
	auto ACItemsIter = mapACItems.find(gsi.iArchID);
	if (ACItemsIter == mapACItems.end())
	{
		return;
	}
	//We iterate through the base names to see if it's a non-POB base
	auto baseIter = mapACBases.find(iBase);
	if (baseIter == mapACBases.end())
	{
		return;
	}
	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(clientId);
	wstring wscBaseName = HkGetBaseNickByID(iBase);

	//PrintUserCmdText(clientId, L"I have found this base, logging the purchase.");
	// check if this item is already under watch, if so increase amount by 1
	wstring wscMsgLog;
	auto saleEntry = mapACSales[clientId].find(gsi.iArchID);
	if (saleEntry != mapACSales[clientId].end())
	{
		++saleEntry->second;
		//PrintUserCmdText(clientId, L"DEBUG: I have logged %i sales.", mapACSales[clientId].items.find(gsi.iArchID)->second);
		wscMsgLog = L"<%sender> has sold <%item> to base <%basename> (Already recorded %isale sales of this item)";
		wscMsgLog = ReplaceStr(wscMsgLog, L"%sender", wscCharname.c_str());
		wscMsgLog = ReplaceStr(wscMsgLog, L"%basename", wscBaseName.c_str());
		wscMsgLog = ReplaceStr(wscMsgLog, L"%item", HkGetWStringFromIDS(packageInfo->iIDSName).c_str());
		wscMsgLog = ReplaceStr(wscMsgLog, L"%isale", stows(itos(saleEntry->second)).c_str());
	}
	else
	{
		mapACSales[clientId][gsi.iArchID] = 1;
		wscMsgLog = L"<%sender> has sold <%item> to base <%basename> (First sale)";
		wscMsgLog = ReplaceStr(wscMsgLog, L"%sender", wscCharname.c_str());
		wscMsgLog = ReplaceStr(wscMsgLog, L"%basename", wscBaseName.c_str());
		wscMsgLog = ReplaceStr(wscMsgLog, L"%item", HkGetWStringFromIDS(packageInfo->iIDSName).c_str());
	}
	string scText = wstos(wscMsgLog);
	Logging("%s", scText.c_str());
}

bool AlleyMF::GFGoodBuy(struct SGFGoodBuyInfo const& gbi, unsigned int clientId)
{
	const GoodInfo* packageInfo = GoodList::find_by_id(gbi.iGoodID);
	if (packageInfo->iType == GOODINFO_TYPE_EQUIPMENT)
	{
		uint iBase = Players[clientId].iBaseID;

		auto acItem = mapACItems.find(gbi.iGoodID);
		if (acItem == mapACItems.end())
		{
			return true;
		}
		//We iterate through the base names to see if it's a non-POB base
		auto baseIter = mapACBases.find(iBase);
		if (baseIter == mapACBases.end())
		{
			return true;
		}

		auto saleEntry = mapACSales[clientId].find(gbi.iGoodID);
		if (saleEntry != mapACSales[clientId].end())
		{
			--saleEntry->second;

			wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(clientId);
			wstring wscBaseName = HkGetBaseNickByID(iBase);

			wstring wscMsgLog = L"<%sender> has bought back <%item> from base <%basename> (%isale purchases left)";
			wscMsgLog = ReplaceStr(wscMsgLog, L"%sender", wscCharname.c_str());
			wscMsgLog = ReplaceStr(wscMsgLog, L"%basename", wscBaseName.c_str());
			wscMsgLog = ReplaceStr(wscMsgLog, L"%item", HkGetWStringFromIDS(packageInfo->iIDSName).c_str());
			wscMsgLog = ReplaceStr(wscMsgLog, L"%isale", stows(itos(saleEntry->second))).c_str();
			string scText = wstos(wscMsgLog);
			Logging("%s", scText.c_str());

			if (saleEntry->second == 0)
			{
				mapACSales[clientId].erase(gbi.iGoodID);
			}
		}
		else
		{
			wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(clientId);
			wstring wscBaseName = HkGetBaseNickByID(iBase);

			pub::Player::SendNNMessage(clientId, pub::GetNicknameId("nnv_anomaly_detected"));
			wstring wscMsgU = L"MF: %name has been permabanned. (Type 2)";
			wscMsgU = ReplaceStr(wscMsgU, L"%name", wscCharname.c_str());

			HkMsgU(wscMsgU);

			wstring wscMsgLog = L"<%sender> was permabanned for attempting to buy an illegal item <%item> from base <%basename> (see DSAM)";
			wscMsgLog = ReplaceStr(wscMsgLog, L"%sender", wscCharname.c_str());
			wscMsgLog = ReplaceStr(wscMsgLog, L"%basename", wscBaseName.c_str());
			wscMsgLog = ReplaceStr(wscMsgLog, L"%item", HkGetWStringFromIDS(packageInfo->iIDSName).c_str());

			LogCheater(clientId, wscMsgLog);
			return false;
		}
	}
	else if (packageInfo->iType == GOODINFO_TYPE_SHIP)
	{
		uint iBase = Players[clientId].iBaseID;

		auto iter = mapACShips.find(iBase);
		if (iter == mapACShips.end())
		{
			return true;
		}
		auto shipListIter = iter->second.find(gbi.iGoodID);
		if (shipListIter != iter->second.end())
		{
			return true;
		}

		wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(clientId);
		wstring wscBaseName = HkGetBaseNickByID(iBase);

		pub::Player::SendNNMessage(clientId, pub::GetNicknameId("nnv_anomaly_detected"));
		wstring wscMsgU = L"MF: %name has been permabanned. (Type 1)";
		wscMsgU = ReplaceStr(wscMsgU, L"%name", wscCharname.c_str());

		HkMsgU(wscMsgU);

		wstring wscMsgLog = L"<%sender> was permabanned for attempting to buy an illegal ship from base <%basename> (see DSAM)";
		wscMsgLog = ReplaceStr(wscMsgLog, L"%sender", wscCharname.c_str());
		wscMsgLog = ReplaceStr(wscMsgLog, L"%basename", wscBaseName.c_str());

		LogCheater(clientId, wscMsgLog);
		return false;
	}

	return true;
}

void AlleyMF::BaseEnter_AFTER(unsigned int baseId, unsigned int clientId)
{
	ClearClientInfo(clientId);
}

void AlleyMF::PlayerLaunch(unsigned int iShip, unsigned int client)
{
	ClearClientInfo(client);
}