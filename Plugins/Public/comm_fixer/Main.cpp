// BountyScan Plugin by Alex. Just looks up the target's ID (tractor). For convenience on Discovery.
// 
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include <windows.h>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Main.h"

PLUGIN_RETURNCODE returncode;

uint constexpr ALPHA_DESIG = 197808;
uint constexpr FREELANCER_COMM_HASH = 0xb967660b;
uint constexpr FREELANCER_AFFIL = 0x1049;
struct Callsign
{
	uint lastFactionAff = 0;
	uint factionLine = FREELANCER_COMM_HASH;
	uint formationLine;
	uint number1;
	uint number2;
};

unordered_map<uint, Callsign> clientData;

struct FactionData
{
	uint msgId;
	vector<uint> formationHashes;
};
unordered_map<uint, FactionData> factionData;

unordered_map<uint, pair<uint, uint>> numberHashes; //number said in the middle and in the end

void LoadSettings()
{

	INI_Reader ini;

	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string currDir = string(szCurDir);
	string scFactionProp = currDir + R"(\..\DATA\MISSIONS\faction_prop.ini)";

	if (!ini.open(scFactionProp.c_str(), false))
	{
		return;
	}

	while (ini.read_header())
	{
		if (!ini.is_header("FactionProps"))
		{
			continue;
		}
		uint currentAff;
		FactionData facData;
		while (ini.read_value())
		{
			if (ini.is_value("affiliation"))
			{
				currentAff = MakeId(ini.get_value_string());
			}
			else if (ini.is_value("msg_id_prefix"))
			{
				facData.msgId = CreateID(ini.get_value_string());
			}
			else if (ini.is_value("formation_desig"))
			{
				int start = ini.get_value_int(0);
				int end = ini.get_value_int(1);

				for (int i = start; i <= end; i++)
				{
					char buf[50];
					int number = i - ALPHA_DESIG + 1;
					sprintf_s(buf, "gcs_refer_formationdesig_%02d", number);
					facData.formationHashes.push_back(CreateID(buf));
				}
			}
		}
		factionData[currentAff] = facData;
	}
	ini.close();

	for (int i = 1; i < 20; i++)
	{
		char buf[50];
		sprintf_s(buf, "gcs_misc_number_%d", i);
		uint number1 = CreateID(buf);
		sprintf_s(buf, "gcs_misc_number_%d-", i);
		uint number2 = CreateID(buf);
		numberHashes[i] = {number1, number2};
	}
}

void __cdecl SendComm(uint sender, uint receiver, uint voiceId, const Costume* costume, uint infocardId, uint* lines, int& lineCount, uint infocardId2, float radioSilenceTimerAfter, bool global)
{
	returncode = DEFAULT_RETURNCODE;

	uint clientId = HkGetClientIDByShip(receiver);
	if (!clientId)
	{
		return;
	}
	auto cd = clientData.find(clientId);
	if (cd == clientData.end())
	{
		return;
	}

	for (int i = 0; i < lineCount; ++i)
	{
		if (lines[i] != FREELANCER_COMM_HASH)
		{
			continue;
		}

		if (i + 4 < lineCount)
		{
			break;
		}

		lines[i] = cd->second.factionLine;
		lines[i + 1] = cd->second.formationLine;
		lines[i + 2] = cd->second.number1;
		lines[i + 4] = cd->second.number2;

		break;
	}
}

void FetchPlayerData(uint client)
{
	uint affiliation;
	Reputation::Vibe::GetAffiliation(Players[client].iReputation, affiliation, false);

	if (!affiliation)
	{
		affiliation = FREELANCER_AFFIL;
	}

	auto fd = factionData.find(affiliation);
	if (fd == factionData.end())
	{
		return;
	}

	auto& cd = clientData[client];

	if (cd.lastFactionAff != affiliation)
	{
		cd.lastFactionAff = affiliation;
		cd.factionLine = fd->second.msgId;
		int randIndex = rand() % fd->second.formationHashes.size();
		cd.formationLine = fd->second.formationHashes.at(randIndex);
		int randNum1 = (rand() % numberHashes.size()) + 1; // +1 because map starts at 1
		cd.number1 = numberHashes.at(randNum1).first;
		int randNum2 = (rand() % numberHashes.size()) + 1;
		cd.number2 = numberHashes.at(randNum2).second;
	}
}

void __stdcall PlayerLaunch(unsigned int ship, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	FetchPlayerData(client);
}

void __stdcall CharacterSelect_AFTER(struct CHARACTER_ID const& cId, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	clientData.erase(client);
}

typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct USERCMD
{
	wchar_t *wszCmd;
	_UserCmdProc proc;
	wchar_t *usage;
};

USERCMD UserCmds[] =
{
	{}
//	{ L"/bountyscan", UserCmd_BountyScan, L"Usage: /bountyscan or /bs" },
//	{ L"/bs", UserCmd_BountyScan, L"Usage: /bountyscan or /bs" },
};

bool UserCmd_Process(uint iClientID, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{

		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				returncode = SKIPPLUGINS_NOFUNCTIONCALL;
				return true;
			}
		}
	}
	return false;
}



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

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "CommFixer";
	p_PI->sShortName = "comm_fixer";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect_AFTER, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch, PLUGIN_HkIServerImpl_PlayerLaunch, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SendComm, PLUGIN_HkIEngine_SendComm, 0));
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));

	return p_PI;
}