// Voice Manager Plugin - Enable players to select a voice for automatic voicelines in combat/flight, just like NPCs
// By Aingar, credit to Venemon for the how-to
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;


unordered_set<uint> bannedShips;

struct VOICE_COMM
{
	Costume costume;
	uint IDS1 = 0;
	uint IDS2 = 0;
};

struct VOICE_DATA
{
	uint voiceHash;
	wstring voiceName;
	wstring description;
	uint lineHash;
	VOICE_COMM* commWindow;
};

vector<VOICE_DATA> voiceData;
unordered_map<string, VOICE_DATA> adminVoiceMap;
vector<uint> testSamples;
unordered_map<uint, VOICE_COMM> commWindowMap;

void LoadSettings();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length() > 0)
		{
			LoadSettings();
		}
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
//Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	INI_Reader ini;

	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + R"(\flhook_plugins\voice_mgr.cfg)";

	if (!ini.open(scPluginCfgFile.c_str(), false))
	{
		return;
	}
	while (ini.read_header())
	{
		if (ini.is_header("General"))
		{
			while (ini.read_value())
			{
				if (ini.is_value("banned_ship"))
				{
					bannedShips.insert(CreateID(ini.get_value_string()));
				}
				else if (ini.is_value("testVoiceLine"))
				{
					testSamples.emplace_back(CreateID(ini.get_value_string()));
				}
			}
		}
		else if (ini.is_header("Costume"))
		{
			uint costumeName;
			VOICE_COMM vc;
			uint accessoryCounter = 0;
			while (ini.read_value())
			{
				if (ini.is_value("name"))
				{
					costumeName = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("body"))
				{
					vc.costume.body = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("head"))
				{
					vc.costume.head = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("lefthand"))
				{
					vc.costume.leftHand = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("righthand"))
				{
					vc.costume.rightHand = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("accessory"))
				{
					vc.costume.accessory[accessoryCounter] = CreateID(ini.get_value_string());
					accessoryCounter++;
				}
				else if (ini.is_value("ids1"))
				{
					vc.IDS1 = ini.get_value_int(0);
				}
				else if (ini.is_value("ids2"))
				{
					vc.IDS2 = ini.get_value_int(0);
				}
			}
			vc.costume.accessories = accessoryCounter;
			commWindowMap[costumeName] = vc;
		}
		else if (ini.is_header("Voices"))
		{
			while (ini.read_value())
			{
				if (ini.is_value("Voice"))
				{
					voiceData.push_back(
						{	CreateID(ini.get_value_string(0)),
							stows(string(ini.get_value_string(0))),
							stows(ini.get_value_string(1)) });
				}
				else if (ini.is_value("AdminVoice"))
				{
					voiceData.push_back(
						{ CreateID(ini.get_value_string(0)),
							stows(string(ini.get_value_string(0))),
							stows(ini.get_value_string(2)),
							CreateID(ini.get_value_string(1))});
				}
			}
		}
		else if (ini.is_header("AdminLines"))
		{
			string name;
			VOICE_DATA voiceData;
			while (ini.read_value())
			{
				if (ini.is_value("admin"))
				{
					name = ini.get_value_string(0);
					voiceData.voiceHash = CreateID(ini.get_value_string(1));
					voiceData.lineHash = CreateID(ini.get_value_string(2));
					voiceData.commWindow = &commWindowMap[CreateID(ini.get_value_string(3))];
					voiceData.description = stows(ini.get_value_string(4));

					adminVoiceMap[name] = voiceData;
				}
			}
		}
	}
	ini.close();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint smallCraftFlag = (Fighter | Freighter | Gunboat);

bool SetShipArch(uint iClientID, uint ship)
{
	returncode = DEFAULT_RETURNCODE;

	auto shipArch = Archetype::GetShip(ship);

	if (!(shipArch->iArchType & smallCraftFlag)
		|| bannedShips.count(shipArch->iArchID))
	{
		string trentVoice = "trent_voice";
		PlayerData& pd = Players[iClientID];
		strcpy(pd.voice, trentVoice.c_str());
		pd.voiceLen = trentVoice.length();
	}
	return true;
}

bool UserCmd_TestVoice(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto& playerData = Players[clientID];
	if (!playerData.iShipID)
	{
		PrintUserCmdText(clientID, L"ERR TestVoice can only be used in space!");
		return true;
	}
	static uint connId = CreateID("li06");
	auto shipArchId = playerData.iShipArchetype;
	auto shipArch = Archetype::GetShip(shipArchId);
	if (!(shipArch->iArchType & smallCraftFlag))
	{
		PrintUserCmdText(clientID, L"ERR Can only be used on Fighters and Freighters");
		return true;
	}
	if (playerData.iSystemID != connId)
	{
		PrintUserCmdText(clientID, L"ERR Can only be used in Connecticut");
		return true;
	}

	wstring input = ToLower(GetParam(param, ' ', 0));
	uint counter = 1;
	if (input == L"list")
	{
		PrintUserCmdText(clientID, L"Available voices:");

		for (VOICE_DATA& voice : voiceData)
		{
			PrintUserCmdText(clientID, L"%u. %ls", counter++, voice.description.c_str());
		}
		return true;
	}
	uint index = ToUInt(input);
	if (!index || index > voiceData.size())
	{
		PrintUserCmdText(clientID, L"ERR invalid selection");
		return true;
	}
	VOICE_DATA selectedVoice = voiceData.at(index - 1);

	uint testHash = testSamples.at(rand() % testSamples.size());

	uint shipId = Players[clientID].iShipID;
	pub::SpaceObj::SendComm(shipId, 0, selectedVoice.voiceHash, nullptr, 0, &testHash, 9, 0, 0.5, true);

	PrintUserCmdText(clientID, L"OK");
	return true;
}

