// FLHook Plugin to hold a miscellaneous collection of commands and 
// other such things that don't fit into other plugins
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "MiscCmds.h"

// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

void LoadSettings();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	srand(static_cast<uint>(time(nullptr)));
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		HkLoadStringDLLs();
		LoadSettings();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		HkUnloadStringDLLs();
	}
	return true;
}

/// Hook will call this function after calling a plugin function to see if we the
/// processing to continue
EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Structures and Declarations
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

// See Main.h for any struct/class defs.
// This is just for declarations

unordered_map<uint, vector<SpaceBuy>> spaceBuyData;
float range = 2500.f;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoadSettings()
{
	


	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\spacebuyorder.cfg";

	INI_Reader ini;
	if (!ini.open(scPluginCfgFile.c_str(), false))
	{
		return;
	}

	while (ini.read_header())
	{
		if (!ini.is_header("space_buy_commodities"))
		{
			continue;
		}

		while (ini.read_value())
		{
			if (ini.is_value("space_buy"))
			{

				int counter = 3;
				uint baseId = CreateID(ini.get_value_string(0));
				vector<SpaceBuy> spaceBuyMap;
				while (!ini.is_value_empty(counter))
				{
					SpaceBuy spaceBuy;
					spaceBuy.goodId = CreateID(ini.get_value_string(counter - 2));
					spaceBuy.amount = ini.get_value_int(counter - 1);
					spaceBuy.price = ini.get_value_int(counter);
					spaceBuyMap.emplace_back(spaceBuy);
					counter += 3;
				}

				spaceBuyData[baseId] = spaceBuyMap;
			}
			else if (ini.is_value("range"))
			{
				range = ini.get_value_float(0);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CheckIsInBase(uint iClientID)
{
	uint iBaseID;
	pub::Player::GetBase(iClientID, iBaseID);
	if (!iBaseID)
	{
		PrintUserCmdText(iClientID, L"You must be in a base to use this command.");
		return false;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

// /refresh - Updates the timestamps of the character file for all the ships on the account.

bool UserCmd_OrderSpaceBuy(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	auto cship = ClientInfo[iClientID].cship;
	if (!cship)
	{
		PrintUserCmdText(iClientID, L"ERR Not in space");
		return false;
	}

	IObjRW* target = cship->get_target();

	if (!target)
	{
		PrintUserCmdText(iClientID, L"ERR No target");
		return false;
	}

	auto cobj = target->cobj;
	if (!cobj || cobj->objectClass != CObject::CSOLAR_OBJECT)
	{
		PrintUserCmdText(iClientID, L"ERR Not a solar");
		return false;
	}

	auto iter = spaceBuyData.find(cobj->id);
	if (iter == spaceBuyData.end() || iter->second.empty())
	{
		PrintUserCmdText(iClientID, L"ERR Target not providing services of this kind");
		return false;
	}

	if (HkDistance3D(cobj->vPos, cship->vPos) > 3000.f)
	{
		PrintUserCmdText(iClientID, L"ERR Out of range");
		return false;
	}

	auto param = GetParam(wscParam, ' ', 0);
	if (!param.empty())
	{
		uint nr = ToInt(param);
		if (!nr || nr > iter->second.size())
		{
			PrintUserCmdText(iClientID, L"ERR Invalid selection");
			return false;
		}

		auto& buy = iter->second[nr-1];

		if (Players[iClientID].iInspectCash < buy.price)
		{
			PrintUserCmdText(iClientID, L"ERR Not enough money");
			return false;
		}

		auto equipArch = Archetype::GetEquipment(Good2Arch(buy.goodId));
		if (!equipArch)
		{
			PrintUserCmdText(iClientID, L"ERR Invalid configuration, contact administration");
			return false;
		}

		int spaceTaken = cship->get_space_for_cargo_type(equipArch);
		if (spaceTaken < buy.amount)
		{
			PrintUserCmdText(iClientID, L"ERR Insufficient Cargo Space");
			static uint insufficientCargoHold = CreateID("insufficient_cargo_space");
			pub::Player::SendNNMessage(iClientID, insufficientCargoHold);
			return false;
		}

		pub::Player::AddCargo(iClientID, buy.goodId, buy.amount, 1.0, false);
		pub::Player::AdjustCash(iClientID, -buy.price);
		static uint ui_buy_commodity = CreateID("ui_buy_commodity");
		pub::Audio::PlaySoundEffect(iClientID, ui_buy_commodity);

		PrintUserCmdText(iClientID, L"OK Good(s) purchased");
		return true;
	}

	PrintUserCmdText(iClientID, L"Available goods:");
	int counter = 0;
	for (auto& buy : iter->second)
	{
		auto gi = GoodList::find_by_id(buy.goodId);
		if (!gi)
		{
			continue;
		}

		auto name = HkGetWStringFromIDS(gi->iIDSName);

		if (buy.amount == 1)
		{
			PrintUserCmdText(iClientID, L"%d: %s - $%d", ++counter, name.c_str(), buy.price);
		}
		else
		{
			PrintUserCmdText(iClientID, L"%d: %s (x%d) - $%d", ++counter, name.c_str(), buy.amount, buy.price);
		}
	}

	return true;
}

bool UserCmd_ForceAbortMission(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	if (!Players[iClientID].iMissionID)
	{
		PrintUserCmdText(iClientID, L"Not on a mission");
		return false;
	}

	Server.AbortMission(iClientID, 0);
	Players[iClientID].iMissionID = 0;
	Players[iClientID].iMissionSetBy = 0;
	PrintUserCmdText(iClientID, L"Mission forcefully aborted");

	return true;
}

int GetMembersInSpace(CPlayerGroup* group)
{
	if (!group)
	{
		return 0;
	}

	uint membersInSpace = 0;

	for (uint i = 0; i < group->GetMemberCount(); i++)
	{
		if (Players[group->GetMember(i)].iShipID)
		{
			membersInSpace++;
		}
	}

	return membersInSpace;
}

bool UserCmd_GroupSize(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	uint targetGroupId = ToUInt(GetParam(wscParam, ' ', 0));

	auto group = Players[iClientID].PlayerGroup;

	if (!targetGroupId && wscParam.empty())
	{
		auto cship = ClientInfo[iClientID].cship;
		if (cship)
		{
			auto target = cship->get_target();
			if (target && target->cobj->objectClass == CObject::CSHIP_OBJECT)
			{
				targetGroupId = reinterpret_cast<CShip*>(target->cobj)->groupId;
			}
		}
	}

	if (!targetGroupId)
	{
		if (group)
		{
			PrintUserCmdText(iClientID, L"Your group(%u) size: %u (%u in space)", group->GetID(), group->GetMemberCount(), GetMembersInSpace(group));
		}
		else
		{
			PrintUserCmdText(iClientID, L"ERR No parameter provided, target not in a group, and you're not in a group");
		}
		return true;
	}

	static auto groupMap = reinterpret_cast<st6::map<const uint, CPlayerGroup*>*>(0x6D90400);
	auto targetGroupIter = groupMap->find(targetGroupId);
	if (!targetGroupId || targetGroupIter == groupMap->end() || targetGroupIter->second->GetMemberCount() == 0)
	{
		PrintUserCmdText(iClientID, L"ERR No such group");
		return false;
	}

	PrintUserCmdText(iClientID, L"Target group(%u) size: %u (%u in space)", targetGroupIter->second->GetID(), targetGroupIter->second->GetMemberCount(), GetMembersInSpace(targetGroupIter->second));
	if (group && group->GetID() != targetGroupId)
	{
		PrintUserCmdText(iClientID, L"Your group(%u) size: %u (%u in space)", group->GetID(), group->GetMemberCount(), GetMembersInSpace(group));
	}
	return true;
}

wstring GetShipClassName(uint shipClass)
{
	switch (shipClass)
	{
	case 0:
		return L"Light Fighter";
	case 1:
		return L"Heavy Fighter";
	case 2:
		return L"Freighter";
	case 3:
		return L"Very Heavy Fighter";
	case 4:
		return L"Bomber";
	case 6:
		return L"Transport";
	case 7:
		return L"Train";
	case 8:
		return L"Heavy Transport";
	case 9:
		return L"Heavy Train";
	case 10:
		return L"Liner";
	case 11:
		return L"Gunboat";
	case 12:
		return L"Frigate";
	case 13:
		return L"Cruiser";
	case 14:
		return L"Asteroid Miner";
	case 15:
		return L"Battlecruiser";
	case 16:
		return L"Battleship";
	case 17:
		return L"Battleship";
	case 18:
		return L"Heavy Battleship";
	case 19:
		return L"Repair Ship";
	default:
		return L"Unknown";
	}
}

bool UserCmd_FleetComp(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	auto group = Players[client].PlayerGroup;
	if (!group)
	{
		PrintUserCmdText(client, L"ERR Not in group");
		return false;
	}

	map<uint, uint> shipClassMap;
	for (uint i = 0; i < group->GetMemberCount(); i++)
	{
		auto shipArch = Archetype::GetShip(Players[group->GetMember(i)].iShipArchetype);
		shipClassMap[shipArch->iShipClass]++;
	}

	PrintUserCmdText(client, L"Fleet composition:");
	for (auto& entry : shipClassMap)
	{
		PrintUserCmdText(client, L"%ux %ls", entry.second, GetShipClassName(entry.first).c_str());
	}

	return true;
}

bool UserCmd_WayPointRally(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	if (!Players[iClientID].iShipID)
	{
		PrintUserCmdText(iClientID, L"ERR Must be in space");
		return false;
	}

	if (!Players[iClientID].PlayerGroup || Players[iClientID].PlayerGroup->GetMemberCount() == 1)
	{
		PrintUserCmdText(iClientID, L"ERR Must be in a non-empty group!");
		return false;
	}

	RequestPathStruct<2> requestPathStruct;
	requestPathStruct.pathEntries[1].pos = ClientInfo[iClientID].cship->vPos;
	requestPathStruct.pathEntries[1].systemId = Players[iClientID].iSystemID;

	wstring clientName = (const wchar_t*)Players.GetActiveCharacterName(iClientID);

	uint counter = 0;
	auto& pg = Players[iClientID].PlayerGroup;
	uint groupSize = pg->GetMemberCount();
	for (uint i = 0; i < groupSize; ++i)
	{
		uint memberId = pg->GetMember(i);
		if (memberId == iClientID)
		{
			continue;
		}
		if (!ClientInfo[memberId].cship)
		{
			continue;
		}

		requestPathStruct.repId = Players[memberId].iReputation;
		requestPathStruct.pathEntries[0].systemId = Players[iClientID].iSystemID;
		requestPathStruct.pathEntries[0].pos = ClientInfo[iClientID].cship->vPos;

		Server.RequestBestPath(memberId, (unsigned char*)&requestPathStruct, 0);
		PrintUserCmdText(memberId, L"%ls is rallying you to their position.", clientName.c_str());
		++counter;
	}

	PrintUserCmdText(iClientID, L"Rallying %u allies to your position.", counter);
	return true;
}

bool UserCmd_WayPoint(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	if (!Players[iClientID].iShipID)
	{
		PrintUserCmdText(iClientID, L"ERR Must be in space");
		return false;
	}

	Vector pos = { ToFloat(GetParam(wscParam, ' ', 0)),ToFloat(GetParam(wscParam, ' ', 1)),ToFloat(GetParam(wscParam, ' ', 2)) };

	RequestPathStruct<2> bestPathStruct;
	bestPathStruct.repId = Players[iClientID].iReputation;
	bestPathStruct.waypointCount = 2;
	bestPathStruct.noPathFound = false;
	bestPathStruct.pathEntries[0].systemId = Players[iClientID].iSystemID;
	bestPathStruct.pathEntries[0].pos = ClientInfo[iClientID].cship->vPos;
	bestPathStruct.pathEntries[1].pos = pos;
	bestPathStruct.pathEntries[1].systemId = Players[iClientID].iSystemID;

	Server.RequestBestPath(iClientID, (unsigned char*)&bestPathStruct, 0);

	return true;
}

bool UserCmd_WayPointPlayer(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	if (!Players[iClientID].iShipID)
	{
		PrintUserCmdText(iClientID, L"ERR Must be in space!");
		return false;
	}

	if (!Players[iClientID].PlayerGroup || Players[iClientID].PlayerGroup->GetMemberCount() == 1)
	{
		PrintUserCmdText(iClientID, L"ERR Must be in a non-empty group!");
		return false;
	}

	if (wscParam.empty())
	{
		PrintUserCmdText(iClientID, L"ERR Must provide target name (at least partial)!");
		return false;
	}

	wstring targetName = ToLower(wscParam);

	auto& pg = Players[iClientID].PlayerGroup;
	uint groupSize = pg->GetMemberCount();
	uint targetClient = 0;
	wstring memberName;

	for (uint i = 0; i < groupSize; ++i)
	{
		uint memberId = pg->GetMember(i);
		if (memberId == iClientID)
		{
			continue;
		}

		memberName = (const wchar_t*)Players.GetActiveCharacterName(memberId);

		if (ToLower(memberName).find(targetName) != wstring::npos)
		{
			targetClient = memberId;
			break;
		}
	}

	if (!targetClient)
	{
		PrintUserCmdText(iClientID, L"ERR Target ship not found!");
		return false;
	}

	if (!ClientInfo[targetClient].cship)
	{
		PrintUserCmdText(iClientID, L"ERR %ls is not in space!", memberName.c_str());
		return false;
	}

	PrintUserCmdText(iClientID, L"Plotting waypoint to: %ls", memberName.c_str());

	RequestPathStruct<2> bestPathStruct;
	bestPathStruct.repId = Players[iClientID].iReputation;
	bestPathStruct.waypointCount = 2;
	bestPathStruct.noPathFound = false;
	bestPathStruct.pathEntries[0].systemId = Players[iClientID].iSystemID;
	bestPathStruct.pathEntries[0].pos = ClientInfo[iClientID].cship->vPos;
	bestPathStruct.pathEntries[1].pos = ClientInfo[targetClient].cship->vPos;
	bestPathStruct.pathEntries[1].systemId = Players[targetClient].iSystemID;

	Server.RequestBestPath(iClientID, (unsigned char*)&bestPathStruct, 0);

	return true;
}

struct RepGroup
{
	ushort affil;
	uint dunno;
	uint clientId;
	uint dunno2[14];
	wchar_t name[24];
	uint dunno3;
	st6::vector<std::pair<uint, float>> feelingsVector;
	uint dunno4;
};

st6::map<uint, RepGroup>* repMap = (st6::map<uint, RepGroup>*)0x64018C4;

// /frelancer - gives the user a freelancer IFF
bool UserCmd_FreelancerIFF(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{

	if (!CheckIsInBase(iClientID))
		return true;

	auto playerVibe = Players[iClientID].iReputation;
	auto static fcAffil = MakeId("fc_freelancer");
	pub::Reputation::SetReputation(playerVibe, fcAffil, 1.0f);
	pub::Reputation::SetAffiliation(playerVibe, fcAffil);

	auto vibe = repMap->find(playerVibe);

	float rep = 0;
	Reputation::Vibe::GetGroupFeelingsTowards(playerVibe, fcAffil, rep);
	if (vibe == repMap->end() || vibe->second.affil != fcAffil || rep < 0.9f)
	{
		PrintUserCmdText(iClientID, L"Freelancer IFF could not be granted, likely due to rephack limits on your currently equipped ID.");
		return true;
	}

	PrintUserCmdText(iClientID, L"Freelancer IFF granted.");
	return true;
}

// /frelancer - gives the user a freelancer IFF
bool UserCmd_PirateIFF(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{

	if (!CheckIsInBase(iClientID))
		return true;

	auto playerVibe = Players[iClientID].iReputation;
	auto static fcAffil = MakeId("fc_pirate");
	pub::Reputation::SetReputation(playerVibe, fcAffil, 1.0f);
	pub::Reputation::SetAffiliation(playerVibe, fcAffil);

	auto vibe = repMap->find(playerVibe);
	
	float rep = 0;
	Reputation::Vibe::GetGroupFeelingsTowards(playerVibe, fcAffil, rep);
	if (vibe == repMap->end() || vibe->second.affil != fcAffil || rep < 0.9f)
	{
		PrintUserCmdText(iClientID, L"Pirate IFF could not be granted, likely due to rephack limits on your currently equipped ID.");
		return true;
	}

	PrintUserCmdText(iClientID, L"Pirate IFF granted.");
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Actual Code
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Clean up when a client disconnects */
void ClearClientInfo(uint iClientID)
{
	
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Client command processing
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct USERCMD
{
	wchar_t *wszCmd;
	_UserCmdProc proc;
	wchar_t *usage;
};

USERCMD UserCmds[] =
{
	{ L"/freelancer", UserCmd_FreelancerIFF, L"" },
	{ L"/pirate", UserCmd_PirateIFF, L"" },
	{ L"/wp", UserCmd_WayPoint, L"" },
	{ L"/wpp", UserCmd_WayPointPlayer, L"" },
	{ L"/rally", UserCmd_WayPointRally, L"" },
	{ L"/missionbug", UserCmd_ForceAbortMission, L""},
	{ L"/groupsize", UserCmd_GroupSize, L""},
	{ L"/gs", UserCmd_GroupSize, L""},
	{ L"/fleetcomp", UserCmd_FleetComp, L""},
	{ L"/fc", UserCmd_FleetComp, L""},
	{ L"/order", UserCmd_OrderSpaceBuy, L""},
};

/**
This function is called by FLHook when a user types a chat string. We look at the
string they've typed and see if it starts with one of the above commands. If it
does we try to process it.
*/
bool UserCmd_Process(uint iClientID, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	// If the chat string does not match the USER_CMD then we do not handle the
	// command, so let other plugins or FLHook kick in. We require an exact match
	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{

		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			// Extract the parameters string from the chat string. It should
			// be immediately after the command and a space.
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			// Dispatch the command to the appropriate processing function.
			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				// We handled the command tell FL hook to stop processing this
				// chat string.
				returncode = SKIPPLUGINS_NOFUNCTIONCALL; // we handled the command, return immediatly
				return true;
			}
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Miscellaneous Commands by a lot of different people.";
	p_PI->sShortName = "misc";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	//p_PI->lstHooks.emplace_back(reinterpret_cast<FARPROC*>(&LoadSettings), PLUGIN_LoadSettings, 0);
	p_PI->lstHooks.emplace_back(reinterpret_cast<FARPROC*>(&ClearClientInfo), PLUGIN_ClearClientInfo, 0);
	p_PI->lstHooks.emplace_back(reinterpret_cast<FARPROC*>(&UserCmd_Process), PLUGIN_UserCmd_Process, 0);

	return p_PI;
}
