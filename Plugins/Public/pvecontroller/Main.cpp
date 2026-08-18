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
#include <cctype>
#include <cstdlib>
#include <random>
#include <vector>

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

struct stManagedLootProperties {
	string nickname;
	float baseChance;
	uint maximumDropNPC;
	uint maximumDropPlayer;
};

struct stPityRecipient {
	string accountDirectory;
	string characterName;
	float weight;
};

struct stAccountPityState {
	string filePath;
	unordered_map<uint, float> lootFailures;
	bool dirty = false;
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

constexpr float DEFAULT_LOOT_DROP_PITY_CHANCE_SCALE = 0.01f;
bool set_bLootDropPityEnabled = false;
float set_fLootDropPityChanceScale = DEFAULT_LOOT_DROP_PITY_CHANCE_SCALE;
bool set_bLootControllerLogEnabled = true;
unordered_map<uint, stManagedLootProperties> mapManagedLootProperties;
unordered_map<uint, string> mapShipArchetypeNicknames;
unordered_map<string, stAccountPityState> mapAccountPityStates;
unordered_map<uint, string> mapDisconnectingPityAccounts;
uint managedLootShipId = 0;
mstime managedLootTimestamp = 0;
unordered_set<uint> managedLootArchetypes;
bool lootControllerLogOpenErrorReported = false;
void LoadSettingsLootDropPity(void);

extern "C" BOOL __cdecl PvEControllerOwnsManagedLoot(uint shipId, uint itemId)
{
	return set_bLootDropPityEnabled && shipId == managedLootShipId && timeInMS() - managedLootTimestamp <= 1000 && managedLootArchetypes.count(itemId);
}

#pragma comment(linker, "/EXPORT:PvEControllerOwnsManagedLoot=_PvEControllerOwnsManagedLoot")

void LootControllerLog(bool forceConsole, const char* format, ...)
{
	bool consoleEnabled = forceConsole || set_iPluginDebug >= PLUGIN_DEBUG_CONSOLE;
	if (!set_bLootControllerLogEnabled && !consoleEnabled)
		return;

	char message[4096] = "";
	va_list marker;
	va_start(marker, format);
	_vsnprintf(message, sizeof(message) - 1, format, marker);
	va_end(marker);
	message[sizeof(message) - 1] = 0;

	if (set_bLootControllerLogEnabled)
	{
		FILE* log = fopen("./flhook_logs/lootcontroller.log", "at");
		if (log)
		{
			time_t now = time(nullptr);
			struct tm localTime;
			localtime_s(&localTime, &now);
			char timestamp[32];
			strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", &localTime);
			fprintf(log, "%s %s\n", timestamp, message);
			fclose(log);
			lootControllerLogOpenErrorReported = false;
		}
		else if (!lootControllerLogOpenErrorReported)
		{
			lootControllerLogOpenErrorReported = true;
			ConPrint(L"PVECONTROLLER: Could not open flhook_logs/lootcontroller.log.\n");
		}
	}

	if (consoleEnabled)
		ConPrint(L"PVECONTROLLER LOOT: %s\n", stows(message).c_str());
}

string GetActiveCharacterName(uint clientId)
{
	const wchar_t* characterName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(clientId));
	return characterName ? wstos(characterName) : "unknown";
}

string GetShipArchetypeNickname(uint shipArchetypeId)
{
	auto nickname = mapShipArchetypeNicknames.find(shipArchetypeId);
	return nickname == mapShipArchetypeNicknames.end() ? "unknown" : nickname->second;
}

string GetSystemNickname(uint systemId)
{
	wstring nickname = HkGetSystemNickByID(systemId);
	return nickname.empty() ? "unknown" : wstos(nickname);
}

string GetPityRecipientDescription(const vector<stPityRecipient>& recipients)
{
	if (recipients.empty())
		return "none";

	string description;
	for (const auto& recipient : recipients)
	{
		char entry[512];
		_snprintf(entry, sizeof(entry) - 1, "%s:%.3f", recipient.characterName.c_str(), recipient.weight);
		entry[sizeof(entry) - 1] = 0;
		if (!description.empty())
			description += ",";
		description += entry;
	}
	return description;
}

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
	LoadSettingsLootDropPity();

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

