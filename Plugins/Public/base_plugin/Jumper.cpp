// AlleyPlugin for FLHookPlugin
// January 2015 by Alley
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
#include <float.h>
#include <FLHook.h>
#include <plugin.h>
#include <list>
#include <unordered_set>

#include <PluginUtilities.h>
#include "Main.h"
#include <random>


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Structures and shit yo
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

unordered_set<uint> unchartedSystems;
unordered_map<wstring, time_t> unchartedDisconnects;
uint unchartedSystemToExclude;
uint set_unchartedDeathGracePeriod = 300;
unordered_set<uint> HyperJump::markedForDeath;

struct SYSTEMJUMPCOORDS
{
	uint system;
	Vector pos;
	Matrix ornt;
};

int GetRandom()
{
	static mt19937 mt = mt19937(random_device()());
	return mt();
}

void HyperJump::CharacterSelect_AFTER(uint client)
{
	if (!Players[client].iBaseID && unchartedSystems.count(Players[client].iSystemID))
	{
		wstring name = (const wchar_t*)Players.GetActiveCharacterName(client);
		auto entryIter = unchartedDisconnects.find(name);
		if (entryIter != unchartedDisconnects.end() && entryIter->second > time(0))
		{
			return;
		}
		markedForDeath.insert(client);
	}
}

void HyperJump::CheckForUnchartedDisconnect(uint client, uint ship)
{
	if (set_SkipUnchartedKill)
	{
		return;
	}
	if (unchartedSystems.count(Players[client].iSystemID) && Players[client].iShipID)
	{
		auto currTime = time(0);
		if (set_unchartedDeathGracePeriod)
		{
			wstring name = (const wchar_t*)Players.GetActiveCharacterName(client);
			unchartedDisconnects[name] = currTime + set_unchartedDeathGracePeriod;
		}
		else
		{
			pub::SpaceObj::SetRelativeHealth(ship, 0.0f);
		}
	}
}

void HyperJump::InitJumpHole(uint baseId, uint destSystem, uint destObject)
{
	StarSystem* dunno;
	IObjRW* inspect;
	GetShipInspect(baseId, inspect, dunno);
	CSolar* solar = (CSolar*)inspect->cobject();

	solar->jumpDestSystem = destSystem;
	solar->jumpDestObj = destObject;
}

bool SetupCustomExitHole(PlayerBase* pb, SYSTEMJUMPCOORDS& coords, uint exitJumpHoleLoadout, uint exitJumpHoleArchetype)
{
	static uint counter = 0;
	auto systemInfo = Universe::get_system(coords.system);
	if (!systemInfo)
	{
		return false;
	}
	string baseNickName = "custom_return_hole_exit_" + (string)systemInfo->nickname.value + itos(counter);
	counter++;

	if (pub::SpaceObj::ExistsAndAlive(CreateID(baseNickName.c_str())) == 0) //0 means alive, -2 dead
	{
		return false;
	}

	SPAWN_SOLAR_STRUCT info;
	info.iSystemId = coords.system;
	info.pos = coords.pos;
	info.ori = coords.ornt;
	info.nickname = baseNickName;
	info.loadoutArchetypeId = exitJumpHoleLoadout;
	info.solarArchetypeId = exitJumpHoleArchetype;
	info.solar_ids = 267199;

	CreateSolar::CreateSolarCallout(&info);

	pb->destObject = info.iSpaceObjId;
	pb->destObjectName = baseNickName;
	pb->destSystem = coords.system;

	StarSystem* dunno;
	IObjRW* inspect;
	GetShipInspect(info.iSpaceObjId, inspect, dunno);
	CSolar* solar = (CSolar*)inspect->cobject();

	solar->jumpDestSystem = pb->destSystem;
	solar->jumpDestObj = pb->destObject;

	pb->baseCSolar->jumpDestSystem = solar->system;
	pb->baseCSolar->jumpDestObj = solar->id;

	customSolarList.insert(info.iSpaceObjId);
	return true;
}

