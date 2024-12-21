// Events for FLHookPlugin
// December 2015 by BestDiscoveryHookDevs2015
//
// 
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
#include <list>
#include <map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Main.h"
#include <sstream>
#include <iostream>
#include <hookext_exports.h>
#include "minijson_writer.hpp"
#include <set>

static int set_iPluginDebug = 0;

struct PlayerEventData
{
	bool eventInteraction = false;
	bool eventEnabled = false;
	uint playerID = 0;
	uint quantity = 0;
	string eventName;
};

PlayerEventData playerData[MAX_CLIENT_ID + 1];

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

void LoadSettings();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	srand((uint)time(0));
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length() > 0)
			LoadSettings();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
	}
	return true;
}

/// Hook will call this function after calling a plugin function to see if we the
/// processing to continue
EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

struct TRADE_EVENT {
	uint uHashID;
	string sEventName;
	string sURL;
	int iBonusCash;
	unordered_set<uint> uStartBase;
	unordered_set<uint> pobStartBase;
	unordered_set<uint> uEndBase;
	unordered_set<uint> pobEndBase;
	int iObjectiveMax = INT32_MAX;
	int iObjectiveCurrent = 0; // Always 0 to prevent having no data
	uint uCommodityID;
	bool bLimited = false; //Whether or not this is limited to a specific set of IDs
	unordered_set<uint> lAllowedIDs;
	time_t startTime = 0;
	time_t endTime = 0;
	map<uint, market_map_t> eventEconOverride;
	bool isActive = true;
	wstring startMessage = L"";
	wstring endMessage = L"";
};

struct COMBAT_EVENT {
	//Basic event settings
	string sEventName;
	string sURL;
	int iObjectiveMax = INT32_MAX;
	int iObjectiveCurrent = 0; // Always 0 to prevent having no data	
	//Combat event data
	unordered_set<uint> lAllowedIDs;
	unordered_set<uint> lTargetIDs;
	unordered_set<uint> lSystems;
	unordered_set<uint> lNPCTargetReputation;
	//Rewards
	int bonusnpc;
	int bonusplayer;
	int iObjectivePlayerReward;
	int iObjectiveNPCReward;
	//Optional commodity reward data
	bool bCommodityReward = false; // assume false
	uint uCommodityID;
	time_t startTime = 0;
	time_t endTime = 0;
	bool isActive = true;
	wstring startMessage = L"";
	wstring endMessage = L"";
};

struct EVENT_TRACKER
{
	//wsccharname and amount of participation
	map<wstring, int> PlayerEventData;
	string eventname;
};

map<string, TRADE_EVENT> mapTradeEvents;
map<string, COMBAT_EVENT> mapCombatEvents;

map<string, EVENT_TRACKER> mapEventTracking;

