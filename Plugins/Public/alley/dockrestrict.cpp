// AlleyPlugin for FLHookPlugin
// February 2015 by Alley
//
// This CPP controls the function to prevent players from being able to dock to anything for x seconds.
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
#include <float.h>
#include <FLHook.h>
#include <plugin.h>
#include <sstream>
#include <iostream>

#include <PluginUtilities.h>
#include "PlayerRestrictions.h"


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Structures and shit yo
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NoDockData {
	unordered_set<uint> blockedBaseIFFs;
	unordered_set<uint> contrabandItems;
	unordered_set<uint> validSystems;
	bool canPolice = false;
};

unordered_map<uint, NoDockData> nodockDataMap;
static unordered_map<uint, wstring> MapActiveSirens;

static int set_duration = 60;

struct ActiveNoDock
{
	unordered_set<uint>* blockedBaseIFFs;
	int duration;
};

static unordered_map<uint, ActiveNoDock> mapActiveNoDock;

static unordered_set<uint> baseblacklist;

static unordered_set<wstring> superNoDockedShips;

FILE *Logfile = fopen("./flhook_logs/nodockcommand.log", "at");

void Logging(const char *szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	if (Logfile) {
		char szBuf[64];
		time_t tNow = time(0);
		struct tm *t = localtime(&tNow);
		strftime(szBuf, sizeof(szBuf), "%Y/%m/%d %H:%M:%S", t);
		fprintf(Logfile, "%s %s\n", szBuf, szBufString);
		fflush(Logfile);
		fclose(Logfile);
	}
	else {
		ConPrint(L"Failed to write nodockcommand log! This might be due to inability to create the directory - are you running as an administrator?\n");
	}
	Logfile = fopen("./flhook_logs/nodockcommand.log", "at");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Settings Loading
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ADOCK::LoadSettings()
{
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\alley.cfg";

	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("nodockcommand"))
			{
				uint currId;
				NoDockData nodockdata;
				while (ini.read_value())
				{
					if (ini.is_value("id"))
					{
						currId = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("contraband"))
					{
						nodockdata.contrabandItems.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("system"))
					{
						nodockdata.validSystems.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("nodock_affiliation"))
					{
						nodockdata.blockedBaseIFFs.insert(MakeId(ini.get_value_string(0)));
					}
					else if (ini.is_value("can_police"))
					{
						nodockdata.canPolice = ini.get_value_bool(0);
					}
				}
				nodockDataMap[currId] = nodockdata;
			}
			else if (ini.is_header("config"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("duration"))
					{
						set_duration = ini.get_value_int(0);
					}
				}
			}
			else if (ini.is_header("supernodockedships"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("ship"))
					{
						superNoDockedShips.insert((const wchar_t*)ini.get_value_wstring());
					}
				}
			}
		}
		ini.close();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Logic
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ADOCK::Timer()
{
	for (auto i = mapActiveNoDock.begin(); i != mapActiveNoDock.end(); ++i)
	{
		if (i->second.duration == 0)
		{
			const wchar_t* wszCharname = (const wchar_t*)Players.GetActiveCharacterName(i->first);
			if (wszCharname) {
				wstring wscMsg = L"%time %victim's docking rights have been restored.";
				wscMsg = ReplaceStr(wscMsg, L"%time", GetTimeString(false));
				wscMsg = ReplaceStr(wscMsg, L"%victim", wszCharname);
				PrintLocalUserCmdText(i->first, wscMsg, 10000);
			}

			mapActiveNoDock.erase(i->first);
		}
		else
		{
			--i->second.duration;
		}
	}
}

void ADOCK::ClearClientInfo(uint iClientID)
{
	MapActiveSirens.erase(iClientID);
}

bool CheckContraband(IObjRW* target, NoDockData& nodockData)
{
	if (nodockData.contrabandItems.empty())
	{
		return true;
	}

	CShip* cship = reinterpret_cast<CShip*>(target->cobj);

	CEquipTraverser tr(Cargo);
	CEquip* equip;
	while (equip = cship->equip_manager.Traverse(tr))
	{
		if (nodockData.contrabandItems.count(equip->archetype->iArchID))
		{
			return true;
		}
	}
	return false;
}

