// FLHook Plugin to hold a miscellaneous collection of commands and 
// other such things that don't fit into other plugins
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Main.h"

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Structures and Declarations
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

// See Main.h for any struct/class defs.
// This is just for declarations


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;
	// Reserved for future use
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

struct RequestPathStruct
{
	uint shipId;
	uint unused1;
	uint unused2;
	Vector sourcePos;
	uint unused6;
	uint sourceSystem;
	Vector targetPos;
	uint unused11;
	uint targetSystem;
};

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
	uint groupId = ToUInt(GetParam(wscParam, ' ', 0));

	auto group = Players[iClientID].PlayerGroup;

	if (!groupId && wscParam.empty())
	{
		if (group)
		{
			PrintUserCmdText(iClientID, L"Your group size: %u (%u in space)", group->GetMemberCount(), GetMembersInSpace(group));
		}
		else
		{
			PrintUserCmdText(iClientID, L"ERR No parameter provided and you're not in a group");
		}
		return true;
	}

	auto groupMap = reinterpret_cast<st6::map<const uint, CPlayerGroup*>*>(0x6D90400);
	auto groupIter = groupMap->find(groupId);
	if (!groupId || groupIter == groupMap->end() || groupIter->second->GetMemberCount() == 0)
	{
		PrintUserCmdText(iClientID, L"ERR No such group");
		return false;
	}

	PrintUserCmdText(iClientID, L"Target group size: %u (%u in space)", groupIter->second->GetMemberCount(), GetMembersInSpace(groupIter->second));
	if (group && groupIter->second != group)
	{
		PrintUserCmdText(iClientID, L"Your group size: %u (%u in space)", group->GetMemberCount(), GetMembersInSpace(group));
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

	RequestPathStruct requestPathStruct;
	requestPathStruct.targetPos = ClientInfo[iClientID].cship->vPos;
	requestPathStruct.targetSystem = Players[iClientID].iSystemID;

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

		requestPathStruct.shipId = Players[iClientID].iShipID;
		requestPathStruct.sourceSystem = Players[iClientID].iSystemID;
		requestPathStruct.sourcePos = ClientInfo[iClientID].cship->vPos;

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

	RequestPathStruct bestPathStruct;
	bestPathStruct.shipId = Players[iClientID].iShipID;
	bestPathStruct.sourceSystem = bestPathStruct.targetSystem = Players[iClientID].iSystemID;
	bestPathStruct.targetPos = pos;
	bestPathStruct.sourcePos = ClientInfo[iClientID].cship->vPos;

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

	RequestPathStruct bestPathStruct;
	bestPathStruct.shipId = Players[iClientID].iShipID;
	bestPathStruct.sourceSystem = Players[iClientID].iSystemID;
	bestPathStruct.targetSystem = Players[targetClient].iSystemID;
	bestPathStruct.targetPos = ClientInfo[targetClient].cship->vPos;
	bestPathStruct.sourcePos = ClientInfo[iClientID].cship->vPos;

	Server.RequestBestPath(iClientID, (unsigned char*)&bestPathStruct, 0);

	return true;
}

bool UserCmd_RefreshCharacters(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	if (!CheckIsInBase(iClientID))
		return true;

	FILETIME ft;
	SYSTEMTIME st;
	GetLocalTime(&st);
	SystemTimeToFileTime(&st, &ft);
	char toWrite[128];
	sprintf_s(toWrite, "%u,%u", ft.dwHighDateTime, ft.dwLowDateTime);

	CAccount *acc = HkGetAccountByClientID(iClientID);
	CAccountListNode *characterList = acc->pFirstListNode;
	PrintUserCmdText(iClientID, L"Refreshing Characters:");
	PrintUserCmdText(iClientID, L""); // Line break to make it look nice
	int iCharactersRefreshed = 0; // Number to keep track of
	HK_ERROR err; // If it errors we want to inform them

	// Save the current char in case for whatever reason they haven't been saved already
	// Prevents any potential data loss.
	HkSaveChar(reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(iClientID)));

	// We need to keep track of the first character name lest it loops forever.
	wstring wscFirstCharacterName;
	while (characterList)
	{
		// We need to make sure the character name is not null (which it is sometimes because Freelancer?)
		if (!characterList->wszCharname)
		{
			// Loop to the next one if this happens.
			characterList = characterList->next;
			continue;
		}

		wstring wscCharacterName;
		try {
			wscCharacterName = reinterpret_cast<wchar_t*>(characterList->wszCharname);
		}
		catch (...) {
			// Loop to the next one if this happens.
			characterList = characterList->next;
			continue;
		}

		// Only store the first name
		if (iCharactersRefreshed == 0)
			wscFirstCharacterName = wscCharacterName;

		// If this isn't the first name, we need to check that we've not looped over the list again
		else
			if (wscCharacterName == wscFirstCharacterName)
				break; // End the loop if the names match

		iCharactersRefreshed++;
		PrintUserCmdText(iClientID, L"Character: %s - Timestamps Refreshed", wscCharacterName.c_str());
		if ((err = HkFLIniWrite(wscCharacterName, L"tstamp", stows(toWrite))) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"ERROR: %s", HkErrGetText(err).c_str());
			return true;
		}

		// Loop again
		characterList = characterList->next;
	}
	// List the amount of characters we've refreshed.
	PrintUserCmdText(iClientID, L"Sucessfully refreshed %u characters.", iCharactersRefreshed);

	// Kick them as a way to prevent save data loss and ensure everything ticks over as intended.
	HkKickReason(reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(iClientID)), L"Updating Character File, please wait 10 seconds before reconnecting.");
	return true;
}

// /frelancer - gives the user a freelancer IFF
bool UserCmd_FreelancerIFF(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{

	if (!CheckIsInBase(iClientID))
		return true;

	HK_ERROR err;
	if ((err = HkSetRep(reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(iClientID)), L"fc_freelancer", 1.0f)) != HKE_OK)
	{
		PrintUserCmdText(iClientID, L"ERROR: %s", HkErrGetText(err).c_str());
		return true;
	}

	PrintUserCmdText(iClientID, L"Freelancer IFF granted. You may need to /droprep if your old IFF exists after logging out/in and undocking.");
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Actual Code
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Clean up when a client disconnects */
void ClearClientInfo(uint iClientID)
{
	returncode = DEFAULT_RETURNCODE;
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
	{ L"/refresh", UserCmd_RefreshCharacters, L"" },
	{ L"/refresh*", UserCmd_RefreshCharacters, L"" },
	{ L"/freelancer", UserCmd_FreelancerIFF, L"" },
	{ L"/wp", UserCmd_WayPoint, L"" },
	{ L"/wpp", UserCmd_WayPointPlayer, L"" },
	{ L"/rally", UserCmd_WayPointRally, L"" },
	{ L"/missionbug", UserCmd_ForceAbortMission, L""},
	{ L"/groupsize", UserCmd_GroupSize, L""},
	{ L"/gs", UserCmd_GroupSize, L""},
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