bool UserCmd_SetVoice(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	if (Players[clientID].iShipID)
	{
		PrintUserCmdText(clientID, L"ERR Cannot use in space");
		return true;
	}
	uint shipArchId = Players[clientID].iShipArchetype;
	auto shipArch = Archetype::GetShip(shipArchId);
	if (!(shipArch->iArchType & smallCraftFlag))
	{
		PrintUserCmdText(clientID, L"ERR Can only be used on Fighters and Freighters");
		return true;
	}
	if (bannedShips.count(shipArch->iArchID))
	{
		PrintUserCmdText(clientID, L"ERR Cannot set a voice to this ship");
		return true;
	}

	wstring input = ToLower(GetParam(param, ' ', 0));
	uint counter = 1;
	if (input == L"list")
	{
		PrintUserCmdText(clientID, L"Available voices:");
		
		for (VOICE_DATA& voice : voiceData)
		{
			PrintUserCmdText(clientID, L"%u. %ls", counter++, voice.description.c_str());
		}
		return true;
	}
	uint index = ToUInt(input);
	if (!index || index > voiceData.size())
	{
		PrintUserCmdText(clientID, L"ERR invalid selection");
		return true;
	}
	string selectedVoice = wstos(voiceData.at(index - 1).voiceName);
	PlayerData& pd = Players[clientID];
	strcpy(pd.voice, selectedVoice.c_str());
	pd.voiceLen = selectedVoice.length();

	PrintUserCmdText(clientID, L"OK");

	return true;
}

typedef bool(*_UserCmdProc)(uint, const wstring&, const wstring&, const wchar_t*);

struct USERCMD
{
	wchar_t* wszCmd;
	_UserCmdProc proc;
	wchar_t* usage;
};

USERCMD UserCmds[] =
{
	{ L"/setvoice", UserCmd_SetVoice, L"Usage: /setvoice list/<option>" },
	{ L"/testvoice", UserCmd_TestVoice, L"Usage: /testvoice list/<option>" },
};

bool UserCmd_Process(uint iClientID, const wstring& wscCmd)
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

#define IS_CMD(a) !wscCmd.compare(L##a)
void AdminVoice(CCmds* cmds, const wstring& cmd)
{
	returncode = SKIPPLUGINS_NOFUNCTIONCALL;

	if (!(cmds->rights & RIGHT_EVENTMODE))
	{
		cmds->Print(L"ERR No permission\n");
		return;
	}

	string voiceLine = wstos(cmds->ArgStr(1));
	bool skipComm = cmds->ArgInt(2);
	if (voiceLine == "list")
	{
		//TODO:
		return;
	}
	if (!adminVoiceMap.count(voiceLine))
	{
		cmds->Print(L"ERR incorrect voiceline\n");
		return;
	}

	uint clientID = HkGetClientIdFromCharname(cmds->GetAdminName());

	if (clientID == -1)
	{
		cmds->Print(L"ERR On console\n");
		return;
	}
	auto selectedLine = adminVoiceMap.at(voiceLine);

	uint shipId = Players[clientID].iShipID;
	if (!shipId)
	{
		cmds->Print(L"ERR Not in space\n");
		return;
	}

	if (selectedLine.commWindow && !skipComm)
	{
		PlayerData* pd = nullptr;
		CShip* adminShip = ClientInfo[clientID].cship;
		const Vector& pos = adminShip->vPos;

		while (pd = Players.traverse_active(pd))
		{
			CShip* cship = ClientInfo[pd->iOnlineID].cship;
			if (!cship)
			{
				continue;
			}
			if (cship->system != adminShip->system)
			{
				continue;
			}
			if(cship->id == adminShip->id)
			{
				pub::SpaceObj::SendComm(0, cship->id, selectedLine.voiceHash, &selectedLine.commWindow->costume, selectedLine.commWindow->IDS1, &selectedLine.lineHash, 9, 0, 0.0, false);
			}
			else if (HkDistance3D(cship->vPos, adminShip->vPos) < 15000)
			{
				pub::SpaceObj::SendComm(adminShip->id, cship->id, selectedLine.voiceHash, &selectedLine.commWindow->costume, selectedLine.commWindow->IDS1, &selectedLine.lineHash, 9, 0, 0.0, false);
			}
		}
	}
	else
	{
		uint targetId;
		pub::SpaceObj::GetTarget(shipId, targetId);
		pub::SpaceObj::SendComm(shipId, 0, selectedLine.voiceHash, nullptr, 0, &selectedLine.lineHash, 9, 0, 0.5, true);
	}
}

bool ExecuteCommandString_Callback(CCmds* cmds, const wstring& wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	if (IS_CMD("av"))
	{
		AdminVoice(cmds, wscCmd);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (IS_CMD("adminvoice"))
	{
		AdminVoice(cmds, wscCmd);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}

	return false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Voice Manager";
	p_PI->sShortName = "voice_mgr";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SetShipArch, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SETSHIPARCH, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