bool ADOCK::NoDockCommand(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	uint clientFacID = ClientInfo[iClientID].playerID;
	auto nodockDataIter = nodockDataMap.find(clientFacID);
	if (nodockDataIter == nodockDataMap.end())
	{
		PrintUserCmdText(iClientID, L"ERR Your ID has no access to this command");
		return true;
	}
	NoDockData& nodockData = nodockDataIter->second;

	if (nodockData.blockedBaseIFFs.empty())
	{
		PrintUserCmdText(iClientID, L"ERR Your ID cannot use this command");
		return true;
	}

	CShip* cship = ClientInfo[iClientID].cship;
	if (!cship) {
		PrintUserCmdText(iClientID, L"ERR You are docked");
		return true;
	}

	IObjRW* target = cship->get_target();

	if (!target) {
		PrintUserCmdText(iClientID, L"ERR No target");
		return true;
	}

	uint targetClientID = target->cobj->ownerPlayer;
	if (!targetClientID)
	{
		PrintUserCmdText(iClientID, L"ERR Target is not a player ship");
		return true;
	}

	if (!nodockData.validSystems.empty() && !nodockData.validSystems.count(cship->system))
	{
		PrintUserCmdText(iClientID, L"ERR You cannot use this command in this system");
		return true;
	}

	if (!CheckContraband(target, nodockData))
	{
		PrintUserCmdText(iClientID, L"ERR Target does not carry contraband");
		return true;
	}

	auto activeNodockMap = mapActiveNoDock.find(targetClientID);
	if (activeNodockMap != mapActiveNoDock.end())
	{
		PrintUserCmdText(iClientID, L"OK Removal of docking rights reset to %d seconds", set_duration);
		PrintUserCmdText(targetClientID, L"Removal of docking rights reset to %d seconds", set_duration);
		activeNodockMap->second.duration = set_duration;
		return true;
	}

	mapActiveNoDock[targetClientID].duration = set_duration;
	mapActiveNoDock[targetClientID].blockedBaseIFFs = &nodockData.blockedBaseIFFs;
	wstring targetName = (const wchar_t*)Players.GetActiveCharacterName(targetClientID);
	//10k space message

	stringstream ss;
	ss << set_duration;
	string strduration = ss.str();

	wstring wscMsg = L"%time %victim's docking rights have been temporarily restricted by %player for %duration seconds for factions:";
	wscMsg = ReplaceStr(wscMsg, L"%time", GetTimeString(false));
	wscMsg = ReplaceStr(wscMsg, L"%player", (const wchar_t*)Players.GetActiveCharacterName(iClientID));
	wscMsg = ReplaceStr(wscMsg, L"%victim", targetName.c_str());
	wscMsg = ReplaceStr(wscMsg, L"%duration", stows(strduration).c_str());
	PrintLocalUserCmdText(iClientID, wscMsg, 20000);
	for (auto faction : nodockData.blockedBaseIFFs)
	{
		PrintLocalUserCmdText(iClientID, HkGetWStringFromIDS(Reputation::get_short_name(faction)).c_str(), 20000);
	}

	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);

	//internal log
	wstring wscMsgLog = L"<%sender> removed docking rights from <%victim>";
	wscMsgLog = ReplaceStr(wscMsgLog, L"%sender", wscCharname.c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%victim", targetName.c_str());
	string scText = wstos(wscMsgLog);
	Logging("%s", scText.c_str());

	return true;
}

