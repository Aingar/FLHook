// PvE Controller for Discovery FLHook
// April 2020 by Kazinsal etc.
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.


#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <random>

#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

using namespace std;

PLUGIN_RETURNCODE returncode;

#define PLUGIN_DEBUG_NONE 0
#define PLUGIN_DEBUG_CONSOLE 1
#define PLUGIN_DEBUG_VERBOSE 2
#define PLUGIN_DEBUG_VERYVERBOSE 3


struct stBountyBasePayout {
	int iBasePayout;
};

struct stDropInfo {
	uint uGoodID;
	float fChance;
	uint uAmountDroppedMin;
	uint uAmountDroppedMax;
};

struct stWarzone {
	uint uFaction1;
	uint uFaction2;
	float fMultiplier;
};

unordered_map<uint, stBountyBasePayout> mapBountyPayouts;
unordered_map<uint, stBountyBasePayout> mapBountyShipPayouts;
unordered_map<uint, float> mapBountyGroupScale;
unordered_map<uint, float> mapBountyArmorScales;
unordered_map<uint, float> mapBountySystemScales;

multimap<uint, stDropInfo> mmapDropInfo;
unordered_set<uint> mapDropExcludedArchetypes;
unordered_map<uint, uint> mapShipClassTypes;
unordered_map<int, float> mapClassDiffMultipliers;

int set_iPluginDebug = 0;
float set_fMaximumRewardRep = 0.0f;
uint set_uLootCrateID = 0;

bool set_bBountiesEnabled = true;
int set_iPoolPayoutTimer = 0;
int iLoadedNPCBountyClasses = 0;
int iLoadedNPCShipBountyOverrides = 0;
int iLoadedNPCBountyGroupScale = 0;
int iLoadedNPCBountySystemScales = 0;
int iLoadedClassTypes = 0;
int iLoadedClassDiffMultipliers = 0;
void LoadSettingsNPCBounties(void);

float set_groupDistance = 20000.f;

bool set_bDropsEnabled = true;
int iLoadedNPCDropClasses = 0;
void LoadSettingsNPCDrops(void);

/// Load settings.
void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\pvecontroller.cfg";

	// Load generic settings
	set_iPluginDebug = IniGetI(scPluginCfgFile, "General", "debug", 0);
	set_fMaximumRewardRep = IniGetF(scPluginCfgFile, "General", "maximum_reward_rep", 0.0f);
	set_uLootCrateID = CreateID(IniGetS(scPluginCfgFile, "NPCDrops", "drop_crate", "lootcrate_ast_loot_metal").c_str());

	// Load settings blocks
	LoadSettingsNPCBounties();
	LoadSettingsNPCDrops();

	//stop NPCs from disabling thrusters when faced by the player
	BYTE nop[] = { 0x90 ,0x90 ,0x90 };
	WriteProcMem((void*)0x62C6220, nop, 3);
}

