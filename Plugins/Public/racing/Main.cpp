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
#include <memory>
#include <PluginUtilities.h>

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

static int raceInternalIdCounter = 0;

struct RaceArch
{
	wstring raceName;
	uint raceStartObj;
	uint raceNum;
	bool loopable = false;
	PathEntry firstWaypoint;
	list<PathEntry> waypoints;
	uint startingSystem;
	vector<Transform> startingPositions;
	float waypointDistance = 70.f;
};

enum class RaceType
{
	TimeTrial,
	Race
};

unordered_map<uint, unordered_map<uint, RaceArch>> raceObjMap;

unordered_map<uint, unordered_map<uint, map<float, wstring>>> scoreboard;

enum class Participant
{
	Racer,
	Disqualified,
	Spectator
};

struct Race;
struct Racer
{
	wstring racerName;
	uint clientId;
	mstime newWaypointTimer;
	list<PathEntry> waypoints;
	int pool;
	Participant participantType;
	vector<mstime> completedWaypoints;
	shared_ptr<Race> race;
};

unordered_map<uint, shared_ptr<Racer>> racersMap;

struct Race
{
	uint hostId;
	uint raceId;
	int cashPool;
	int loopCount = 1;
	unordered_map<uint, shared_ptr<Racer>> participants;
	RaceArch* raceArch;
	bool started = false;
	bool hasWinner = false;
	int startCountdown = 0;
	RaceType raceType = RaceType::Race;
	RequestPathStruct loopWaypointCache;
};

list<shared_ptr<Race>> raceList;
unordered_set<uint> racingEngines;

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

void FreezePlayer(uint client, float time, bool instantStop)
{
	wchar_t buf[30];
	_snwprintf(buf, sizeof(buf), L" Freeze %0.1f %d", time, static_cast<int>(instantStop));
	SendCommand(client, buf);
}

void UnfreezePlayer(uint client)
{
	SendCommand(client, L" Unfreeze cruise");
}

void ToggleRaceMode(uint client, bool newState, float waypointDistance)
{
	wchar_t buf[50];
	_snwprintf(buf, sizeof(buf), L" RaceMode %s %0.0f", newState ? L"on" : L"off", waypointDistance);
	SendCommand(client, buf);
}

void NotifyPlayers(shared_ptr<Race> race, uint exceptionPlayer, wstring msg, ...)
{
	wchar_t wszBuf[1024 * 8] = L"";
	va_list marker;
	va_start(marker, msg);

	_vsnwprintf(wszBuf, (sizeof(wszBuf) / 2) - 1, msg.c_str(), marker);

	for (auto& participant : race->participants)
	{
		if (participant.first == exceptionPlayer)
		{
			continue;
		}
		if (participant.second->participantType == Participant::Disqualified)
		{
			continue;
		}
		PrintUserCmdText(participant.first, wszBuf);
	}
}

float GetFinishTime(shared_ptr<Racer> racer, mstime currTime)
{
	float timeDiff = static_cast<float>(currTime - *racer->completedWaypoints.begin()) / 1000;

	return timeDiff;
}


float GetLapTime(shared_ptr<Racer> racer, mstime currTime)
{
	uint lapSize = racer->race->raceArch->waypoints.size();
	uint currentElem = racer->completedWaypoints.size();
	mstime lapStartTime = racer->completedWaypoints.at(currentElem - lapSize);

	float lapTime = static_cast<float>(currTime - lapStartTime) / 1000;

	return lapTime;
}

void ProcessWinner(shared_ptr<Racer> racer, bool isWinner, mstime currTime)
{
	if (isWinner)
	{
		PrintUserCmdText(racer->clientId, L"You've won the \"%s\" race, %d lap(s) with time of %0.3f!", racer->race->raceArch->raceName.c_str(), racer->race->loopCount, GetFinishTime(racer, currTime));
		int cashPool = racer->race->cashPool;
		if (cashPool)
		{
			pub::Player::AdjustCash(racer->clientId, racer->race->cashPool);
			PrintUserCmdText(racer->clientId, L"Rewarded $%d credits!", racer->race->cashPool);
		}

		NotifyPlayers(racer->race, racer->clientId, L"%s has won the race with time of %0.3f!", racer->racerName.c_str(), GetFinishTime(racer, currTime));

		return;
	}
	
	PrintUserCmdText(racer->clientId, L"You've finished the \"%s\" race, %d lap(s) with time of %0.3f!", racer->race->raceArch->raceName.c_str(), racer->race->loopCount, GetFinishTime(racer, currTime));

}