bool ADOCK::IsDockAllowed(uint iShip, uint iDockTarget, uint iClientID)
{
	if (!superNoDockedShips.empty())
	{
		boolean supernodocked = false;
		wstring curCharName = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
		auto sndIter = superNoDockedShips.find(curCharName);
		if (sndIter != superNoDockedShips.end())
		{
			auto iobj = HkGetInspectObj(iDockTarget);
			if (iobj && iobj->cobj->type & (Station | DockingRing))
			{
				PrintUserCmdText(iClientID, L"You are not allowed to dock on any base.");
				return false;
			}
		}
	}

	// instead of complicated code, we just check if he's nice. If so, we ignore the rest of the code.
	auto activeNoDockIter = mapActiveNoDock.find(iClientID);
	if (activeNoDockIter == mapActiveNoDock.end())
	{
		return true;
	}

	auto iobj = HkGetInspectObj(iDockTarget);
	uint affiliation = 0;
	if (!iobj || iobj->get_affiliation(affiliation) != 0)
	{
		return true;
	}

	if (!(iobj->cobj->type & (Station | DockingRing)))
	{
		return true;
	}

	// if he's not nice, we check if the base is subject to nodock effect.
	if (!activeNoDockIter->second.blockedBaseIFFs 
		|| activeNoDockIter->second.blockedBaseIFFs->count(affiliation))
	{
		//we have found this base in the blacklist. nodock will therefore work. don't let him dock.
		PrintUserCmdText(iClientID, L"Your docking permissions have been temporarily taken away. %d seconds remaining", activeNoDockIter->second.duration);
		return false;
	}

	//otherwise this is probably not meant to work	
	return true;
}

void ADOCK::AdminNoDock(CCmds* cmds, const wstring &wscCharname)
{
	if (cmds->rights != RIGHT_SUPERADMIN)
	{
		cmds->Print(L"ERR No permission\n");
		return;
	}

	HKPLAYERINFO targetPlyr;
	if (HkGetPlayerInfo(wscCharname, targetPlyr, false) != HKE_OK)
	{
		cmds->Print(L"ERR Player not found\n");
		return;
	}

	for (auto i = mapActiveNoDock.begin(); i != mapActiveNoDock.end(); ++i)
	{
		if (i->first == targetPlyr.iClientID)
		{
			cmds->Print(L"OK Removal of docking rights reset to %d seconds", set_duration);
			PrintUserCmdText(targetPlyr.iClientID, L"Removal of docking rights reset to %d seconds", set_duration);
			i->second.duration = set_duration;
			return;
		}
	}

	mapActiveNoDock[targetPlyr.iClientID].duration = set_duration;
	mapActiveNoDock[targetPlyr.iClientID].blockedBaseIFFs = nullptr;

	//10k space message

	stringstream ss;
	ss << set_duration;
	string strduration = ss.str();

	wstring wscMsg = L"%time %victim's docking rights have been removed by %player for minimum %duration seconds";
	wscMsg = ReplaceStr(wscMsg, L"%time", GetTimeString(false));
	wscMsg = ReplaceStr(wscMsg, L"%player", cmds->GetAdminName().c_str());
	wscMsg = ReplaceStr(wscMsg, L"%victim", targetPlyr.wscCharname.c_str());
	wscMsg = ReplaceStr(wscMsg, L"%duration", stows(strduration).c_str());
	PrintLocalUserCmdText(targetPlyr.iClientID, wscMsg, 10000);

	//internal log
	wstring wscMsgLog = L"<%sender> removed docking rights from <%victim>";
	wscMsgLog = ReplaceStr(wscMsgLog, L"%sender", cmds->GetAdminName().c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%victim", targetPlyr.wscCharname.c_str());
	string scText = wstos(wscMsgLog);
	Logging("%s", scText.c_str());

	cmds->Print(L"OK\n");
	return;
}

bool ADOCK::PoliceCmd(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	CShip* cship = ClientInfo[iClientID].cship;

	if (!cship)
	{
		PrintUserCmdText(iClientID, L"ERR You are docked");
		return true;
	}

	auto playerID = ClientInfo[iClientID].playerID;
	auto nodockData = nodockDataMap.find(playerID);
	if(nodockData == nodockDataMap.end() || !nodockData->second.canPolice)
	{
		PrintUserCmdText(iClientID, L"ERR You are not allowed to use this command.");
		return true;
	}

	if (MapActiveSirens.find(iClientID) != MapActiveSirens.end())
	{
		UnSetFuse(iClientID, CreateID("dsy_police_liberty"));
		MapActiveSirens.erase(iClientID);
		PrintUserCmdText(iClientID, L"Police system deactivated.");
	}
	else
	{
		SetFuse(iClientID, CreateID("dsy_police_liberty"), 999999);
		MapActiveSirens[iClientID] = L"test";
		PrintUserCmdText(iClientID, L"Police system activated.");
	}

	return true;
}