uint SuhlDeathCounter = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SendEconOverride()
{
	std::map<uint, market_map_t> econOverrideMap;
	for (auto& te : mapTradeEvents)
	{
		if (!te.second.isActive)
		{
			continue;
		}
		if (te.second.eventEconOverride.empty())
		{
			continue;
		}
		econOverrideMap.insert(te.second.eventEconOverride.begin(), te.second.eventEconOverride.end());
	}

	Plugin_Communication(CUSTOM_EVENT_ECON_UPDATE, &econOverrideMap);
}

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	set<string> activeEvents;

	for (auto& te : mapTradeEvents)
	{
		if (te.second.isActive)
		{
			activeEvents.insert(te.first);
		}
	}

	for (auto& ce : mapCombatEvents)
	{
		if (ce.second.isActive)
		{
			activeEvents.insert(ce.first);
		}
	}

	mapEventTracking.clear();
	mapTradeEvents.clear();
	mapCombatEvents.clear();


	unordered_map <uint, string> mapIDs;

	string idfile = "..\\data\\equipment\\misc_equip.ini";
	int idcount = 0;

	INI_Reader ini;

	if (ini.open(idfile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("Tractor"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						string shipname = ini.get_value_string(0);
						uint shiphash = CreateID(ini.get_value_string(0));

						mapIDs[shiphash] = shipname;
						++idcount;
						break;
					}
				}
			}
		}
		ini.close();
	}

	string File_FLHook = "..\\exe\\flhook_plugins\\events.cfg";
	string File_FLHookStatus = "..\\exe\\flhook_plugins\\events_status.cfg";
	string File_FLHookTracker = "..\\exe\\flhook_plugins\\events_tracker.cfg";
	string File_FLHookSuhl = "..\\exe\\flhook_plugins\\suhl_tracker.cfg";
	int iLoaded = 0;
	int iLoaded2 = 0;
	int iLoaded3 = 0;

	time_t currTime = time(0);

	INI_Reader ini;
	if (ini.open(File_FLHook.c_str(), false))
	{
		while (ini.read_header())
		{
			//Trade events
			if (ini.is_header("TradeEvent"))
			{
				TRADE_EVENT te;
				string id;

				set<uint> addedIds;
				
				bool invalidData = false;
				string invalidDataReason;
				while (ini.read_value())
				{
					if (ini.is_value("id"))
					{
						id = ini.get_value_string(0);
						te.uHashID = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("name"))
					{
						te.sEventName = ini.get_value_string(0);
					}
					else if (ini.is_value("url"))
					{
						te.sURL = ini.get_value_string(0);
					}
					else if (ini.is_value("startbase"))
					{
						uint baseHash = CreateID(ini.get_value_string(0));
						if (!Universe::get_base(baseHash))
						{
							invalidData = true;
							invalidDataReason = ini.get_value_string(0);
							break;
						}
						te.uStartBase.insert(baseHash);
					}
					else if (ini.is_value("endbase"))
					{
						uint baseHash = CreateID(ini.get_value_string(0));
						if (!Universe::get_base(baseHash))
						{
							invalidData = true;
							invalidDataReason = ini.get_value_string(0);
							break;
						}
						te.uEndBase.insert(baseHash);
					}
					else if (ini.is_value("bonus"))
					{
						te.iBonusCash = ini.get_value_int(0);
					}
					else if (ini.is_value("objectivemax"))
					{
						te.iObjectiveMax = ini.get_value_int(0);
					}
					else if (ini.is_value("commodity"))
					{
						uint commodityHash = CreateID(ini.get_value_string(0));
						if (!Archetype::GetEquipment(commodityHash))
						{
							invalidData = true;
							invalidDataReason = ini.get_value_string(0);
							break;
						}
						pub::GetGoodID(te.uCommodityID, ini.get_value_string(0));
					}
					else if (ini.is_value("limited"))
					{
						te.bLimited = ini.get_value_bool(0);
					}
					else if (ini.is_value("allowedid"))
					{
						uint idHash = CreateID(ini.get_value_string(0));
						if (!mapIDs.count(idHash) || addedIds.count(idHash))
						{
							invalidData = true;
							invalidDataReason = ini.get_value_string(0);
							break;
						}
						addedIds.insert(idHash);
						te.lAllowedIDs.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("flhookstartbase"))
					{
						te.pobStartBase.insert(CreateID(ini.get_value_string()));
					}
					else if (ini.is_value("flhookendbase"))
					{
						te.pobEndBase.insert(CreateID(ini.get_value_string()));
					}
					else if (ini.is_value("starttime"))
					{
						te.startTime = ini.get_value_int(0);
						if (te.startTime > currTime)
						{
							te.isActive = false;
						}
					}
					else if (ini.is_value("endtime"))
					{
						te.endTime = ini.get_value_int(0);
						if (te.endTime && te.endTime < currTime)
						{
							te.isActive = false;
							ConPrint(L"EVENT %ls has loaded, but is already concluded!\n", stows(te.sEventName).c_str());
						}
					}
					else if (ini.is_value("startmessage"))
					{
						te.startMessage = stows(ini.get_value_string());
					}
					else if (ini.is_value("endmessage"))
					{
						te.endMessage = stows(ini.get_value_string());
					}
					else if (ini.is_value("marketgoodinfo"))
					{
						uint baseId = CreateID(ini.get_value_string(0));
						uint iGoodID = CreateID(ini.get_value_string(1));
						uint iSellPrice = ini.get_value_int(2);
						float fPrice = ini.get_value_float(3);
						bool bBaseBuys = (ini.get_value_int(4) == 1);

						auto& MarketGoodEntry = te.eventEconOverride[baseId][iGoodID];
						MarketGoodEntry.iGoodID = iGoodID;
						MarketGoodEntry.fPrice = fPrice;
						MarketGoodEntry.iMin = iSellPrice;
						MarketGoodEntry.iTransType = (!bBaseBuys) ? TransactionType_Buy : TransactionType_Sell;
					}
				}
				if (invalidData)
				{
					ConPrint(L"ERROR Event '%ls' failed to load due to nickname '%ls'\n", stows(te.sEventName).c_str(), stows(invalidDataReason).c_str());
				}
				else
				{
					if (te.isActive && !activeEvents.count(id))
					{
						HkMsgU(ReplaceStr(L"The event '%eventName' has begun! For more details, look up our website. Best of luck!", L"%eventName", stows(te.sEventName)));
					}
					mapTradeEvents[id] = te;
					++iLoaded;
				}
			}
			//Combat Events
			else if (ini.is_header("CombatEvent"))
			{
				COMBAT_EVENT ce;
				string id;
				bool invalidData = false;
				string invalidDataReason;
				set<uint> addedIDs;

				while (ini.read_value())
				{
					//Default event settings
					if (ini.is_value("id"))
					{
						id = ini.get_value_string(0);
					}
					else if (ini.is_value("name"))
					{
						ce.sEventName = ini.get_value_string(0);
					}
					else if (ini.is_value("url"))
					{
						ce.sURL = ini.get_value_string(0);
					}
					else if (ini.is_value("objectivemax"))
					{
						ce.iObjectiveMax = ini.get_value_int(0);
					}
					//Combat settings
					else if (ini.is_value("allowedid"))
					{
						uint idHash = CreateID(ini.get_value_string(0));
						if (!mapIDs.count(idHash) || addedIDs.count(idHash))
						{
							invalidData = true;
							invalidDataReason = ini.get_value_string(0);
							break;
						}
						addedIDs.insert(idHash);
						ce.lAllowedIDs.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("targetid"))
					{
						uint idHash = CreateID(ini.get_value_string(0));
						if (!mapIDs.count(idHash) || addedIDs.count(idHash))
						{
							invalidData = true;
							invalidDataReason = ini.get_value_string(0);
							break;
						}
						addedIDs.insert(idHash);
						ce.lTargetIDs.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("system"))
					{
						uint sysHash = CreateID(ini.get_value_string());
						auto sysInfo = Universe::get_system(sysHash);
						if (!sysInfo)
						{
							invalidData = true;
							invalidDataReason = ini.get_value_string(0);
							break;
						}
						ce.lSystems.insert(CreateID(ini.get_value_string(0)));
					}
					//Bonus values
					else if (ini.is_value("bonusnpc"))
					{
						ce.bonusnpc = ini.get_value_int(0);
						ce.iObjectiveNPCReward = ini.get_value_int(1);
					}
					else if (ini.is_value("bonusplayer"))
					{
						ce.bonusplayer = ini.get_value_int(0);
						ce.iObjectivePlayerReward = ini.get_value_int(1);
					}
					//NPC target reputations if enabled
					else if (ini.is_value("targetnpc"))
					{
						ce.lNPCTargetReputation.insert(MakeId(ini.get_value_string(0)));
					}
					//Optional commodity reward
					else if (ini.is_value("commodityreward"))
					{
						ce.bCommodityReward = ini.get_value_bool(0);
					}
					else if (ini.is_value("commodity"))
					{
						pub::GetGoodID(ce.uCommodityID, ini.get_value_string(0));
					}
					else if (ini.is_value("starttime"))
					{
						ce.startTime = ini.get_value_int(0);
						if (ce.startTime > currTime)
						{
							ce.isActive = false;
						}
					}
					else if (ini.is_value("endtime"))
					{
						ce.endTime = ini.get_value_int(0);
						if (ce.endTime && ce.endTime < currTime)
						{
							ce.isActive = false;
							ConPrint(L"EVENT %ls has loaded, but is already concluded!\n", stows(ce.sEventName).c_str());
						}
					}
					else if (ini.is_value("startmessage"))
					{
						ce.startMessage = stows(ini.get_value_string());
					}
					else if (ini.is_value("endmessage"))
					{
						ce.endMessage = stows(ini.get_value_string());
					}
				}

				if (invalidData)
				{
					ConPrint(L"ERROR Event '%ls' failed to load due to nickname '%ls'\n", stows(ce.sEventName).c_str(), stows(invalidDataReason).c_str());
				}
				else
				{
					if (ce.isActive && !activeEvents.count(id))
					{
						HkMsgU(ReplaceStr(L"The event '%eventName' has begun! For more details, look up our website. Best of luck!", L"%eventName", stows(ce.sEventName)));
					}
					mapCombatEvents[id] = ce;
					++iLoaded;
				}
			}
		}
		ini.close();
	}

	if (ini.open(File_FLHookStatus.c_str(), false))
	{
		while (ini.read_header())
		{
			//TRADE EVENTS
			if (ini.is_header("TradeEvent"))
			{
				bool exist = false;
				string id;
				int currentcount;

				while (ini.read_value())
				{
					if (ini.is_value("id"))
					{
						id = ini.get_value_string(0);
						//this is to ensure we don't keep data for events that ceased to exist
						if (mapTradeEvents.find(id) != mapTradeEvents.end())
						{
							exist = true;
						}
					}
					else if (ini.is_value("currentcount"))
					{
						currentcount = ini.get_value_int(0);
					}
				}

				if (exist)
				{
					mapTradeEvents[id].iObjectiveCurrent = currentcount;
					ConPrint(L"Event TE: Found event ID %s and loaded count %i\n", stows(id).c_str(), currentcount);
					++iLoaded2;
				}
			}
			//COMBAT EVENTS
			if (ini.is_header("CombatEvent"))
			{
				bool exist = false;
				string id;
				int currentcount;

				while (ini.read_value())
				{
					if (ini.is_value("id"))
					{
						id = ini.get_value_string(0);
						//this is to ensure we don't keep data for events that ceased to exist
						if (mapCombatEvents.find(id) != mapCombatEvents.end())
						{
							exist = true;
						}
					}
					else if (ini.is_value("currentcount"))
					{
						currentcount = ini.get_value_int(0);
					}
				}

				if (exist)
				{
					mapCombatEvents[id].iObjectiveCurrent = currentcount;
					ConPrint(L"Event CE: Found event ID %s and loaded count %i\n", stows(id).c_str(), currentcount);
					++iLoaded2;
				}
			}
		}
		ini.close();
	}

	if (ini.open(File_FLHookTracker.c_str(), false))
	{
		while (ini.read_header())
		{
			//TRADE EVENTS
			if (ini.is_header("EventData"))
			{
				bool exist = false;

				string id;
				wstring wscCharname;
				int iCount;
				string name;

				while (ini.read_value())
				{
					if (ini.is_value("id"))
					{
						id = ini.get_value_string(0);
						//this is to ensure we don't keep data for events that ceased to exist
						if (mapTradeEvents.find(id) != mapTradeEvents.end())
						{
							exist = true;
							name = mapTradeEvents[id].sEventName;
						}
						else if (mapCombatEvents.find(id) != mapCombatEvents.end())
						{
							exist = true;
							name = mapCombatEvents[id].sEventName;
						}
					}
					else if ((ini.is_value("data")) && (exist == true))
					{
						string delim = ", ";
						string data = ini.get_value_string();
						wscCharname = stows(data.substr(0, data.find(delim)));
						iCount = ToInt(data.substr(data.find(delim) + delim.length()));
						mapEventTracking[id].PlayerEventData[wscCharname] = iCount;
					}
				}

				if (exist)
				{
					mapEventTracking[id].eventname = name;
					++iLoaded3;
				}
			}
		}
		ini.close();
	}

	if (ini.open(File_FLHookSuhl.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("SuhlData"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("SuhlKills"))
					{
						SuhlDeathCounter = ini.get_value_int(0);
					}
				}
			}
		}
		ini.close();
	}

	SendEconOverride();

	ConPrint(L"EVENT: Loaded %u events\n", iLoaded);
	ConPrint(L"EVENT DEBUG: Loaded %u event data\n", iLoaded2);
	ConPrint(L"EVENT DEBUG: Loaded %u event player data\n", iLoaded3);

	CUSTOM_POB_EVENT_NOTIFICATION_INIT_STRUCT info;
	unordered_map<uint, unordered_set<uint>> pobData;
	for (auto& te : mapTradeEvents)
	{
		for (auto& pobEntry : te.second.pobStartBase)
		{
			pobData[pobEntry].insert(te.second.uCommodityID);
		}
		for (auto& pobExit : te.second.pobEndBase)
		{
			pobData[pobExit].insert(te.second.uCommodityID);
		}
	}
	info.data = pobData;
	Plugin_Communication(CUSTOM_POB_EVENT_NOTIFICATION_INIT, &info);

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint GetPlayerId(uint clientId)
{	
	return ClientInfo[clientId].playerID;
}

