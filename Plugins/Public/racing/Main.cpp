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

struct WaypointData
{
	Vector pos;
	uint objId;
	uint system;
};

struct Race
{
	list<WaypointData> waypoints;
	uint startingSystem;
	vector<Transform> startingPositions;
};

unordered_map<uint, unordered_map<wstring, Race>> raceObjMap;

struct PendingRace
{
	uint hostId;
	int cashPool;
	unordered_set<uint> participantSet;
	vector<uint> participants;
	Race* race;
};

unordered_map<uint, PendingRace> pendingRaces;

struct Racer
{
	mstime startTime;
	list<WaypointData> waypoints;
};

unordered_map<uint, Racer> racersMap;

unordered_map<uint, std::pair<Race*, int>> raceRegistration;

void LoadSettings();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
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
	string scPluginCfgFile = string(szCurDir) + R"(\flhook_plugins\racing.cfg)";

	if (!ini.open(scPluginCfgFile.c_str(), false))
	{
		return;
	}
	while (ini.read_header())
	{
		if (ini.is_header("Race"))
		{
			uint startObj;
			wstring raceName;
			Race race;
			while (ini.read_value())
			{
				if (ini.is_value("start_object"))
				{
					startObj = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("race_name"))
				{
					raceName = stows(ini.get_value_string(0));
				}
				else if (ini.is_value("waypoint_obj"))
				{
					uint objId = CreateID(ini.get_value_string(0));
					auto obj = HkGetInspectObj(objId);
					if (!obj)
					{
						ConPrint(L"ERROR PARSING RACE CONFIG: %s\n", stows(ini.get_value_string(0)).c_str());
						continue;
					}
					
					if (race.waypoints.empty())
					{
						race.startingSystem = obj->cobj->system;
					}

					race.waypoints.push_back({ obj->cobj->vPos, objId, obj->cobj->system });
				}
				else if (ini.is_value("waypoint_pos"))
				{
					Vector pos = { ini.get_value_float(1), ini.get_value_float(2), ini.get_value_float(3) };
					uint system = CreateID(ini.get_value_string(0));
					if (race.waypoints.empty())
					{
						race.startingSystem = system;
					}
					race.waypoints.push_back({ pos, 0u, system });
				}
				else if (ini.is_value("starting_pos"))
				{
					Vector pos = { ini.get_value_float(0), ini.get_value_float(1), ini.get_value_float(2) };
					Vector rot = { ini.get_value_float(3), ini.get_value_float(4), ini.get_value_float(5) };
					Transform startingPos = { pos, EulerMatrix(rot) };
					race.startingPositions.push_back(startingPos);
				}
			}
			raceObjMap[startObj][raceName] = race;
		}
	}
	ini.close();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool UserCmd_SetupRace(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto cship = ClientInfo[clientID].cship;
	if (!cship)
	{
		PrintUserCmdText(clientID, L"ERR Not in space!");
		return true;
	}

	auto target = cship->get_target();
	if (!target)
	{
		PrintUserCmdText(clientID, L"ERR No target!");
		return true;
	}

	if (pendingRaces.count(clientID))
	{
		PrintUserCmdText(clientID, L"ERR You're already hosting a race!");
		return true;
	}

	auto raceObjIter = raceObjMap.find(target->get_id());
	if (raceObjIter == raceObjMap.end())
	{
		PrintUserCmdText(clientID, L"ERR Invalid start point object selected!");
		return true;
	}

	auto raceName = GetParam(param, ' ', 1);
	auto raceIter = raceObjIter->second.find(raceName);
	if (raceIter == raceObjIter->second.end())
	{
		PrintUserCmdText(clientID, L"ERR Invalid race name for selected start! Available races:");
		for (auto& raceName : raceObjIter->second)
		{
			PrintUserCmdText(clientID, L"- %s", raceName.first.c_str());
		}
		return true;
	}

	int initialPool = ToInt(GetParam(param, ' ', 2));
	if (initialPool < 0)
	{
		initialPool = 0;
	}
	if (initialPool < Players[clientID].iInspectCash)
	{
		PrintUserCmdText(clientID, L"ERR You don't have enough cash!");
		return true;
	}

	PendingRace pr;
	pr.race = &raceIter->second;
	pr.hostId = clientID;
	pr.participants.push_back(clientID);
	pr.participantSet.insert(clientID);
	pr.cashPool = initialPool;

	wstring hostName = (const wchar_t*)Players.GetActiveCharacterName(clientID);

	static wchar_t buf[100];
	_snwprintf(buf, sizeof(buf), L"New race: %s started by %s!", raceName.c_str(), hostName.c_str());
	HkMsgS(pr.race->startingSystem, buf);

	if (initialPool)
	{
		_snwprintf(buf, sizeof(buf), L"Initial prize pool: %d credits!", pr.cashPool);
		HkMsgS(pr.race->startingSystem, buf);
	}

	pendingRaces[clientID] = pr;
	raceRegistration[clientID] = { pr.race, pr.cashPool };
}