bool GetLootDropPityAccountDirectory(uint clientId, string& accountDirectory)
{
	CAccount* account = Players.FindAccountFromClientID(clientId);
	if (!account)
		return false;

	wstring directory;
	if (HkGetAccountDirName(account, directory) != HKE_OK || directory.empty())
		return false;

	accountDirectory = wstos(directory);
	return !accountDirectory.empty();
}

stAccountPityState& GetLootDropPityState(const string& accountDirectory)
{
	auto existing = mapAccountPityStates.find(accountDirectory);
	if (existing != mapAccountPityStates.end())
		return existing->second;

	stAccountPityState state;
	state.filePath = scAcctPath + accountDirectory + "\\pvecontroller.ini";
	list<INISECTIONVALUE> values;
	IniGetSection(state.filePath, "LootDropPity", values);
	for (const auto& value : values)
	{
		float failures = static_cast<float>(atof(value.scValue.c_str()));
		if (failures > 0.0f && isfinite(failures))
			state.lootFailures[CreateID(value.scKey.c_str())] = failures;
	}

	return mapAccountPityStates.emplace(accountDirectory, move(state)).first->second;
}

void FlushLootDropPityState(stAccountPityState& state)
{
	if (!state.dirty)
		return;

	vector<pair<string, float>> values;
	values.reserve(state.lootFailures.size());
	for (const auto& score : state.lootFailures)
	{
		auto properties = mapManagedLootProperties.find(score.first);
		if (properties != mapManagedLootProperties.end() && score.second > 0.0f)
			values.emplace_back(properties->second.nickname, score.second);
	}
	std::sort(values.begin(), values.end(), [](const pair<string, float>& left, const pair<string, float>& right) {
		return left.first < right.first;
	});
	string section;
	for (const auto& value : values)
	{
		section += value.first + "=" + to_string(value.second);
		section.push_back('\0');
	}
	if (!section.empty())
		section.push_back('\0');
	if (!WritePrivateProfileSectionA("LootDropPity", section.empty() ? nullptr : section.c_str(), state.filePath.c_str()))
	{
		ConPrint(L"PVECONTROLLER: Could not write loot drop pity state to %s.\n", stows(state.filePath).c_str());
		return;
	}

	state.dirty = false;
}