void LoadSettingsNPCBounties()
{
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\pvecontroller.cfg";

	// Clear the bounty tables
	mapBountyPayouts.clear();
	iLoadedNPCBountyClasses = 0;
	mapBountyShipPayouts.clear();
	iLoadedNPCShipBountyOverrides = 0;
	mapBountyGroupScale.clear();
	iLoadedNPCBountyGroupScale = 0;
	mapBountySystemScales.clear();
	iLoadedNPCBountySystemScales = 0;
	mapShipClassTypes.clear();
	iLoadedClassTypes = 0;
	mapClassDiffMultipliers.clear();
	iLoadedClassDiffMultipliers = 0;

	// Load ratting bounty settings
	set_iPoolPayoutTimer = IniGetI(scPluginCfgFile, "NPCBounties", "pool_payout_timer", 0);

	// Load the big stuff
	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("NPCBounties"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("enabled"))
					{
						if (ini.get_value_int(0) == 0)
							set_bBountiesEnabled = false;
					}
					else if (ini.is_value("group_range"))
					{
						set_groupDistance = ini.get_value_float(0);
					}
					else if (ini.is_value("group_scale"))
					{
						mapBountyGroupScale[ini.get_value_int(0)] = ini.get_value_float(1);
						++iLoadedNPCBountyGroupScale;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded group scale multiplier %u, %f.\n", ini.get_value_int(0), ini.get_value_float(1));
					}
					else if (ini.is_value("class"))
					{
						int iClass = ini.get_value_int(0);
						mapBountyPayouts[iClass].iBasePayout = ini.get_value_int(1);
						++iLoadedNPCBountyClasses;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded class base value %u, $%d.\n", iClass, mapBountyPayouts[iClass].iBasePayout);
					}
					else if (ini.is_value("ship"))
					{
						uint uShiparchHash = CreateID(ini.get_value_string(0));
						mapBountyShipPayouts[uShiparchHash].iBasePayout = ini.get_value_int(1);
						++iLoadedNPCShipBountyOverrides;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded override for \"%s\" == %u, $%d.\n", stows(ini.get_value_string(0)).c_str(), uShiparchHash, mapBountyShipPayouts[uShiparchHash].iBasePayout);
					}
					else if (ini.is_value("system_multiplier"))
					{
						uint uSystemHash = CreateID(ini.get_value_string(0));
						mapBountySystemScales[uSystemHash] = ini.get_value_float(1);
						++iLoadedNPCBountySystemScales;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded system scale multiplier for \"%s\" == %u, %f.\n", stows(ini.get_value_string(0)).c_str(), uSystemHash, ini.get_value_float(1));
					}
					else if (ini.is_value("class_type"))
					{
						for (uint i = 1; i <= ini.get_num_parameters() - 1; i++) {
							mapShipClassTypes[ini.get_value_int(i)] = ini.get_value_int(0);
							++iLoadedClassTypes;
							if (set_iPluginDebug)
								ConPrint(L"PVECONTROLLER: Loaded ship class (%u) as type (%u) \n", ini.get_value_int(i), ini.get_value_int(0));
						}
					}
					else if (ini.is_value("class_diff"))
					{
						mapClassDiffMultipliers[ini.get_value_int(0)] = ini.get_value_float(1);
						++iLoadedClassDiffMultipliers;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded class difference multiplier for %i == %f.\n", ini.get_value_int(0), ini.get_value_float(1));
					}

				}
			}

		}
		ini.close();
	}

	if (set_iPluginDebug)
	{
		ConPrint(L"PVECONTROLLER: NPC bounties are %s.\n", set_bBountiesEnabled ? L"enabled" : L"disabled");
		ConPrint(L"PVECONTROLLER: Loaded %u NPC bounty group scale values.\n", iLoadedNPCBountyGroupScale);
		ConPrint(L"PVECONTROLLER: Loaded %u NPC bounty classes.\n", iLoadedNPCBountyClasses);
		ConPrint(L"PVECONTROLLER: Loaded %u NPC bounty ship overrides.\n", iLoadedNPCShipBountyOverrides);
		ConPrint(L"PVECONTROLLER: Loaded %u NPC bounty system scale multipliers.\n", iLoadedNPCBountySystemScales);
		ConPrint(L"PVECONTROLLER: Loaded %u ship class types.\n", iLoadedClassTypes);
		ConPrint(L"PVECONTROLLER: Loaded %u NPC bounty class difference multipliers.\n", iLoadedClassDiffMultipliers);
	}
}

void LoadSettingsNPCDrops()
{
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\pvecontroller.cfg";

	// Clear the drop tables.
	mmapDropInfo.clear();
	iLoadedNPCDropClasses = 0;

	// Load the big stuff
	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("NPCDrops"))
			{
				while (ini.read_value())
				{
					if (!strcmp(ini.get_name_ptr(), "enabled"))
					{
						if (ini.get_value_int(0) == 0)
							set_bDropsEnabled = false;
					}

					if (!strcmp(ini.get_name_ptr(), "class"))
					{
						stDropInfo drop;
						int iClass = ini.get_value_int(0);
						string szGood = ini.get_value_string(1);
						drop.uGoodID = CreateID(szGood.c_str());
						drop.fChance = ini.get_value_float(2);
						drop.uAmountDroppedMin = ini.get_value_int(3);
						drop.uAmountDroppedMax = ini.get_value_int(4);
						if (drop.uAmountDroppedMin == 0)
						{
							drop.uAmountDroppedMin = 1;
						}
						drop.uAmountDroppedMax = ini.get_value_int(4);
						mmapDropInfo.insert(make_pair(iClass, drop));
						++iLoadedNPCDropClasses;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded class %u drop %s (0x%08X), %f chance.\n", iClass, stows(szGood).c_str(), CreateID(szGood.c_str()), drop.fChance);
					}
				}
			}
			if (ini.is_header("NPCDropsExclusions"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("excludedShipArch"))
					{
						mapDropExcludedArchetypes.insert(CreateID(ini.get_value_string(0)));
					}
				}
			}

		}
		ini.close();
	}

	ConPrint(L"PVECONTROLLER: NPC drops are %s.\n", set_bDropsEnabled ? L"enabled" : L"disabled");
	ConPrint(L"PVECONTROLLER: Loaded %u NPC drops by class.\n", iLoadedNPCDropClasses);
}


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


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NPCBountyPayout(uint iClientID, int cash) {

	pub::Player::AdjustCash(iClientID, cash);
	PrintUserCmdText(iClientID, L"A bounty of $%s credits has been deposited into your account.", ToMoneyStr(cash).c_str());
	
}

