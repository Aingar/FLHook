/**
 Base Plugin for FLHook-Plugin
 by Cannon.

0.1:
 Initial release
*/

// includes 

#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Main.h"
#include <sstream>
#include <hookext_exports.h>

using st6_malloc_t = void* (*)(size_t);
using st6_free_t = void(*)(void*);
IMPORT st6_malloc_t st6_malloc;
IMPORT st6_free_t st6_free;

bool set_SkipUnchartedKill = false;

string lastDespawnedFilename;

unordered_map<uint, unordered_set<uint>> eventCommodities;

uint set_export_cooldown = 900;

// Clients
unordered_map<uint, CLIENT_DATA> clients;

// Bases
unordered_map<uint, PlayerBase*> player_bases;
unordered_map<uint, unordered_set<CSolar*>> POBSolarsBySystemMap;
unordered_map<uint, PlayerBase*>::iterator baseSaveIterator = player_bases.begin();

vector<ScheduledRespawn> basesToRespawn;

unordered_map<uint, std::pair<uint, Costume>> factionCostumeMap;

/// 0 = HTML, 1 = JSON, 2 = Both
int ExportType = 0;

/// The debug mode
int set_plugin_debug = 0;
int set_plugin_debug_special = 0;

/// List of banned systems
unordered_set<uint> bannedSystemList;

/// The ship used to construct and upgrade bases
uint set_construction_shiparch = 0;

/// Mininmum distances for base deployment
bool enableDistanceCheck = false;
float minMiningDistance = 30000;
float minPlanetDistance = 2500;
float minStationDistance = 10000;
float minLaneDistance = 5000;
float minJumpDistance = 15000;
float minDistanceMisc = 2500;
float minOtherPOBDistance = 5000;

unordered_set<uint> lowTierMiningCommoditiesSet;

/// Deployment command cooldown trackimg
unordered_map<uint, uint> deploymentCooldownMap;
uint deploymentCooldownDuration = 60;

/// Map of good to quantity for items required by construction ship
map<uint, uint> construction_items;

/// Construction cost in credits
int construction_credit_cost = 0;

/// list of items and quantity used to repair 10000 units of damage
vector<REPAIR_ITEM> set_base_repair_items;

/// list of items used by human crew
vector<uint> set_base_crew_consumption_items;
vector<uint> set_base_crew_food_items;

uint set_crew_check_frequency = 60 * 60 * 12; // 12 hours

/// The commodity used as crew for the base
uint set_base_crew_type;

unordered_set<uint> humanCargoList;

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

/// Global recipe map
unordered_map<uint, RECIPE> recipeMap;

/// Maps of shortcut numbers to recipes to construct item.
unordered_map<wstring, map<uint, RECIPE>> recipeCraftTypeNumberMap;
unordered_map<wstring, map<wstring, RECIPE>> recipeCraftTypeNameMap;
unordered_map<uint, vector<wstring>> factoryNicknameToCraftTypeMap;
unordered_map<wstring, RECIPE> moduleNameRecipeMap;
unordered_map<wstring, map<uint, RECIPE>> craftListNumberModuleMap;
unordered_set<wstring> buildingCraftLists;

void AddFactoryRecipeToMaps(const RECIPE& recipe);
void AddModuleRecipeToMaps(const RECIPE& recipe, const vector<wstring> craft_types, const wstring& build_type, uint recipe_number);

/// Map of space obj IDs to base modules to speed up damage algorithms.
unordered_map<uint, Module*> spaceobj_modules;

/// Map of core upgrade recipes
unordered_map<uint, uint> core_upgrade_recipes;

/// Map of core level to default storage capacity:
unordered_map<uint, uint> core_upgrade_storage;

/// Path to shield status html page
string set_status_path_html;

/// same thing but for json
string set_status_path_json;

string set_status_path_json_public_shop;

/// Damage to the base every tick
float set_damage_per_tick = 600;

/// Additional damage penalty for stations without proper crew
float no_crew_damage_multiplier = 1;

// The seconds per damage tick
uint set_damage_tick_time = 16;

// The seconds per tick
uint set_tick_time = 16;

// How much damage do we heal per repair cycle?
uint repair_per_repair_cycle = 60000;

uint base_access_entry_limit = 20;

int defense_platform_activation_offset = 30;


// set of configurable variables defining the diminishing returns on damage during POB siege
// POB starts at base_shield_strength, then every 'threshold' of damage taken, 
// shield goes up in absorption by the 'increment'
// threshold size is to be configured per core level.
unordered_map<int, float> shield_reinforcement_threshold_map;
float shield_reinforcement_increment = 0.0f;
float base_shield_strength = 0.97f;

int vulnerability_window_length = 120; // 2 hours

int vulnerability_window_change_cooldown = 3600 * 24 * 30; // 30 days

int vulnerability_window_minimal_spread = 60 * 8; // 8 hours

bool single_vulnerability_window = false;

const uint shield_fuse = CreateID("player_base_shield");

/// List of commodities forbidden to store on POBs
unordered_set<uint> forbidden_player_base_commodity_set;

// If true, use the new solar based defense platform spawn 	 	
bool set_new_spawn = true;

/// True if the settings should be reloaded
bool load_settings_required = true;

/// holiday mode
bool set_holiday_mode = false;

//archtype structure
unordered_map<string, ARCHTYPE_STRUCT> mapArchs;

//commodities to watch for logging
unordered_map<uint, wstring> listCommodities;

//the hostility and weapon platform activation from damage caused by one player
float damage_threshold = 400000;

//the amount of damage necessary to deal to one base in order to trigger siege status
float siege_mode_damage_trigger_level = 8000000;

//the distance between bases to share siege mod activation
float siege_mode_chain_reaction_trigger_distance = 8000;

unordered_set<uint> customSolarList;

//siege weaponry definitions
unordered_map<uint, float> siegeWeaponryMap;

vector<pair<uint, float>> rearmamentCreditRatio;

int GetRandom(int min, int max)
{
	return (rand() % (max - min)) + min;
}