void BeamPlayers(shared_ptr<Race> race, float freezeTime)
{
	uint positionIndex = 0;
	auto& positionIter = race->raceArch->startingPositions;

	for (auto& participant : race->participants)
	{
		if (participant.second->participantType != Participant::Racer)
		{
			continue;
		}

		uint player = participant.first;
		if (Players[player].iSystemID != race->raceArch->startingSystem)
		{
			PrintUserCmdText(player, L"ERR You're in the wrong system! Unable to beam you into position");
			continue;
		}
		if (race->raceArch->startingPositions.size() < positionIndex)
		{
			PrintUserCmdText(player, L"ERR Unable to find a starting position for you, contact admins/developers!");
			continue;
		}
		auto& startPos = race->raceArch->startingPositions.at(positionIndex++);
		HkRelocateClient(player, startPos.pos, startPos.ori);



		PrintUserCmdText(participant.first, L"Race beginning, beaming you into position!");

		if (freezeTime)
		{
			FreezePlayer(player, freezeTime, true);
		}

		ToggleRaceMode(player, true, race->raceArch->waypointDistance);
	}
}

shared_ptr<Racer> RegisterPlayer(shared_ptr<Race>& race, uint client, int initialPool, bool spectator)
{
	if (race->started && !spectator)
	{
		PrintUserCmdText(client, L"ERR this race is already underway, you can join as a spectator");
		return nullptr;
	}

	initialPool = max(0, initialPool);

	if (racersMap.count(client))
	{
		PrintUserCmdText(client, L"ERR You are already registered to a race");
		return nullptr;
	}

	if (initialPool)
	{
		if (Players[client].iInspectCash < initialPool)
		{
			PrintUserCmdText(client, L"ERR You don't have that much cash!");
			return nullptr;
		}

		PrintUserCmdText(client, L"Added %d to the race reward pool!", initialPool);
		pub::Player::AdjustCash(client, -initialPool);
	}

	shared_ptr<Racer> racer = make_shared<Racer>();
	racer->clientId = client;
	racer->participantType = spectator ? Participant::Spectator : Participant::Racer;
	racer->pool = initialPool;
	race->cashPool += initialPool;
	racer->racerName = (const wchar_t*)Players.GetActiveCharacterName(client);
	racer->race = race;

	race->participants[client] = racer;

	if (!spectator)
	{
		PrintUserCmdText(client, L"Added to the race as racer!");
	}
	else
	{
		PrintUserCmdText(client, L"Added to the race as a spectator!");
	}

	racersMap[client] = racer;

	return racer;
}

shared_ptr<Race> CreateRace(uint hostId, RaceArch& raceArch, int initialPool, int loopCount)
{
	if (loopCount == 0)
	{
		loopCount = 1;
	}

	Race race;

	race.hostId = hostId;
	race.raceId = ++raceInternalIdCounter;
	race.raceArch = &raceArch;
	race.loopCount = loopCount;
	race.cashPool = 0;

	shared_ptr<Race> racePtr = make_shared<Race>(race);
	raceList.push_back(racePtr);
	RegisterPlayer(racePtr, hostId, initialPool, false);

	return racePtr;
}

void DisbandRace(shared_ptr<Race> race)
{

	for (auto& participant : race->participants)
	{
		if (participant.second->participantType == Participant::Disqualified)
		{
			continue;
		}
		int poolEntry = participant.second->pool;
		if (poolEntry)
		{
			PrintUserCmdText(participant.first, L"Race disbanded, $%d credits refunded", poolEntry);
			pub::Player::AdjustCash(participant.first, poolEntry);
		}
		else
		{
			PrintUserCmdText(participant.first, L"Race disbanded");
		}
		ToggleRaceMode(participant.first, false, 250.f);
		racersMap.erase(participant.first);
	}

	for (auto& iter = raceList.begin(); iter != raceList.end(); ++iter)
	{
		if (iter->get()->raceId == race->raceId)
		{
			raceList.erase(iter);
			break;
		}
	}
}