void LoadSettingsLootDropPity()
{
	char currentDirectory[MAX_PATH];
	GetCurrentDirectory(sizeof(currentDirectory), currentDirectory);
	string currentDirectoryString = currentDirectory;
	string pluginConfig = currentDirectoryString + "\\flhook_plugins\\pvecontroller.cfg";
	string freelancerIniFile = currentDirectoryString + "\\freelancer.ini";
	string gameDirectory = currentDirectoryString.substr(0, currentDirectoryString.length() - 4) + "\\DATA\\";

	string enabledSetting = IniGetS(pluginConfig, "LootDropPity", "enabled", "");
	bool enabledDefaulted = enabledSetting.empty();
	bool enabledValid = true;
	if (enabledDefaulted)
	{
		set_bLootDropPityEnabled = false;
	}
	else
	{
		char* settingEnd = nullptr;
		long enabledValue = strtol(enabledSetting.c_str(), &settingEnd, 10);
		while (settingEnd && *settingEnd && isspace(static_cast<unsigned char>(*settingEnd)))
			settingEnd++;
		enabledValid = settingEnd != enabledSetting.c_str() && settingEnd && !*settingEnd && (enabledValue == 0 || enabledValue == 1);
		set_bLootDropPityEnabled = enabledValid && enabledValue == 1;
		if (!enabledValid)
			ConPrint(L"PVECONTROLLER: Managed loot drops disabled; invalid [LootDropPity] enabled=\"%s\".\n", stows(enabledSetting).c_str());
	}
	string chanceScaleSetting = IniGetS(pluginConfig, "LootDropPity", "chance_scale", "");
	bool chanceScaleDefaulted = chanceScaleSetting.empty();
	bool chanceScaleValid = true;
	if (chanceScaleDefaulted)
	{
		set_fLootDropPityChanceScale = DEFAULT_LOOT_DROP_PITY_CHANCE_SCALE;
	}
	else
	{
		char* settingEnd = nullptr;
		set_fLootDropPityChanceScale = strtof(chanceScaleSetting.c_str(), &settingEnd);
		while (settingEnd && *settingEnd && isspace(static_cast<unsigned char>(*settingEnd)))
			settingEnd++;
		chanceScaleValid = settingEnd != chanceScaleSetting.c_str() && settingEnd && !*settingEnd && isfinite(set_fLootDropPityChanceScale) && set_fLootDropPityChanceScale >= 0.0f;
		if (!chanceScaleValid)
		{
			set_bLootDropPityEnabled = false;
			set_fLootDropPityChanceScale = DEFAULT_LOOT_DROP_PITY_CHANCE_SCALE;
			ConPrint(L"PVECONTROLLER: Managed loot drops disabled; invalid [LootDropPity] chance_scale=\"%s\".\n", stows(chanceScaleSetting).c_str());
		}
	}
	string logEnabledSetting = IniGetS(pluginConfig, "LootDropPity", "log_enabled", "");
	bool logEnabledDefaulted = logEnabledSetting.empty();
	bool logEnabledValid = true;
	if (logEnabledDefaulted)
	{
		set_bLootControllerLogEnabled = true;
	}
	else
	{
		char* settingEnd = nullptr;
		long logEnabledValue = strtol(logEnabledSetting.c_str(), &settingEnd, 10);
		while (settingEnd && *settingEnd && isspace(static_cast<unsigned char>(*settingEnd)))
			settingEnd++;
		logEnabledValid = settingEnd != logEnabledSetting.c_str() && settingEnd && !*settingEnd && (logEnabledValue == 0 || logEnabledValue == 1);
		set_bLootControllerLogEnabled = logEnabledValid ? logEnabledValue == 1 : true;
		if (!logEnabledValid)
			ConPrint(L"PVECONTROLLER: Invalid [LootDropPity] log_enabled=\"%s\"; using default 1.\n", stows(logEnabledSetting).c_str());
	}
	mapManagedLootProperties.clear();
	mapShipArchetypeNicknames.clear();
	lootControllerLogOpenErrorReported = false;

	INI_Reader ini;
	vector<string> equipmentFiles;
	vector<string> shipFiles;
	if (ini.open(freelancerIniFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (!ini.is_header("Data"))
				continue;
			while (ini.read_value())
			{
				if (ini.is_value("equipment"))
					equipmentFiles.emplace_back(ini.get_value_string());
				else if (ini.is_value("ships"))
					shipFiles.emplace_back(ini.get_value_string());
			}
		}
		ini.close();

		for (const auto& shipFile : shipFiles)
		{
			string shipPath = gameDirectory + shipFile;
			if (!ini.open(shipPath.c_str(), false))
				continue;

			while (ini.read_header())
			{
				if (!ini.is_header("Ship"))
					continue;

				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						string nickname = ini.get_value_string(0);
						mapShipArchetypeNicknames[CreateID(nickname.c_str())] = nickname;
						break;
					}
				}
			}
			ini.close();
		}

		for (const auto& equipmentFile : equipmentFiles)
		{
			string equipmentPath = gameDirectory + equipmentFile;
			if (!ini.open(equipmentPath.c_str(), false))
				continue;

			while (ini.read_header())
			{
				string nickname;
				float chance = 0.0f;
				uint maximumDropNPC = 5000;
				uint maximumDropPlayer = 5000;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
						nickname = ini.get_value_string(0);
					else if (ini.is_value("drop_chance_npc_unmounted"))
						chance = ini.get_value_float(0);
					else if (ini.is_value("max_drop_npc"))
						maximumDropNPC = ini.get_value_int(0);
					else if (ini.is_value("max_drop_player"))
						maximumDropPlayer = ini.get_value_int(0);
				}

				if (!nickname.empty() && chance > 0.0f && chance < 1.0f)
				{
					stManagedLootProperties properties;
					properties.nickname = nickname;
					properties.baseChance = chance;
					properties.maximumDropNPC = maximumDropNPC;
					properties.maximumDropPlayer = maximumDropPlayer;
					mapManagedLootProperties[CreateID(nickname.c_str())] = properties;
				}
			}
			ini.close();
		}
	}
	else
	{
		set_bLootDropPityEnabled = false;
		ConPrint(L"PVECONTROLLER: Managed loot drops disabled; could not open %s.\n", stows(freelancerIniFile).c_str());
	}
	if (mapManagedLootProperties.empty())
		set_bLootDropPityEnabled = false;

	ConPrint(L"PVECONTROLLER: Managed loot drops and pity are %s; loaded %u managed loot rows.\n",
		set_bLootDropPityEnabled ? L"enabled" : L"disabled",
		mapManagedLootProperties.size());
	ConPrint(L"PVECONTROLLER: Loot drop pity settings: enabled=%u (%s), chance_scale=%.6f (%s), log_enabled=%u (%s).\n",
		set_bLootDropPityEnabled ? 1 : 0,
		enabledDefaulted ? L"default; enabled missing" : enabledValid ? L"configured" : L"invalid; disabled",
		set_fLootDropPityChanceScale,
		chanceScaleDefaulted ? L"default; chance_scale missing" : chanceScaleValid ? L"configured" : L"invalid; disabled",
		set_bLootControllerLogEnabled ? 1 : 0,
		logEnabledDefaulted ? L"default" : logEnabledValid ? L"configured" : L"invalid; default used");
	ConPrint(L"PVECONTROLLER: Loot event file logging is %s at flhook_logs/lootcontroller.log; per-event console output is %s.\n",
		set_bLootControllerLogEnabled ? L"enabled" : L"disabled",
		set_iPluginDebug >= PLUGIN_DEBUG_CONSOLE ? L"enabled" : L"disabled");
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