uint GetAffliationFromClient(uint client)
{
	int rep;
	pub::Player::GetRep(client, rep);

	uint affiliation;
	Reputation::Vibe::Verify(rep);
	Reputation::Vibe::GetAffiliation(rep, affiliation, false);
	return affiliation;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

PlayerBase *GetPlayerBase(uint base)
{
	const auto& i = player_bases.find(base);
	if (i != player_bases.end())
	{
		return i->second;
	}
	return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

PlayerBase *GetPlayerBaseForClient(uint client)
{
	auto& j = clients.find(client);
	if (j == clients.end())
	{
		return nullptr;
	}

	auto i = player_bases.find(j->second.player_base);
	if (i == player_bases.end())
	{
		return nullptr;
	}
	return i->second;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

PlayerBase *GetLastPlayerBaseForClient(uint client)
{
	auto& j = clients.find(client);
	if (j == clients.end())
	{
		return nullptr;
	}
	auto& i = player_bases.find(j->second.last_player_base);
	if (i == player_bases.end())
	{
		return nullptr;
	}
	return i->second;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Logging(const char *szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	char szBuf[64];
	time_t tNow = time(0);
	struct tm *t = localtime(&tNow);
	strftime(szBuf, sizeof(szBuf), "%Y/%m/%d %H:%M:%S", t);

	FILE *Logfile = fopen(("./flhook_logs/flhook_cheaters.log"), "at");
	if (Logfile)
	{
		fprintf(Logfile, "%s %s\n", szBuf, szBufString);
		fflush(Logfile);
		fclose(Logfile);
	}
}

// These logging functions need consolidating.
void BaseLogging(const char *szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	char szBuf[64];
	time_t tNow = time(0);
	struct tm *t = localtime(&tNow);
	strftime(szBuf, sizeof(szBuf), "%Y/%m/%d %H:%M:%S", t);

	FILE *BaseLogfile = fopen("./flhook_logs/playerbase_events.log", "at");
	if (BaseLogfile)
	{
		fprintf(BaseLogfile, "%s %s\n", szBuf, szBufString);
		fflush(BaseLogfile);
		fclose(BaseLogfile);
	}
}

void RespawnBase(PlayerBase* base)
{
	string filepath = base->path;
	player_bases.erase(base->base);
	delete base;
	base = nullptr;
	PlayerBase* newBase = new PlayerBase(filepath);
	player_bases[newBase->base] = newBase;
	newBase->Spawn();
}

FILE *LogfileEventCommodities = fopen("./flhook_logs/event_pobsales.log", "at");

void LoggingEventCommodity(const char *szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	char szBuf[64];
	time_t tNow = time(0);
	struct tm *t = localtime(&tNow);
	strftime(szBuf, sizeof(szBuf), "%Y/%m/%d %H:%M:%S", t);
	fprintf(LogfileEventCommodities, "%s %s\n", szBuf, szBufString);
	fflush(LogfileEventCommodities);
	fclose(LogfileEventCommodities);
	LogfileEventCommodities = fopen("./flhook_logs/event_pobsales.log", "at");
}

void Notify_Event_Commodity_Sold(uint iClientID, string commodity, int count, string basename)
{
	//internal log
	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
	wstring wscMsgLog = L"<%player> has sold <%units> of the event commodity <%eventname> to the POB <%pob>";
	wscMsgLog = ReplaceStr(wscMsgLog, L"%player", wscCharname.c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%eventname", stows(commodity).c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%units", itows(count).c_str());
	wscMsgLog = ReplaceStr(wscMsgLog, L"%pob", stows(basename).c_str());
	string scText = wstos(wscMsgLog);
	LoggingEventCommodity("%s", scText.c_str());
}

void LogCheater(uint client, const wstring &reason)
{
	CAccount *acc = Players.FindAccountFromClientID(client);

	if (!HkIsValidClientID(client) || !acc)
	{
		AddLog("ERROR: invalid parameter in log cheater, clientid=%u acc=%08x reason=%s", client, acc, wstos(reason).c_str());
		return;
	}

	//internal log
	string scText = wstos(reason);
	Logging("%s", scText.c_str());

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

// For the specified client setup the reputation to any bases in the
// client's system.
void SyncReputationForClientShip(uint ship, uint client)
{
	int player_rep;
	pub::SpaceObj::GetRep(ship, player_rep);

	uint system;
	pub::SpaceObj::GetSystem(ship, system);

	for (auto& base : player_bases)
	{
		if (base.second->system == system)
		{
			float attitude = base.second->GetAttitudeTowardsClient(client);
			if (set_plugin_debug > 1)
				ConPrint(L"SyncReputationForClientShip:: ship=%u attitude=%f base=%08x\n", ship, attitude, base.first);
			for (auto module : base.second->modules)
			{
				if (module)
				{
					module->SetReputation(player_rep, attitude);
				}
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// HTML-encodes a string and returns the encoded string.
wstring HtmlEncode(wstring text)
{
	wstring sb;
	int len = text.size();
	for (int i = 0; i < len; i++)
	{
		switch (text[i])
		{
		case L'<':
			sb.append(L"&lt;");
			break;
		case L'>':
			sb.append(L"&gt;");
			break;
		case L'"':
			sb.append(L"&quot;");
			break;
		case L'&':
			sb.append(L"&amp;");
			break;
		default:
			if (text[i] > 159)
			{
				sb.append(L"&#");
				sb.append(itows((int)text[i]));
				sb.append(L";");
			}
			else
			{
				sb.append(1, text[i]);
			}
			break;
		}
	}
	return sb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Clear client info when a client connects.
void ClearClientInfo(uint client)
{
	returncode = DEFAULT_RETURNCODE;
	clients.erase(client);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoadSettings()
{
	Universe::IBase* base = Universe::GetFirstBase();
	while (base)
	{
		if (string(base->cNickname).find("proxy_base") != string::npos)
		{
			CSolar* solar = reinterpret_cast<CSolar*>(CObject::Find(base->lSpaceObjID, CObject::CSOLAR_OBJECT));
			if (solar)
			{
				solar->Release();
				solar->dockTargetId2 = 0;
			}
		}
		base = Universe::GetNextBase();
	}

	returncode = DEFAULT_RETURNCODE;
	load_settings_required = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ValidateItem(const char* goodName)
{
	const GoodInfo* gi = GoodList_get()->find_by_name(goodName);
	if (!gi)
	{
		ConPrint(L"\n\nBASE ERROR Invalid good found in config: %ls\n\n", stows((string)goodName).c_str());
	}
}

void LoadRecipes()
{

	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);

	recipeMap.clear();
	recipeCraftTypeNumberMap.clear();
	recipeCraftTypeNameMap.clear();
	factoryNicknameToCraftTypeMap.clear();
	moduleNameRecipeMap.clear();
	craftListNumberModuleMap.clear();

	string cfg_fileitems = string(szCurDir) + R"(\flhook_plugins\base_recipe_items.cfg)";
	string cfg_filemodules = string(szCurDir) + R"(\flhook_plugins\base_recipe_modules.cfg)";

	INI_Reader ini;

	if (ini.open(cfg_filemodules.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("recipe"))
			{
				RECIPE recipe;
				vector<wstring> craft_types;
				wstring build_type;
				uint recipe_number;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						recipe.nickname = CreateID(ini.get_value_string(0));
						recipe.nicknameString = ini.get_value_string(0);
					}
					else if (ini.is_value("infotext"))
					{
						recipe.infotext = stows(ini.get_value_string(0));
					}
					else if (ini.is_value("craft_list"))
					{
						craft_types.emplace_back(stows(ToLower(ini.get_value_string(0))));
					}
					else if (ini.is_value("build_type"))
					{
						build_type = stows(ToLower(ini.get_value_string(0)));
					}
					else if (ini.is_value("recipe_number"))
					{
						recipe_number = ini.get_value_int(0);
					}
					else if (ini.is_value("module_class"))
					{
						recipe.shortcut_number = ini.get_value_int(0);
					}
					else if (ini.is_value("cooking_rate"))
					{
						recipe.cooking_rate = ini.get_value_float(0);
					}
					else if (ini.is_value("credit_cost"))
					{
						recipe.credit_cost = ini.get_value_int(0);
					}
					else if (ini.is_value("consumed"))
					{
						ValidateItem(ini.get_value_string(0));
						recipe.consumed_items.emplace_back(make_pair(CreateID(ini.get_value_string(0)), ini.get_value_int(1)));
					}
					else if (ini.is_value("consumed_dynamic"))
					{
						int counter = 0;
						vector<pair<uint, uint>> vector;
						string itemName;
						do
						{
							itemName = ini.get_value_string(counter * 2);
							int amount = ini.get_value_int(counter * 2 + 1);
							if (!itemName.empty())
							{
								ValidateItem(itemName.c_str());
								vector.push_back({ CreateID(itemName.c_str()), amount });
							}
							counter++;
						} while (!itemName.empty());
						recipe.dynamic_consumed_items.push_back(vector);
					}
					else if (ini.is_value("consumed_dynamic_alt"))
					{
						DYNAMIC_ITEM items;
						items.sharedAmount = ini.get_value_int(0);
						string itemName;
						int counter = 1;
						do
						{
							itemName = ini.get_value_string(counter);
							if (!itemName.empty())
							{
								ValidateItem(itemName.c_str());
								items.items.push_back(CreateID(itemName.c_str()));
							}
							counter++;
						} while (!itemName.empty());
						recipe.dynamic_consumed_items_alt.push_back(items);
					}
					else if (ini.is_value("reqlevel"))
					{
						recipe.reqlevel = ini.get_value_int(0);
					}
					else if (ini.is_value("cargo_storage"))
					{
						recipe.moduleCargoStorage = ini.get_value_int(0);
					}
				}
				AddModuleRecipeToMaps(recipe, craft_types, build_type, recipe_number);
			}
		}
		ini.close();
	}

	if (ini.open(cfg_fileitems.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("recipe"))
			{
				RECIPE recipe;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						recipe.nickname = CreateID(ini.get_value_string(0));
						recipe.nicknameString = ini.get_value_string(0);
					}
					else if (ini.is_value("produced_item"))
					{
						ValidateItem(ini.get_value_string(0));
						int goodAmount = ini.get_value_int(1);
						if (!goodAmount)
						{
							ConPrint(L"Error: no amount or zero specified in a recipe %ls\n", stows(recipe.nicknameString).c_str());
							continue;
						}
						recipe.produced_items.emplace_back(make_pair(CreateID(ini.get_value_string(0)), ini.get_value_int(1)));
					}
					else if (ini.is_value("produced_affiliation"))
					{
						unordered_map<uint, pair<uint, uint>> itemMap;
						ValidateItem(ini.get_value_string(0));
						itemMap[0] = { CreateID(ini.get_value_string(0)), ini.get_value_int(1) };
						int counter = 0;

						string factionName;
						do
						{
							factionName = ini.get_value_string(counter * 3 + 2);
							string commodityName = ini.get_value_string(counter * 3 + 3);
							int amount = ini.get_value_int(counter * 3 + 4);
							if (!factionName.empty())
							{
								ValidateItem(commodityName.c_str());
								itemMap[MakeId(factionName.c_str())] = { CreateID(commodityName.c_str()), amount };
							}
							counter++;
						} while (!factionName.empty());

						recipe.affiliation_produced_items.push_back(itemMap);
					}
					else if (ini.is_value("loop_production"))
					{
						recipe.loop_production = ini.get_value_int(0);
					}
					else if (ini.is_value("shortcut_number"))
					{
						recipe.shortcut_number = ini.get_value_int(0);
					}
					else if (ini.is_value("craft_type"))
					{
						recipe.craft_type = stows(ToLower(ini.get_value_string(0)));
					}
					else if (ini.is_value("infotext"))
					{
						recipe.infotext = stows(ini.get_value_string());
					}
					else if (ini.is_value("cooking_rate"))
					{
						recipe.cooking_rate = ini.get_value_float(0);
					}
					else if (ini.is_value("credit_cost"))
					{
						recipe.credit_cost = ini.get_value_int(0);
					}
					else if (ini.is_value("consumed"))
					{
						ValidateItem(ini.get_value_string(0));
						recipe.consumed_items.emplace_back(make_pair(CreateID(ini.get_value_string(0)), ini.get_value_int(1)));
					}
					else if (ini.is_value("consumed_dynamic"))
					{
						int counter = 0;
						vector<pair<uint, uint>> vector;
						string itemName;
						do
						{
							itemName = ini.get_value_string(counter * 2);
							int amount = ini.get_value_int(counter * 2 + 1);
							if (!itemName.empty())
							{
								ValidateItem(itemName.c_str());
								vector.push_back({ CreateID(itemName.c_str()), amount });
							}
							counter++;
						} while (!itemName.empty());
						recipe.dynamic_consumed_items.push_back(vector);
					}
					else if (ini.is_value("consumed_dynamic_alt"))
					{
						DYNAMIC_ITEM items;
						items.sharedAmount = ini.get_value_int(0);
						string itemName;
						int counter = 1;
						do
						{
							itemName = ini.get_value_string(counter);
							if (!itemName.empty())
							{
								ValidateItem(itemName.c_str());
								items.items.push_back(CreateID(itemName.c_str()));
							}
							counter++;
						} while (!itemName.empty());
						recipe.dynamic_consumed_items_alt.push_back(items);
					}
					else if (ini.is_value("consumed_affiliation"))
					{
						unordered_map<uint, pair<uint, uint>> itemMap;
						ValidateItem(ini.get_value_string(0));
						itemMap[0] = { CreateID(ini.get_value_string(0)), ini.get_value_int(1) };
						int counter = 0;

						string factionName;
						do
						{
							factionName = ini.get_value_string(counter * 3 + 2);
							string commodityName = ini.get_value_string(counter * 3 + 3);
							int amount = ini.get_value_int(counter * 3 + 4);
							if (!factionName.empty())
							{
								ValidateItem(commodityName.c_str());
								itemMap[MakeId(factionName.c_str())] = { CreateID(commodityName.c_str()), amount };
							}
							counter++;
						} while (!factionName.empty());

						recipe.affiliation_consumed_items.push_back(itemMap);
					}
					else if (ini.is_value("catalyst"))
					{
						ValidateItem(ini.get_value_string(0));
						uint cargoHash = CreateID(ini.get_value_string(0));
						if (humanCargoList.count(cargoHash))
						{
							recipe.catalyst_workforce.emplace_back(make_pair(cargoHash, ini.get_value_int(1)));
						}
						else
						{
							recipe.catalyst_items.emplace_back(make_pair(cargoHash, ini.get_value_int(1)));
						}
					}
					else if (ini.is_value("reqlevel"))
					{
						recipe.reqlevel = ini.get_value_int(0);
					}
					else if (ini.is_value("affiliation_bonus"))
					{
						recipe.affiliationBonus[MakeId(ini.get_value_string(0))] = ini.get_value_float(1);
					}
					else if (ini.is_value("restricted"))
					{
						recipe.restricted = ini.get_value_bool(0);
					}
				}
				AddFactoryRecipeToMaps(recipe);
			}
		}
		ini.close();
	}

	PlayerCommands::PopulateHelpMenus();
}

/// Load the configuration
void LoadSettingsActual()
{
	returncode = DEFAULT_RETURNCODE;

	EquipmentUtilities::ReadIniNicknames();

	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string cfg_file = string(szCurDir) + R"(\flhook_plugins\base.cfg)";
	string cfg_filearch = string(szCurDir) + R"(\flhook_plugins\base_archtypes.cfg)";
	string cfg_fileforbiddencommodities = string(szCurDir) + R"(\flhook_plugins\base_forbidden_cargo.cfg)";
	uint bmapLoadHyperspaceHubConfig = 0;

	CSolar* csolar = (CSolar*)CObject::FindFirst(CObject::CSOLAR_OBJECT);
	while (csolar)
	{
		if (!csolar->dockTargetId || csolar->type != ObjectType::Station)
		{
			csolar = (CSolar*)CObject::FindNext();
			continue;
		}
		uint affiliation;
		Reputation::Vibe::GetAffiliation(csolar->id, affiliation, false);
		if (!factionCostumeMap.count(affiliation))
		{
			factionCostumeMap[affiliation] = { csolar->voiceId, csolar->commCostume };
		}
		csolar = (CSolar*)CObject::FindNext();
	}

	for (auto base : player_bases)
	{
		delete base.second;
	}

	player_bases.clear();
	construction_items.clear();
	set_base_repair_items.clear();
	set_base_crew_consumption_items.clear();
	set_base_crew_food_items.clear();
	humanCargoList.clear();

	DefenseModule::LoadSettings(string(szCurDir) + R"(\flhook_plugins\base_wp_ai.cfg)");

	INI_Reader ini;
	if (ini.open(cfg_file.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("general"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("debug"))
					{
						set_plugin_debug = ini.get_value_int(0);
					}
					else if (ini.is_value("export_cooldown"))
					{
						set_export_cooldown = ini.get_value_int(0);
					}
					else if (ini.is_value("status_path_html"))
					{
						set_status_path_html = ini.get_value_string();
					}
					else if (ini.is_value("status_path_json"))
					{
						set_status_path_json = ini.get_value_string();
					}
					else if (ini.is_value("set_status_path_json_public_shop"))
					{
						set_status_path_json_public_shop = ini.get_value_string();
					}
					else if (ini.is_value("damage_threshold"))
					{
						damage_threshold = ini.get_value_float(0);
					}
					else if (ini.is_value("siege_mode_damage_trigger_level"))
					{
						siege_mode_damage_trigger_level = ini.get_value_float(0);
					}
					else if (ini.is_value("siege_mode_chain_reaction_trigger_distance"))
					{
						siege_mode_damage_trigger_level = ini.get_value_float(0);
					}
					else if (ini.is_value("status_export_type"))
					{
						ExportType = ini.get_value_int(0);
					}
					else if (ini.is_value("damage_per_tick"))
					{
						set_damage_per_tick = ini.get_value_float(0);
					}
					else if (ini.is_value("no_crew_damage_multiplier"))
					{
						no_crew_damage_multiplier = ini.get_value_float(0);
					}
					else if (ini.is_value("damage_tick_time"))
					{
						set_damage_tick_time = ini.get_value_int(0);
					}
					else if (ini.is_value("tick_time"))
					{
						set_tick_time = ini.get_value_int(0);
					}
					else if (ini.is_value("health_to_heal_per_cycle"))
					{
						repair_per_repair_cycle = ini.get_value_int(0);
					}
					else if (ini.is_value("shield_reinforcement_threshold_per_core"))
					{
						shield_reinforcement_threshold_map.emplace(ini.get_value_int(0), ini.get_value_float(1));
					}
					else if (ini.is_value("shield_reinforcement_increment"))
					{
						shield_reinforcement_increment = ini.get_value_float(0);
					}
					else if (ini.is_value("base_shield_strength"))
					{
						base_shield_strength = ini.get_value_float(0);
					}
					else if (ini.is_value("base_vulnerability_window_length"))
					{
						vulnerability_window_length = ini.get_value_int(0);
					}
					else if (ini.is_value("single_vulnerability_window"))
					{
						single_vulnerability_window = ini.get_value_bool(0);
					}
					else if (ini.is_value("defense_platform_activation_offset"))
					{
						defense_platform_activation_offset = ini.get_value_int(0);
					}
					else if (ini.is_value("construction_shiparch"))
					{
						set_construction_shiparch = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("construction_item"))
					{
						ValidateItem(ini.get_value_string(0));
						uint good = CreateID(ini.get_value_string(0));
						uint quantity = ini.get_value_int(1);
						construction_items[good] = quantity;
					}
					else if (ini.is_value("construction_credit_cost"))
					{
						construction_credit_cost = ini.get_value_int(0);
					}
					else if (ini.is_value("base_crew_item"))
					{
						ValidateItem(ini.get_value_string(0));
						set_base_crew_type = CreateID(ini.get_value_string(0));
						humanCargoList.insert(set_base_crew_type);
					}
					else if (ini.is_value("human_cargo_item"))
					{
						ValidateItem(ini.get_value_string(0));
						humanCargoList.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("base_repair_item"))
					{
						ValidateItem(ini.get_value_string(0));
						REPAIR_ITEM item;
						item.good = CreateID(ini.get_value_string(0));
						item.quantity = ini.get_value_int(1);
						set_base_repair_items.emplace_back(item);
					}
					else if (ini.is_value("base_crew_consumption_item"))
					{
						ValidateItem(ini.get_value_string(0));
						uint good = CreateID(ini.get_value_string(0));
						set_base_crew_consumption_items.emplace_back(good);
					}
					else if (ini.is_value("base_crew_food_item"))
					{
						ValidateItem(ini.get_value_string(0));
						uint good = CreateID(ini.get_value_string(0));
						set_base_crew_food_items.emplace_back(good);
					}
					else if (ini.is_value("set_crew_check_frequency"))
					{
						set_crew_check_frequency = ini.get_value_int(0);
					}
					else if (ini.is_value("set_new_spawn"))
					{
						set_new_spawn = true;
					}
					else if (ini.is_value("set_holiday_mode"))
					{
						set_holiday_mode = ini.get_value_bool(0);
						if (set_holiday_mode)
						{
							ConPrint(L"BASE: Attention, POB Holiday mode is enabled.\n");
						}
					}
					else if (ini.is_value("watch"))
					{
						uint c = CreateID(ini.get_value_string());
						listCommodities[c] = stows(ini.get_value_string());

					}
					else if (ini.is_value("min_mining_distance"))
					{
						minMiningDistance = max(0.0f, ini.get_value_float(0));
					}
					else if (ini.is_value("min_planet_distance"))
					{
						minPlanetDistance = max(0.0f, ini.get_value_float(0));
					}
					else if (ini.is_value("min_station_distance"))
					{
						minStationDistance = max(0.0f, ini.get_value_float(0));
					}
					else if (ini.is_value("min_trade_lane_distance"))
					{
						minLaneDistance = max(0.0f, ini.get_value_float(0));
					}
					else if (ini.is_value("min_distance_misc"))
					{
						minDistanceMisc = max(0.0f, ini.get_value_float(0));
					}
					else if (ini.is_value("min_pob_distance"))
					{
						minOtherPOBDistance = max(0.0f, ini.get_value_float(0));
					}
					else if (ini.is_value("min_jump_distance"))
					{
						minJumpDistance = max(0.0f, ini.get_value_float(0));
					}
					else if (ini.is_value("low_tier_mining_exemption"))
					{
						lowTierMiningCommoditiesSet.insert(CreateID(ini.get_value_string()));
					}
					else if(ini.is_value("deployment_cooldown"))
					{
						deploymentCooldownDuration = ini.get_value_int(0);
					}
					else if (ini.is_value("enable_distance_check"))
					{
						enableDistanceCheck = ini.get_value_bool(0);
					}
					else if (ini.is_value("banned_system"))
					{
						bannedSystemList.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("randomize_hyperspace_hub_days"))
					{
						string typeStr = ToLower(ini.get_value_string(0));
						if (typeStr.find("monday") != string::npos)
							bmapLoadHyperspaceHubConfig |= 1 << 0;
						if (typeStr.find("tuesday") != string::npos)
							bmapLoadHyperspaceHubConfig |= 1 << 1;
						if (typeStr.find("wednesday") != string::npos)
							bmapLoadHyperspaceHubConfig |= 1 << 2;
						if (typeStr.find("thursday") != string::npos)
							bmapLoadHyperspaceHubConfig |= 1 << 3;
						if (typeStr.find("friday") != string::npos)
							bmapLoadHyperspaceHubConfig |= 1 << 4;
						if (typeStr.find("saturday") != string::npos)
							bmapLoadHyperspaceHubConfig |= 1 << 5;
						if (typeStr.find("sunday") != string::npos)
							bmapLoadHyperspaceHubConfig |= 1 << 6;
						if (typeStr.find("always") != string::npos)
							bmapLoadHyperspaceHubConfig |= 0xff;
					}
					else if (ini.is_value("siege_gun"))
					{
						siegeWeaponryMap[CreateID(ini.get_value_string(0))] = ini.get_value_float(1);
					}
					else if (ini.is_value("vulnerability_window_change_cooldown"))
					{
						vulnerability_window_change_cooldown = 3600 * 24 * ini.get_value_int(0);
					}
					else if (ini.is_value("core_recipe"))
					{
						core_upgrade_recipes[ini.get_value_int(0)] = CreateID(ini.get_value_string(1));
					}
					else if (ini.is_value("core_storage"))
					{
						core_upgrade_storage[ini.get_value_int(0)] = ini.get_value_int(1);
					}
					else if (ini.is_value("rearmament_good"))
					{
						uint goodId = CreateID(ini.get_value_string(0));
						auto gi = GoodList::find_by_id(goodId);
						float creditValue = ini.get_value_float(1);
						if (creditValue > gi->fPrice)
						{
							ConPrint(L"BASE ERROR: rearmament good %s set too high, setting to %0.0f!\n", stows(ini.get_value_string(0)).c_str(), gi->fPrice);
							creditValue = gi->fPrice;
						}
						rearmamentCreditRatio.push_back({ goodId, creditValue });
					}
				}
			}
		}
		ini.close();
	}

	if (ini.open(cfg_filearch.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("arch"))
			{
				ARCHTYPE_STRUCT archstruct;
				string nickname = "default";
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						nickname = ini.get_value_string(0);
					}
					else if (ini.is_value("invulnerable"))
					{
						archstruct.invulnerable = ini.get_value_int(0);
					}
					else if (ini.is_value("logic"))
					{
						archstruct.logic = ini.get_value_int(0);
					}
					else if (ini.is_value("idrestriction"))
					{
						archstruct.idrestriction = ini.get_value_int(0);
					}
					else if (ini.is_value("isjump"))
					{
						archstruct.isjump = ini.get_value_int(0);
					}
					else if (ini.is_value("ishubreturn"))
					{
						archstruct.ishubreturn = ini.get_value_int(0);
					}
					else if (ini.is_value("shipclassrestriction"))
					{
						archstruct.shipclassrestriction = ini.get_value_int(0);
					}
					else if (ini.is_value("allowedshipclasses"))
					{
						archstruct.allowedshipclasses.insert(ini.get_value_int(0));
					}
					else if (ini.is_value("allowedids"))
					{
						archstruct.allowedids.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("display"))
					{
						archstruct.display = ini.get_value_bool(0);
					}
					else if (ini.is_value("miningevent"))
					{
						archstruct.miningevent = ini.get_value_string(0);
					}
					else if (ini.is_value("shield"))
					{
						archstruct.hasShield = ini.get_value_int(0);
					}
					else if (ini.is_value("siegegunonly"))
					{
						archstruct.siegeGunOnly = ini.get_value_int(0);
					}
					else if (ini.is_value("vulnerabilitywindow"))
					{
						archstruct.vulnerabilityWindowUse = ini.get_value_int(0);
					}
				}
				mapArchs[nickname] = archstruct;
			}
		}
		ini.close();
	}
  
	if (ini.open(cfg_fileforbiddencommodities.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("forbidden_commodities"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("commodity_name"))
					{
						forbidden_player_base_commodity_set.insert(CreateID(ini.get_value_string(0)));
					}
				}
			}
		}
		ini.close();
	}

	LoadRecipes();

	char datapath[MAX_PATH];
	GetUserDataPath(datapath);

	// Create base account dir if it doesn't exist
	string basedir = string(datapath) + R"(\Accts\MultiPlayer\player_bases\)";
	CreateDirectoryA(basedir.c_str(), 0);

	// Load and spawn all bases
	string path = string(datapath) + R"(\Accts\MultiPlayer\player_bases\base_*.ini)";

	WIN32_FIND_DATA findfile;
	HANDLE h = FindFirstFile(path.c_str(), &findfile);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			string filepath = string(datapath) + R"(\Accts\MultiPlayer\player_bases\)" + findfile.cFileName;
			PlayerBase *base = new PlayerBase(filepath);
			if (base && !base->nickname.empty())
			{
				if (player_bases.count(base->base))
				{
					AddLog("BASE %s ALREADY EXISTS!\nPATH: %s\nPATH2: %s", wstos(player_bases[base->base]->basename).c_str(), filepath.c_str(), player_bases[base->base]->path.c_str());
					ConPrint(L"BASE %s ALREADY EXISTS!\nPATH: %s\nPATH2 %s\n", player_bases[base->base]->basename.c_str(), stows(filepath).c_str(), stows(player_bases[base->base]->path.c_str()));
					continue;
				}
				player_bases[base->base] = base;
				base->Spawn();
			}
			else
			{
				AddLog("ERROR POB file corrupted: %s", findfile.cFileName);
			}
		} while (FindNextFile(h, &findfile));
		FindClose(h);
	}

	// loadHyperspaceHubConfig is weekday where 0 = sunday, 6 = saturday
	// if it's today, randomize appropriate 'jump hole' POBs
	if (bmapLoadHyperspaceHubConfig)
	{
		time_t tNow = time(0);
		struct tm *t = localtime(&tNow);
		uint currWeekday = (t->tm_wday + 6)%7; // conversion from sunday-week-start to monday-start
		if (bmapLoadHyperspaceHubConfig & (1 << currWeekday)) // 1 - monday, 2 - tuesday, 4 - wednesday and so on
		{
			HyperJump::LoadHyperspaceHubConfig(string(szCurDir));
		}
	}

	HyperJump::InitJumpHoleConfig();

	// Load and sync player state
	struct PlayerData *pd = 0;
	while (pd = Players.traverse_active(pd))
	{
		uint client = pd->iOnlineID;
		if (HkIsInCharSelectMenu(client))
			continue;

		// If this player is in space, set the reputations.
		if (pd->iShipID)
			SyncReputationForClientShip(pd->iShipID, client);

		// Get state if player is in player base and  reset the commodity list
		// and send a dummy entry if there are no commodities in the market
		LoadDockState(client);
		if (clients[client].player_base)
		{
			PlayerBase *base = GetPlayerBaseForClient(client);
			if (base)
			{
				// Reset the commodity list	and send a dummy entry if there are no
				// commodities in the market
				SaveDockState(client);
				SendMarketGoodSync(base, client);
				SendBaseStatus(client, base);
			}
			else
			{
				// Force the ship to launch to space as the base has been destroyed
				DeleteDockState(client);
				SendResetMarketOverride(client);
				ForceLaunch(client);
			}
		}
	}
	PlayerCommands::Aff_initer();
}