bool ExecuteCommandString_Callback(CCmds* cmds, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	if (wscCmd.compare(L"pvecontroller"))
		return false;

	if (!(cmds->rights & RIGHT_PLUGINS)) { cmds->Print(L"ERR No permission\n"); return false; }

	if (!cmds->ArgStrToEnd(1).compare(L"reloadall"))
	{
		cmds->Print(L"PVECONTROLLER: COMPLETE LIVE RELOAD requested by %s.\n", cmds->GetAdminName());
		LoadSettings();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live reload completed.\n");
		return true;
	}
	else if (!cmds->ArgStrToEnd(1).compare(L"reloadnpcbounties"))
	{
		cmds->Print(L"PVECONTROLLER: Live NPC bounties reload requested by %s.\n", cmds->GetAdminName());
		LoadSettingsNPCBounties();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live NPC bounties reload completed.\n");
		return true;
	}
	else if (!cmds->ArgStrToEnd(1).compare(L"reloadnpcdrops"))
	{
		cmds->Print(L"PVECONTROLLER: Live NPC drops reload requested by %s.\n", cmds->GetAdminName());
		LoadSettingsNPCDrops();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live NPC drops reload completed.\n");
		return true;
	}
	else
	{
		cmds->Print(L"Usage:\n");
		cmds->Print(L"  .pvecontroller reloadall -- Reloads ALL settings on the fly.\n");
		cmds->Print(L"  .pvecontroller reloadnpcbounties -- Reloads NPC bounty settings on the fly.\n");
		cmds->Print(L"  .pvecontroller reloadnpcdrops -- Reloads NPC drop settings on the fly.\n");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint lastProcessedId = 0;

void __stdcall HkCb_ShipDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
	returncode = DEFAULT_RETURNCODE;

	CShip* cship = (CShip*)iobj->cobj;
	auto killerData = npcToDropLoot.find(cship->id);
	if (killerData == npcToDropLoot.end())
	{
		return;
	}

	uint iKillerClientId = killerData->second;

	if (!iKillerClientId)
		return;

	if (cship->id == lastProcessedId)
	{
		return;
	}
	lastProcessedId = cship->id;

	Archetype::Ship* victimShiparch = reinterpret_cast<Archetype::Ship*>(cship->archetype);
	uint uArchID = victimShiparch->iArchID;

	// Grab some info we'll need later.
	uint uKillerSystem = iobj->cobj->system;
	unsigned int uKillerAffiliation = 0;

	// Deny bounties and drops for kills on targets above the maximum reward reputation threshold.
	int iTargetRep = cship->repVibe, iPlayerRep = Players[iKillerClientId].iReputation;
	uint uTargetAffiliation;
	float fAttitude = 0.0f;
	Reputation::Vibe::GetAffiliation(iTargetRep, uTargetAffiliation, false);
	Reputation::Vibe::GetAffiliation(iPlayerRep, uKillerAffiliation, false);
	pub::Reputation::GetGroupFeelingsTowards(iPlayerRep, uTargetAffiliation, fAttitude);
	if (fAttitude > set_fMaximumRewardRep) {
		return;
	}

	// Process bounties if enabled.
	if (set_bBountiesEnabled) {
		float fBountyPayout = 0;

		// Determine bounty payout.
		const auto& iter = mapBountyShipPayouts.find(uArchID);
		if (iter != mapBountyShipPayouts.end()) {
			fBountyPayout = static_cast<float>(iter->second.iBasePayout);
		}
		else {
			const auto& iter = mapBountyPayouts.find(victimShiparch->iShipClass);
			if (iter != mapBountyPayouts.end()) {
				fBountyPayout = static_cast<float>(iter->second.iBasePayout);
			}
		}

		// Multiply by system multiplier if applicable.
		if (iLoadedNPCBountySystemScales) {
			const auto& itSystemScale = mapBountySystemScales.find(uKillerSystem);
			if (itSystemScale != mapBountySystemScales.end())
				fBountyPayout *= itSystemScale->second;
		}

		// Multiply by class diff multiplier if applicable.
		if (iLoadedClassDiffMultipliers) {
			uint iKillerShipClass = Archetype::GetShip(Players[iKillerClientId].iShipArchetype)->iShipClass;

			int classDiff = 0;
			const auto& itVictimType = mapShipClassTypes.find(victimShiparch->iShipClass);
			const auto& itKillerType = mapShipClassTypes.find(iKillerShipClass);
			if (itVictimType != mapShipClassTypes.end() && itKillerType != mapShipClassTypes.end())
				classDiff = itVictimType->second - itKillerType->second;

			const auto& itDiffMultiplier = mapClassDiffMultipliers.find(classDiff);
			if (itDiffMultiplier != mapClassDiffMultipliers.end())
				fBountyPayout *= itDiffMultiplier->second;
		}

		// If we've turned bounties off, don't pay it.
		if (!set_bBountiesEnabled)
			fBountyPayout = 0;

		if (fBountyPayout) {
			auto playerGroup = Players[iKillerClientId].PlayerGroup;
			if (!playerGroup)
			{
				NPCBountyPayout(iKillerClientId, static_cast<int>(fBountyPayout));
			}
			else
			{
				uint memberCount = playerGroup->GetMemberCount();
				vector<uint> inSystemMembers;
				for (uint i = 0 ; i < memberCount ; i++)
				{
					uint memberId = playerGroup->GetMember(i);
					auto memberShip = ClientInfo[memberId].cship;
					if (!memberShip || Players[memberId].iSystemID != uKillerSystem)
					{
						continue;
					}
					if (HkDistance3D(memberShip->vPos, cship->vPos) < set_groupDistance)
					{
						inSystemMembers.emplace_back(memberId);
					}
				}
				auto groupScale = mapBountyGroupScale.find(inSystemMembers.size());
				if (groupScale != mapBountyGroupScale.end())
				{
					fBountyPayout *= groupScale->second;
				}
				else
				{
					fBountyPayout /= inSystemMembers.size();
				}
				for (auto member : inSystemMembers)
				{
					NPCBountyPayout(member, static_cast<int>(fBountyPayout));
				}
			}
		}
	}

	// Process drops if enabled.
	if (!set_bDropsEnabled || mapDropExcludedArchetypes.count(uArchID))
	{
		return;
	}

	auto& iter = mmapDropInfo.lower_bound(victimShiparch->iShipClass);
	if (iter == mmapDropInfo.end())
	{
		return;
	}
	
	const auto& iterEnd = mmapDropInfo.upper_bound(victimShiparch->iShipClass);
	while (iter != iterEnd)
	{
		const auto& dropData = iter->second;
		float roll = static_cast<float>(rand()) / RAND_MAX;
		if (roll < dropData.fChance)
		{
			Vector vLoc = cship->vPos;
			Vector randomVector = RandomVector(static_cast<float>(rand() % 60) + 20.f);
			vLoc.x += randomVector.x;
			vLoc.y += randomVector.y;
			vLoc.z += randomVector.z;

			uint finalAmount;
			if (dropData.uAmountDroppedMax)
			{
				finalAmount = dropData.uAmountDroppedMin + (rand() % (dropData.uAmountDroppedMax - dropData.uAmountDroppedMin + 1));
			}
			else
			{
				finalAmount = dropData.uAmountDroppedMin;
			}
			CreateLootSimple(uKillerSystem, cship->id, dropData.uGoodID, finalAmount, vLoc, false);

		}
		iter++;
	}
}

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "PvE Controller by Kazinsal, rewritten by Aingar.";
	p_PI->sShortName = "pvecontroller";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
	
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkCb_ShipDestroyed, PLUGIN_ShipDestroyed, 0));
	
	return p_PI;
}