float GetLootDropPityScore(const string& accountDirectory, uint itemId)
{
	stAccountPityState& state = GetLootDropPityState(accountDirectory);
	auto score = state.lootFailures.find(itemId);
	return score == state.lootFailures.end() ? 0.0f : score->second;
}

void ResetLootDropPityScore(const vector<stPityRecipient>& recipients, uint itemId)
{
	for (const auto& recipient : recipients)
	{
		stAccountPityState& state = GetLootDropPityState(recipient.accountDirectory);
		if (state.lootFailures.erase(itemId))
			state.dirty = true;
	}
}

void IncrementLootDropPityScore(const vector<stPityRecipient>& recipients, uint itemId)
{
	for (const auto& recipient : recipients)
	{
		if (recipient.weight == 0.0f)
			continue;
		stAccountPityState& state = GetLootDropPityState(recipient.accountDirectory);
		state.lootFailures[itemId] += recipient.weight;
		state.dirty = true;
	}
}

uint CreateManagedLootDrop(CShip* ship, uint itemId, uint count)
{
	Vector dropPosition = ship->vPos;
	Vector randomVector = RandomVector(static_cast<float>(rand() % 60) + 20.0f);
	dropPosition.x += randomVector.x;
	dropPosition.y += randomVector.y;
	dropPosition.z += randomVector.z;
	return CreateLootSimple(ship->system, ship->id, itemId, count, dropPosition, false);
}

unordered_map<uint, uint> GetManagedLootCargo(CShip* ship)
{
	unordered_map<uint, uint> lootCargo;
	CEquipTraverser traverser(EquipmentClass::Cargo);
	CECargo* cargo = nullptr;
	while (cargo = reinterpret_cast<CECargo*>(ship->equip_manager.Traverse(traverser)))
	{
		if (mapManagedLootProperties.find(cargo->archetype->iArchID) != mapManagedLootProperties.end())
			lootCargo[cargo->archetype->iArchID] += cargo->count;
	}
	return lootCargo;
}