void RebuildCSolarSystemList()
{
	POBSolarsBySystemMap.clear();
	for (auto& base : player_bases)
	{
		if (!base.second->baseCSolar)
		{
			continue;
		}
		uint solarId = base.second->base;
		IObjRW* iobjPtr;
		StarSystem* starSystem;
		GetShipInspect(solarId, iobjPtr, starSystem);
		if (iobjPtr)
		{
			CSolar* csolar = reinterpret_cast<CSolar*>(iobjPtr->cobj);
			if (csolar != base.second->baseCSolar)
			{
				base.second->baseCSolar = csolar;
				if (base.second->archetype && base.second->archetype->isjump)
				{
					base.second->baseCSolar->jumpDestSystem = base.second->destSystem;
					base.second->baseCSolar->jumpDestObj = CreateID(base.second->destObjectName.c_str());
				}
				ConPrint(L"Base solar changed! %ls\n", base.second->basename.c_str());
				AddLog("Base solar changed! %s", wstos(base.second->basename).c_str());
			}
			POBSolarsBySystemMap[base.second->system].insert(csolar);
		}
		else
		{
			ConPrint(L"Base solar went missing! %ls\n", base.second->basename.c_str());
			AddLog("Base solar went missing! %s", wstos(base.second->basename).c_str());
			base.second->baseCSolar = nullptr;
		}

	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HkTimerCheckKick()
{
	returncode = DEFAULT_RETURNCODE;

	if (load_settings_required)
	{
		load_settings_required = false;
		LoadSettingsActual();
	}

	if (!basesToRespawn.empty())
	{
		for (auto& iter = basesToRespawn.begin() ; iter != basesToRespawn.end();)
		{
			if (--iter->secondsUntil != 0)
			{
				iter++;
				continue;
			}

			WIN32_FIND_DATA findfile;
			HANDLE h = FindFirstFile(iter->path.c_str(), &findfile);
			if (h == INVALID_HANDLE_VALUE)
			{
				ConPrint(L"Unable to respawn base, file not found:\n%s\n", stows(iter->path).c_str());
				iter = basesToRespawn.erase(iter);
				continue;
			}

			uint baseNickname = CreateID(IniGetS(iter->path, "Base", "nickname", "").c_str());

			pub::SpaceObj::Destroy(baseNickname, DestroyType::VANISH);

			PlayerBase* base = new PlayerBase(iter->path);

			FindClose(h);
			if (base && !base->nickname.empty())
			{
				player_bases[base->base] = base;
				base->Spawn();
				ConPrint(L"Base %s respawned.\n", base->basename.c_str());
			}
			iter = basesToRespawn.erase(iter);
		}
	}

	uint curr_time = (uint)time(0);

	if (curr_time % set_tick_time == 0)
	{
		RebuildCSolarSystemList();
	}

	for(auto& iter : player_bases)
	{
		PlayerBase *base = iter.second;
		base->Timer(curr_time);
	}

	if (!player_bases.empty())
	{
		if (baseSaveIterator == player_bases.end())
		{
			baseSaveIterator = player_bases.begin();
		}
		bool saveSuccessful = false;
		while (!saveSuccessful && baseSaveIterator != player_bases.end())
		{
			auto& pb = baseSaveIterator->second;
			if (pb->logic == 1 || pb->invulnerable == 0)
			{
				if (pb->pinned_item_updated && pb->baseCSolar)
				{
					pb->UpdateBaseInfoText();
					pb->pinned_item_updated = false;
				}
				pb->Save();
				saveSuccessful = true;
			}
			baseSaveIterator++;
		}
	}

	if ((curr_time % set_export_cooldown) == 0)
	{
		// Write status to an html formatted page every 60 seconds
		if ((ExportType == 0 || ExportType == 2) && set_status_path_html.size() > 0)
		{
			ExportData::ToHTML();
		}

		// Write status to a json formatted page every 60 seconds
		if ((ExportType == 1 || ExportType == 2) && set_status_path_json.size() > 0)
		{
			ExportData::ToJSON();
		}
	}

	if ((curr_time % set_tick_time) == 0)
	{
		ExportData::ToJSONBasic();
	}
}

bool __stdcall HkCb_IsDockableError(uint dock_with, uint base)
{
	if (GetPlayerBase(base) || customSolarList.count(base))
	{
		return false;
	}
	ConPrint(L"ERROR: Base not found dock_with=%08x base=%08x\n", dock_with, base);
	return true;
}

__declspec(naked) void HkCb_IsDockableErrorNaked()
{
	__asm
	{
		test[esi + 0x1b4], eax
		jnz no_error
		push[edi + 0xB8]
		push[esi + 0x1b4]
		call HkCb_IsDockableError
		test al, al
		jz no_error
		push 0x62b76d3
		ret
		no_error :
		push 0x62b76fc
			ret
	}
}

bool __stdcall HkCb_Land(IObjRW *obj, uint base_dock_id, uint base)
{
	if (!obj)
	{
		return true;
	}
	uint client = reinterpret_cast<CShip*>(obj->cobj)->ownerPlayer;
	if (!client)
	{
		return true;
	}

	auto& clientData = clients[client];

	clientData.player_base = clientData.docking_base;
	clientData.last_player_base = clientData.docking_base;
	clientData.docking_base = 0;

	return true;
}

__declspec(naked) void HkCb_LandNaked()
{
	__asm
	{
		mov al, [ebx + 0x1c]
		test al, al
		jz not_in_dock

		mov eax, [ebx + 0x18] // base id
		push eax
		mov eax, [esp + 0x14] // dock target
		push eax
		push edi // objinspect
		call HkCb_Land
		test al, al
		jz done

		not_in_dock :
		// Copied from moor.dll to support mooring.
		mov	al, [ebx + 0x1c]
			test	al, al
			jnz	done
			// It's false, so a safe bet that it's a moor.  Is it the player?
			mov	eax, [edi]
			mov	ecx, edi
			call[eax + 0xbc] // is_player
			test	al, al
			jnz done




			done :
		push 0x6D0C251
			ret
	}
}

static bool patched = false;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	srand((uint)time(0));
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (!patched)
		{
			patched = true;

			hModServer = GetModuleHandleA("server.dll");
			{

				// Call our function on landing
				byte patch[] = { 0xe9 }; // jmpr
				WriteProcMem((char*)hModServer + 0x2c24c, patch, sizeof(patch));
				PatchCallAddr((char*)hModServer, 0x2c24c, (char*)HkCb_LandNaked);
			}

			hModCommon = GetModuleHandleA("common.dll");
			{
				// Suppress "is dockable " error message
				byte patch[] = { 0xe9 }; // jmpr
				WriteProcMem((char*)hModCommon + 0x576cb, patch, sizeof(patch));
				PatchCallAddr((char*)hModCommon, 0x576cb, (char*)HkCb_IsDockableErrorNaked);
			}

			{
				// Suppress GetArch() error on max hit points call
				byte patch[] = { 0x90, 0x90 }; // nop nop
				WriteProcMem((char*)hModCommon + 0x995b6, patch, sizeof(patch));
				WriteProcMem((char*)hModCommon + 0x995fc, patch, sizeof(patch));
			}
		}

		HkLoadStringDLLs();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		if (patched)
		{
			{
				// Unpatch the landing hook
				byte patch[] = { 0x8A, 0x43, 0x1C, 0x84, 0xC0 };
				WriteProcMem((char*)hModServer + 0x2c24c, patch, sizeof(patch));
			}

			{
				// Unpatch the Suppress "is dockable " error message
				byte patch[] = { 0x85, 0x86, 0xb4, 0x01, 0x00 };
				WriteProcMem((char*)hModCommon + 0x576cb, patch, sizeof(patch));
			}
		}

		for (auto& base : player_bases)
		{
			delete base.second;
		}
		player_bases.clear();

		for (uint customSolar : customSolarList)
		{
			if (pub::SpaceObj::ExistsAndAlive(customSolar) == 0) // this method returns -2 for dead, 0 for alive
			{
				pub::SpaceObj::Destroy(customSolar, DestroyType::FUSE);
			}
		}
		customSolarList.clear();

		HkUnloadStringDLLs();
	}
	return true;
}