void HyperJump::InitJumpHoleConfig()
{
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string cfg_filehyperspaceHub = (string)szCurDir + R"(\flhook_plugins\base_hyperspacehub.cfg)";
	uint exitJumpHoleArchetype = CreateID("jumphole_noentry");
	uint exitJumpHoleLoadout = CreateID("wormhole_unstable");
	exitJumpHoleArchetype = CreateID(IniGetS(cfg_filehyperspaceHub, "general", "exitJumpHoleArchetype", "jumphole_noentry").c_str());
	exitJumpHoleLoadout = CreateID(IniGetS(cfg_filehyperspaceHub, "general", "exitJumpHoleLoadout", "wormhole_unstable").c_str());

	vector<PlayerBase*> invalidJumpHoles;
	for (auto& base : player_bases)
	{
		bool completedLoad = false;
		PlayerBase* pbase = base.second;
		if (!pbase->archetype->isjump)
		{
			continue;
		}

		if (pbase->archetype->ishubreturn)
		{
			SYSTEMJUMPCOORDS coords = { pbase->destSystem, pbase->destPos, pbase->destOri };
			completedLoad = SetupCustomExitHole(pbase, coords, exitJumpHoleLoadout, exitJumpHoleArchetype);
		}
		else if (pub::SpaceObj::ExistsAndAlive(pbase->destObject) == 0) // method returns 0 for alive, -2 otherwise
		{
			completedLoad = true;
			InitJumpHole(base.first, pbase->destSystem, pbase->destObject);
		}
		
		if (!completedLoad)
		{
			invalidJumpHoles.emplace_back(pbase);
		}
	}

	for (auto pbase : invalidJumpHoles)
	{
		wstring fileName = stows(pbase->path.substr(pbase->path.find_last_of('\\') + 1));
		ConPrint(L"ERROR: Jump Base %ls's jump target/target system does not exist, despawning it to prevent issues\ntargetObject: %u, targetSystem: %u\nfilename: %ls\n", stows(pbase->nickname).c_str(), pbase->destObject, pbase->destSystem, fileName.c_str());
		pbase->base_health = 0;
		CoreModule(pbase).SpaceObjDestroyed(CoreModule(pbase).space_obj, false, false);
	}
}

struct DeepRegion
{
	string name;
	uint cooldown = 0;
	string openArch;
	string openLoadout;
	string closeArch;
	string closeLoadout;
	vector<uint> entriesToRandomize;
};