void BeginManagedLootDrop(CShip* ship)
{
	managedLootShipId = ship->id;
	managedLootTimestamp = 0;
	managedLootArchetypes.clear();
}

void ProcessManagedPlayerLootDrops(CShip* ship, uint killerId)
{
	if (!set_bLootDropPityEnabled)
		return;

	string victimName = GetActiveCharacterName(ship->ownerPlayer);
	uint killerClientId = HkGetClientIDByShip(killerId);
	string killerName = killerClientId == static_cast<uint>(-1) ? "npc_or_environment" : GetActiveCharacterName(killerClientId);
	uint shipArchetypeId = ship->archetype->iArchID;
	string shipNickname = GetShipArchetypeNickname(shipArchetypeId);
	string systemNickname = GetSystemNickname(ship->system);

	for (const auto& lootCargo : GetManagedLootCargo(ship))
	{
		uint itemId = lootCargo.first;
		const stManagedLootProperties& properties = mapManagedLootProperties.find(itemId)->second;
		managedLootArchetypes.insert(itemId);
		uint dropCount = min(lootCargo.second, properties.maximumDropPlayer);
		if (!dropCount)
			continue;
		uint lootId = CreateManagedLootDrop(ship, itemId, dropCount);
		LootControllerLog(!lootId,
			"event=%s victim=\"%s\" killer=\"%s\" player_ship=\"%s\" player_object=%u player_arch=0x%08X system=\"%s\" loot=\"%s\" count=%u loot_object=%u",
			lootId ? "player_drop" : "player_drop_failed", victimName.c_str(), killerName.c_str(), shipNickname.c_str(), ship->id,
			shipArchetypeId, systemNickname.c_str(), properties.nickname.c_str(), dropCount, lootId);
	}
}

