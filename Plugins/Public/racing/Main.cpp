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

struct RaceArch
{
	wstring raceName;
	list<WaypointData> waypoints;
	uint startingSystem;
	vector<Transform> startingPositions;
};

enum class RaceType
{
	TimeTrial,
	Race
};

unordered_map<uint, unordered_map<wstring, RaceArch>> raceObjMap;

struct Race
{
	uint hostId;
	int cashPool;
	int loopCount = 1;
	unordered_set<uint> participantSet;
	vector<uint> participants;
	RaceArch* race;
	bool started = false;
	RaceType raceType = RaceType::Race;
};

unordered_map<uint, Race> raceHostMap;

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

void SendCommand(uint client, const wstring& message)
{
	HkFMsg(client, L"<TEXT>" + XMLText(message) + L"</TEXT>");
}

void FreezePlayer(uint client, float time)
{
	wchar_t buf[30];
	_snwprintf(buf, sizeof(buf), L" Freeze %f", time);
	SendCommand(client, buf);
}

void UnfreezePlayer(uint client)
{
	SendCommand(client, L" Unfreeze");
}

void BeamPlayers(const Race& race)
{
	uint positionIndex = 0;
	auto positionIter = race.race->startingPositions;
	for (auto player : race.participants)
	{
		if (Players[player].iSystemID != race.race->startingSystem)
		{
			PrintUserCmdText(player, L"ERR You're in the wrong system! Unable to beam you into position");
			continue;
		}
		if (race.race->startingPositions.size() < positionIndex)
		{
			PrintUserCmdText(player, L"ERR Unable to find a starting position for you, contact admins/developers!");
			continue;
		}
		auto& startPos = race.race->startingPositions.at(positionIndex++);
		HkRelocateClient(player, startPos.pos, startPos.ori);
	}
}

void DisqualifyPlayer(uint client)
{
	auto regIter = raceRegistration.find(client);
	if (regIter == raceRegistration.end())
	{
		return;
	}

	raceRegistration.erase(client);

}

void NotifyPlayers(Race* race, const wstring& msg)
{
	for (auto participant : race->participants)
	{
		PrintUserCmdText(participant, msg);
	}
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
			RaceArch race;
			Vector startObjPos;
			Matrix startObjRot;
			while (ini.read_value())
			{
				if (ini.is_value("start_object"))
				{
					startObj = CreateID(ini.get_value_string());
					auto iobj = HkGetInspectObj(startObj);
					if (!iobj)
					{
						ConPrint(L"ERR Racing: Starting object %s does not exist!\n", stows(ini.get_value_string()).c_str());
						break;
					}

					startObjRot = iobj->get_orientation();
					startObjPos = iobj->get_position();
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
					Vector startPos = startObjPos;
					Vector relativePos = { ini.get_value_float(0), ini.get_value_float(1), ini.get_value_float(2) };
					TranslateX(startPos, startObjRot, relativePos.x);
					TranslateY(startPos, startObjRot, relativePos.y);
					TranslateZ(startPos, startObjRot, relativePos.z);
					Transform startingPos = { startPos, startObjRot };
					race.startingPositions.push_back(startingPos);
				}
			}
			if (race.startingPositions.empty())
			{
				ConPrint(L"Race %s failed to load: Has no starting positions defined!", raceName.c_str());
				continue;
			}
			if (race.waypoints.empty())
			{
				ConPrint(L"Race %s failed to load: Has no waypoints defined!", raceName.c_str());
				continue;
			}
			raceObjMap[startObj][raceName] = race;
		}
	}
	ini.close();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool UserCmd_RaceDisband(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto regIter = raceRegistration.find(clientID);
	if (regIter == raceRegistration.end())
	{
		PrintUserCmdText(clientID, L"ERR not registered in a race");
		return false;
	}
	Race* race = regIter->second.first;
	if (clientID != race->hostId)
	{
		PrintUserCmdText(clientID, L"ERR not a host of the race");
		return false;
	}

	for (auto participant : race->participants)
	{
		auto regIter2 = raceRegistration.find(participant);
		if (regIter2 == raceRegistration.end())
		{
			continue;
		}

		int poolEntry = regIter2->second.second;
		if (poolEntry)
		{
			PrintUserCmdText(clientID, L"Race disbanded, $%d credits refunded", regIter2->second.second);
			pub::Player::AdjustCash(participant, regIter2->second.second);
		}
		else
		{
			PrintUserCmdText(clientID, L"Race disbanded");
		}
		raceRegistration.erase(participant);

	}

	raceHostMap.erase(clientID);
	return true;
}

bool UserCmd_RaceInfo(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto regIter = raceRegistration.find(clientID);
	if (regIter == raceRegistration.end())
	{
		PrintUserCmdText(clientID, L"ERR not registered in a race");
		return false;
	}


	Race* race = regIter->second.first;
	wstring playerName = (const wchar_t*)Players.GetActiveCharacterName(race->hostId);
	PrintUserCmdText(clientID, L"Host: %s", playerName.c_str());

	PrintUserCmdText(clientID, L"Track: %s", race->race->raceName.c_str());
	PrintUserCmdText(clientID, L"Laps: %d", race->loopCount);

	PrintUserCmdText(clientID, L"Current pool: $%d credits", race->cashPool);
	PrintUserCmdText(clientID, L"Your contribution: $%d credits", regIter->second.second);
	PrintUserCmdText(clientID, L"Participants:");
	for (auto& participant : race->participants)
	{
		wstring playerName = (const wchar_t*)Players.GetActiveCharacterName(participant);
		PrintUserCmdText(clientID, L"- %s", playerName.c_str());
	}

	return false;
}