bool UserCmd_StartRaceSolo(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto cship = ClientInfo[clientID].cship;
	if (!cship)
	{
		PrintUserCmdText(clientID, L"ERR Not in space!");
		return true;
	}

	auto pendingRace = pendingRaces.find(clientID);
	if (pendingRace == pendingRaces.end(clientID))
	{
		PrintUserCmdText(clientID, L"ERR You're not hosting a race!");
		return true;
	}

	for (auto& participant : pendingRace->second.participants)
	{
		PrintUserCmdText(participant, L"Beaming you into position, best of luck!");
		raceRegistration.erase(participant);
	}



	pendingRaces.erase(clientID);
}

bool UserCmd_StartRaceSolo(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto cship = ClientInfo[clientID].cship;
	if (!cship)
	{
		PrintUserCmdText(clientID, L"ERR Not in space!");
		return true;
	}

	auto target = cship->get_target();
	if (!target)
	{
		PrintUserCmdText(clientID, L"ERR No target!");
		return true;
	}

	auto raceObjIter = raceObjMap.find(target->get_id());
	if (raceObjIter == raceObjMap.end())
	{
		PrintUserCmdText(clientID, L"ERR Invalid start point object selected!");
		return true;
	}

	auto raceName = GetParam(param, ' ', 1);
	auto raceIter = raceObjIter->second.find(raceName);
	if (raceIter == raceObjIter->second.end())
	{
		PrintUserCmdText(clientID, L"ERR Invalid race name for selected start! Available races:");
		for (auto& raceName : raceObjIter->second)
		{
			PrintUserCmdText(clientID, L"- %s", raceName.first.c_str());
		}
		return true;
	}

	racersMap[clientID] = { 0, raceIter->second.waypoints };

	RequestPathStruct bestPath;
	bestPath.noPathFound = false;
	bestPath.waypointCount = raceIter->second.waypoints.size();
	bestPath.repId = Players[clientID].iReputation;
	
	auto waypointListIter = raceIter->second.waypoints.begin();
	for (uint i = 0; i < raceIter->second.waypoints.size(); ++i)
	{
		bestPath.pathEntries[i] = { waypointListIter->pos, waypointListIter->objId, waypointListIter->system };
		waypointListIter++;
	}

	pub::Player::ReturnBestPath(clientID, (uchar*)&bestPath, 12 + (bestPath.waypointCount * 20));

	PrintUserCmdText(clientID, L"Race started!");

	return true;
}

int Update()
{
	static bool firstRun = true;
	if (firstRun)
	{
		firstRun = false;
		LoadSettings();
	}
	for (auto& racer = racersMap.begin();racer!= racersMap.end();)
	{
		auto cship = ClientInfo[racer->first].cship;
		if (!cship)
		{
			PrintUserCmdText(racer->first, L"You died or docked and got disqualified!");
			racer = racersMap.erase(racer);
			continue;
		}

		auto currWaypoint = racer->second.waypoints.begin();

		if (cship->system == currWaypoint->system &&
			HkDistance3D(cship->vPos, racer->second.waypoints.begin()->pos) < 50)
		{
			if (!racer->second.startTime)
			{
				racer->second.startTime = timeInMS();
				PrintUserCmdText(racer->first, L"Timer start!");
			}
			else if (racer->second.waypoints.size() > 1)
			{
				racer->second.waypoints.erase(racer->second.waypoints.begin());
				float timeDiff = static_cast<float>(timeInMS() - racer->second.startTime) / 1000;
				PrintUserCmdText(racer->first, L"Passed a checkpoint! %0.3fs", timeDiff);
			}
			else
			{
				float timeDiff = static_cast<float>(timeInMS() - racer->second.startTime) / 1000;
				PrintUserCmdText(racer->first, L"Race complete! Final time: %0.3fs", timeDiff);
				racer = racersMap.erase(racer);
				continue;
			}
		}
		racer++;
	}

	returncode = DEFAULT_RETURNCODE;
	return 0;
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
	{ L"/solorace", UserCmd_StartRaceSolo, L"Usage: /startrace" },
	{ L"/setrace", UserCmd_SetupRace, L"Usage: /startrace" },
	{ L"/startrace", UserCmd_StartRace, L"Usage: /startrace" },
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
bool ExecuteCommandString_Callback(CCmds* cmds, const wstring& wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	//if (IS_CMD("av"))
	//{
	//	AdminVoice(cmds, wscCmd);
	//	returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	//	return true;
	//}

	return false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Racing";
	p_PI->sShortName = "racing";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Update, PLUGIN_HkIServerImpl_Update, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