void ProcessManagedNPCLootDrops(CShip* ship, uint killerClientId, const vector<stPityRecipient>& recipients)
{
	if (!set_bLootDropPityEnabled)
		return;

	string killerName = GetActiveCharacterName(killerClientId);
	string recipientDescription = GetPityRecipientDescription(recipients);
	uint shipArchetypeId = ship->archetype->iArchID;
	string shipNickname = GetShipArchetypeNickname(shipArchetypeId);
	string systemNickname = GetSystemNickname(ship->system);
	float progressIncrement = 0.0f;
	for (const auto& recipient : recipients)
		progressIncrement += recipient.weight;

	for (const auto& lootCargo : GetManagedLootCargo(ship))
	{
		uint itemId = lootCargo.first;
		const stManagedLootProperties& properties = mapManagedLootProperties.find(itemId)->second;
		managedLootArchetypes.insert(itemId);
		uint rollCount = min(lootCargo.second, properties.maximumDropNPC);
		if (!rollCount)
			continue;
		for (uint rollIndex = 1; rollIndex <= rollCount; rollIndex++)
		{
			float combinedFailures = 0.0f;
			for (const auto& recipient : recipients)
				combinedFailures += GetLootDropPityScore(recipient.accountDirectory, itemId);
			float conditionalPityChance = 0.0f;
			if (!recipients.empty())
			{
				if (combinedFailures > 0.0f && set_fLootDropPityChanceScale > 0.0f)
					conditionalPityChance = 1.0f - powf(1.0f - properties.baseChance, combinedFailures * set_fLootDropPityChanceScale);
				conditionalPityChance = max(0.0f, min(1.0f, conditionalPityChance));
			}
			float totalChance = 1.0f - (1.0f - properties.baseChance) * (1.0f - conditionalPityChance);
			if (totalChance >= 0.99f)
				totalChance = 1.0f;
			float pityAddedChance = totalChance - properties.baseChance;
			float roll = static_cast<float>(rand()) / (static_cast<float>(RAND_MAX) + 1.0f);

			if (roll < totalChance)
			{
				uint lootId = CreateManagedLootDrop(ship, itemId, 1);
				if (lootId)
				{
					ResetLootDropPityScore(recipients, itemId);
					LootControllerLog(false,
						"event=npc_drop killer=\"%s\" recipients=\"%s\" npc_ship=\"%s\" npc_object=%u npc_arch=0x%08X system=\"%s\" loot=\"%s\" unit=%u units=%u count=1 base_chance=%.4f pity_added_chance=%.4f total_chance=%.4f roll=%.4f progress_before=%.3f progress_after=0.000 loot_object=%u",
						killerName.c_str(), recipientDescription.c_str(), shipNickname.c_str(), ship->id, shipArchetypeId, systemNickname.c_str(),
						properties.nickname.c_str(), rollIndex, rollCount, properties.baseChance, pityAddedChance, totalChance, roll, combinedFailures, lootId);
					continue;
				}
				LootControllerLog(true,
					"event=npc_drop_failed killer=\"%s\" recipients=\"%s\" npc_ship=\"%s\" npc_object=%u npc_arch=0x%08X system=\"%s\" loot=\"%s\" unit=%u units=%u count=1 base_chance=%.4f pity_added_chance=%.4f total_chance=%.4f roll=%.4f progress=%.3f",
					killerName.c_str(), recipientDescription.c_str(), shipNickname.c_str(), ship->id, shipArchetypeId, systemNickname.c_str(),
					properties.nickname.c_str(), rollIndex, rollCount, properties.baseChance, pityAddedChance, totalChance, roll, combinedFailures);
				continue;
			}

			if (recipients.empty())
			{
				LootControllerLog(false,
					"event=npc_miss_no_recipients killer=\"%s\" recipients=\"none\" npc_ship=\"%s\" npc_object=%u npc_arch=0x%08X system=\"%s\" loot=\"%s\" unit=%u units=%u base_chance=%.4f pity_added_chance=0.0000 total_chance=%.4f roll=%.4f",
					killerName.c_str(), shipNickname.c_str(), ship->id, shipArchetypeId, systemNickname.c_str(), properties.nickname.c_str(),
					rollIndex, rollCount, properties.baseChance, totalChance, roll);
				continue;
			}

			IncrementLootDropPityScore(recipients, itemId);
			LootControllerLog(false,
				"event=npc_miss killer=\"%s\" recipients=\"%s\" npc_ship=\"%s\" npc_object=%u npc_arch=0x%08X system=\"%s\" loot=\"%s\" unit=%u units=%u base_chance=%.4f pity_added_chance=%.4f total_chance=%.4f roll=%.4f progress_before=%.3f progress_after=%.3f",
				killerName.c_str(), recipientDescription.c_str(), shipNickname.c_str(), ship->id, shipArchetypeId, systemNickname.c_str(),
				properties.nickname.c_str(), rollIndex, rollCount, properties.baseChance, pityAddedChance, totalChance, roll, combinedFailures,
				combinedFailures + progressIncrement);
		}
	}
}

void FlushLootDropPityClient(uint clientId)
{
	string accountDirectory;
	if (!GetLootDropPityAccountDirectory(clientId, accountDirectory))
		return;
	auto state = mapAccountPityStates.find(accountDirectory);
	if (state != mapAccountPityStates.end() && state->second.dirty)
		FlushLootDropPityState(state->second);
}

bool __stdcall LootDropPityLand(uint clientId, FLPACKET_LAND& packet)
{
	returncode = DEFAULT_RETURNCODE;
	FlushLootDropPityClient(clientId);
	return true;
}

void __stdcall LootDropPityBaseExit(uint baseId, uint clientId)
{
	returncode = DEFAULT_RETURNCODE;
	FlushLootDropPityClient(clientId);
}

void __stdcall LootDropPitySystemSwitchOut(uint shipId, uint clientId)
{
	returncode = DEFAULT_RETURNCODE;
	FlushLootDropPityClient(clientId);
}