bool UserCmd_RaceWithdraw(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto regIter = raceRegistration.find(clientID);
	if (regIter == raceRegistration.end())
	{
		PrintUserCmdText(clientID, L"ERR not registered in a race");
		return false;
	}
	Race* race = regIter->second.first;
	if (race->hostId == clientID)
	{
		PrintUserCmdText(clientID, L"ERR you can't withdraw from your own race, use /race cancel instead");
		return true;
	}

	wstring playerName = (const wchar_t*)Players.GetActiveCharacterName(clientID);
	if (!race->started)
	{
		for (uint index = 0; index < race->participants.size(); ++index)
		{
			if (race->participants[index] == clientID)
			{
				race->participants.erase(race->participants.begin() + index);
				break;
			}
		}
		if (regIter->second.second)
		{
			pub::Player::AdjustCash(clientID, regIter->second.second);
			race->cashPool -= regIter->second.second;
			race->participantSet.erase(clientID);
			PrintUserCmdText(clientID, L"Withdrawn from the race, credits refunded");
			static wchar_t buf[1024];
			_snwprintf(buf, sizeof(buf), L"%s has withdrawn from the race, race credit pool reduced to %d", playerName.c_str(), race->cashPool);

			NotifyPlayers(race, buf);
		}
		else
		{
			static wchar_t buf[1024];
			_snwprintf(buf, sizeof(buf), L"%s has withdrawn from the race", playerName.c_str());

			NotifyPlayers(race, buf);
			PrintUserCmdText(clientID, L"Withdrawn from the race");
		}
	}
	else
	{
		PrintUserCmdText(clientID, L"Withdrawn from the race");
		NotifyPlayers(race, L"%s has withdrawn from the race");
	}
	raceRegistration.erase(regIter);


	return true;
}

bool UserCmd_RaceAddPool(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto regIter = raceRegistration.find(clientID);
	if (regIter == raceRegistration.end())
	{
		PrintUserCmdText(clientID, L"ERR: Not registered to a race!");
		return false;
	}

	int pool = ToInt(GetParam(param, ' ', 1));
	if (pool <= 0)
	{
		PrintUserCmdText(clientID, L"ERR: Invalid value!");
		return false;
	}

	if (pool > Players[clientID].iInspectCash)
	{
		PrintUserCmdText(clientID, L"ERR: You don't have that much money!");
		return false;
	}

	regIter->second.second += pool;
	regIter->second.first->cashPool += pool;

	wstring playerName = (const wchar_t*)Players.GetActiveCharacterName(clientID);
	for (auto& player : regIter->second.first->participants)
	{
		PrintUserCmdText(clientID, L"Notice: %s has added %d credits into the race pool!", playerName.c_str(), pool);
	}

	pub::Player::AdjustCash(clientID, -pool);

	return false;
}

bool UserCmd_RaceSetup(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
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

	if (raceHostMap.count(clientID))
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

	Race pr;
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

	raceHostMap[clientID] = pr;
	raceRegistration[clientID] = { &pr, pr.cashPool };

	return true;
}

bool UserCmd_RaceStart(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto cship = ClientInfo[clientID].cship;
	if (!cship)
	{
		PrintUserCmdText(clientID, L"ERR Not in space!");
		return true;
	}

	auto pendingRace = raceHostMap.find(clientID);
	if (pendingRace == raceHostMap.end(clientID))
	{
		PrintUserCmdText(clientID, L"ERR You're not hosting a race!");
		return true;
	}

	for (auto& participant : pendingRace->second.participants)
	{
		PrintUserCmdText(participant, L"Beaming you into position, best of luck!");
		raceRegistration.erase(participant);
	}

	raceHostMap.erase(clientID);
	return true;
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

	auto raceName = GetParam(param, ' ', 0);
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

	auto& racePos = raceIter->second.startingPositions.begin();
	HkRelocateClient(clientID, racePos->pos, racePos->ori);

	pub::Player::ReturnBestPath(clientID, (uchar*)&bestPath, 12 + (bestPath.waypointCount * 20));

	PrintUserCmdText(clientID, L"Race started!");
	//TODO: Remove the test freeze
	FreezePlayer(clientID, 5.0f);

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
	{ L"/race solo", UserCmd_StartRaceSolo, L"Usage: /race solo" },
	{ L"/race host", UserCmd_RaceSetup, L"Usage: /race host" },
	{ L"/race start", UserCmd_RaceStart, L"Usage: /race start" },
	{ L"/race addpool", UserCmd_RaceAddPool, L"Usage: /race addpool <amount>" },
	{ L"/race withdraw", UserCmd_RaceWithdraw, L"Usage: /race withdraw" },
	{ L"/race disband", UserCmd_RaceDisband, L"Usage: /race disband" },
	//{ L"/startrace", UserCmd_StartRace, L"Usage: /startrace" },
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
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipDestroyed, PLUGIN_ShipDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter, PLUGIN_HkIServerImpl_BaseEnter_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DisconnectDelay, PLUGIN_DelayedDisconnect, 0));

	return p_PI;
}