void DisqualifyPlayer(uint client)
{
	auto regIter = racersMap.find(client);
	if (regIter == racersMap.end())
	{
		return;
	}

	auto& racer = regIter->second;
	racer->participantType = Participant::Disqualified;
	if (!racer->race->started)
	{

		pub::Player::AdjustCash(client, racer->pool);
		racer->race->cashPool -= racer->pool;

		if (racer->pool)
		{
			NotifyPlayers(racer->race, racer->clientId, L"%s has withdrawn from the race, race credit pool reduced to %d", 0, racer->racerName.c_str(), racer->race->cashPool);
		}
		else
		{
			NotifyPlayers(racer->race, racer->clientId, L"%s has withdrawn from the race", racer->racerName.c_str());
		}
		
	}

	auto& race = racer->race;

	ToggleRaceMode(client, false, 250.f);
	racersMap.erase(client);

	for (auto& participant : race->participants)
	{
		if (participant.second->participantType == Participant::Racer)
		{
			return;
		}
	}

	DisbandRace(race);
}

void SendFirstWaypoint(shared_ptr<Racer> racer)
{
	racer->waypoints = { racer->race->raceArch->firstWaypoint };

	static RequestPathStruct sendStruct;
	sendStruct.noPathFound = false;
	sendStruct.repId = Players[racer->clientId].iReputation;
	sendStruct.waypointCount = 2;
	sendStruct.pathEntries[0] = racer->race->raceArch->firstWaypoint;
	sendStruct.pathEntries[1] = racer->race->raceArch->firstWaypoint;

	try
	{
		pub::Player::ReturnBestPath(racer->clientId,
			(uchar*)&sendStruct, 12 + (sendStruct.waypointCount * 20));
	}
	catch (...)
	{
		ConPrint(L"DEBUG: %s %d %d %d %s\n", wstos(racer->racerName).c_str(), racer->clientId, racer->race->loopWaypointCache.waypointCount, 12 + (racer->race->loopWaypointCache.waypointCount * 20), racer->race->raceArch->raceName.c_str());
	}
}

void SendLoopWaypoints(shared_ptr<Racer> racer)
{
	racer->waypoints = racer->race->raceArch->waypoints;
	
	static RequestPathStruct sendStruct = racer->race->loopWaypointCache;
	sendStruct.repId = Players[racer->clientId].iReputation;
	try
	{
		pub::Player::ReturnBestPath(racer->clientId,
			(uchar*)&sendStruct, 12 + (sendStruct.waypointCount * 20));
	}
	catch (...)
	{
		ConPrint(L"DEBUG: %s %d %d %d %s\n", wstos(racer->racerName).c_str(), racer->clientId, racer->race->loopWaypointCache.waypointCount, 12 + (racer->race->loopWaypointCache.waypointCount * 20), racer->race->raceArch->raceName.c_str());
	}
}

void BeginRace(shared_ptr<Race> race, int countdown)
{
	auto& waypoints = race->raceArch->waypoints;
	auto& bestPath = race->loopWaypointCache;
	bestPath.noPathFound = false;
	bestPath.waypointCount = race->raceArch->waypoints.size();
	auto iter = waypoints.begin();
	for (int i = 0; i < static_cast<int>(waypoints.size()); ++i)
	{
		bestPath.pathEntries[i] = { *iter };
		iter++;
	}

	for (auto& participant : race->participants)
	{
		SendFirstWaypoint(participant.second);
	}
	
	BeamPlayers(race, static_cast<float>(countdown));
	race->started = true;
	race->startCountdown = countdown;
}