void __stdcall LootDropPityCharacterInfoReq(uint clientId, bool p2)
{
	returncode = DEFAULT_RETURNCODE;
	if (!p2)
		FlushLootDropPityClient(clientId);
}

void __stdcall LootDropPityDisconnect(uint clientId, enum EFLConnection connection)
{
	returncode = DEFAULT_RETURNCODE;
	string accountDirectory;
	if (GetLootDropPityAccountDirectory(clientId, accountDirectory))
	{
		auto state = mapAccountPityStates.find(accountDirectory);
		if (state != mapAccountPityStates.end())
		{
			if (state->second.dirty)
				mapDisconnectingPityAccounts[clientId] = accountDirectory;
			else
				mapAccountPityStates.erase(state);
		}
	}
}

void __stdcall LootDropPityDisconnectAfter(uint clientId, enum EFLConnection connection)
{
	returncode = DEFAULT_RETURNCODE;
	auto account = mapDisconnectingPityAccounts.find(clientId);
	if (account == mapDisconnectingPityAccounts.end())
		return;
	auto state = mapAccountPityStates.find(account->second);
	if (state != mapAccountPityStates.end())
	{
		FlushLootDropPityState(state->second);
		if (!state->second.dirty)
			mapAccountPityStates.erase(state);
	}
	mapDisconnectingPityAccounts.erase(account);
}