void HyperJump::LoadHyperspaceHubConfig(const string& configPath)
{

	string cfg_filejumpMap = configPath + R"(\flhook_plugins\jump_allowedsystems.cfg)";
	string cfg_filehyperspaceHub = configPath + R"(\flhook_plugins\base_hyperspacehub.cfg)";
	string cfg_filehyperspaceHubTimer = configPath + R"(\flhook_plugins\base_hyperspacehubtimer.cfg)";
	vector<uint> legalReturnSystems;
	vector<uint> returnJumpHoles;
	vector<uint> hubToUnchartedJumpHoles;
	vector<uint> unchartedToHubJumpHoles;
	static map<uint, vector<SYSTEMJUMPCOORDS>> mapSystemJumps;
	uint lastJumpholeRandomization = 0;
	uint randomizationCooldown = 3600 * 23;
	uint randomizationCooldownOffset = 3600 * 9;
	vector<DeepRegion> deepRegionVector;
	INI_Reader ini;
	map<string, pair<uint, uint>> deepRegionTimerMap;

	if (ini.open(cfg_filehyperspaceHubTimer.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("Timer")) {
				while (ini.read_value())
				{
					if (ini.is_value("lastRandomization"))
					{
						lastJumpholeRandomization = ini.get_value_int(0);
					}
					else if (ini.is_value("randomizationCooldown"))
					{
						randomizationCooldown = ini.get_value_int(0);
					}
					else if (ini.is_value("randomizationCooldownOffset"))
					{
						randomizationCooldownOffset = ini.get_value_int(0);
					}
					else if (ini.is_value("systemToExclude"))
					{
						unchartedSystemToExclude = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("disconnectGracePeriod"))
					{
						set_unchartedDeathGracePeriod = ini.get_value_int(0);
					}
					else
					{
						deepRegionTimerMap[ini.get_name_ptr()] = { CreateID(ini.get_value_string(0)), ini.get_value_int(1) };
					}
				}
			}
			else if (ini.is_header("uncharted_systems"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("system"))
					{
						unchartedSystems.insert(CreateID(ini.get_value_string(0)));
					}
				}
			}
		}

		ini.close();
	}

	time_t currTime = time(0);

	if (ini.open(cfg_filejumpMap.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("system_jump_positions")) {
				while (ini.read_value())
				{
					if (ini.is_value("jump_position"))
					{
						SYSTEMJUMPCOORDS coords;
						coords.system = CreateID(ini.get_value_string(0));
						coords.pos = { ini.get_value_float(1), ini.get_value_float(2), ini.get_value_float(3) };

						Vector erot = { ini.get_value_float(4), ini.get_value_float(5), ini.get_value_float(6) };
						coords.ornt = EulerMatrix(erot);

						mapSystemJumps[coords.system].push_back(coords);
					}
				}
			}
		}

		ini.close();
	}

	if (ini.open(cfg_filehyperspaceHub.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("deep_region_group"))
			{
				DeepRegion dr;
				while (ini.read_value())
				{
					if (ini.is_value("name"))
					{
						dr.name = ini.get_value_string();
					}
					else if (ini.is_value("cooldown"))
					{
						dr.cooldown = ini.get_value_int(0);
					}
					else if (ini.is_value("openarch"))
					{
						dr.openArch = ini.get_value_string();
					}
					else if (ini.is_value("openloadout"))
					{
						dr.openLoadout = ini.get_value_string();
					}
					else if (ini.is_value("closedarch"))
					{
						dr.closeArch = ini.get_value_string();
					}
					else if (ini.is_value("closedloadout"))
					{
						dr.closeLoadout = ini.get_value_string();
					}
					else if (ini.is_value("entrance"))
					{
						uint baseId = CreateID(ini.get_value_string());
						auto baseIter = player_bases.find(baseId);
						auto timerIter = deepRegionTimerMap.find(dr.name);
						if (timerIter != deepRegionTimerMap.end() && (baseIter == player_bases.end() || baseIter->second->destSystem == timerIter->second.first))
						{
							ConPrint(L"Skipped %x\n", timerIter->second.first);
							continue;
						}
						dr.entriesToRandomize.push_back(CreateID(ini.get_value_string()));
					}
				}
				deepRegionVector.push_back(dr);
			}
			else if (ini.is_header("return_system_data"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("return_system"))
					{
						legalReturnSystems.push_back(CreateID(ini.get_value_string(0)));
					}
				}
			}
			else if (ini.is_header("return_jump_bases"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("base"))
					{
						string nickname = ini.get_value_string(0);
						uint nicknameHash = CreateID(nickname.c_str());
						if (!player_bases.count(nicknameHash))
						{
							ConPrint(L"HYPERSPACE HUB: Warning! Return jumphole %s not found, check config!\n", stows(nickname).c_str());
							continue;
						}
						returnJumpHoles.push_back(nicknameHash);
					}
				}
			}
			else if (ini.is_header("uncharted_return_jump_bases"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("base"))
					{
						string nickname = ini.get_value_string(0);
						uint nicknameHash = CreateID(nickname.c_str());
						if (!player_bases.count(nicknameHash))
						{
							ConPrint(L"HYPERSPACE HUB: Warning! Uncharted to Hub jumphole %s not found, check config!\n", stows(nickname).c_str());
							continue;
						}
						if (player_bases.at(nicknameHash)->system == unchartedSystemToExclude)
						{
							continue;
						}
						unchartedToHubJumpHoles.push_back(nicknameHash);
					}
				}
			}
			else if (ini.is_header("uncharted_jump_bases"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("base"))
					{
						string nickname = ini.get_value_string(0);
						uint nicknameHash = CreateID(nickname.c_str());
						if (!player_bases.count(nicknameHash))
						{
							ConPrint(L"HYPERSPACE HUB: Warning! Hub to Uncharted jumphole %s not found, check config!\n", stows(nickname).c_str());
							continue;
						}
						hubToUnchartedJumpHoles.push_back(nicknameHash);
					}
				}
			}
		}

		ini.close();
	}

	if (returnJumpHoles.size() > legalReturnSystems.size()
		|| hubToUnchartedJumpHoles.size() > unchartedToHubJumpHoles.size())
	{
		ConPrint(L"HYPERSPACE HUB: ERROR! more random jump bases than distinct available destinations, aborting randomization!\n");
		return;
	}

	if (lastJumpholeRandomization + randomizationCooldown < currTime)
	{
		ConPrint(L"HYPERSPACE HUB: randomizing connections\n");
		for (uint returnJH : returnJumpHoles)
		{

			PlayerBase* pb = player_bases[returnJH];
			uint index = GetRandom() % legalReturnSystems.size();
			if (mapSystemJumps.count(legalReturnSystems.at(index)) == 0)
			{
				ConPrint(L"HYPERSPACE HUB: Jump Point data for return system not found, aborting randomization!\n");
				continue;
			}
			const auto& coordsList = mapSystemJumps[legalReturnSystems.at(index)];
			const auto& coords = coordsList.at(GetRandom() % coordsList.size());

			pb->destSystem = coords.system;
			pb->destPos = coords.pos;
			pb->destOri = coords.ornt;

			const auto& systemInfo = Universe::get_system(coords.system);
			pb->basename = HkGetWStringFromIDS(systemInfo->strid_name) + L" Jump Hole";
			legalReturnSystems.erase(legalReturnSystems.begin() + index);

			pb->Save();
			RespawnBase(pb);
		}

		bool isFirst = true;
		for (uint unchartedJH : hubToUnchartedJumpHoles)
		{
			PlayerBase* originJumpHole = player_bases[unchartedJH];
			uint randomizedIndex = GetRandom() % unchartedToHubJumpHoles.size();
			uint randomizedTarget = unchartedToHubJumpHoles.at(randomizedIndex);
			auto targetJumpHole = player_bases.at(randomizedTarget);

			originJumpHole->destObject = CreateID(targetJumpHole->nickname.c_str());
			originJumpHole->destObjectName = targetJumpHole->nickname;
			originJumpHole->destSystem = targetJumpHole->system;

			auto unchartedSystemInfo = Universe::get_system(targetJumpHole->system);
			originJumpHole->basename = L"Unstable " + HkGetWStringFromIDS(unchartedSystemInfo->strid_name) + L" Jump Hole";

			targetJumpHole->destObject = CreateID(originJumpHole->nickname.c_str());
			targetJumpHole->destObjectName = originJumpHole->nickname;
			targetJumpHole->destSystem = originJumpHole->system;

			auto& selectedSystemCoordList = mapSystemJumps[targetJumpHole->system];
			if (selectedSystemCoordList.empty())
			{
				AddLog("Exception: Unable to form a JH to %u system\n", targetJumpHole->system);
				continue;
			}
			auto& coords = selectedSystemCoordList.at(GetRandom() % selectedSystemCoordList.size());
			targetJumpHole->position = coords.pos;
			targetJumpHole->rotation = coords.ornt;

			auto originSystemInfo = Universe::get_system(originJumpHole->system);
			targetJumpHole->basename = L"Unstable " + HkGetWStringFromIDS(originSystemInfo->strid_name) + L" Jump Hole";


			unchartedToHubJumpHoles.erase(unchartedToHubJumpHoles.begin() + randomizedIndex);

			originJumpHole->Save();
			targetJumpHole->Save();
			RespawnBase(originJumpHole);
			RespawnBase(targetJumpHole);

			if (isFirst)
			{
				isFirst = false;
				auto systemInfo = Universe::get_system(originJumpHole->destSystem);
				WritePrivateProfileStringA("Timer", "systemToExclude", systemInfo->nickname.value, cfg_filehyperspaceHubTimer.c_str());
			}
		}
		WritePrivateProfileStringA("Timer", "lastRandomization", itos((int)(currTime - (currTime % randomizationCooldown) + randomizationCooldownOffset)).c_str(), cfg_filehyperspaceHubTimer.c_str());
	}

	for (DeepRegion& dr : deepRegionVector)
	{
		auto timerIter = deepRegionTimerMap.find(dr.name);
		if (timerIter != deepRegionTimerMap.end() && currTime < timerIter->second.second + dr.cooldown)
		{
			continue;
		}

		int entry = GetRandom() % dr.entriesToRandomize.size();
		int counter = -1;
		uint baseID = dr.entriesToRandomize.at(entry);
		if (timerIter->second.first == baseID)
		{
			entry = (entry + 1) % dr.entriesToRandomize.size();
		}

		for (uint baseID : dr.entriesToRandomize)
		{
			counter++;
			auto baseIter = player_bases.find(baseID);
			if (baseIter == player_bases.end())
			{
				ConPrint(L"HYPERSPACE HUB: Deep Region %s entrance of %x not found!\n", stows(dr.name).c_str(), baseID);
				continue;
			}

			PlayerBase* base = baseIter->second;
			auto connectedBaseIter = player_bases.find(base->destObject);
			if (connectedBaseIter == player_bases.end())
			{
				ConPrint(L"HYPERSPACE HUB: Deep Region %s exitpoint of %s not found!\n", stows(dr.name).c_str(), base->basename.c_str());
				continue;
			}

			auto entrySysInfo = Universe::get_system(base->system);
			auto exitSysInfo = Universe::get_system(connectedBaseIter->second->system);

			auto entrySysInfoName = HkGetWStringFromIDS(entrySysInfo->strid_name);
			auto exitSysInfoName = HkGetWStringFromIDS(exitSysInfo->strid_name);
			if (counter == entry)
			{
				base->basesolar = dr.openArch;
				base->baseloadout = dr.openLoadout;
				base->basename = exitSysInfoName + L" Jump Hole";
				connectedBaseIter->second->basesolar = dr.openArch;
				connectedBaseIter->second->baseloadout = dr.openLoadout;
				connectedBaseIter->second->basename = entrySysInfoName + L" Jump Hole";


				auto systemInfo = Universe::get_system(base->destSystem);
				string entryVar = string(systemInfo->nickname.value) + ", " + itos((int)currTime);
				WritePrivateProfileStringA("Timer", dr.name.c_str(), entryVar.c_str(), cfg_filehyperspaceHubTimer.c_str());
				ConPrint(L"Spawned leading to %s\n", stows(systemInfo->nickname.value).c_str());
			}
			else
			{
				base->basesolar = dr.closeArch;
				base->baseloadout = dr.closeLoadout;
				base->basename = L"Unaligned " + exitSysInfoName + L" Jump Hole";
				connectedBaseIter->second->basesolar = dr.closeArch;
				connectedBaseIter->second->baseloadout = dr.closeLoadout;
				connectedBaseIter->second->basename = L"Unaligned " + entrySysInfoName + L" Jump Hole";
			}
			base->Save();
			connectedBaseIter->second->Save();
			RespawnBase(base);
			RespawnBase(connectedBaseIter->second);
		}
	}
}