bool UserCmd_Process(uint client, const wstring &args)
{
	returncode = DEFAULT_RETURNCODE;
	if (args.find(L"/base login") == 0)
	{
		PlayerCommands::BaseLogin(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base addpwd") == 0)
	{
		PlayerCommands::BaseAddPwd(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base rmpwd") == 0)
	{
		PlayerCommands::BaseRmPwd(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base lstpwd") == 0)
	{
		PlayerCommands::BaseLstPwd(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base setmasterpwd") == 0)
	{
		PlayerCommands::BaseSetMasterPwd(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/access") == 0)
	{
		PlayerCommands::BaseAccess(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base myfac") == 0)
	{
		PlayerCommands::BaseViewMyFac(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base rep") == 0)
	{
		PlayerCommands::BaseRep(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base defensemode") == 0)
	{
		PlayerCommands::BaseDefenseMode(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base deploy") == 0)
	{
		PlayerCommands::BaseDeploy(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base testdeploy") == 0)
	{
		PlayerCommands::BaseTestDeploy(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base setpublic") == 0)
	{
		PlayerCommands::BaseSetPublic(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/shop") == 0)
	{
		PlayerCommands::Shop(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/bank") == 0)
	{
		PlayerCommands::Bank(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base info") == 0)
	{
		PlayerCommands::BaseInfo(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base supplies") == 0)
	{
		PlayerCommands::GetNecessitiesStatus(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/craft") == 0)
	{
		PlayerCommands::BaseFacMod(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base defmod") == 0)
	{
		PlayerCommands::BaseDefMod(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/build") == 0)
	{
		PlayerCommands::BaseBuildMod(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/destroy") == 0)
	{
		PlayerCommands::BaseBuildModDestroy(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base swap") == 0)
	{
		PlayerCommands::BaseSwapModule(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base setshield") == 0)
	{
		PlayerCommands::BaseSetVulnerabilityWindow(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base setfood") == 0)
	{
		PlayerCommands::SetPrefFood(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/base") == 0)
	{
		PlayerCommands::BaseHelp(client, args);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"/rearm") == 0)
	{
		RearmamentModule::Rearm(client);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	return false;
}

static void ForcePlayerBaseDock(uint client, PlayerBase *base)
{
	auto& cd = clients[client];
	cd.player_base = base->base;
	cd.last_player_base = base->base;
	cd.docking_base = base->base;

	if (set_plugin_debug > 1)
	{
		ConPrint(L"ForcePlayerBaseDock client=%u player_base=%u\n", client, cd.player_base);
	}
	HkBeamById(client, base->proxy_base);
}

static bool IsDockingAllowed(PlayerBase *base, uint client)
{
	// Base allows neutral ships to dock
	if (base->GetAttitudeTowardsClient(client) > -0.55f)
	{
		return true;
	}

	return false;
}

int __cdecl Dock_Call(unsigned int const &iShip, unsigned int const &base, int& iCancel, enum DOCK_HOST_RESPONSE& response)
{
	returncode = DEFAULT_RETURNCODE;

	//AP::ClearClientInfo(client);

	if (!((response == PROCEED_DOCK || response == DOCK) && iCancel != -1))
	{
		return 0;
	}
	PlayerBase* pbase = GetPlayerBase(base);
	if (!pbase)
	{
		return 0;
	}

	uint client = HkGetClientIDByShip(iShip);

	if (!client)
	{
		return 0;
	}

	if (!pbase->dockKeyList.empty())
	{
		CShip* cship = ClientInfo[client].cship;
		CEquipTraverser tr(-1);
		CEquip* equip;
		bool success = false;
		while (equip = cship->equip_manager.Traverse(tr))
		{
			if (pbase->dockKeyList.count(equip->archetype->iArchID))
			{
				success = true;
				break;
			}
		}

		if (!success)
		{
			if (pbase->noDockKeyMessage.empty())
			{
				PrintUserCmdText(client, L"ERR Unable to dock");
			}
			else
			{
				PrintUserCmdText(client, pbase->noDockKeyMessage.c_str());
			}

			iCancel = -1;
			response = ACCESS_DENIED;
			return 0;
		}
	}

	if (pbase->archetype && pbase->archetype->isjump == 1)
	{
		//check if we have an ID restriction
		if (pbase->archetype->idrestriction == 1)
		{
			bool foundid = false;
			for (list<EquipDesc>::iterator item = Players[client].equipDescList.equip.begin(); item != Players[client].equipDescList.equip.end(); item++)
			{
				if (item->bMounted && pbase->archetype->allowedids.count(item->iArchID))
				{
					foundid = true;
					break;
				}
			}
			if (foundid == false)
			{
				PrintUserCmdText(client, L"ERR Unable to dock with this ID.");
				iCancel = -1;
				response = ACCESS_DENIED;
				return 0;
			}
		}

		//check if we have a shipclass restriction
		if (pbase->archetype->shipclassrestriction == 1)
		{
			bool foundclass = false;
			// get the player ship class
			Archetype::Ship* TheShipArch = Archetype::GetShip(Players[client].iShipArchetype);
			uint shipclass = TheShipArch->iShipClass;

			if(!pbase->archetype->allowedshipclasses.count(shipclass))
			{
				PrintUserCmdText(client, L"ERR Unable to dock with a vessel of this type.");
				iCancel = -1;
				response = ACCESS_DENIED;
				return 0;
			}
		}

		return 0;
	}

	// Shield is up, docking is not possible.
	if (pbase->shield_timeout)
	{
		PrintUserCmdText(client, L"Docking failed because base shield is active");
		iCancel = -1;
		response = ACCESS_DENIED;
		return 0;
	}

	if (!IsDockingAllowed(pbase, client))
	{
		PrintUserCmdText(client, L"Docking at this base is restricted");
		iCancel = -1;
		response = ACCESS_DENIED;
		return 0;
	}

	clients[client].docking_base = base;

	SendBaseStatus(client, pbase);
	
	return 0;
}

void __stdcall CharacterSelect(struct CHARACTER_ID const &cId, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	// Sync base names for the new player
	for (auto& base : player_bases)
	{
		HkChangeIDSString(client, base.second->solar_ids, base.second->basename);
		if (base.second->baseCSolar && base.second->description_ids)
		{
			SendBaseIDSList(client, base.second->baseCSolar->id, base.second->description_ids);
			HkChangeIDSString(client, base.second->description_ids, base.second->description_text);
		}
	}
}

void __stdcall CharacterSelect_AFTER(struct CHARACTER_ID const &cId, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	auto& cd = clients[client];

	if (set_plugin_debug > 1)
		ConPrint(L"CharacterSelect_AFTER client=%u player_base=%u\n", client, cd.player_base);

	HyperJump::CharacterSelect_AFTER(client);

	// If this ship is in a player base is then set then docking ID to emulate
	// a landing.
	LoadDockState(client);
	if (cd.player_base)
	{
		if (set_plugin_debug > 1)
			ConPrint(L"CharacterSelect_AFTER[2] client=%u player_base=%u\n", client, cd.player_base);

		// If this base does not exist, dump the ship into space
		PlayerBase *base = GetPlayerBase(cd.player_base);
		if (!base)
		{
			DeleteDockState(client);
			SendResetMarketOverride(client);
			ForceLaunch(client);
		}
	}
}

void __stdcall BaseEnter(uint baseId, uint client)
{
	auto& cd = clients[client];

	if (set_plugin_debug > 1)
		ConPrint(L"BaseEnter base=%u client=%u player_base=%u last_player_base=%u\n", baseId, client,
			cd.player_base, cd.last_player_base);

	returncode = DEFAULT_RETURNCODE;

	cd.admin = false;
	cd.viewshop = false;

	// Check if ships is currently docked on a docking module
	CUSTOM_MOBILE_DOCK_CHECK_STRUCT mobileCheck;
	mobileCheck.iClientID = client;
	Plugin_Communication(PLUGIN_MESSAGE::CUSTOM_MOBILE_DOCK_CHECK, &mobileCheck);

	// skip processing in case of having docked on player ship to avoid overwriting F9 menu.
	if (mobileCheck.isMobileDocked)
	{
		return;
	}

	// If the last player base is set then we have not docked at a non player base yet.
	if (cd.last_player_base)
	{
		cd.player_base = cd.last_player_base;
	}

	// If the player is registered as being in a player controlled base then 
	// send the economy update, player system update and save a file to indicate
	// that we're in the base->
	if (cd.player_base)
	{
		PlayerBase *base = GetPlayerBaseForClient(client);
		if (base)
		{
			if (base->proxy_base != baseId)
			{
				PrintUserCmdText(client, L"The POB you are on has moved systems. Launch to be beamed back to it.");
				return;
			}
			if (!IsDockingAllowed(base, client))
			{
				wstring rights;
				HkGetAdmin((const wchar_t*)Players.GetActiveCharacterName(client), rights);
				if (rights.find(L"superadmin") == wstring::npos
					&& rights.find(L"beamkill") == wstring::npos)
				{
					ForceLaunch(client);
					SendResetMarketOverride(client);
					DeleteDockState(client);
					PrintUserCmdText(client, L"You are no longer welcome on this base.");

					return;
				}
			}

			RearmamentModule::CheckPlayerInventory(client, base);
			// Reset the commodity list	and send a dummy entry if there are no
			// commodities in the market
			SaveDockState(client);
			SendMarketGoodSync(base, client);
			SendBaseStatus(client, base);
			return;
		}
		else
		{
			// Force the ship to launch to space as the base has been destroyed
			DeleteDockState(client);
			SendResetMarketOverride(client);
			ForceLaunch(client);
			return;
		}
	}

	DeleteDockState(client);
	SendResetMarketOverride(client);
}

void __stdcall BaseExit(uint base, uint client)
{
	returncode = DEFAULT_RETURNCODE;

	auto& cd = clients[client];

	if (set_plugin_debug > 1)
		ConPrint(L"BaseExit base=%u client=%u player_base=%u\n", base, client, cd.player_base);

	// Reset client state and save it retaining the last player base ID to deal with respawn.
	cd.admin = false;
	cd.viewshop = false;

	CUSTOM_MOBILE_DOCK_CHECK_STRUCT mobileCheck;
	mobileCheck.iClientID = client;
	Plugin_Communication(PLUGIN_MESSAGE::CUSTOM_MOBILE_DOCK_CHECK, &mobileCheck);

	if (cd.player_base || mobileCheck.isMobileDocked)
	{
		if (set_plugin_debug)
		{
			ConPrint(L"BaseExit base=%u client=%u player_base=%u\n", base, client, cd.player_base);
		}

		if (cd.player_base) {
			cd.last_player_base = cd.player_base;
			cd.player_base = 0;
			SaveDockState(client);
			cd.player_base = cd.last_player_base;
		}
	}
	else
	{
		DeleteDockState(client);
	}

	// Clear the base market and text
	SendResetMarketOverride(client);
	SendSetBaseInfoText2(client, L"");

}

void __stdcall RequestEvent(int iIsFormationRequest, unsigned int iShip, unsigned int iDockTarget, unsigned int p4, unsigned long p5, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	if (client)
	{
		if (!iIsFormationRequest)
		{
			PlayerBase *base = GetPlayerBase(iDockTarget);
			if (base)
			{
				// Shield is up, docking is not possible.
				if (base->shield_timeout)
				{
					PrintUserCmdText(client, L"Docking failed because base shield is active");
					pub::Player::SendNNMessage(client, pub::GetNicknameId("info_access_denied"));
					returncode = SKIPPLUGINS_NOFUNCTIONCALL;
					return;
				}

				if (!IsDockingAllowed(base, client))
				{
					PrintUserCmdText(client, L"Docking at this base is restricted");
					pub::Player::SendNNMessage(client, pub::GetNicknameId("info_access_denied"));
					returncode = SKIPPLUGINS_NOFUNCTIONCALL;
					return;
				}
			}
		}
	}
}

struct LaunchComm
{
	int dockId;
	CSolar* solar;
};

static std::unordered_map<uint, LaunchComm> unprocessedLaunchComms;

/// The base the player is launching from.
PlayerBase* player_launch_base = nullptr;

/// If the ship is launching from a player base record this so that
/// override the launch location.
bool __stdcall LaunchPosHook(uint space_obj, const CEqObj &p1, const Vector &pos, const Matrix &rot, int dockId, uint client)
{
	returncode = DEFAULT_RETURNCODE;
	if (player_launch_base)
	{
		unprocessedLaunchComms[client] = { dockId, player_launch_base->baseCSolar };
	}
	return true;
}

/// If the ship is launching from a player base record this so that
/// we will override the launch location.
void __stdcall PlayerLaunch(unsigned int ship, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	player_launch_base = nullptr;

	if (set_plugin_debug > 1)
		ConPrint(L"PlayerLaunch ship=%u client=%u\n", ship, client);

	auto& cd = clients[client];
	if (!cd.player_base)
		return;

	player_launch_base = GetPlayerBase(cd.player_base);
	if (!player_launch_base)
	{
		return;
	}

	auto pobListIter = POBSolarsBySystemMap.find(player_launch_base->system);
	if (pobListIter == POBSolarsBySystemMap.end())
	{
		return;
	}

	for (auto& solar : pobListIter->second)
	{
		if (solar == player_launch_base->baseCSolar)
		{
			continue;
		}
		solar->dockTargetId2 = 0;
	}
	cd.player_base = 0;
}

static uint GetShipMessageId(uint shipId)
{
	IObjRW* inspect;
	StarSystem* starSystem;
	if (!GetShipInspect(shipId, inspect, starSystem))
		return 0;
	const Archetype::Ship* shipArchetype = static_cast<Archetype::Ship*>(inspect->cobj->archetype);
	char msgIdPrefix[64];
	strncpy_s(msgIdPrefix, sizeof(msgIdPrefix), shipArchetype->msgidprefix_str, shipArchetype->msgidprefix_len);
	return CreateID(msgIdPrefix);
}

static void SendLaunchWellWishes(uint shipId, LaunchComm& launchComm)
{
	try
	{
		int vecSize = launchComm.solar->solararch()->dockInfo.size();
		if (vecSize < launchComm.dockId)
		{
			return;
		}
		auto& dockInfo = launchComm.solar->solararch()->dockInfo.at(launchComm.dockId);

		std::string clearMessageIdBase;
		switch (dockInfo.dockType)
		{
		case Archetype::DockType::Berth:
		case Archetype::DockType::Jump:
			clearMessageIdBase = "gcs_docklaunch_clear_berth_0";
			break;

		case Archetype::DockType::MoorSmall:
		case Archetype::DockType::MoorMedium:
		case Archetype::DockType::MoorLarge:
			clearMessageIdBase = "gcs_docklaunch_clear_moor_0";
			break;

		case Archetype::DockType::Ring:
			clearMessageIdBase = "gcs_docklaunch_clear_ring_0";
			break;

		default:
			return;
		}

		std::vector<uint> lines = {
			GetShipMessageId(shipId),
			CreateID((clearMessageIdBase + std::to_string(GetRandom(1,2)) + "-").c_str()),
			CreateID(("gcs_misc_wellwish_0" + std::to_string(GetRandom(1,2)) + "-").c_str())
		};

		pub::SpaceObj::SendComm(launchComm.solar->id, shipId, launchComm.solar->voiceId, &launchComm.solar->commCostume, 0, lines.data(), lines.size(), 19007 /* base comms type*/, 0.5f, false);
	}
	catch (...)
	{
		AddLog("BASEUNDOCKECEPTIONCATCH %x %u %u", launchComm.solar->id, launchComm.dockId, (uint)launchComm.solar->isDynamic);
		return;
	}
}

void __stdcall PlayerLaunch_AFTER(unsigned int ship, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	if (HyperJump::markedForDeath.count(client))
	{
		HyperJump::markedForDeath.erase(client);
		pub::SpaceObj::SetRelativeHealth(ship, 0.0f);
		returncode = SKIPPLUGINS;
		return;
	}

	if (player_launch_base)
	{
		if (Players[client].iSystemID != player_launch_base->system)
		{
			ForcePlayerBaseDock(client, player_launch_base);
		}

		for (auto& solar : POBSolarsBySystemMap[player_launch_base->system])
		{
			if (solar == player_launch_base->baseCSolar)
			{
				continue;
			}
			solar->dockTargetId2 = player_launch_base->proxy_base;
		}

		auto launchCommIter = unprocessedLaunchComms.find(client);
		if (launchCommIter != unprocessedLaunchComms.end())
		{
			SendLaunchWellWishes(ship, launchCommIter->second);
			unprocessedLaunchComms.erase(client);
		}

		player_launch_base = nullptr;
	}

	SyncReputationForClientShip(ship, client);
}

void __stdcall JumpInComplete(unsigned int system, unsigned int ship)
{
	returncode = DEFAULT_RETURNCODE;

	if (set_plugin_debug > 1)
		ConPrint(L"JumpInComplete system=%u ship=%u\n");

	uint client = HkGetClientIDByShip(ship);
	if (client)
	{
		SyncReputationForClientShip(ship, client);
	}
}

bool lastTransactionBase = false;
uint lastTransactionArchID = 0;
int lastTransactionCount = 0;
uint lastTransactionClientID = 0;

bool CheckIfCommodityForbidden(uint goodId)
{
	return forbidden_player_base_commodity_set.find(goodId) != forbidden_player_base_commodity_set.end();
}

void __stdcall GFGoodSell(struct SGFGoodSellInfo const &gsi, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	lastTransactionBase = false;

	// If the client is in a player controlled base
	PlayerBase *base = GetPlayerBaseForClient(client);
	if (!base)
	{
		return;
	}
	returncode = SKIPPLUGINS_NOFUNCTIONCALL;

	auto& cd = clients[client];

	if (base->market_items.find(gsi.iArchID) == base->market_items.end()
		&& !cd.admin)
	{
		PrintUserCmdText(client, L"ERR: Base will not accept goods, goods not approved by base owner.");
		cd.reverse_sell = true;
		return;
	}

	if (CheckIfCommodityForbidden(gsi.iArchID))
	{
		PrintUserCmdText(client, L"ERR: Cargo is not allowed on Player Bases");
		clients[client].reverse_sell = true;
		return;
	}

	MARKET_ITEM &item = base->market_items[gsi.iArchID];

	int count = gsi.iCount;
	int price = item.sellPrice * count;

	if ((item.quantity + count) > item.max_stock)
	{
		PrintUserCmdText(client, L"ERR: Base cannot accept goods, stock limit reached.");
		cd.reverse_sell = true;
		return;
	}

	if (price < 0)
	{
		clients[client].reverse_sell = true;
		PrintUserCmdText(client, L"KITTY ALERT. Illegal sale detected.");

		wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(client);
		pub::Player::SendNNMessage(client, pub::GetNicknameId("nnv_anomaly_detected"));
		wstring wscMsgU = L"KITTY ALERT: Possible type 4 POB cheating by %name (Count = %count, Price = %price, Good = %good, Base = %base)\n";
		wscMsgU = ReplaceStr(wscMsgU, L"%name", wscCharname.c_str());
		wscMsgU = ReplaceStr(wscMsgU, L"%count", itows(count).c_str());
		wscMsgU = ReplaceStr(wscMsgU, L"%price", itows(item.price).c_str());
		wscMsgU = ReplaceStr(wscMsgU, L"%good", itows(gsi.iArchID).c_str());
		wscMsgU = ReplaceStr(wscMsgU, L"%base", base->basename.c_str());

		ConPrint(wscMsgU);
		LogCheater(client, wscMsgU);

		return;
	}

	// If the base doesn't have sufficient cash to support this purchase
	// reduce the amount purchased and shift the cargo back to the ship.
	if (base->money < price)
	{
		PrintUserCmdText(client, L"ERR: Base cannot accept goods, insufficient cash");
		clients[client].reverse_sell = true;
		return;
	}

	if (count > LONG_MAX / item.price)
	{
		cd.reverse_sell = true;
		PrintUserCmdText(client, L"KITTY ALERT. Illegal sale detected.");
		wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(client);
		pub::Player::SendNNMessage(client, pub::GetNicknameId("nnv_anomaly_detected"));
		wstring wscMsgU = L"KITTY ALERT: Possible type 3 POB cheating by %name (Base = %base, Count = %count, Good = %good, Price = %price)\n";
		wscMsgU = ReplaceStr(wscMsgU, L"%name", wscCharname.c_str());
		wscMsgU = ReplaceStr(wscMsgU, L"%count", itows(count).c_str());
		wscMsgU = ReplaceStr(wscMsgU, L"%price", itows(item.price).c_str());
		wscMsgU = ReplaceStr(wscMsgU, L"%good", itows(gsi.iArchID).c_str());
		wscMsgU = ReplaceStr(wscMsgU, L"%base", base->basename.c_str());

		ConPrint(wscMsgU);
		LogCheater(client, wscMsgU);

		return;
	}

	// Prevent player from getting invalid net worth.
	float fValue;
	pub::Player::GetAssetValue(client, fValue);

	int iCurrMoney;
	pub::Player::InspectCash(client, iCurrMoney);

	long long lNewMoney = iCurrMoney;
	lNewMoney += price;

	if (fValue + price > 2100000000 || lNewMoney > 2100000000)
	{
		PrintUserCmdText(client, L"ERR: Character too valuable.");
		clients[client].reverse_sell = true;
		return;
	}

	if (base->AddMarketGood(gsi.iArchID, gsi.iCount))
	{
		lastTransactionBase = true;
		lastTransactionArchID = gsi.iArchID;
		lastTransactionCount = gsi.iCount;
		lastTransactionClientID = client;
	}
	else
	{
		PrintUserCmdText(client, L"ERR: Base will not accept goods, insufficient storage capacity.");
		cd.reverse_sell = true;
		return;
	}

	if (base->pinned_market_items.count(gsi.iArchID))
	{
		base->pinned_item_updated = true;
	}

	pub::Player::AdjustCash(client, price);
	base->ChangeMoney(0 - price);
	base->Save();

	if (listCommodities.find(gsi.iArchID) != listCommodities.end())
	{
		string cname = wstos(listCommodities[gsi.iArchID]);
		string cbase = wstos(base->basename);

		Notify_Event_Commodity_Sold(client, cname, gsi.iCount, cbase);
	}

	//build string and log the purchase
	wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
	const GoodInfo *gi = GoodList_get()->find_by_id(gsi.iArchID);
	string gname = wstos(HtmlEncode(HkGetWStringFromIDS(gi->iIDSName)));
	string msg = "Player " + wstos(charname) + " sold item " + gname + " x" + itos(count);
	Log::LogBaseAction(wstos(base->basename), msg.c_str());

	//Event plugin hooks
	if (HookExt::IniGetB(client, "event.enabled") && (cd.reverse_sell == false))
	{
		//HkMsgU(L"DEBUG: event pob found");
		if (gsi.iArchID == HookExt::IniGetI(client, "event.eventpobcommodity"))
		{
			//HkMsgU(L"DEBUG: POB event commodity found");
			//At this point, send the data to HookExt
			PrintUserCmdText(client, L"Processing event deposit, please wait up to 15 seconds...");
			HookExt::AddPOBEventData(client, wstos(HookExt::IniGetWS(client, "event.eventid")), gsi.iCount);
		}
	}

	if (base->pinned_market_items.count(gsi.iArchID))
	{
		base->pinned_item_updated = true;
	}

	auto iter = eventCommodities.find(base->base);
	if (iter != eventCommodities.end() && iter->second.count(gsi.iArchID))
	{
		CUSTOM_POB_EVENT_NOTIFICATION_SELL_STRUCT info;
		info.clientId = client;
		info.info = gsi;
		Plugin_Communication(CUSTOM_POB_EVENT_NOTIFICATION_SELL, &info);
	}
}

void __stdcall ReqRemoveItem(unsigned short slot, int count, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	auto& cd = clients[client];
	if (cd.player_base && cd.reverse_sell)
	{
		returncode = SKIPPLUGINS;
		int hold_size;
		HkEnumCargo((const wchar_t*)Players.GetActiveCharacterName(client), clients[client].cargo, hold_size);
	}
}

void __stdcall ReqRemoveItem_AFTER(unsigned short iID, int count, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	auto& cd = clients[client];
	uint player_base = cd.player_base;
	if (player_base)
	{
		if (cd.reverse_sell)
		{
			returncode = SKIPPLUGINS;
			cd.reverse_sell = false;

			for(CARGO_INFO& ci : cd.cargo)
			{
				if (ci.iID == iID)
				{
					Server.ReqAddItem(ci.iArchID, ci.hardpoint.value, count, ci.fStatus, ci.bMounted, client);
					return;
				}
			}
		}
		else
		{
			// Update the player CRC so that the player is not kicked for 'ship related' kick
			ResetPlayerWorth(client);
		}
	}
}

int shipPurchasePrice = 0;
void __stdcall GFGoodBuy(struct SGFGoodBuyInfo const &gbi, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	// If the client is in a player controlled base
	PlayerBase *base = GetPlayerBaseForClient(client);
	if (base)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		auto& cd = clients[client];

		if (Players[client].equipDescList.equip.size() >= 127)
		{
			PrintUserCmdText(client, L"ERR Too many individual items in hold, aborting purchase to prevent character corruption");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			cd.stop_buy = true;
			return;
		}

		auto& mi = base->market_items[gbi.iGoodID];

		uint count = gbi.iCount;
		if (count > mi.quantity)
			count = mi.quantity;

		int price = (int)mi.price * count;
		int curr_money;
		pub::Player::InspectCash(client, curr_money);

		const wstring &charname = (const wchar_t*)Players.GetActiveCharacterName(client);

		// In theory, these should never be called.
		if (count == 0 || ((mi.min_stock > (mi.quantity - count)) && !cd.admin))
		{
			PrintUserCmdText(client, L"ERR Base will not sell goods");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			cd.stop_buy = true;
			return;
		}
		else if (curr_money < price)
		{
			PrintUserCmdText(client, L"ERR Not enough credits");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			cd.stop_buy = true;
			return;
		}

		if (((mi.min_stock > (mi.quantity - count)) && cd.admin))
			PrintUserCmdText(client, L"Permitted player-owned base good sale in violation of shop's minimum stock value due to base admin login.");

		cd.stop_buy = false;
		base->RemoveMarketGood(gbi.iGoodID, count);
		pub::Player::AdjustCash(client, 0 - price);
		base->ChangeMoney(price);
		base->Save();

		if (base->pinned_market_items.count(gbi.iGoodID))
		{
			base->pinned_item_updated = true;
		}

		//build string and log the purchase
		const GoodInfo *gi = GoodList_get()->find_by_id(gbi.iGoodID);
		string gname = wstos(HtmlEncode(HkGetWStringFromIDS(gi->iIDSName)));
		string msg = "Player " + wstos(charname) + " purchased item " + gname + " x" + itos(count);
		Log::LogBaseAction(wstos(base->basename), msg.c_str());

		if (gi && gi->iType == GOODINFO_TYPE_SHIP)
		{
			returncode = SKIPPLUGINS;
			PrintUserCmdText(client, L"Purchased ship, kicking you to force a save.");
			HkDelayedKick(client, 5);

			shipPurchasePrice = curr_money - price;
			const auto gi2 = GoodList_get()->find_by_ship_arch(Players[client].iShipArchetype);
			if (gi2)
			{
				shipPurchasePrice += static_cast<int>(gi2->fPrice * 0.5f);
			}
		}
		else if (gi && gi->iType == GOODINFO_TYPE_HULL)
		{
			returncode = SKIPPLUGINS;
			PrintUserCmdText(client, L"Purchased hull");
		}

		auto iter = eventCommodities.find(base->base);
		if (iter != eventCommodities.end() && iter->second.count(gbi.iGoodID))
		{
			CUSTOM_POB_EVENT_NOTIFICATION_BUY_STRUCT info;
			info.clientId = client;
			info.info = gbi;
			Plugin_Communication(CUSTOM_POB_EVENT_NOTIFICATION_BUY, &info);
		}
	}
}

void __stdcall GFGoodBuy_AFTER(struct SGFGoodBuyInfo const &gbi, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;

	// If the client is in a player controlled base
	PlayerBase *base = GetPlayerBaseForClient(iClientID);
	if (base)
	{
		returncode = SKIPPLUGINS;
		// Update the player CRC so that the player is not kicked for 'ship related' kick
		ResetPlayerWorth(iClientID);

		//PrintUserCmdText(iClientID, L"You will be kicked to update your ship.");
		//HkSaveChar((const wchar_t*)Players.GetActiveCharacterName(iClientID));
		//HkDelayedKick(iClientID, 10);

	}
}

void __stdcall ReqAddItem(uint& good, char const *hardpoint, int count, float fStatus, bool& bMounted, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	PlayerBase *base = GetPlayerBaseForClient(client);
	if (base)
	{
		returncode = SKIPPLUGINS;
		auto& cd = clients[client];
		if (cd.stop_buy)
		{
			clients[client].stop_buy = false;
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		}
	}
}

void __stdcall ReqAddItem_AFTER(unsigned int good, char const *hardpoint, int count, float fStatus, bool bMounted, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	// If the client is in a player controlled base
	PlayerBase *base = GetPlayerBaseForClient(client);
	if (base)
	{
		returncode = SKIPPLUGINS;
		PlayerData *pd = &Players[client];

		// Add to check-list which is being compared to the users equip-list when saving
		// char to fix "Ship or Equipment not sold on base" kick
		EquipDesc ed;
		ed.sID = pd->lastEquipId;
		ed.iCount = 1;
		ed.iArchID = good;
		pd->lShadowEquipDescList.add_equipment_item(ed, false);

		// Update the player CRC so that the player is not kicked for 'ship related' kick

		ResetPlayerWorth(client);
	}
}

int cashChange = 0;
int lastCashChangeClient = 0;

int __stdcall Update(void)
{
	returncode = DEFAULT_RETURNCODE;
	cashChange = 0;
	lastCashChangeClient = 0;

	return 0;
}

/// Ignore cash commands from the client when we're in a player base.
void __stdcall ReqChangeCash(int cash, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	if (clients[client].player_base)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cashChange = cash;
		lastCashChangeClient = client;
	}
}

/// Ignore cash commands from the client when we're in a player base.
void __stdcall ReqSetCash(int cash, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	if (shipPurchasePrice)
	{
		return;
	}
	if (clients[client].player_base)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
	cashChange = 0;
	lastCashChangeClient = 0;
}

void __stdcall ReqSetCash_AFTER(int cash, unsigned int client)
{
	if (shipPurchasePrice)
	{
		int moneyDiff = shipPurchasePrice - Players[client].iInspectCash;
		pub::Player::AdjustCash(client, moneyDiff);
		ResetPlayerWorth(client);
	}
}
void SetEquipPacket(uint client, st6::vector<EquipDesc>&)
{
	returncode = DEFAULT_RETURNCODE;
	if (cashChange && client == lastCashChangeClient)
	{
		pub::Player::AdjustCash(client, cashChange);

		cashChange = 0;
		lastCashChangeClient = 0;
	}
}

void SetHullPacket(uint client, float)
{
	returncode = DEFAULT_RETURNCODE;
	if (cashChange && client == lastCashChangeClient)
	{
		pub::Player::AdjustCash(client, cashChange);

		cashChange = 0;
		lastCashChangeClient = 0;
	}
}

void SetColGrp(uint client, st6::list<CollisionGroupDesc>&)
{
	returncode = DEFAULT_RETURNCODE;
	if (cashChange && client == lastCashChangeClient)
	{
		pub::Player::AdjustCash(client, cashChange);

		cashChange = 0;
		lastCashChangeClient = 0;
	}
}

void __stdcall ReqEquipment(class EquipDescList const &edl, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	if (clients[client].player_base)
		returncode = SKIPPLUGINS;
}

void __stdcall ReqShipArch_AFTER(uint shipArchId, uint clientId)
{
	if (shipPurchasePrice && clients[clientId].player_base)
	{
		shipPurchasePrice = 0;
		ResetPlayerWorth(clientId);
	}
}

void __stdcall BaseDestroyed(IObjRW* iobj, bool isKill, uint dunno)
{
	returncode = DEFAULT_RETURNCODE;
	uint space_obj = iobj->get_id();
	auto& i = spaceobj_modules.find(space_obj);
	if (i != spaceobj_modules.end())
	{
		returncode = SKIPPLUGINS;
		i->second->SpaceObjDestroyed(space_obj);
		return;
	}
	customSolarList.erase(space_obj);
}

void __stdcall SolarDamageHull(IObjRW* iobj, float& incDmg, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;
	if (!dmg->iInflictorPlayerID)
	{
		incDmg = 0;
		return;
	}

	CSolar* base = reinterpret_cast<CSolar*>(iobj->cobj);
	if (base->hitPoints > 1'000'000'000)
	{
		incDmg = 0;
		return;
	}

	if (!spaceobj_modules.count(base->id))
	{
		return;
	}


	Module* damagedModule = spaceobj_modules.at(base->id);

	// This call is for us, skip all plugins.
	incDmg = damagedModule->SpaceObjDamaged(base->id, dmg->get_inflictor_id(), incDmg);
}

bool HasSRPAccess(uint client, uint baseId)
{
	auto base = player_bases.find(baseId);
	if (base == player_bases.end())
	{
		return false;
	}

	wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
	uint playeraff = GetAffliationFromClient(client);

	return base->second->IsOnSRPList(charname, playeraff);
}

#define IS_CMD(a) !args.compare(L##a)
#define RIGHT_CHECK(a) if(!(cmd->rights & a)) { cmd->Print(L"ERR No permission\n"); return true; }
bool ExecuteCommandString_Callback(CCmds* cmd, const wstring &args)
{
	returncode = DEFAULT_RETURNCODE;

	if (args.find(L"setdebugspecial") == 0)
	{
		RIGHT_CHECK(RIGHT_BASES)
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		set_plugin_debug_special = cmd->ArgInt(1);
		return true;
	}
	else if (args.find(L"solarcheck") == 0)
	{
		RIGHT_CHECK(RIGHT_BASES)

		auto nickname = CreateID(wstos(cmd->ArgStrToEnd(1)).c_str());

		auto csolar = (CSolar*)CObject::Find(nickname, CObject::CSOLAR_OBJECT);
		if (csolar)
		{
			csolar->Release();
		}
		IObjRW* obj;
		StarSystem* syst;
		GetShipInspect(nickname, obj, syst);

		cmd->Print(L"%x %x\n", csolar, obj);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"setunchartedkill") == 0)
	{
		RIGHT_CHECK(RIGHT_BASES)
		set_SkipUnchartedKill = !set_SkipUnchartedKill;
		cmd->Print(L"skip unch kill %u\n", (uint)set_SkipUnchartedKill);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"testmodulerecipe") == 0)
	{
		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		PlayerBase *base = GetPlayerBaseForClient(client);
		if (!base)
		{
			cmd->Print(L"ERR Not in player base");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		const wchar_t* recipe_name = cmd->ArgStr(1).c_str();

		RECIPE recipe = moduleNameRecipeMap[recipe_name];
		for (auto& i = recipe.consumed_items.begin(); i != recipe.consumed_items.end(); ++i)
		{
			base->market_items[i->first].quantity += i->second;
			SendMarketGoodUpdated(base, i->first, base->market_items[i->first]);
			cmd->Print(L"Added %ux %08x", i->second, i->first);
		}
		base->Save();
		cmd->Print(L"OK");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"testfacrecipe") == 0)
	{
		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		PlayerBase *base = GetPlayerBaseForClient(client);
		if (!base)
		{
			cmd->Print(L"ERR Not in player base");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		const wchar_t* craft_type = cmd->ArgStr(1).c_str();
		const wchar_t* recipe_name = cmd->ArgStr(2).c_str();

		RECIPE recipe = recipeCraftTypeNameMap[craft_type][recipe_name];
		for (auto& i = recipe.consumed_items.begin(); i != recipe.consumed_items.end(); ++i)
		{
			base->market_items[i->first].quantity += i->second;
			SendMarketGoodUpdated(base, i->first, base->market_items[i->first]);
			cmd->Print(L"Added %ux %08x", i->second, i->first);
		}
		base->Save();
		cmd->Print(L"OK");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"testdeploy") == 0)
	{
		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		if (!client)
		{
			cmd->Print(L"ERR Not in game");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		for (map<uint, uint>::iterator i = construction_items.begin(); i != construction_items.end(); ++i)
		{
			uint good = i->first;
			uint quantity = i->second;
			pub::Player::AddCargo(client, good, quantity, 1.0, false);
		}

		cmd->Print(L"OK");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.compare(L"beam") == 0)
	{
		wstring charname = cmd->ArgCharname(1);
		wstring basename = cmd->ArgStrToEnd(2);

		// Fall back to default behaviour.
		if (!(cmd->rights & RIGHT_BEAMKILL))
		{
			return false;
		}

		HKPLAYERINFO info;
		if (HkGetPlayerInfo(charname, info, false) != HKE_OK)
		{
			return false;
		}

		if (info.iShip == 0)
		{
			return false;
		}

		// Search for an match at the start of the name
		for (auto& i : player_bases)
		{
			if (ToLower(i.second->basename).find(ToLower(basename)) == 0)
			{
				ForcePlayerBaseDock(info.iClientID, i.second);
				cmd->Print(L"OK");
				returncode = SKIPPLUGINS_NOFUNCTIONCALL;
				return true;
			}
		}

		// Exact match failed, try a for an partial match
		for (auto& i : player_bases)
		{
			if (ToLower(i.second->basename).find(ToLower(basename)) != -1)
			{
				ForcePlayerBaseDock(info.iClientID, i.second);
				cmd->Print(L"OK");
				returncode = SKIPPLUGINS_NOFUNCTIONCALL;
				return true;
			}
		}

		// Fall back to default flhook .beam command
		return false;
	}
	else if (args.find(L"basedestroy") == 0)
	{
		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());

		PlayerBase *base = nullptr;
		for (auto& i : player_bases)
		{
			if (i.second->basename == cmd->ArgStrToEnd(1) || stows(i.second->nickname) == cmd->ArgStrToEnd(1))
			{
				base = i.second;
				break;
			}
		}

		if (!base)
		{
			cmd->Print(L"ERR Base doesn't exist\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		base->base_health = 0;
		auto coreModule = reinterpret_cast<CoreModule*>(base->modules[0]);
		coreModule->SpaceObjDestroyed(coreModule->space_obj);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"basedespawn") == 0)
	{
		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		wstring baseName = cmd->ArgStrToEnd(1);

		PlayerBase* base = nullptr;
		for (auto& i : player_bases)
		{
			if (i.second->basename == baseName)
			{
				base = i.second;
				break;
			}
		}

		if (!base)
		{
			cmd->Print(L"ERR Base doesn't exist\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		lastDespawnedFilename = base->path;
		base->base_health = 0;
		auto coreModule = reinterpret_cast<CoreModule*>(base->modules[0]);
		coreModule->SpaceObjDestroyed(coreModule->space_obj, false, false);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;

	}
	else if (args.find(L"baserespawn") == 0)
	{
		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());

		char datapath[MAX_PATH];
		GetUserDataPath(datapath);

		wstring baseName = cmd->ArgStrToEnd(1);

		string path;

		if (baseName.empty())
		{
			path = lastDespawnedFilename;
		}
		else
		{
			path = string(datapath) + R"(\Accts\MultiPlayer\player_bases\)" + wstos(baseName) + ".ini";
		}

		WIN32_FIND_DATA findfile;
		HANDLE h = FindFirstFile(path.c_str(), &findfile);
		if (h == INVALID_HANDLE_VALUE)
		{
			cmd->Print(L"ERR Base file not found\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		uint baseNickname = CreateID(IniGetS(path, "Base", "nickname", "").c_str());

		if (pub::SpaceObj::ExistsAndAlive(baseNickname) == 0) // -2 for nonexistant object, 0 for existing and alive
		{
			cmd->Print(L"ERR Base already spawned!\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		PlayerBase* base = new PlayerBase(path);

		FindClose(h);
		if (base && !base->nickname.empty())
		{
			player_bases[base->base] = base;
			base->Spawn();
			cmd->Print(L"Base %ls respawned!\n", base->basename.c_str());
		}
		else
		{
			cmd->Print(L"ERROR POB file corrupted: %ls\n", stows(path).c_str());
		}

		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"basetogglegod") == 0)
	{

		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		bool optype = cmd->ArgInt(1);

		PlayerBase *base = nullptr;
		for (auto& i : player_bases)
		{
			if (i.second->basename == cmd->ArgStrToEnd(2))
			{
				base = i.second;
				break;
			}
		}

		if (!base)
		{
			cmd->Print(L"ERR Base doesn't exist\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}


		if (optype == true)
		{
			base->invulnerable = true;
			cmd->Print(L"OK Base made invulnerable.");
		}
		else if (optype == false)
		{
			base->invulnerable = false;
			cmd->Print(L"OK Base made vulnerable.");
		}

		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"testbase") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());

		uint ship;
		pub::Player::GetShip(client, ship);
		if (!ship)
		{
			PrintUserCmdText(client, L"ERR Not in space");
			return true;
		}

		int min = 100;
		int max = 5000;
		int randomsiegeint = min + (rand() % (int)(max - min + 1));

		string randomname = "TB";

		stringstream ss;
		ss << randomsiegeint;
		string str = ss.str();

		randomname.append(str);

		// Check for conflicting base name
		if (GetPlayerBase(CreateID(PlayerBase::CreateBaseNickname(randomname).c_str())))
		{
			PrintUserCmdText(client, L"ERR Deployment error, please reiterate.");
			return true;
		}

		wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
		AddLog("NOTICE: Base created %s by %s (%s)",
			randomname.c_str(),
			wstos(charname).c_str(),
			wstos(HkGetAccountID(HkGetAccountByCharname(charname))).c_str());

		wstring password = L"hastesucks";
		wstring basename = stows(randomname);

		PlayerBase *newbase = new PlayerBase(client, password, basename);
		player_bases[newbase->base] = newbase;
		newbase->basetype = "legacy";
		newbase->archetype = &mapArchs[newbase->basetype];
		newbase->basesolar = "legacy";
		newbase->baseloadout = "legacy";
		newbase->defense_mode = PlayerBase::DEFENSE_MODE::IFF;
		newbase->isCrewSupplied = true;
		newbase->base_health = 1000000.f;

		newbase->invulnerable = newbase->archetype->invulnerable;
		newbase->logic = newbase->archetype->logic;

		newbase->Spawn();
		newbase->Save();

		PrintUserCmdText(client, L"OK: Siege Cannon deployed");
		PrintUserCmdText(client, L"Default administration password is %s", password.c_str());

		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (args.find(L"jumpcreate") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());

		uint ship;
		pub::Player::GetShip(client, ship);
		if (!ship)
		{
			PrintUserCmdText(client, L"ERR Not in space");
			return true;
		}

		wstring usage = L"Usage: .jumpcreate <archtype> <loadout> <type> <dest object> <affiliation> <name>";

		// If the ship is moving, abort the processing.
		Vector dir1;
		Vector dir2;
		pub::SpaceObj::GetMotion(ship, dir1, dir2);
		if (dir1.x > 5 || dir1.y > 5 || dir1.z > 5)
		{
			PrintUserCmdText(client, L"ERR Ship is moving");
			return true;
		}

		wstring archtype = cmd->ArgStr(1);
		if (!archtype.length())
		{
			PrintUserCmdText(client, L"ERR No archtype");
			PrintUserCmdText(client, usage.c_str());
			return true;
		}
		wstring loadout = cmd->ArgStr(2);
		if (!loadout.length())
		{
			PrintUserCmdText(client, L"ERR No loadout");
			PrintUserCmdText(client, usage.c_str());
			return true;
		}
		wstring type = cmd->ArgStr(3);
		if (!type.length())
		{
			PrintUserCmdText(client, L"ERR No type");
			PrintUserCmdText(client, usage.c_str());
			return true;
		}
		wstring destobject = cmd->ArgStr(4);
		if (!destobject.length())
		{
			PrintUserCmdText(client, L"ERR No destination object");
			PrintUserCmdText(client, usage.c_str());
			return true;
		}

		wstring theaffiliation = cmd->ArgStr(5);
		if (!theaffiliation.length())
		{
			PrintUserCmdText(client, L"ERR No affiliation");
			PrintUserCmdText(client, usage.c_str());
			return true;
		}


		wstring basename = cmd->ArgStrToEnd(6);
		if (!basename.length())
		{
			PrintUserCmdText(client, L"ERR No name entered");
			PrintUserCmdText(client, usage.c_str());
			return true;
		}



		// Check for conflicting base name
		if (GetPlayerBase(CreateID(PlayerBase::CreateBaseNickname(wstos(basename)).c_str())))
		{
			PrintUserCmdText(client, L"ERR Base name already exists");
			return true;
		}

		wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
		AddLog("NOTICE: Base created %s by %s (%s)",
			wstos(basename).c_str(),
			wstos(charname).c_str(),
			wstos(HkGetAccountID(HkGetAccountByCharname(charname))).c_str());

		wstring password = L"nopassword";

		PlayerBase *newbase = new PlayerBase(client, password, basename);
		player_bases[newbase->base] = newbase;
		newbase->affiliation = CreateID(wstos(theaffiliation).c_str());
		newbase->basetype = wstos(type);
		newbase->archetype = &mapArchs[newbase->basetype];
		newbase->basesolar = wstos(archtype);
		newbase->baseloadout = wstos(loadout);
		newbase->defense_mode = PlayerBase::DEFENSE_MODE::IFF;;
		newbase->base_health = 10000000000;

		newbase->destObject = CreateID(wstos(destobject).c_str());

		newbase->invulnerable = newbase->archetype->invulnerable;
		newbase->logic = newbase->archetype->logic;

		newbase->Spawn();
		newbase->Save();

		HyperJump::InitJumpHoleConfig();

		PrintUserCmdText(client, L"OK: Solar deployed");
		//PrintUserCmdText(client, L"Default administration password is %s", password.c_str());

		returncode = SKIPPLUGINS_NOFUNCTIONCALL; 
		return true;
	}
	else if (args.find(L"basecreate") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_BASES)

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		PlayerBase *base = GetPlayerBaseForClient(client);

		uint ship;
		pub::Player::GetShip(client, ship);
		if (!ship)
		{
			PrintUserCmdText(client, L"ERR Not in space");
			return true;
		}

		// If the ship is moving, abort the processing.
		Vector dir1;
		Vector dir2;
		pub::SpaceObj::GetMotion(ship, dir1, dir2);
		if (dir1.x > 5 || dir1.y > 5 || dir1.z > 5)
		{
			PrintUserCmdText(client, L"ERR Ship is moving");
			return true;
		}

		wstring password = cmd->ArgStr(1);
		if (!password.length())
		{
			PrintUserCmdText(client, L"ERR No password");
			PrintUserCmdText(client, L"Usage: .basecreate <password> <archtype> <loadout> <type> <name>");
			return true;
		}
		wstring archtype = cmd->ArgStr(2);
		if (!archtype.length())
		{
			PrintUserCmdText(client, L"ERR No archtype");
			PrintUserCmdText(client, L"Usage: .basecreate <password> <archtype> <loadout> <type> <name>");
			return true;
		}
		wstring loadout = cmd->ArgStr(3);
		if (!loadout.length())
		{
			PrintUserCmdText(client, L"ERR No loadout");
			PrintUserCmdText(client, L"Usage: .basecreate <password> <archtype> <loadout> <type> <name>");
			return true;
		}
		wstring type = cmd->ArgStr(4);
		if (!type.length())
		{
			PrintUserCmdText(client, L"ERR No type");
			PrintUserCmdText(client, L"Usage: .basecreate <password> <archtype> <loadout> <type> <name>");
			return true;
		}
		uint theaffiliation = cmd->ArgInt(5);

		wstring basename = cmd->ArgStrToEnd(6);
		if (!basename.length())
		{
			PrintUserCmdText(client, L"ERR No name");
			PrintUserCmdText(client, L"Usage: .basecreate <password> <archtype> <loadout> <type> <name>");
			return true;
		}



		// Check for conflicting base name
		if (GetPlayerBase(CreateID(PlayerBase::CreateBaseNickname(wstos(basename)).c_str())))
		{
			PrintUserCmdText(client, L"ERR Base name already exists");
			return true;
		}

		wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
		AddLog("NOTICE: Base created %s by %s (%s)",
			wstos(basename).c_str(),
			wstos(charname).c_str(),
			wstos(HkGetAccountID(HkGetAccountByCharname(charname))).c_str());

		PlayerBase *newbase = new PlayerBase(client, password, basename);
		player_bases[newbase->base] = newbase;
		newbase->affiliation = theaffiliation;
		newbase->basetype = wstos(type);
		newbase->archetype = &mapArchs[newbase->basetype];
		newbase->basesolar = wstos(archtype);
		newbase->baseloadout = wstos(loadout);
		newbase->defense_mode = PlayerBase::DEFENSE_MODE::NODOCK_NEUTRAL;
		newbase->base_health = 10000000000;
		newbase->isCrewSupplied = true;

		newbase->invulnerable = newbase->archetype->invulnerable;
		newbase->logic = newbase->archetype->logic;

		newbase->Spawn();
		newbase->Save();

		PrintUserCmdText(client, L"OK: Base deployed");
		PrintUserCmdText(client, L"Default administration password is %s", password.c_str());
		return true;
	}
	else if (args.find(L"basedebugon") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_BASES)

		set_plugin_debug = 1;
		cmd->Print(L"OK base debug is on, sure hope you know what you're doing here.\n");
		return true;
	}
	else if (args.find(L"basedebugoff") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_BASES)

		set_plugin_debug = 0;
		cmd->Print(L"OK base debug is off.\n");
		return true;
	}
	else if (args.find(L"checkbasedistances") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_BASES)


		ConPrint(L"POB distance check for following settings:\n"
			L"High Tier Mining: %um\n"
			L"Planets: %um\n"
			L"Solars: %um\n"
			L"Trade Lanes: %um\n"
			L"JH/JG: %um\n"
			L"POBs: %um\n"
			L"Other: %um\n",
			static_cast<uint>(minMiningDistance),
			static_cast<uint>(minPlanetDistance),
			static_cast<uint>(minStationDistance),
			static_cast<uint>(minLaneDistance),
			static_cast<uint>(minJumpDistance),
			static_cast<uint>(minOtherPOBDistance),
			static_cast<uint>(minDistanceMisc));

		for (const auto& base : player_bases)
		{
			if (base.second->basetype != "legacy" || base.second->invulnerable || !base.second->logic)
			{
				continue;
			}

			Vector& pos = base.second->position;
			uint system = base.second->system;
			if (!PlayerCommands::CheckSolarDistances(0, system, pos))
			{
				ConPrint(L" - %ls\n", base.second->basename.c_str());
			}
		}
		return true;
	}
	else if (args.find(L"baselogin") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_SUPERADMIN);
		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		if (client == -1)
		{
			ConPrint(L"Only usable ingame\n");
			return true;
		}

		PlayerBase* base = GetPlayerBaseForClient(client);
		if (base)
		{
			clients[client].admin = true;
			SendMarketGoodSync(base, client);
			PrintUserCmdText(client, L"Logged in as admin");
		}
		else
		{
			PrintUserCmdText(client, L"ERR Not in a player base!");
		}

		return true;
	}
	else if (args.find(L"baseaddcargo") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_CARGO);

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		if (client == -1)
		{
			ConPrint(L"Only usable ingame\n");
			return true;
		}

		PlayerBase* base = GetPlayerBaseForClient(client);
		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in a player base!");
			return true;
		}

		uint goodId = CreateID(wstos(cmd->ArgStr(1)).c_str());

		auto gi = GoodList_get()->find_by_id(goodId);
		if (!gi)
		{
			PrintUserCmdText(client, L"ERR Invalid good id!");
			return true;
		}

		uint amount = cmd->ArgUInt(2);

		if (!amount)
		{
			PrintUserCmdText(client, L"ERR Invalid good amount!");
			return true;
		}

		base->AddMarketGood(goodId, amount);
		base->Save();

		BaseLogging("%s added %s x%u to %s", wstos(cmd->GetAdminName()).c_str(), wstos(cmd->ArgStr(1)).c_str(), amount, wstos(base->basename).c_str());

		return true;
	}
	else if (args.find(L"basecheckpos") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_SUPERADMIN);

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		if (client == -1)
		{
			cmd->Print(L"Only usable ingame\n");
			return true;
		}

		if (!ClientInfo[client].cship)
		{
			cmd->Print(L"Only usable in space\n");
			return true;
		}
		if (PlayerCommands::CheckSolarDistances(client, Players[client].iSystemID, ClientInfo[client].cship->vPos))
		{
			cmd->Print(L"All Good");
		}
		else
		{
			cmd->Print(L"Position Invalid");
		}

		return true;
	}
	else if (args.find(L"basepull") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_SUPERADMIN);

		uint client = HkGetClientIdFromCharname(cmd->GetAdminName());
		if (client == -1)
		{
			cmd->Print(L"Only usable ingame\n");
			return true;
		}

		if (!ClientInfo[client].cship)
		{
			cmd->Print(L"Only usable in space\n");
			return true;
		}

		auto baseName = cmd->ArgStrToEnd(1);
		PlayerBase* pb = nullptr;
		if(baseName.empty())
		{
			auto target = ClientInfo[client].cship->get_target();
			auto pbIter = player_bases.find(target->get_id());
			if (pbIter == player_bases.end())
			{
				cmd->Print(L"Base name not provided and target is not a POB\n");
				return true;
			}
			pb = pbIter->second;
		}
		else
		{
			for (auto& i : player_bases)
			{
				if (i.second->basename == baseName)
				{
					pb = i.second;
					break;
				}
			}
		}

		if (!pb)
		{
			cmd->Print(L"Player Base with this name not found\n");
			return true;
		}

		if (!pb->baseCSolar)
		{
			cmd->Print(L"No CSolar found for POB!\n");
			return true;
		}

		pb->baseCSolar->vPos = ClientInfo[client].cship->vPos;
		pb->baseCSolar->mRot = ClientInfo[client].cship->mRot;
		pb->position = ClientInfo[client].cship->vPos;
		pb->rotation = ClientInfo[client].cship->mRot;

		cmd->Print(L"POB pulled to your location, change visible on system re-entry\n");
		return true;
	}
	else if (args.find(L"dumpbaseinfo") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		RIGHT_CHECK(RIGHT_SUPERADMIN);

		for (auto& base : player_bases)
		{
			bool printedName = false;
			for (int i = 1; i < MAX_PARAGRAPHS; ++i)
			{
				if (base.second->infocard_para[i].empty())
				{
					continue;
				}

				if (!printedName)
				{
					printedName = true;
					ConPrint(L"\n%ls\n", base.second->basename.c_str());
				}

				ConPrint(L"%ls\n", base.second->infocard_para[i].c_str());
			}
		}
		return true;
	}

	return false;
}

void DelayedDisconnect(uint clientId, uint shipId)
{
	returncode = DEFAULT_RETURNCODE;
	HyperJump::CheckForUnchartedDisconnect(clientId, shipId);
}

void Plugin_Communication_CallBack(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;

	if (msg == CUSTOM_BASE_BEAM)
	{
		returncode = SKIPPLUGINS;
		CUSTOM_BASE_BEAM_STRUCT* info = reinterpret_cast<CUSTOM_BASE_BEAM_STRUCT*>(data);
		PlayerBase *base = GetPlayerBase(info->iTargetBaseID);
		if (base)
		{
			ForcePlayerBaseDock(info->iClientID, base);
			info->bBeamed = true;
		}
	}
	if (msg == CUSTOM_IS_IT_POB)
	{
		returncode = SKIPPLUGINS;
		CUSTOM_BASE_IS_IT_POB_STRUCT* info = reinterpret_cast<CUSTOM_BASE_IS_IT_POB_STRUCT*>(data);
		PlayerBase *base = GetPlayerBase(info->iBase);
		if (base)
		{
			info->bAnswer = true;
		}
	}
	else if (msg == CUSTOM_BASE_IS_DOCKED)
	{
		returncode = SKIPPLUGINS;
		CUSTOM_BASE_IS_DOCKED_STRUCT* info = reinterpret_cast<CUSTOM_BASE_IS_DOCKED_STRUCT*>(data);
		PlayerBase *base = GetPlayerBaseForClient(info->iClientID);
		if (base)
		{
			info->iDockedBaseID = base->base;
		}
	}
	else if (msg == CUSTOM_JUMP)
	{
		CUSTOM_JUMP_STRUCT* info = reinterpret_cast<CUSTOM_JUMP_STRUCT*>(data);
		uint client = HkGetClientIDByShip(info->iShipID);
		SyncReputationForClientShip(info->iShipID, client);
	}
	else if (msg == CUSTOM_REVERSE_TRANSACTION)
	{
		if (lastTransactionBase)
		{
			CUSTOM_REVERSE_TRANSACTION_STRUCT* info = reinterpret_cast<CUSTOM_REVERSE_TRANSACTION_STRUCT*>(data);
			if (info->iClientID != lastTransactionClientID)
			{
				ConPrint(L"base: CUSTOM_REVERSE_TRANSACTION: Something is very wrong! Expected client ID %d but got %d\n", lastTransactionClientID, info->iClientID);
				return;
			}
			PlayerBase *base = GetPlayerBaseForClient(info->iClientID);

			MARKET_ITEM &item = base->market_items[lastTransactionArchID];
			int price = item.price * lastTransactionCount;

			base->RemoveMarketGood(lastTransactionArchID, lastTransactionCount);

			pub::Player::AdjustCash(info->iClientID, -price);
			base->ChangeMoney(price);
			base->Save();
		}
	}
	else if (msg == CUSTOM_BASE_LAST_DOCKED)
	{
		returncode = SKIPPLUGINS;
		LAST_PLAYER_BASE_NAME_STRUCT* info = reinterpret_cast<LAST_PLAYER_BASE_NAME_STRUCT*>(data);
		if (clients.count(info->clientID))
		{
			uint lastBaseID = clients[info->clientID].last_player_base;
			if (player_bases.count(lastBaseID))
			{
				info->lastBaseName = player_bases[lastBaseID]->basename;
			}
			else
			{
				info->lastBaseName = L"Destroyed Player Base";
			}
		}
		else
		{
			info->lastBaseName = L"Object Unknown";
		}
	}
	else if (msg == CUSTOM_SPAWN_SOLAR)
	{
		returncode = SKIPPLUGINS;
		SPAWN_SOLAR_STRUCT* info = reinterpret_cast<SPAWN_SOLAR_STRUCT*>(data);
		CreateSolar::CreateSolarCallout(info);
	}
	else if (msg == CUSTOM_DESPAWN_SOLAR)
	{
		returncode = SKIPPLUGINS;
		DESPAWN_SOLAR_STRUCT* info = reinterpret_cast<DESPAWN_SOLAR_STRUCT*>(data);
		CreateSolar::DespawnSolarCallout(info);
	}
	else if (msg == CUSTOM_POB_DOCK_ALERT)
	{
		returncode = SKIPPLUGINS;
		CUSTOM_POB_DOCK_ALERT_STRUCT* info = reinterpret_cast<CUSTOM_POB_DOCK_ALERT_STRUCT*>(data);
		PlayerBase* pb = GetPlayerBaseForClient(info->client);
		if (pb)
		{
			PrintLocalMsgAroundObject(CreateID(pb->nickname.c_str()), *info->msg, info->range);
		}
	}
	else if (msg == CUSTOM_POB_EVENT_NOTIFICATION_INIT)
	{
		returncode = SKIPPLUGINS;
		CUSTOM_POB_EVENT_NOTIFICATION_INIT_STRUCT* info = reinterpret_cast<CUSTOM_POB_EVENT_NOTIFICATION_INIT_STRUCT*>(data);
		eventCommodities = info->data;
	}
	else if (msg == CUSTOM_POPUP_INIT)
	{
		uint* clientId = reinterpret_cast<uint*>(data);
		clients[*clientId].lastPopupWindowType = POPUPWINDOWTYPE::NONE;
	}
	else if (msg == CUSTOM_BEAM_LAST_BASE)
	{
		uint* clientId = reinterpret_cast<uint*>(data);
		uint playerBaseId = clients[*clientId].last_player_base;
		if (playerBaseId)
		{
			ForcePlayerBaseDock(*clientId, GetPlayerBase(playerBaseId));
		}
		else
		{
			HkBeamById(*clientId, Players[*clientId].iLastBaseID);
		}
		returncode = SKIPPLUGINS;
		return;
	}
	else if (msg == CUSTOM_CHECK_POB_SRP_ACCESS)
	{
		POB_SRP_ACCESS_STRUCT* info = reinterpret_cast<POB_SRP_ACCESS_STRUCT*>(data);

		info->dockAllowed = HasSRPAccess(info->clientId, info->baseId);

		returncode = SKIPPLUGINS;
		return;
	}
	return;
}

void AddFactoryRecipeToMaps(const RECIPE& recipe)
{
	wstring recipeNameKey = ToLower(recipe.infotext);
	recipeMap[recipe.nickname] = recipe;
	recipeCraftTypeNumberMap[recipe.craft_type][recipe.shortcut_number] = recipe;
	recipeCraftTypeNameMap[recipe.craft_type][recipeNameKey] = recipe;
}

void AddModuleRecipeToMaps(const RECIPE& recipe, const vector<wstring> craft_types, const wstring& build_type, uint recipe_number)
{
	wstring recipeNameKey = ToLower(recipe.infotext);

	for (const wstring& craftType : craft_types)
	{
		factoryNicknameToCraftTypeMap[recipe.nickname].emplace_back(ToLower(craftType));
	}
	recipeMap[recipe.nickname] = recipe;
	moduleNameRecipeMap[recipeNameKey] = recipe;
	if (!build_type.empty())
	{
		craftListNumberModuleMap[build_type][recipe_number] = recipe;
		buildingCraftLists.insert(build_type);
	}
}

void PopUpDialogue(uint client, uint buttonPressed)
{
	returncode = DEFAULT_RETURNCODE;

	auto& cd = clients[client];
	if (buttonPressed == POPUPDIALOG_BUTTONS_CENTER_OK)
	{
		cd.lastPopupWindowType = POPUPWINDOWTYPE::NONE;
		HkChangeIDSString(client, 1244, L"YES");
		HkChangeIDSString(client, 1245, L"NO");
		return;
	}
	if (cd.lastPopupWindowType == POPUPWINDOWTYPE::SHOP)
	{
		auto pb = player_bases.find(cd.player_base);
		if (pb == player_bases.end())
		{
			return;
		}
		if (buttonPressed == POPUPDIALOG_BUTTONS_RIGHT_LATER)
		{
			PlayerCommands::ShowShopStatus(client, pb->second, cd.lastShopFilterKeyword, cd.lastPopupPage + 1);
		}
		else if (buttonPressed == POPUPDIALOG_BUTTONS_LEFT_YES)
		{
			PlayerCommands::ShowShopStatus(client, pb->second, cd.lastShopFilterKeyword, cd.lastPopupPage - 1);
		}
		else if (buttonPressed == POPUPDIALOG_BUTTONS_CENTER_NO)
		{
			PlayerCommands::ShowShopHelp(client);
		}
	}
	else if (cd.lastPopupWindowType == POPUPWINDOWTYPE::SHOP_HELP)
	{
		auto pb = player_bases.find(cd.player_base);
		if (pb == player_bases.end())
		{
			return;
		}
		else if (buttonPressed == POPUPDIALOG_BUTTONS_LEFT_YES)
		{
			PlayerCommands::ShowShopStatus(client, pb->second, cd.lastShopFilterKeyword, cd.lastPopupPage);
		}
	}
	else if (cd.lastPopupWindowType == POPUPWINDOWTYPE::HELP)
	{
		if (buttonPressed == POPUPDIALOG_BUTTONS_RIGHT_LATER)
		{
			PlayerCommands::BaseHelp(client, L"/base help " + itows(cd.lastPopupPage + 1));
		}
		else if (buttonPressed == POPUPDIALOG_BUTTONS_LEFT_YES)
		{
			PlayerCommands::BaseHelp(client, L"/base help " + itows(cd.lastPopupPage - 1));
		}
	}
}

static std::unordered_map<uint, std::unordered_set<uint>> dockQueues;
// Gets called whenever a dock request begins, ends, is cancelled, or the ship is destroyed/despawned. Does not get called when the station gets destroyed.
int __cdecl Dock_Call_After(unsigned int const& ship, unsigned int const& dockTargetId, int& dockPortIndex, DOCK_HOST_RESPONSE& response)
{
	returncode = DEFAULT_RETURNCODE;

	auto pbIter = player_bases.find(dockTargetId);
	if (pbIter == player_bases.end() || pbIter->second->archetype->isjump)
	{
		return 0;
	}

	// dockPortIndex -1 means docking was cancelled.
	if (dockPortIndex < 0 || response == DOCK_HOST_RESPONSE::DOCK)
	{
		dockQueues[dockTargetId].erase(ship);
		return 0;
	}

	if (response == DOCK_HOST_RESPONSE::DOCK_IN_USE)
	{
		dockQueues[dockTargetId].insert(ship);
	}


	auto solar = pbIter->second->baseCSolar;
	const Archetype::EqObj* solarArchetype = static_cast<Archetype::EqObj*>(solar->archetype);
	Archetype::DockType dockType;
	try
	{
		dockType = solarArchetype->dockInfo.at(dockPortIndex).dockType;
	}
	catch (const std::out_of_range& e)
	{
		AddLog("Dock Call error: %s", e.what());
		return 0;
	}

	std::vector<uint> lines;
	switch (response)
	{
	case DOCK_HOST_RESPONSE::PROCEED_DOCK:
	{
		std::string dockTypeMessageId;
		std::string dockTargetMessageId;
		switch (dockType)
		{
		case Archetype::DockType::Jump:
		case Archetype::DockType::Berth:
			dockTypeMessageId = "gcs_dockrequest_todock";
			dockTargetMessageId = "gcs_dockrequest_todock_number";
			break;

		case Archetype::DockType::MoorSmall:
		case Archetype::DockType::MoorMedium:
		case Archetype::DockType::MoorLarge:
			dockTypeMessageId = "gcs_dockrequest_tomoor";
			dockTargetMessageId = "gcs_dockrequest_tomoor_number";
			break;

		case Archetype::DockType::Ring:
			dockTypeMessageId = "gcs_dockrequest_toland";
			dockTargetMessageId = "gcs_dockrequest_toland-";
			break;

		default:
			dockTargetMessageId = "";
			break;
		}


		std::string dockNumberMessageId;
		switch (dockType)
		{
		case Archetype::DockType::Jump:
		case Archetype::DockType::Berth:
		case Archetype::DockType::MoorSmall:
		case Archetype::DockType::MoorMedium:
		case Archetype::DockType::MoorLarge:
			dockNumberMessageId = "gcs_misc_number_" + std::to_string(dockPortIndex + 1) + "-";
			break;

		default:
			dockNumberMessageId = "";
			break;
		}

		if (dockQueues[dockTargetId].count(ship))
		{
			lines = {
				GetShipMessageId(ship),
				CreateID("gcs_dockrequest_nowcleared_01+"),
				CreateID((!dockTypeMessageId.empty() ? (dockTypeMessageId + "-") : "").c_str()),
				CreateID("gcs_dockrequest_proceed_01+"),
				CreateID(dockTargetMessageId.c_str()),
				CreateID(dockNumberMessageId.c_str())
			};
		}
		else
		{
			lines = {
				CreateID(("gcs_misc_ack_0" + std::to_string(GetRandom(1,3)) + "-").c_str()),
				CreateID("gcs_dockrequest_yourrequest+"),
				CreateID(dockTypeMessageId.c_str()),
				CreateID("gcs_dockrequest_granted_01-"),
				CreateID("gcs_dockrequest_proceed_01+"),
				CreateID(dockTargetMessageId.c_str()),
				CreateID(dockNumberMessageId.c_str())
			};
		}
		break;
	}



	case DOCK_HOST_RESPONSE::DOCK_IN_USE:
	{
		lines = {
			CreateID(("gcs_misc_ack_0" + std::to_string(GetRandom(1,3)) + "-").c_str()),
			CreateID("gcs_dockrequest_standby_01-"),
			CreateID("gcs_dockrequest_delayedreason_01-"),
			CreateID("gcs_dockrequest_willbecleared_01-")
		};
		break;
	}



	case DOCK_HOST_RESPONSE::DOCK_DENIED:
	{
		std::string dockTypeMessageId;
		switch (dockType)
		{
		case Archetype::DockType::Berth:
		case Archetype::DockType::Jump:
			dockTypeMessageId = "gcs_dockrequest_todock";
			break;

		case Archetype::DockType::MoorSmall:
		case Archetype::DockType::MoorMedium:
		case Archetype::DockType::MoorLarge:
			dockTypeMessageId = "gcs_dockrequest_tomoor";
			break;

		case Archetype::DockType::Ring:
			dockTypeMessageId = "gcs_dockrequest_toland";
			break;

		default:
			dockTypeMessageId = "";
			break;
		}

		lines = {
			CreateID(("gcs_misc_ack_0" + std::to_string(GetRandom(1,3)) + "-").c_str()),
			CreateID("gcs_dockrequest_yourrequest+"),
			CreateID(dockTypeMessageId.c_str()),
			CreateID("gcs_dockrequest_denied_01-"),
			CreateID("gcs_dockrequest_nofit_01-")
		};
		break;
	}



	case DOCK_HOST_RESPONSE::ACCESS_DENIED:
		lines = { CreateID("gcs_dockrequest_denied_01-") };
		break;
	}

	uint shipId = ship;
	pub::SpaceObj::SendComm(solar->id, shipId, solar->voiceId, &solar->commCostume, 0, lines.data(), lines.size(), 19007 /* base comms type*/, 0.5f, false);
	return 0;
}

void ServerCrash()
{
	for (auto& pb : player_bases)
	{
		auto base = pb.second;
		if (base->archetype && (base->archetype->logic || !base->archetype->invulnerable))
		{
			base->Save();
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Functions to hook */
EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Base Plugin by cannon";
	p_PI->sShortName = "base";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
  
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 1));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ClearClientInfo, PLUGIN_ClearClientInfo, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&DelayedDisconnect, PLUGIN_DelayedDisconnect, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect, PLUGIN_HkIServerImpl_CharacterSelect, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&RequestEvent, PLUGIN_HkIServerImpl_RequestEvent, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch, PLUGIN_HkIServerImpl_PlayerLaunch, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch_AFTER, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect_AFTER, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&JumpInComplete, PLUGIN_HkIServerImpl_JumpInComplete, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter, PLUGIN_HkIServerImpl_BaseEnter, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&BaseExit, PLUGIN_HkIServerImpl_BaseExit, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&Dock_Call, PLUGIN_HkCb_Dock_Call, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&Dock_Call_After, PLUGIN_HkCb_Dock_Call_AFTER, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&LaunchPosHook, PLUGIN_LaunchPosHook, 0));

	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&SetEquipPacket, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SETEQUIPMENT, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&SetHullPacket, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SETHULLSTATUS, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&SetColGrp, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SETCOLLISIONGROUPS, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&Update, PLUGIN_HkIServerImpl_Update, 0));

	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodSell, PLUGIN_HkIServerImpl_GFGoodSell, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ReqRemoveItem, PLUGIN_HkIServerImpl_ReqRemoveItem, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ReqRemoveItem_AFTER, PLUGIN_HkIServerImpl_ReqRemoveItem_AFTER, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodBuy, PLUGIN_HkIServerImpl_GFGoodBuy, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodBuy_AFTER, PLUGIN_HkIServerImpl_GFGoodBuy_AFTER, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ReqAddItem, PLUGIN_HkIServerImpl_ReqAddItem, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ReqAddItem_AFTER, PLUGIN_HkIServerImpl_ReqAddItem_AFTER, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ReqChangeCash, PLUGIN_HkIServerImpl_ReqChangeCash, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ReqSetCash, PLUGIN_HkIServerImpl_ReqSetCash, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ReqSetCash_AFTER, PLUGIN_HkIServerImpl_ReqSetCash_AFTER, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ReqEquipment, PLUGIN_HkIServerImpl_ReqEquipment, 11));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ReqShipArch_AFTER, PLUGIN_HkIServerImpl_ReqShipArch_AFTER, 11));

	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&HkTimerCheckKick, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&BaseDestroyed, PLUGIN_BaseDestroyed, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&SolarDamageHull, PLUGIN_SolarHullDmg, 15));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 11));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&PopUpDialogue, PLUGIN_HKIServerImpl_PopUpDialog, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ServerCrash, PLUGIN_ServerCrash, 0));
	return p_PI;
}

void ResetAllBasesShieldStrength()
{
	for (auto& i : player_bases)
	{
		i.second->shield_strength_multiplier = base_shield_strength;
		i.second->damage_taken_since_last_threshold = 0;
	}
}