bool ExecuteCommandString_Callback(CCmds* cmds, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	if (wscCmd.compare(L"pvecontroller"))
		return false;

	if (!(cmds->rights & RIGHT_PLUGINS)) { cmds->Print(L"ERR No permission\n"); return false; }
	wstring action = ToLower(cmds->ArgStr(1));

	if (action == L"reloadall")
	{
		cmds->Print(L"PVECONTROLLER: COMPLETE LIVE RELOAD requested by %s.\n", cmds->GetAdminName());
		LoadSettings();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live reload completed.\n");
		return true;
	}
	else if (action == L"reloadnpcbounties")
	{
		cmds->Print(L"PVECONTROLLER: Live NPC bounties reload requested by %s.\n", cmds->GetAdminName());
		LoadSettingsNPCBounties();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live NPC bounties reload completed.\n");
		return true;
	}
	else if (action == L"reloadnpcdrops")
	{
		cmds->Print(L"PVECONTROLLER: Live NPC drops reload requested by %s.\n", cmds->GetAdminName());
		LoadSettingsNPCDrops();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live NPC drops reload completed.\n");
		return true;
	}
	else if (action == L"reloadlootpity")
	{
		LoadSettingsLootDropPity();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live loot drop pity reload completed.\n");
		return true;
	}
	else if (action == L"pitystatus" || action == L"pityreset")
	{
		wstring characterName = cmds->ArgStrToEnd(2);
		uint clientId = HkGetClientIdFromCharname(characterName);
		string accountDirectory;
		if (characterName.empty() || clientId == static_cast<uint>(-1) || !GetLootDropPityAccountDirectory(clientId, accountDirectory))
		{
			cmds->Print(L"ERR Target character must be online.\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		stAccountPityState& state = GetLootDropPityState(accountDirectory);
		if (action == L"pityreset")
		{
			state.lootFailures.clear();
			state.dirty = true;
			FlushLootDropPityState(state);
			cmds->Print(L"PVECONTROLLER: Loot drop pity reset.\n");
		}
		else
		{
			vector<pair<string, float>> scores;
			for (const auto& score : state.lootFailures)
			{
				auto properties = mapManagedLootProperties.find(score.first);
				if (properties != mapManagedLootProperties.end())
					scores.emplace_back(properties->second.nickname, score.second);
			}
			std::sort(scores.begin(), scores.end(), [](const pair<string, float>& left, const pair<string, float>& right) {
				return left.first < right.first;
			});
			cmds->Print(L"PVECONTROLLER: %u loot pity entries.\n", scores.size());
			for (const auto& score : scores)
				cmds->Print(L"  %s = %.3f\n", stows(score.first).c_str(), score.second);
		}
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else
	{
		cmds->Print(L"Usage:\n");
		cmds->Print(L"  .pvecontroller reloadall -- Reloads ALL settings on the fly.\n");
		cmds->Print(L"  .pvecontroller reloadnpcbounties -- Reloads NPC bounty settings on the fly.\n");
		cmds->Print(L"  .pvecontroller reloadnpcdrops -- Reloads NPC drop settings on the fly.\n");
		cmds->Print(L"  .pvecontroller reloadlootpity -- Reloads loot drop pity settings and equipment data.\n");
		cmds->Print(L"  .pvecontroller pitystatus <character> -- Shows account loot pity state.\n");
		cmds->Print(L"  .pvecontroller pityreset <character> -- Clears account loot pity state.\n");
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
	if (cship->id == lastProcessedId)
		return;
	lastProcessedId = cship->id;
	BeginManagedLootDrop(cship);

	if (cship->ownerPlayer)
	{
		ProcessManagedPlayerLootDrops(cship, killerId);
		managedLootTimestamp = timeInMS();
		return;
	}

	auto killerData = npcToDropLoot.find(cship->id);
	if (killerData == npcToDropLoot.end())
	{
		return;
	}

	uint iKillerClientId = killerData->second;

	if (!iKillerClientId)
		return;

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
	bool eligibleForRewards = fAttitude <= set_fMaximumRewardRep;
	vector<stPityRecipient> pityRecipients;

	// Process bounties if enabled.
	if (eligibleForRewards && set_bBountiesEnabled) {
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
				string accountDirectory;
				if (GetLootDropPityAccountDirectory(iKillerClientId, accountDirectory))
					pityRecipients.push_back({ accountDirectory, GetActiveCharacterName(iKillerClientId), 1.0f });
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
				if (!inSystemMembers.empty())
				{
					float pityWeight = 1.0f / inSystemMembers.size();
					auto groupScale = mapBountyGroupScale.find(inSystemMembers.size());
					if (groupScale != mapBountyGroupScale.end())
					{
						pityWeight = groupScale->second;
						fBountyPayout *= pityWeight;
					}
					else
					{
						fBountyPayout /= inSystemMembers.size();
					}
					unordered_map<string, stPityRecipient> accountRecipients;
					for (auto member : inSystemMembers)
					{
						NPCBountyPayout(member, static_cast<int>(fBountyPayout));
						string accountDirectory;
						if (GetLootDropPityAccountDirectory(member, accountDirectory))
						{
							auto existing = accountRecipients.find(accountDirectory);
							if (existing == accountRecipients.end() || pityWeight > existing->second.weight)
								accountRecipients[accountDirectory] = { accountDirectory, GetActiveCharacterName(member), pityWeight };
						}
					}
					for (const auto& accountRecipient : accountRecipients)
						pityRecipients.push_back(accountRecipient.second);
				}
			}
		}
	}

	if (eligibleForRewards)
	{
		std::sort(pityRecipients.begin(), pityRecipients.end(), [](const stPityRecipient& left, const stPityRecipient& right) {
			return left.characterName < right.characterName;
		});
		ProcessManagedNPCLootDrops(cship, iKillerClientId, pityRecipients);
		managedLootTimestamp = timeInMS();
	}

	// Process drops if enabled.
	if (!eligibleForRewards || !set_bDropsEnabled || mapDropExcludedArchetypes.count(uArchID))
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
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LootDropPityLand, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_LAND, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LootDropPityBaseExit, PLUGIN_HkIServerImpl_BaseExit_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LootDropPitySystemSwitchOut, PLUGIN_HkIServerImpl_SystemSwitchOutComplete_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LootDropPityCharacterInfoReq, PLUGIN_HkIServerImpl_CharacterInfoReq_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LootDropPityDisconnect, PLUGIN_HkIServerImpl_DisConnect, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LootDropPityDisconnectAfter, PLUGIN_HkIServerImpl_DisConnect_AFTER, 0));
	
	return p_PI;
}