FILE *Logfile = fopen("./flhook_logs/event_log.log", "at");

void Logging(const char *szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	char szBuf[64];
	time_t tNow = time(0);
	struct tm *t = localtime(&tNow);
	strftime(szBuf, sizeof(szBuf), "%d/%m/%Y %H:%M:%S", t);
	fprintf(Logfile, "%s %s\n", szBuf, szBufString);
	fflush(Logfile);
	fclose(Logfile);
	Logfile = fopen("./flhook_logs/event_log.log", "at");
}

void Notify_TradeEvent_Start(uint iClientID, string eventname)
{
	//internal log
	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
	wstring wscMsgLog = L"<%player> has registered for the event <%eventname>";
	wscMsgLog = ReplaceStr(wscMsgLog, L"%player", wscCharname.c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%eventname", stows(eventname).c_str());
	string scText = wstos(wscMsgLog);
	Logging("%s", scText.c_str());
}

void Notify_TradeEvent_Exit(uint iClientID, string eventname, string reason)
{
	//internal log
	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
	wstring wscMsgLog = L"<%player> has been unregistered from the event <%eventname>, reason: <%reason>";
	wscMsgLog = ReplaceStr(wscMsgLog, L"%player", wscCharname.c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%eventname", stows(eventname).c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%reason", stows(reason).c_str());
	string scText = wstos(wscMsgLog);
	Logging("%s", scText.c_str());
}

void Notify_TradeEvent_Completed(uint iClientID, string eventname, int iCargoCount, int iBonus)
{
	//internal log
	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
	wstring wscMsgLog = L"<%player> has completed the event <%eventname> and delivered <%units> for a bonus of <%bonus> credits";
	wscMsgLog = ReplaceStr(wscMsgLog, L"%player", wscCharname.c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%eventname", stows(eventname).c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%units", stows(itos(iCargoCount)).c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%bonus", stows(itos(iBonus)).c_str());
	string scText = wstos(wscMsgLog);
	Logging("%s", scText.c_str());
}

void Notify_CombatEvent_PlayerKill(uint iClientIDKiller, uint iClientIDVictim, string eventname, int iCash, int iKillValue)
{
	//internal log
	wstring wscCharnameKiller = (const wchar_t*)Players.GetActiveCharacterName(iClientIDKiller);
	wstring wscCharnameVictim = (const wchar_t*)Players.GetActiveCharacterName(iClientIDVictim);
	wstring wscMsgLog = L"<%player> has killed <%victim> for the event <%eventname>, bonus: <%cash> worth <%points> points.";
	wscMsgLog = ReplaceStr(wscMsgLog, L"%player", wscCharnameKiller.c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%victim", wscCharnameVictim.c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%eventname", stows(eventname).c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%cash", stows(itos(iCash)).c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%points", stows(itos(iKillValue)).c_str());
	string scText = wstos(wscMsgLog);
	Logging("%s", scText.c_str());
}

void __stdcall CharacterSelect_AFTER(struct CHARACTER_ID const & cId, unsigned int iClientID)
{
	playerData[iClientID].eventName.clear();
	playerData[iClientID].quantity = 0;
	playerData[iClientID].eventEnabled = HookExt::IniGetB(iClientID, "event.enabled");
	if (playerData[iClientID].eventEnabled)
	{
		playerData[iClientID].eventName = wstos(HookExt::IniGetWS(iClientID, "event.eventid"));
		playerData[iClientID].quantity = HookExt::IniGetI(iClientID, "event.quantity");
		//check if this event still exist
		if ((!empty(playerData[iClientID].eventName)) && (mapTradeEvents.find(playerData[iClientID].eventName) != mapTradeEvents.end()))
		{
			PrintUserCmdText(iClientID, L"You are still eligible to complete the event: %s", stows(mapTradeEvents[playerData[iClientID].eventName].sEventName).c_str());
			PrintUserCmdText(iClientID, L"Amount of registered cargo: %u", playerData[iClientID].quantity);
		}
		else
		{
			//else disable event mode
			PrintUserCmdText(iClientID, L"You have been unregistered from an expired event.");
			Notify_TradeEvent_Exit(iClientID, playerData[iClientID].eventName, "Logged in with expired event data");
			playerData[iClientID].eventInteraction = true;
			playerData[iClientID].eventEnabled = false;
			playerData[iClientID].eventName.clear();
			playerData[iClientID].quantity = 0;
			HookExt::IniSetB(iClientID, "event.enabled", false);
			HookExt::IniSetWS(iClientID, "event.eventid", L"");
			HookExt::IniSetI(iClientID, "event.quantity", 0);
		}
	}
}

void __stdcall GFGoodBuy_AFTER(struct SGFGoodBuyInfo const &gbi, unsigned int iClientID)
{
	if (mapTradeEvents.empty())
	{
		return;
	}

	for (map<string, TRADE_EVENT>::iterator i = mapTradeEvents.begin(); i != mapTradeEvents.end(); ++i)
	{
		//check if it's one of the commodities undergoing an event
		if (!i->second.isActive || gbi.iGoodID != i->second.uCommodityID)
		{
			continue;
		}

		if (i->second.bLimited)
		{
			uint pID = GetPlayerId(iClientID);
			bool bFoundID = false;

			if(!i->second.lAllowedIDs.count(pID))
			{
				continue;
			}
		}

		//this is as an if so if the player exited event mode he can reenter event mode
		//check if this is the event's stating point
		if (!i->second.uStartBase.count(gbi.iBaseID))
		{
			CUSTOM_BASE_IS_DOCKED_STRUCT info;
			info.iClientID = iClientID;
			info.iDockedBaseID = 0;
			Plugin_Communication(CUSTOM_BASE_IS_DOCKED, &info);
			if (!i->second.pobStartBase.count(info.iDockedBaseID))
			{
				continue;
			}
		}

		playerData[iClientID].eventInteraction = true;
		playerData[iClientID].eventEnabled = true;
		playerData[iClientID].eventName = i->first;
		playerData[iClientID].quantity += gbi.iCount;

		HookExt::IniSetB(iClientID, "event.enabled", playerData[iClientID].eventEnabled);
		HookExt::IniSetWS(iClientID, "event.eventid", stows(playerData[iClientID].eventName));
		HookExt::IniSetI(iClientID, "event.quantity", playerData[iClientID].quantity);

		pub::Audio::PlaySoundEffect(iClientID, CreateID("ui_gain_level"));
		PrintUserCmdText(iClientID, L"You have entered the event: %s, you will be paid %d extra credits for every unit you deliver.", stows(i->second.sEventName).c_str(), i->second.iBonusCash);
		PrintUserCmdText(iClientID, L"Amount of registered cargo: %u", playerData[iClientID].quantity);
		Notify_TradeEvent_Start(iClientID, i->second.sEventName);

		pub::Save(iClientID, 1);

		break;
	}
}

void TradeEvent_Sale(struct SGFGoodSellInfo const &gsi, unsigned int iClientID)
{
	if (!playerData[iClientID].eventEnabled)
	{
		return;
	}
	string& eventid = playerData[iClientID].eventName;
	auto i = mapTradeEvents.find(eventid);
	if (i == mapTradeEvents.end())
	{
		return;
	}
	//this if is if we are interacting with this commodity and already in event mode
	if (!i->second.isActive || gsi.iArchID != i->second.uCommodityID)
	{
		return;
	}

	int iInitialCount = HookExt::IniGetI(iClientID, "event.quantity");

	playerData[iClientID].eventInteraction = true;
	playerData[iClientID].eventEnabled = false;
	playerData[iClientID].eventName.clear();
	playerData[iClientID].quantity = 0;

	HookExt::IniSetB(iClientID, "event.enabled", false);
	HookExt::IniSetWS(iClientID, "event.eventid", L"");
	HookExt::IniSetI(iClientID, "event.quantity", 0);

	uint iBaseID = Players[iClientID].iBaseID;

	//check if this is the event's end point
	if (!i->second.uEndBase.count(iBaseID))
	{
		CUSTOM_BASE_IS_DOCKED_STRUCT info;
		info.iClientID = iClientID;
		info.iDockedBaseID = 0;
		Plugin_Communication(CUSTOM_BASE_IS_DOCKED, &info);
		if (!info.iDockedBaseID || !i->second.pobEndBase.count(info.iDockedBaseID))
		{
			//leave event mode
			PrintUserCmdText(iClientID, L"You have been unregistered from the event: %s", stows(i->second.sEventName).c_str());
			Notify_TradeEvent_Exit(iClientID, i->second.sEventName, "Sold commodity to other base than delivery point");
			pub::Save(iClientID, 1);
			return;
		}
	}
	iInitialCount = min(iInitialCount, gsi.iCount);

	int bonus = 0;

	pub::Audio::PlaySoundEffect(iClientID, CreateID("ui_gain_level"));
	PrintUserCmdText(iClientID, L"You have finished the event: %s", stows(i->second.sEventName).c_str());

	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);

	if (i->second.iObjectiveCurrent == i->second.iObjectiveMax)
	{
		PrintUserCmdText(iClientID, L"Sorry, this event is currently completed.");
		Notify_TradeEvent_Exit(iClientID, i->second.sEventName, "Completed trade run but event is completed");
		return;
	}
	else if ((i->second.iObjectiveCurrent + iInitialCount) >= i->second.iObjectiveMax)
	{
		int amount = (i->second.iObjectiveCurrent + iInitialCount) - i->second.iObjectiveMax;
		bonus = i->second.iBonusCash * amount;
		mapEventTracking[i->first].PlayerEventData[wscCharname] += amount;

		i->second.iObjectiveCurrent = i->second.iObjectiveMax;

		PrintUserCmdText(iClientID, L"You have completed the final delivery. Congratulations !");
		Notify_TradeEvent_Exit(iClientID, i->second.sEventName, "NOTIFICATION: Final Delivery");
	}
	else
	{
		bonus = i->second.iBonusCash * iInitialCount;
		i->second.iObjectiveCurrent += iInitialCount;
		mapEventTracking[i->first].PlayerEventData[wscCharname] += iInitialCount;
	}

	pub::Player::AdjustCash(iClientID, bonus);

	PrintUserCmdText(iClientID, L"You receive a bonus of: %d credits", bonus);
	Notify_TradeEvent_Completed(iClientID, i->second.sEventName, gsi.iCount, bonus);
}

void __stdcall GFGoodSell_AFTER(struct SGFGoodSellInfo const &gsi, unsigned int iClientID)
{
	TradeEvent_Sale(gsi, iClientID);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Actual Code
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void __stdcall PlayerLaunch_AFTER(struct CHARACTER_ID const & cId, unsigned int iClientID)
{
	playerData[iClientID].playerID = 0;
}

void ProcessEventData()
{
	if (mapTradeEvents.empty() && mapCombatEvents.empty())
	{
		return;
	}
	///////////////////////////////////////////////////////////////////////////////////////
	// JSON DUMPING INIT 
	///////////////////////////////////////////////////////////////////////////////////////

	stringstream stream;
	minijson::object_writer writer(stream);

	///////////////////////////////////////////////////////////////////////////////////////
	// INI DUMPING INIT
	///////////////////////////////////////////////////////////////////////////////////////

	//shamelessly stolen from the failed siege system
	string siegedump;

	//////////////////////////////////////////////////////////////////////////////////////
	// TRADE ITERATOR INIT
	///////////////////////////////////////////////////////////////////////////////////////

	for (map<string, TRADE_EVENT>::iterator iter = mapTradeEvents.begin(); iter != mapTradeEvents.end(); iter++)
	{
		///////////////////////////////////////////////////////////////////////////////////////
		// JSON DUMPING
		///////////////////////////////////////////////////////////////////////////////////////

		//begin the object writer
		minijson::object_writer pw = writer.nested_object(iter->first.c_str());

		//add basic elements
		pw.write("name", iter->second.sEventName);
		pw.write("url", iter->second.sURL);
		pw.write("current", iter->second.iObjectiveCurrent);
		pw.write("max", iter->second.iObjectiveMax);
		pw.close();

		///////////////////////////////////////////////////////////////////////////////////////
		// END JSON DUMPING
		///////////////////////////////////////////////////////////////////////////////////////

		///////////////////////////////////////////////////////////////////////////////////////
		// INI DUMPING
		///////////////////////////////////////////////////////////////////////////////////////

		string siegeblock;
		siegeblock = "[TradeEvent]\n";
		siegeblock.append("id = " + iter->first + "\n");

		stringstream ss;
		ss << iter->second.iObjectiveCurrent;
		string str = ss.str();

		siegeblock.append("currentcount = " + str + "\n");

		siegedump.append(siegeblock);

		///////////////////////////////////////////////////////////////////////////////////////
		// END INI DUMPING
		///////////////////////////////////////////////////////////////////////////////////////
	}

	//////////////////////////////////////////////////////////////////////////////////////
	// TRADE ITERATOR END
	///////////////////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////////////////
	// COMBAT ITERATOR INIT
	///////////////////////////////////////////////////////////////////////////////////////

	for (map<string, COMBAT_EVENT>::iterator iter = mapCombatEvents.begin(); iter != mapCombatEvents.end(); iter++)
	{
		///////////////////////////////////////////////////////////////////////////////////////
		// JSON DUMPING
		///////////////////////////////////////////////////////////////////////////////////////

		//begin the object writer
		minijson::object_writer pw = writer.nested_object(iter->first.c_str());

		//add basic elements
		pw.write("name", iter->second.sEventName);
		pw.write("url", iter->second.sURL);
		pw.write("current", iter->second.iObjectiveCurrent);
		pw.write("max", iter->second.iObjectiveMax);
		pw.close();

		///////////////////////////////////////////////////////////////////////////////////////
		// END JSON DUMPING
		///////////////////////////////////////////////////////////////////////////////////////

		///////////////////////////////////////////////////////////////////////////////////////
		// INI DUMPING
		///////////////////////////////////////////////////////////////////////////////////////

		string siegeblock;
		siegeblock = "[CombatEvent]\n";
		siegeblock.append("id = " + iter->first + "\n");

		stringstream ss;
		ss << iter->second.iObjectiveCurrent;
		string str = ss.str();

		siegeblock.append("currentcount = " + str + "\n");

		siegedump.append(siegeblock);

		///////////////////////////////////////////////////////////////////////////////////////
		// END INI DUMPING
		///////////////////////////////////////////////////////////////////////////////////////

	}

	//////////////////////////////////////////////////////////////////////////////////////
	// COMBAT ITERATOR END
	///////////////////////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////////////////////////////
	// BEGIN JSON DUMPING
	///////////////////////////////////////////////////////////////////////////////////////

	writer.close();

	//dump to a file
	FILE *filejson = fopen("c:/stats/event_status.json", "w");
	if (filejson)
	{
		fprintf(filejson, stream.str().c_str());
		fclose(filejson);
	}
	///////////////////////////////////////////////////////////////////////////////////////
	// END JSON DUMPING
	///////////////////////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////////////////////////////
	// BEGIN INI DUMPING
	///////////////////////////////////////////////////////////////////////////////////////

	//dump to a file
	FILE *fileini = fopen("..\\exe\\flhook_plugins\\events_status.cfg", "w");
	if (fileini)
	{
		fprintf(fileini, siegedump.c_str());
		fclose(fileini);
	}

	///////////////////////////////////////////////////////////////////////////////////////
	// END INI DUMPING
	///////////////////////////////////////////////////////////////////////////////////////


}

void ProcessEventPlayerInfo()
{
	///////////////////////////////////////////////////////////////////////////////////////
	// JSON DUMPING INIT 
	///////////////////////////////////////////////////////////////////////////////////////

	stringstream stream;
	minijson::object_writer writer(stream);

	///////////////////////////////////////////////////////////////////////////////////////
	// INI DUMPING INIT
	///////////////////////////////////////////////////////////////////////////////////////

	//shamelessly stolen from the failed siege system
	string siegedump;

	//////////////////////////////////////////////////////////////////////////////////////
	// ITERATOR INIT
	///////////////////////////////////////////////////////////////////////////////////////

	for (map<string, EVENT_TRACKER>::iterator iter = mapEventTracking.begin(); iter != mapEventTracking.end(); iter++)
	{


		//begin the json object writer
		minijson::object_writer pw = writer.nested_object(iter->first.c_str());

		//begin the ini writer
		string siegeblock;
		siegeblock = "[EventData]\n";
		siegeblock.append("id = " + iter->first + "\n");

		for (map<wstring, int>::iterator i2 = iter->second.PlayerEventData.begin(); i2 != iter->second.PlayerEventData.end(); i2++)
		{
			pw.write(wstos(i2->first).c_str(), i2->second);

			stringstream ss;
			ss << i2->second;
			string str = ss.str();

			siegeblock.append("data = " + wstos(i2->first) + ", " + str + "\n");
		}

		pw.close();
		siegedump.append(siegeblock);

		///////////////////////////////////////////////////////////////////////////////////////
		// END INI DUMPING
		///////////////////////////////////////////////////////////////////////////////////////
	}

	//////////////////////////////////////////////////////////////////////////////////////
	// ITERATOR END
	///////////////////////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////////////////////////////
	// BEGIN JSON DUMPING
	///////////////////////////////////////////////////////////////////////////////////////

	writer.close();

	//dump to a file
	FILE *filejson = fopen("c:/stats/event_tracker.json", "w");
	if (filejson)
	{
		fprintf(filejson, stream.str().c_str());
		fclose(filejson);
	}
	///////////////////////////////////////////////////////////////////////////////////////
	// END JSON DUMPING
	///////////////////////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////////////////////////////
	// BEGIN INI DUMPING
	///////////////////////////////////////////////////////////////////////////////////////

	//dump to a file
	FILE *fileini = fopen("..\\exe\\flhook_plugins\\events_tracker.cfg", "w");
	if (fileini)
	{
		fprintf(fileini, siegedump.c_str());
		fclose(fileini);
	}

	fileini = fopen("..\\exe\\flhook_plugins\\suhl_tracker.cfg", "w");
	if(fileini)
	{
		fprintf(fileini, "[SuhlData]\nSuhlKills = %u", SuhlDeathCounter);
		fclose(fileini);
	}

	///////////////////////////////////////////////////////////////////////////////////////
	// END INI DUMPING
	///////////////////////////////////////////////////////////////////////////////////////
}

void CheckActiveEvent()
{
	time_t currTime = time(0);
	for (auto& event : mapCombatEvents)
	{
		auto& ce = event.second;
		if (ce.isActive)
		{
			if (ce.endTime && ce.endTime <= currTime)
			{
				ce.isActive = false;
				HkMsgU(ReplaceStr(L"The event '%eventName' has concluded. Thanks to all participants!", L"%eventName", stows(ce.sEventName)));
				if (!ce.endMessage.empty())
				{
					HkMsgU(ce.endMessage);
				}
			}
		}
		else
		{
			if (ce.startTime && ce.startTime <= currTime && ce.endTime > currTime)
			{
				ce.isActive = true;
				HkMsgU(ReplaceStr(L"The event '%eventName' has begun! For more details, look up our website. Best of luck!", L"%eventName", stows(ce.sEventName)));
				if (!ce.startMessage.empty())
				{
					HkMsgU(ce.startMessage);
				}
			}
		}
	}

	for (auto& event : mapTradeEvents)
	{
		auto& te = event.second;
		if (te.isActive)
		{
			if (te.endTime && te.endTime <= currTime)
			{
				te.isActive = false;
				HkMsgU(ReplaceStr(L"The event '%eventName' has concluded. Thanks to all participants!", L"%eventName", stows(te.sEventName)));
				if (!te.endMessage.empty())
				{
					HkMsgU(te.endMessage);
				}
				if (!te.eventEconOverride.empty())
				{
					SendEconOverride();
				}
			}
		}
		else
		{
			if (te.startTime && te.startTime <= currTime && te.endTime > currTime)
			{
				te.isActive = true;
				HkMsgU(ReplaceStr(L"The event '%eventName' has begun! For more details, look up our website. Best of luck!", L"%eventName", stows(te.sEventName)));
				if (!te.startMessage.empty())
				{
					HkMsgU(te.startMessage);
				}
				if (!te.eventEconOverride.empty())
				{
					SendEconOverride();
				}
			}
		}
	}
}

void HkTimerCheckKick()
{
	returncode = DEFAULT_RETURNCODE;
	uint curr_time_events = (uint)time(0);

	if ((curr_time_events % 30) == 0)
	{
		ProcessEventData();
		ProcessEventPlayerInfo();
	}

	if ((curr_time_events % 60) == 0)
	{
		CheckActiveEvent();
	}
}


/// Hook for ship distruction. It's easier to hook this than the PlayerDeath one.
void SendDeathMsg(const wstring &wscMsg, uint& iSystem, uint& iClientIDVictim, uint& iClientIDKiller, DamageCause& dmgCause)
{
	returncode = DEFAULT_RETURNCODE;

	if (!iClientIDVictim || !iClientIDKiller)
	{
		return;
	}

	const wchar_t *victim = (const wchar_t*)Players.GetActiveCharacterName(iClientIDVictim);
	const wchar_t *killer = (const wchar_t*)Players.GetActiveCharacterName(iClientIDKiller);


	if (playerData[iClientIDVictim].eventEnabled)
	{
		string& sIDVictimEvent = playerData[iClientIDVictim].eventName;
		//else disable event mode
		playerData[iClientIDVictim].eventEnabled = false;
		playerData[iClientIDVictim].eventName.clear();
		playerData[iClientIDVictim].quantity = 0;
		playerData[iClientIDVictim].eventInteraction = true;
		HookExt::IniSetB(iClientIDVictim, "event.enabled", false);
		HookExt::IniSetWS(iClientIDVictim, "event.eventid", L"");
		HookExt::IniSetI(iClientIDVictim, "event.quantity", 0);
		PrintUserCmdText(iClientIDVictim, L"You have died and have been unregistered from the event: %s", stows(mapTradeEvents[sIDVictimEvent].sEventName).c_str());
	}

	//Combat event handling for player death
	uint pIDKiller = GetPlayerId(iClientIDKiller);
	uint pIDVictim = GetPlayerId(iClientIDVictim);

	for (auto& i = mapCombatEvents.begin(); i != mapCombatEvents.end(); ++i)
	{
		//Check if this event has been completed already
		if (!i->second.isActive || i->second.iObjectiveCurrent == i->second.iObjectiveMax)
		{
			continue;
		}

		if (!i->second.lSystems.count(iSystem))
		{
			continue;
		}

		if (!i->second.lAllowedIDs.count(pIDKiller))
		{
			continue;
		}

		if (!i->second.lTargetIDs.count(pIDVictim))
		{
			continue;
		}
		//If we reach this point we have a winner
		//Check event status first
		if ((i->second.iObjectiveCurrent + i->second.iObjectivePlayerReward) >= i->second.iObjectiveMax)
		{
			i->second.iObjectiveCurrent = i->second.iObjectiveMax;
			PrintUserCmdText(iClientIDKiller, L"You have delivered the final kill. Congratulations !");
			Notify_TradeEvent_Exit(iClientIDKiller, i->second.sEventName, "NOTIFICATION: Final Kill"); //must be changed
		}
		else
		{
			i->second.iObjectiveCurrent += i->second.iObjectivePlayerReward;
		}

		//Once we have updated the status, handle the reward
		//Provide commodity reward if chosen
		if (i->second.bCommodityReward == true)
		{
			//TODO
			break;
		}
		//Else provide money reward
		else
		{
			wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientIDKiller);
			HkAddCash(wscCharname, i->second.bonusplayer);

			mapEventTracking[i->first].PlayerEventData[wscCharname] += i->second.iObjectivePlayerReward;

			pub::Audio::PlaySoundEffect(iClientIDKiller, CreateID("ui_gain_level"));
			PrintUserCmdText(iClientIDKiller, L"You receive a bonus of %d credits and contributed %d points.", i->second.bonusplayer, i->second.iObjectivePlayerReward);
			Notify_CombatEvent_PlayerKill(iClientIDKiller, iClientIDVictim, i->second.sEventName, i->second.bonusplayer, i->second.iObjectivePlayerReward);
			break;
		}
	}
}


void __stdcall ShipDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
	returncode = DEFAULT_RETURNCODE;

	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);


	if (cship->ownerPlayer)
	{
		if (iobj->cobj->currentDamageZone)
		{
			if (iobj->cobj->currentDamageZone->iZoneID == 0xac4c6708)
			{
				static time_t lastAnnouncement = 0;
				time_t currTime = time(0);
				if (currTime > lastAnnouncement + 300)
				{
					wstring playerName = (const wchar_t*)Players.GetActiveCharacterName(cship->ownerPlayer);
					HkMsgU(L"Suhl Anomaly has claimed " + playerName);
					lastAnnouncement = currTime;
				}
				SuhlDeathCounter++;
			}
		}
		return;
	}

	if (mapCombatEvents.empty())
	{
		return;
	}

	uint killerPlayerId = HkGetClientIDByShip(killerId);

	if (!killerPlayerId)
	{
		return;
	}

	for (auto& i = mapCombatEvents.begin(); i != mapCombatEvents.end(); ++i)
	{
		COMBAT_EVENT& event = i->second;
		//Check if this event has been completed already
		if (!event.isActive || event.iObjectiveCurrent >= event.iObjectiveMax)
		{
			continue;
		}

		if (!event.lSystems.count(cship->system))
		{
			continue;
		}


		uint killerFactionId = GetPlayerId(killerPlayerId);

		if (!event.lAllowedIDs.count(killerFactionId))
		{
			continue;
		}

		uint aff;
		pub::Reputation::GetAffiliation(cship->repVibe, aff);

		if (!event.lNPCTargetReputation.count(aff))
		{
			continue;
		}

		if (event.bonusnpc)
		{
			pub::Player::AdjustCash(killerPlayerId, event.bonusnpc);
			PrintUserCmdText(killerPlayerId, L"You've received $%ls credits as an event reward.", ToMoneyStr(event.bonusnpc).c_str());
		}

		if (event.iObjectiveNPCReward)
		{
			PrintUserCmdText(killerPlayerId, L"%u points awarded.", event.iObjectiveNPCReward);
			event.iObjectiveCurrent += event.iObjectiveNPCReward;
		}

		wstring charname = (const wchar_t*)Players.GetActiveCharacterName(killerPlayerId);

		mapEventTracking[i->first].PlayerEventData[charname] += i->second.iObjectiveNPCReward;

		if (event.iObjectiveCurrent >= event.iObjectiveMax)
		{
			wchar_t buf[150];
			_snwprintf(buf, sizeof(buf), L"Event %ls objective has been completed, congratulations!", stows(event.sEventName).c_str());
			HkMsgU(buf);
		}
	}
}