void CheckForHighscore(shared_ptr<Racer> racer, float time)
{
	auto& raceArch = racer->race->raceArch;
	auto& trackScoreboard = scoreboard[raceArch->raceStartObj][raceArch->raceNum];

	constexpr uint SIZE = 50;

	if (trackScoreboard.size() < SIZE)
	{
		trackScoreboard[time] = racer->racerName;
		wchar_t buf[150];
		_snwprintf(buf, sizeof(buf), L"%s has scored a new highscore on %s leaderboard!", racer->racerName.c_str(), racer->race->raceArch->raceName.c_str());
		HkMsgS(Players[racer->clientId].iSystemID, buf);
		return;
	}
	if (trackScoreboard.rbegin()->first > time)
	{
		trackScoreboard[time] = racer->racerName;
		trackScoreboard.erase(std::prev(trackScoreboard.end()));
		wchar_t buf[150];
		_snwprintf(buf, sizeof(buf), L"%s has scored a new highscore on %s leaderboard!", racer->racerName.c_str(), racer->race->raceArch->raceName.c_str());
		HkMsgS(Players[racer->clientId].iSystemID, buf);
		return;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void LoadRacingEngines()
{
	INI_Reader ini;

	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string currDir = string(szCurDir);
	string scFreelancerIniFile = currDir + R"(\freelancer.ini)";

	string gameDir = currDir.substr(0, currDir.length() - 4);
	gameDir += string(R"(\DATA\)");

	if (!ini.open(scFreelancerIniFile.c_str(), false))
	{
		return;
	}

	vector<string> equipFiles;

	while (ini.read_header())
	{
		if (!ini.is_header("Data"))
		{
			continue;
		}
		while (ini.read_value())
		{
			if (ini.is_value("equipment"))
			{
				equipFiles.emplace_back(ini.get_value_string());
			}
		}
	}

	ini.close();

	for (string equipFile : equipFiles)
	{
		equipFile = gameDir + equipFile;
		if (!ini.open(equipFile.c_str(), false))
		{
			continue;
		}

		uint currNickname;
		while (ini.read_header())
		{
			if (!ini.is_header("Engine"))
			{
				continue;
			}
			while (ini.read_value())
			{
				if (ini.is_value("nickname"))
				{
					currNickname = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("racing_engine") && ini.get_value_bool(0))
				{
					racingEngines.insert(currNickname);
				}
			}
		}
		ini.close();
	}
}

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
		if (!ini.is_header("Race"))
		{
			continue;
		}

		uint startObj;
		RaceArch race;
		Vector startObjPos;
		Matrix startObjRot;
		bool firstWaypoint = true;
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
				race.raceName = stows(ini.get_value_string(0));
			}
			else if (ini.is_value("race_num"))
			{
				race.raceNum = ini.get_value_int(0);
			}
			else if (ini.is_value("waypoint_dist"))
			{
				race.waypointDistance = ini.get_value_float(0);
			}
			else if (ini.is_value("loopable"))
			{
				race.loopable = ini.get_value_bool(0);
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
				
				if (firstWaypoint)
				{
					firstWaypoint = false;
					race.firstWaypoint = { obj->cobj->vPos, objId, obj->cobj->system };
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

				if (firstWaypoint)
				{
					firstWaypoint = false;
					race.firstWaypoint = { pos, 0u, system };
					continue;
				}

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
			ConPrint(L"Race %s failed to load: Has no starting positions defined!", race.raceName.c_str());
			continue;
		}
		if (race.waypoints.empty())
		{
			ConPrint(L"Race %s failed to load: Has no waypoints defined!", race.raceName.c_str());
			continue;
		}

		if (race.loopable)
		{
			race.waypoints.push_back(race.firstWaypoint);
		}
		raceObjMap[startObj][race.raceNum] = race;
	}
	ini.close();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool UserCmd_RaceDisband(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto regIter = racersMap.find(clientID);
	if (regIter == racersMap.end())
	{
		PrintUserCmdText(clientID, L"ERR not registered in a race");
		return false;
	}
	auto& race = regIter->second->race;
	if (clientID != race->hostId)
	{
		PrintUserCmdText(clientID, L"ERR not a host of the race");
		return false;
	}

	if (race->started)
	{
		PrintUserCmdText(clientID, L"ERR race is already underway!");
		return false;
	}

	DisbandRace(race);
	return true;
}

bool UserCmd_RaceInfo(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto regIter = racersMap.find(clientID);
	if (regIter == racersMap.end())
	{
		PrintUserCmdText(clientID, L"ERR not registered in a race");
		return false;
	}

	auto& race = regIter->second->race;
	wstring playerName = (const wchar_t*)Players.GetActiveCharacterName(race->hostId);
	PrintUserCmdText(clientID, L"Host: %s", playerName.c_str());

	PrintUserCmdText(clientID, L"Track: %s", race->raceArch->raceName.c_str());
	PrintUserCmdText(clientID, L"Laps: %d", race->loopCount);

	//PrintUserCmdText(clientID, L"Current pool: $%d credits", race->cashPool);
	PrintUserCmdText(clientID, L"Your contribution: $%d credits", regIter->second->pool);
	PrintUserCmdText(clientID, L"Participants:");
	for (auto& participant : race->participants)
	{
		if(participant.second->participantType == Participant::Racer)
		{
			PrintUserCmdText(clientID, L"- %s", participant.second->racerName.c_str());
		}
	}

	return false;
}

bool UserCmd_RaceWithdraw(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto regIter = racersMap.find(clientID);
	if (regIter == racersMap.end())
	{
		PrintUserCmdText(clientID, L"ERR not registered in a race");
		return false;
	}
	auto& racer = regIter->second;
	auto& race = regIter->second->race;

	wstring playerName = (const wchar_t*)Players.GetActiveCharacterName(clientID);
	if (!race->started && racer->pool)
	{
		pub::Player::AdjustCash(clientID, racer->pool);
		race->cashPool -= racer->pool;
		PrintUserCmdText(clientID, L"Withdrawn from the race, credits refunded");

		NotifyPlayers(race, clientID, L"%s has withdrawn from the race, race credit pool reduced to %d", playerName.c_str(), race->cashPool);
	}
	else
	{
		PrintUserCmdText(clientID, L"Withdrawn from the race");
		NotifyPlayers(race, clientID, L"%s has withdrawn from the race", playerName.c_str());
	}

	if (racer->participantType == Participant::Racer)
	{
		racer->participantType = Participant::Disqualified;
	}
	else if (racer->participantType == Participant::Spectator)
	{
		racer->race->participants.erase(racer->clientId);
	}
	ToggleRaceMode(clientID, false, 250.f);
	racersMap.erase(regIter);


	return true;
}

bool UserCmd_RaceAddPool(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	auto regIter = racersMap.find(clientID);
	if (regIter == racersMap.end())
	{
		PrintUserCmdText(clientID, L"ERR: Not registered to a race!");
		return false;
	}

	int pool = ToInt(GetParam(param, ' ', 0));
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

	auto& racer = regIter->second;
	
	racer->pool += pool;
	racer->race->cashPool += pool;

	wstring playerName = (const wchar_t*)Players.GetActiveCharacterName(clientID);
	NotifyPlayers(racer->race, 0, L"Notice: %s has added %d credits into the race pool, current total $%d!", playerName.c_str(), pool, racer->race->cashPool);

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

	if (racersMap.count(clientID))
	{
		PrintUserCmdText(clientID, L"ERR You are already registered in a race");
		return false;
	}

	auto raceObjIter = raceObjMap.find(target->get_id());
	if (raceObjIter == raceObjMap.end())
	{
		PrintUserCmdText(clientID, L"ERR Invalid start point object selected!");
		return true;
	}

	if (HkDistance3D(target->get_position(), cship->vPos) > 5000.f)
	{
		PrintUserCmdText(clientID, L"ERR Too far from the start object!");
		return true;
	}

	auto raceNum = ToUInt(GetParam(param, ' ', 1));
	auto raceIter = raceObjIter->second.find(raceNum);
	if (raceIter == raceObjIter->second.end())
	{
		if (raceObjIter->second.size() == 1)
		{
			raceIter = raceObjIter->second.begin();
		}
		else
		{
			PrintUserCmdText(clientID, L"ERR Invalid race name for selected start! Available races:");
			for (auto& race : raceObjIter->second)
			{
				PrintUserCmdText(clientID, L"%d - %s", race.second.raceNum, race.second.raceName.c_str());
			}
			return true;
		}
	}

	int initialPool = 0;// ToInt(GetParam(param, ' ', 2));
	//if (initialPool < 0)
	//{
	//	initialPool = 0;
	//}
	//if (initialPool && initialPool > Players[clientID].iInspectCash)
	//{
	//	PrintUserCmdText(clientID, L"ERR You don't have enough cash!");
	//	return true;
	//}

	CreateRace(clientID, raceIter->second, initialPool, 1);

	wstring hostName = (const wchar_t*)Players.GetActiveCharacterName(clientID);

	static wchar_t buf[100];
	_snwprintf(buf, sizeof(buf), L"New race: %s started by %s!", raceIter->second.raceName.c_str(), hostName.c_str());
	HkMsgS(raceIter->second.startingSystem, buf);

	if (initialPool)
	{
		_snwprintf(buf, sizeof(buf), L"Initial prize pool: %d credits!", initialPool);
		HkMsgS(raceIter->second.startingSystem, buf);
	}

	return true;
}

bool UserCmd_RaceSetLaps(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{

	auto cship = ClientInfo[clientID].cship;
	if (!cship)
	{
		PrintUserCmdText(clientID, L"ERR Not in space!");
		return true;
	}


	auto regIter = racersMap.find(clientID);
	if (regIter == racersMap.end())
	{
		PrintUserCmdText(clientID, L"ERR: Not registered to a race!");
		return false;
	}

	auto& race = regIter->second->race;
	if (race->hostId != clientID)
	{
		PrintUserCmdText(clientID, L"ERR You're not hosting a race!");
		return true;
	}

	if (!race->raceArch->loopable)
	{
		PrintUserCmdText(clientID, L"ERR This race is not a loop, it cannot have laps set!");
		return true;
	}

	int loops = ToInt(param);

	if (loops <= 0)
	{
		PrintUserCmdText(clientID, L"ERR Invalid input!");
		return true;
	}

	race->loopCount = loops;

	NotifyPlayers(race, 0, L"Race loop count changed to %d!", loops);

	if (loops > 69)
	{
		NotifyPlayers(race, 0, L"Are you SURE about that?");
	}

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


	auto regIter = racersMap.find(clientID);
	if (regIter == racersMap.end())
	{
		PrintUserCmdText(clientID, L"ERR: Not registered to a race!");
		return false;
	}

	if (regIter->second->race->hostId != clientID)
	{
		PrintUserCmdText(clientID, L"ERR You're not hosting a race!");
		return true;
	}

	BeginRace(regIter->second->race, 10);

	return true;
}

bool UserCmd_RaceSolo(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
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

	if (HkDistance3D(target->get_position(), cship->vPos) > 5000.f)
	{
		PrintUserCmdText(clientID, L"ERR Too far from the start object!");
		return true;
	}

	auto raceNum = ToUInt(GetParam(param, ' ', 1));
	auto& raceIter = raceObjIter->second.find(raceNum);
	if (raceIter == raceObjIter->second.end())
	{
		if (raceObjIter->second.size() == 1)
		{
			raceIter = raceObjIter->second.begin();
		}
		else
		{
			PrintUserCmdText(clientID, L"ERR Invalid race name for selected start! Available races:");
			for (auto& race : raceObjIter->second)
			{
				PrintUserCmdText(clientID, L"%d - %s", race.second.raceNum, race.second.raceName.c_str());
			}
			return true;
		}
	}

	auto loopCount = ToUInt(GetParam(param, ' ', 0));

	if (loopCount == 0)
	{
		if (raceIter->second.loopable)
		{
			PrintUserCmdText(clientID, L"ERR Invalid loop count!");
			PrintUserCmdText(clientID, usage);
			return true;
		}
		loopCount = 1;
	}

	if (racersMap.count(clientID))
	{
		PrintUserCmdText(clientID, L"ERR Already in a race!");
		return true;
	}

	auto& race = CreateRace(clientID, raceIter->second, 0, loopCount);
	BeginRace(race, 5);

	return true;
}

bool UserCmd_RaceJoin(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	int pool = ToInt(GetParam(param, ' ', 0));
	wstring spectator = ToLower(GetParam(param, ' ', 1));
	bool isSpectator = spectator.find(L"spec") == 0;

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

	uint targetClientId = target->cobj->ownerPlayer;
	if (!targetClientId)
	{
		PrintUserCmdText(clientID, L"ERR Target not a player!");
		return true;
	}

	auto raceData = racersMap.find(target->cobj->ownerPlayer);
	if (raceData == racersMap.end())
	{
		PrintUserCmdText(clientID, L"ERR Target not in a race!");
		return true;
	}

	auto newRacer = RegisterPlayer(raceData->second->race, clientID, pool, isSpectator);

	if (!newRacer)
	{
		return true;
	}

	static wchar_t buf[100];
	if (isSpectator)
	{
		NotifyPlayers(raceData->second->race, clientID, L"%s is now spectating the race!", newRacer->racerName.c_str());
	}
	else
	{
		NotifyPlayers(raceData->second->race, clientID, L"%s has joined the race!", newRacer->racerName.c_str());
	}

	return true;
}

void ProcessWaypoint(shared_ptr<Racer> racer, mstime currTime)
{
	if (!racer->newWaypointTimer && racer->completedWaypoints.empty())
	{
		racer->newWaypointTimer = currTime;
		return;
	}

	uint completedCount = racer->completedWaypoints.size() + 1;
	if (completedCount % racer->race->raceArch->waypoints.size() == 1) // full loop
	{
		if (completedCount != 1)
		{
			float lapTime = GetLapTime(racer, currTime);
			if (racer->race->raceArch->loopable && racer->race->loopCount > 1)
			{
				PrintUserCmdText(racer->clientId, L"Completed a lap! %d/%d, time %0.3fs", completedCount / racer->race->raceArch->waypoints.size(), racer->race->loopCount, lapTime);
			}
			CheckForHighscore(racer, lapTime);
		}
		racer->newWaypointTimer = currTime;
	}

	if (completedCount == (racer->race->loopCount * racer->race->raceArch->waypoints.size()) + 1)
	{
		float timeDiff = static_cast<float>(currTime - *racer->completedWaypoints.begin()) / 1000;

		if (!racer->race->hasWinner)
		{
			ProcessWinner(racer, true, currTime);
			racer->race->hasWinner = true;
		}
		else
		{
			ProcessWinner(racer, false, currTime);
		}

		ToggleRaceMode(racer->clientId, false, 250.f);
		racersMap.erase(racer->clientId);
		return;
	}

	racer->waypoints.erase(racer->waypoints.begin());
	if (racer->completedWaypoints.empty())
	{
		PrintUserCmdText(racer->clientId, L"Time counting started");
	}
	else
	{
		float timeDiff = static_cast<float>(currTime - *racer->completedWaypoints.rbegin()) / 1000;
		PrintUserCmdText(racer->clientId, L"Checkpoint #%d: %0.3fs (+%0.3fs)", racer->completedWaypoints.size(), GetFinishTime(racer, currTime), timeDiff);
	}
	racer->completedWaypoints.push_back(currTime);

}

void UpdateRacers()
{

	mstime currTime = timeInMS();
	for (auto& racerData = racersMap.begin(); racerData != racersMap.end();racerData++)
	{
		auto cship = ClientInfo[racerData->first].cship;
		if (!cship)
		{
			PrintUserCmdText(racerData->first, L"You died or docked and got disqualified!");
			DisqualifyPlayer(racerData->first);
			continue;
		}

		if (racerData->second->waypoints.empty())
		{
			if (racerData->second->newWaypointTimer + 1000 > currTime)
			{
				continue;
			}
			SendLoopWaypoints(racerData->second);
		}
	}
}

void UpdateRaces()
{
	for (auto& race : raceList)
	{
		if (!race->startCountdown)
		{
			continue;
		}

		--race->startCountdown;
		if (race->startCountdown)
		{

			for (auto& participant : race->participants)
			{
				if (participant.second->participantType == Participant::Disqualified)
				{
					continue;
				}
				PrintUserCmdText(participant.first, L"%d...", race->startCountdown);
			}
			continue;
		}

		for (auto& participant : race->participants)
		{
			if (participant.second->participantType == Participant::Disqualified)
			{
				continue;
			}
			if (participant.second->participantType == Participant::Racer)
			{
				UnfreezePlayer(participant.first);
			}
			PrintUserCmdText(participant.first, L"GO GO GO!");
		}

		race->started = true;
	}
}

int Update()
{
	returncode = DEFAULT_RETURNCODE;
	static bool firstRun = true;
	if (firstRun)
	{
		firstRun = false;
		LoadSettings();
		LoadRacingEngines();
	}
	if (!racersMap.empty())
	{
		UpdateRacers();
	}

	returncode = DEFAULT_RETURNCODE;
	return 0;
}

void Timer()
{
	returncode = DEFAULT_RETURNCODE;

	if (!raceList.empty())
	{
		UpdateRaces();
	}
}

bool UserCmd_RaceHelp(uint clientID, const wstring& cmd, const wstring& param, const wchar_t* usage)
{
	PrintUserCmdText(clientID, L"/race solo <lapCount> [raceNum]");
	PrintUserCmdText(clientID, L"/race host");
	PrintUserCmdText(clientID, L"/race setlaps <lapCount>");
	PrintUserCmdText(clientID, L"/race start");
	PrintUserCmdText(clientID, L"/race disband");
	PrintUserCmdText(clientID, L"/race withdraw");
	PrintUserCmdText(clientID, L"/race info");

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
	{ L"/race solo", UserCmd_RaceSolo, L"Usage: /race solo <lapCount> [raceNum]" },
	{ L"/race join", UserCmd_RaceJoin, L"Usage: /race join <poolCashAmount> [spectator]" },
	{ L"/race setlaps", UserCmd_RaceSetLaps, L"Usage: /race setlaps <numberOfLaps>" },
	{ L"/race host", UserCmd_RaceSetup, L"Usage: /race host" },
	{ L"/race start", UserCmd_RaceStart, L"Usage: /race start" },
	//{ L"/race addpool", UserCmd_RaceAddPool, L"Usage: /race addpool <amount>" },
	{ L"/race withdraw", UserCmd_RaceWithdraw, L"Usage: /race withdraw" },
	{ L"/race disband", UserCmd_RaceDisband, L"Usage: /race disband" },
	{ L"/race info", UserCmd_RaceInfo, L"Usage: /race info" },
	{ L"/race", UserCmd_RaceHelp, L"Usage: /race info" },
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

void __stdcall CharacterSelect(struct CHARACTER_ID const& charId, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	DisqualifyPlayer(client);
}

void __stdcall ShipDestroyed(IObjRW* ship, bool isKill, uint killerId)
{
	returncode = DEFAULT_RETURNCODE;
	if (ship->is_player())
	{
		DisqualifyPlayer(ship->cobj->ownerPlayer);
	}
}

void __stdcall BaseEnter(unsigned int baseId, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	DisqualifyPlayer(client);
}

void DelayedDisconnect(uint client, uint ship)
{
	returncode = DEFAULT_RETURNCODE;
	DisqualifyPlayer(client);
}

void __stdcall ShipExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;
	
	uint clientId = iobj->cobj->ownerPlayer;
	if (!clientId || dmg->damageCause != DamageCause::CruiseDisrupter)
	{
		return;
	}

	auto& racer = racersMap.find(clientId);
	if (racer != racersMap.end() && racer->second->race->started)
	{
		dmg->damageCause = DamageCause::MissileTorpedo;
		return;
	}

	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);

	CEquip* engine = cship->equip_manager.FindFirst(EquipmentClass::Engine);
	if (!engine)
	{
		return;
	}

	if (racingEngines.count(engine->archetype->iArchID))
	{
		FreezePlayer(clientId, 8.0f, false);
	}
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

void __stdcall SubmitChat(CHAT_ID cId, unsigned long size, const DWORD* rdlReader, CHAT_ID cIdTo, int p2)
{
	returncode = DEFAULT_RETURNCODE;

	const uint* rdlUint = reinterpret_cast<const uint*>(rdlReader);
	if (!cId.iID || size != 16 || rdlUint[0] != 0x12AC3)
	{
		return;
	}

	returncode = SKIPPLUGINS_NOFUNCTIONCALL;

	auto racerIter = racersMap.find(cId.iID);
	if (racerIter == racersMap.end())
	{
		return;
	}
	auto& racer = racerIter->second;

	CShip* cship = ClientInfo[racer->clientId].cship;
	if (!cship)
	{
		return;
	}

	const float* rdlFloat = reinterpret_cast<const float*>(rdlReader);
	Vector pos = { rdlFloat[1], rdlFloat[2], rdlFloat[3] };
	auto currWaypoint = racer->waypoints.begin();

	if (cship->system != currWaypoint->systemId || HkDistance3D(pos, cship->vPos) > 750)
	{
		PrintUserCmdText(racer->clientId, L"ERR Disqualified due to skipping a waypoint or position desync");
		DisqualifyPlayer(racer->clientId);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return;
	}

	mstime currTime = timeInMS();
	ProcessWaypoint(racer, currTime);

	returncode = SKIPPLUGINS_NOFUNCTIONCALL;
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
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Timer, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SubmitChat, PLUGIN_HkIServerImpl_SubmitChat, 5));
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipExplosionHit, PLUGIN_ExplosionHit, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipDestroyed, PLUGIN_ShipDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter, PLUGIN_HkIServerImpl_BaseEnter_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DelayedDisconnect, PLUGIN_DelayedDisconnect, 0));

	return p_PI;
}