#define IS_CMD(a) !args.compare(L##a)
#define RIGHT_CHECK(a) if(!(cmd->rights & a)) { cmd->Print(L"ERR No permission\n"); return true; }
bool ExecuteCommandString_Callback(CCmds* cmd, const wstring& args)
{
	returncode = DEFAULT_RETURNCODE;
	if (!(cmd->rights & RIGHT_SUPERADMIN))
	{
		return false;
	}

	if (IS_CMD("eventreload"))
	{
		LoadSettings();
		cmd->Print(L"Event data reloaded\n");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (IS_CMD("eventaddpoints"))
	{
		string eventName = wstos(cmd->ArgStr(1));
		wstring charName = cmd->ArgStr(2);
		int points = cmd->ArgInt(3);

		if (mapCombatEvents.count(eventName) && mapCombatEvents.at(eventName).isActive)
		{
			mapCombatEvents[eventName].iObjectiveCurrent += points;
		}
		else if (mapTradeEvents.count(eventName) && mapTradeEvents.at(eventName).isActive)
		{
			mapTradeEvents[eventName].iObjectiveCurrent += points;
		}
		else
		{
			cmd->Print(L"This event doesn't exist!\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		mapEventTracking[eventName].PlayerEventData[charName] += points;
		cmd->Print(L"Event points added\n");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (IS_CMD("eventobjective"))
	{
		uint groupId = cmd->ArgUInt(1);

		auto playerGroup = CPlayerGroup::FromGroupID(groupId);

		if (!playerGroup)
		{
			cmd->Print(L"Group doesn't exist\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}
		uint memberCount = playerGroup->GetMemberCount(); 
		if (!memberCount)
		{
			cmd->Print(L"Group is empty\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		wstring objectiveText = cmd->ArgStrToEnd(2);

		FmtStr caption(0, 0);
		caption.begin_mad_lib(526999);
		caption.end_mad_lib();

		for (uint i = 0; i < memberCount; ++i)
		{
			uint memberId = playerGroup->GetMember(i);
			HkChangeIDSString(memberId, 526999, objectiveText);
			pub::Player::DisplayMissionMessage(memberId, caption, MissionMessageType::MissionMessageType_Type2, true);
		}

		cmd->Print(L"Objective sent\n");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}

	return false;
}


void Plugin_Communication_CallBack(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;

	if (msg == CUSTOM_POB_EVENT_NOTIFICATION_BUY)
	{
		auto info = reinterpret_cast<CUSTOM_POB_EVENT_NOTIFICATION_BUY_STRUCT*>(data);
		GFGoodBuy_AFTER(info->info, info->clientId);
		returncode = SKIPPLUGINS;
	}
	else if (msg == CUSTOM_POB_EVENT_NOTIFICATION_SELL)
	{
		auto info = reinterpret_cast<CUSTOM_POB_EVENT_NOTIFICATION_SELL_STRUCT*>(data);
		GFGoodSell_AFTER(info->info, info->clientId);
		returncode = SKIPPLUGINS;
	}
}

void __stdcall DisConnect(unsigned int iClientID, enum EFLConnection p2)
{
	if (playerData[iClientID].eventInteraction)
	{
		HookExt::IniSetB(iClientID, "event.enabled", playerData[iClientID].eventEnabled);
		HookExt::IniSetWS(iClientID, "event.eventid", stows(playerData[iClientID].eventName));
		HookExt::IniSetI(iClientID, "event.quantity", playerData[iClientID].quantity);
		playerData[iClientID].eventEnabled = false;
		playerData[iClientID].eventName.clear();
		playerData[iClientID].quantity = 0;
		playerData[iClientID].eventInteraction = false;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Events by Alley";
	p_PI->sShortName = "event";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkTimerCheckKick, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch_AFTER, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect_AFTER, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodBuy_AFTER, PLUGIN_HkIServerImpl_GFGoodBuy_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodSell_AFTER, PLUGIN_HkIServerImpl_GFGoodSell_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SendDeathMsg, PLUGIN_SendDeathMsg, 2));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipDestroyed, PLUGIN_ShipDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DisConnect, PLUGIN_HkIServerImpl_DisConnect, 0));

	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
