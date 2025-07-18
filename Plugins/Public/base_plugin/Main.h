#ifndef __MAIN_H__
#define __MAIN_H__ 1

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <set>
#include <map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include <unordered_map>
#include <unordered_set>

#define POPUPDIALOG_BUTTONS_LEFT_YES 1
#define POPUPDIALOG_BUTTONS_CENTER_NO 2
#define POPUPDIALOG_BUTTONS_RIGHT_LATER 4
#define POPUPDIALOG_BUTTONS_CENTER_OK 8

using namespace std;

static uint STORAGE_MODULE_CAPACITY = 40000;
static uint MAX_PINNED_ITEMS = 15;
const ushort DEFAULT_AFFILIATION = MakeId("fc_uk_grp");

void LogCheater(uint client, const wstring& reason);
uint GetAffliationFromClient(uint client);

struct AICONFIG
{
	pub::AI::Personality::GunUseStruct gunUse;
	pub::AI::Personality::MissileUseStruct missileUse;
	pub::AI::Personality::JobStruct job;
};

struct DYNAMIC_ITEM
{
	uint sharedAmount = 0;
	vector<uint> items;
};

struct RECIPE
{
	uint nickname = 0;
	string nicknameString = "";
	vector<pair<uint, uint>> produced_items;
	vector<unordered_map<uint, pair<uint, uint>>> affiliation_produced_items;
	uint shortcut_number = 0;
	bool loop_production = false;
	bool restricted = false;
	wstring infotext = L"";
	wstring craft_type = L"";
	float cooking_rate = 0;
	vector<unordered_map<uint, pair<uint, uint>>> affiliation_consumed_items;
	vector<vector<pair<uint, uint>>> dynamic_consumed_items;
	vector<DYNAMIC_ITEM> dynamic_consumed_items_alt;
	vector<pair<uint, uint>> consumed_items;
	vector<pair<uint, uint>> catalyst_items;
	vector<pair<uint, uint>> catalyst_workforce;
	uint credit_cost = 0;
	uint reqlevel = 0;
	unordered_map<uint, float> affiliationBonus;
	uint moduleCargoStorage = 0;
};

struct BASE_VULNERABILITY_WINDOW {
	int start;
	int end;
};

struct WEAR_N_TEAR_MODIFIER {
	float fromHP;
	float toHP;
	float modifier;
};

struct ARCHTYPE_STRUCT
{
	int logic;
	int invulnerable;
	list<string> modules;
	int idrestriction;
	unordered_set<uint> allowedids;
	int shipclassrestriction;
	unordered_set<uint> allowedshipclasses;
	bool isjump = false;
	bool ishubreturn = false;
	bool display = false;
	bool hasShield = false;
	bool siegeGunOnly = false;
	bool vulnerabilityWindowUse = false;
	string miningevent;
};

struct MARKET_ITEM
{
	MARKET_ITEM() : quantity(0), price(1), sellPrice(1), min_stock(100000), max_stock(100000), is_pinned(false), is_public(false), shipHullId(0) {}

	// Number of units of commodity stored in this base
	uint quantity;

	// Buy price for commodity.
	int price;

	// Sell price for commodity.
	int sellPrice;

	// Stop selling if the base holds less than this number of items
	uint min_stock;

	// Stop buying if the base holds more than this number of items
	uint max_stock;

	// Displays the selected item in the F9 window
	bool is_pinned;

	// Making public to all players without restrictions
	bool is_public;

	uint shipHullId;
};

struct NEWS_ITEM
{
	wstring headline;
	wstring text;
};

class PlayerBase;

class Module
{
public:
	int type;
	int cargoSpace = 0;
	static const int TYPE_BUILD = 0;
	static const int TYPE_CORE = 1;
	static const int TYPE_FACTORY = 2;
	static const int TYPE_STORAGE = 3;
	static const int TYPE_DEFENSE_1 = 4;
	static const int TYPE_DEFENSE_2 = 9;
	static const int TYPE_DEFENSE_3 = 10;
	static const int TYPE_REARMAMENT = 11;


	Module(uint the_type) : type(the_type){}
	virtual ~Module() {}
	virtual void Spawn() {}
	virtual wstring GetInfo(bool xml) = 0;
	virtual void LoadState(INI_Reader& ini) = 0;
	virtual void SaveState(FILE* file) = 0;

	virtual bool Timer(uint time) { return false; }

	virtual float SpaceObjDamaged(uint space_obj, uint attacking_space_obj, float incoming_damage) { return 0.0f; }
	virtual bool SpaceObjDestroyed(uint space_obj) { return false; }
	virtual void SetReputation(int player_rep, float attitude) {}

};

class CoreModule : public Module
{
public:
	PlayerBase* base;

	// The space ID of this base
	uint space_obj;

	// If true, do not use food and commodities
	bool dont_eat;

	// If true, do not take damage
	bool dont_rust;

	bool baseHealthChanged;
	bool undergoingDestruction;

	CoreModule(PlayerBase* the_base);
	~CoreModule();
	void Spawn();
	wstring GetInfo(bool xml);

	void LoadState(INI_Reader& ini);
	void SaveState(FILE* file);

	bool Timer(uint time);
	float SpaceObjDamaged(uint space_obj, uint attacking_space_obj, float incoming_damage);
	bool SpaceObjDestroyed(uint space_obj, bool moveFile = true, bool broadcastDeath = true);
	void SetReputation(int player_rep, float attitude);
	void EnableShieldFuse(bool shieldState);

	void RepairDamage();
};

class StorageModule : public Module
{
public:
	PlayerBase* base;

	StorageModule(PlayerBase* the_base);
	~StorageModule();
	wstring GetInfo(bool xml);

	void LoadState(INI_Reader& ini);
	void SaveState(FILE* file);
};

class DefenseModule : public Module
{
public:
	PlayerBase* base;

	// The space object of the platform
	uint space_obj1;
	uint space_obj2;

	// The position of the platform
	Vector pos;

	DefenseModule(PlayerBase* the_base);
	DefenseModule(PlayerBase* the_base, uint the_type);
	~DefenseModule();
	wstring GetInfo(bool xml);

	void LoadState(INI_Reader& ini);
	void SaveState(FILE* file);

	bool Timer(uint time);
	float SpaceObjDamaged(uint space_obj, uint attacking_space_obj, float incoming_damage);
	bool SpaceObjDestroyed(uint space_obj);
	void SetReputation(int player_rep, float attitude);
	void Reset();

	static void LoadSettings(const string& path);
};

class BuildModule : public Module
{
public:
	PlayerBase* base;

	RECIPE active_recipe;
	float amassedCookingRate;

	BuildModule(PlayerBase* the_base);
	BuildModule(PlayerBase* the_base, const RECIPE* moduleRecipe);
	bool TryConsume(float volumeToProcess);

	wstring GetInfo(bool xml);

	bool Paused = false;
	void LoadState(INI_Reader& ini);
	void SaveState(FILE* file);
	static const RECIPE* GetModuleRecipe(wstring& module_name, wstring& build_list);

	bool Timer(uint time);
};

class FactoryModule : public Module
{
public:
	PlayerBase* base;

	uint factoryNickname;

	// The currently active recipe
	RECIPE active_recipe;

	float amassedCookingRate;

	// List of queued recipes;
	list<uint> build_queue;

	bool sufficientCatalysts = true;

	FactoryModule(PlayerBase* the_base);
	FactoryModule(PlayerBase* the_base, uint factoryNickname);
	wstring GetInfo(bool xml);
	void LoadState(INI_Reader& ini);
	void SaveState(FILE* file);
	void SetActiveRecipe(uint product, bool resetAmassedCookingRate);
	bool Timer(uint time);
	bool TryConsume(float volumeToProcess);
	static FactoryModule* FactoryModule::FindModuleByProductInProduction(PlayerBase* pb, uint searchedProduct);
	static const RECIPE* FactoryModule::GetFactoryProductRecipe(wstring& craftType, wstring& product);
	static void FactoryModule::StopAllProduction(PlayerBase* pb);
	static void FactoryModule::ClearAllProductionQueues(PlayerBase* pb);
	static bool FactoryModule::IsFactoryModule(Module* module);

	bool Paused = false;
	bool ToggleQueuePaused(bool NewState);
	bool AddToQueue(uint product);
	bool ClearQueue();
	void ClearRecipe();
};

class RearmamentModule : public Module
{
	PlayerBase* base;
public:
	RearmamentModule(PlayerBase* the_base);
	RearmamentModule::~RearmamentModule();
	wstring GetInfo(bool xml);
	void LoadState(INI_Reader& ini);
	void SaveState(FILE* file);
	static void RearmamentModule::CheckPlayerInventory(uint clientId, PlayerBase* base);
	static void RearmamentModule::Rearm(uint clientId);
};

class BasePassword
{
public:
	wstring pass;
	bool admin = false;
	bool viewshop = false;

	friend bool operator ==(const BasePassword& lhs, const BasePassword& rhs);
};
inline bool operator ==(const BasePassword& lhs, const BasePassword& rhs)
{
	return lhs.pass == rhs.pass;
};

class PlayerBase
{
public:
	PlayerBase(uint client, const wstring& password, const wstring& basename);
	PlayerBase(const string& path);
	~PlayerBase();

	void Spawn();

	void Timer(uint curr_time);

	void SetupDefaults();
	void Load();
	void Save();

	bool AddMarketGood(uint good, uint quantity);
	void RemoveMarketGood(uint good, uint quantity);
	void ChangeMoney(INT64 quantity);
	int GetRemainingCargoSpace();
	void RecalculateCargoSpace();
	uint HasMarketItem(uint good);
	uint HasFedWorkerItem(uint good);
	bool FeedCrew(uint crewId, uint count);

	static string CreateBaseNickname(const string& basename);

	float GetAttitudeTowardsClient(uint client);
	void SyncReputationForBase();
	void SyncReputationForBaseObject(uint space_obj);
	bool IsOnHostileList(const wstring& charname, uint affiliation);
	bool IsOnAllyList(const wstring& charname, uint affiliation);
	bool IsOnSRPList(const wstring& charname, uint affiliation);

	void SpaceObjDamaged(uint space_obj, uint attacking_space_obj, float incoming_damage);
	void CheckVulnerabilityWindow(uint currTime);
	void LogDamageDealers();

	wstring GetBaseHeaderText();
	wstring BuildBaseDescription();
	void UpdateBaseInfoText();

	ARCHTYPE_STRUCT* archetype;

	bool isFreshlyBuilt;

	bool isPublic;

	// The base nickname
	string nickname;

	// Reference to the base's CSolar object
	CSolar* baseCSolar;

	// The base affiliation
	uint affiliation;

	//base type
	string basetype;

	//base appearance
	string basesolar;

	//base appearance
	string baseloadout;
	// The name of the base shown to other players
	wstring basename;

	wstring description_text;

	// The header infocard for the base
	wstring infocardHeader;

	// The infocard for the base
	wstring infocard;

	// The infocard paragraphs for the base
#define MAX_CHARACTERS 500
#define MAX_PARAGRAPHS 5
	wstring infocard_para[MAX_PARAGRAPHS + 1];

	// The system the base is in
	uint system;

	// The position of the base
	Vector position;

	// The rotation of the base
	Matrix rotation;

	// The basic armour and commodity storage available on this base->
	uint base_level;

	// Total storage space capacity
	uint storage_space;

	uint preferred_food;

	// The commodities carried by this base->
	unordered_map<uint, MARKET_ITEM> market_items;

	unordered_map<uint, uint> fed_workers;

	unordered_set<uint> pinned_market_items;

	unordered_set<uint> public_market_items;

	bool pinned_item_updated;

	// The money this base has
	INT64 money;

	// The current hit points of the base
	float base_health;

	// The maximum hit points of the base
	float max_base_health;

	// When the base is spawned, this is the IDS of the base name
	uint solar_ids;

	// Space F9 info
	uint description_ids;

	// The ingame hash of the nickname
	uint base;

	unordered_map<wstring, uint> last_login_attempt_time;
	unordered_map<wstring, uint> unsuccessful_logins_in_a_row;

	// The list of administration passwords
	list<BasePassword> passwords;

	// If 0 then base is neutral to all ships. Only ships on the ally tag list may dock.
	// If 1 then base is hostile to all ships unless they are on the ally tag list.
	// If 2 then base is neutral to all ships and any ship may dock.

	enum class DEFENSE_MODE
	{
		IFF = 1,
		NODOCK_NEUTRAL,
		NODOCK_HOSTILE
	};

	DEFENSE_MODE defense_mode;

	bool has_shield = false;

	bool siege_gun_only = false;

	bool use_vulnerability_window = false;

	uint failed_update_counter = 0;

	unordered_map<wstring, float> damageTakenMap;

	//shield strength parameters
	float shield_strength_multiplier;
	float base_shield_reinforcement_threshold;
	float damage_taken_since_last_threshold;

	// List of allied ship tags.
	list<wstring> ally_tags;

	//List of allied ships
	unordered_set<wstring> ally_names;

	//List of allied factions
	unordered_set<uint> ally_factions;

	//List of hostile factions
	unordered_set<uint> hostile_factions;

	// List of ships that are hostile to this base
	unordered_set<wstring> temp_hostile_names;

	// List of ships that are permanently hostile to this base
	list<wstring> hostile_tags;

	// List of ships that are permanently hostile to this base
	unordered_set<wstring> hostile_names;

	// List of SRP allied ship tags.
	list<wstring> srp_tags;

	//List of SRP allied ships
	unordered_set<wstring> srp_names;

	//List of SRP allied factions
	unordered_set<uint> srp_factions;

	// Modules for base
	vector<Module*> modules;
	unordered_map<wstring, FactoryModule*> craftTypeTofactoryModuleMap;

	// Available crafting types
	set<wstring> availableCraftList;

	// Path to base ini file.
	string path;

	// The proxy base associated with the system this base is in.
	uint proxy_base;

	// if true, the base was repaired or is able to be repaired
	bool isCrewSupplied;

	map<uint, int> reservedCatalystMap;

	// The state of the shield
	bool isShieldOn;

	// The number of seconds that shield will be active
	uint shield_timeout;

	int logic;
	int invulnerable;

	uint lastVulnerabilityWindowChange = 0;

	BASE_VULNERABILITY_WINDOW vulnerabilityWindow1 = { -1, -1 };
	BASE_VULNERABILITY_WINDOW vulnerabilityWindow2 = { -1, -1 };

	enum class BASE_VULNERABILITY_STATE
	{
		INVULNERABLE,
		PREVULNERABLE,
		VULNERABLE
	};

	BASE_VULNERABILITY_STATE vulnerableWindowStatus = BASE_VULNERABILITY_STATE::INVULNERABLE;

	////////////Unique to Solars/////////////

	//the destination system, once again for jumps
	uint destSystem;

	//the destination vector
	uint destObject;
	string destObjectName;

	//return hub JHs only, destination data
	Vector destPos;
	Matrix destOri;

	unordered_set<uint> dockKeyList;
	wstring noDockKeyMessage;

	/////////////////////////////////////////

	bool isRearmamentAvailable = false;

	float rearmamentCostPerCredit = 1.0f;
};

PlayerBase* GetPlayerBase(uint base);
PlayerBase* GetPlayerBaseForClient(uint client);

void BaseLogging(const char* szString, ...);

void RespawnBase(PlayerBase* base);
void DeleteBase(PlayerBase* base, bool moveFile);
void LoadDockState(uint client);
void SaveDockState(uint client);
void DeleteDockState(uint client);

/// Send a command to the client at destination ID 0x9999
void SendCommand(uint client, const wstring& message);
void SendSetBaseInfoText(uint client, const wstring& message);
void SendSetBaseInfoText2(uint client, const wstring& message);
void SendResetMarketOverride(uint client);
void SendMarketGoodUpdated(PlayerBase* base, uint good, MARKET_ITEM& item);
void SendMarketGoodSync(PlayerBase* base, uint client);
wstring UIntToPrettyStr(uint value);
void SendBaseStatus(uint client, PlayerBase* base);
void SendBaseStatus(PlayerBase* base);
void ForceLaunch(uint client);
void SendBaseIDSList(uint client, uint solarId, uint ids);

enum class POPUPWINDOWTYPE
{
	NONE,
	SHOP,
	SHOP_HELP,
	HELP
};

struct CLIENT_DATA
{
	CLIENT_DATA() : reverse_sell(false), stop_buy(false), admin(false), viewshop(false), docking_base(0),
		player_base(0), last_player_base(0), lastPopupPage(0), lastPopupWindowType(POPUPWINDOWTYPE::NONE){}

	// If true reverse the last sell by readding the item.
	bool reverse_sell;

	// The cargo list used by the reverse sell.
	list<CARGO_INFO> cargo;

	// If true block the current buy and associated reqitemadd function.
	bool stop_buy;

	// True if this player is the base administrator.
	bool admin;

	// True if this player is able to view the shop.
	bool viewshop;

	// Set to player base hash if ship is in base-> 0 if not.
	uint player_base;

	// Set to player base hash if ship is in base or was last in a player base-> 0 after 
	// docking at any non player base->
	uint last_player_base;

	uint docking_base;

	uint lastPopupPage;
	POPUPWINDOWTYPE lastPopupWindowType;
	wstring lastShopFilterKeyword;
};

namespace ExportData
{
	void ToHTML();
	void ToJSON();
	void ToJSONBasic();
}

namespace HyperJump
{
	void InitJumpHole(uint baseId, uint destSystem, uint destObject);
	void LoadHyperspaceHubConfig(const string& configPath);
	void InitJumpHoleConfig();
	void CheckForUnchartedDisconnect(uint client, uint ship);
	void ClearClientInfo(uint iClientID);
	void CharacterSelect_AFTER(uint client);


	extern unordered_set<uint> markedForDeath;
}

namespace PlayerCommands
{
	void BaseHelp(uint client, const wstring& args);

	void BaseLogin(uint client, const wstring& args);
	void BaseAddPwd(uint client, const wstring& args);
	void BaseRmPwd(uint client, const wstring& args);
	void BaseLstPwd(uint client, const wstring& args);
	void BaseSetMasterPwd(uint client, const wstring& args);

	void BaseAccess(uint client, const wstring& args);
	void BaseViewMyFac(uint client, const wstring& args);
	void BaseRep(uint client, const wstring& args);

	void BaseInfo(uint client, const wstring& args);
	void BaseDefenseMode(uint client, const wstring& args);
	void BaseDefMod(uint client, const wstring& args);
	void BaseBuildMod(uint client, const wstring& args);
	void BaseBuildModDestroy(uint client, const wstring& args);
	void BaseFacMod(uint client, const wstring& args);
	void PopulateHelpMenus();
	void Bank(uint client, const wstring& args);
	void Shop(uint client, const wstring& args);
	void BaseSwapModule(uint client, const wstring& args);
	void GetNecessitiesStatus(uint client, const wstring& args);
	bool CheckSolarDistances(uint client, uint systemID, Vector pos);
	void BaseSetVulnerabilityWindow(uint client, const wstring& args);
	void SetPrefFood(uint client, const wstring& cmd);
	void BaseSetPublic(uint client, const wstring& cmd);

	void BaseDeploy(uint client, const wstring& args);
	void BaseTestDeploy(uint client, const wstring& args);

	void ShowShopStatus(uint client, PlayerBase* base, wstring substring, int page);
	void ShowShopHelp(uint client);

	void Aff_initer();
}

namespace Log
{
	void LogBaseAction(string basename, const char *message);
	void LogGenericAction(string message);
}

namespace CreateSolar
{
	pub::AI::SetPersonalityParams MakePersonality();
	void SpawnSolar(unsigned int& spaceID, pub::SpaceObj::SolarInfo const& solarInfo);
	void CreateSolarCallout(SPAWN_SOLAR_STRUCT* info);
	void DespawnSolarCallout(DESPAWN_SOLAR_STRUCT* info);
}

extern unordered_map<uint, CLIENT_DATA> clients;

extern unordered_map<uint, Module*> spaceobj_modules;

extern unordered_map<uint, uint> core_upgrade_recipes;

extern unordered_map<uint, uint> core_upgrade_storage;

// Map of ingame hash to info
extern unordered_map<uint, class PlayerBase*> player_bases;
extern unordered_map<uint, PlayerBase*>::iterator baseSaveIterator;

extern int set_plugin_debug;
extern int set_plugin_debug_special;

/// Global recipe map
extern unordered_map<uint, RECIPE> recipeMap;
/// Maps of shortcut numbers to recipes to construct item.
extern unordered_map<wstring, map<uint, RECIPE>> recipeCraftTypeNumberMap;
extern unordered_map<wstring, map<wstring, RECIPE>> recipeCraftTypeNameMap;
extern unordered_map<uint, vector<wstring>> factoryNicknameToCraftTypeMap;
extern unordered_map<wstring, RECIPE> moduleNameRecipeMap;
extern unordered_map<wstring, map<uint, RECIPE>> craftListNumberModuleMap;
extern unordered_set<wstring> buildingCraftLists;

struct REPAIR_ITEM
{
	uint good;
	uint quantity;
};
extern vector<REPAIR_ITEM> set_base_repair_items;

extern uint set_base_crew_type;

extern vector<uint> set_base_crew_consumption_items;

extern vector<uint> set_base_crew_food_items;

extern uint set_crew_check_frequency;

extern unordered_map<string, ARCHTYPE_STRUCT> mapArchs;

/// List of banned systems
extern unordered_set<uint> bannedSystemList;

/// The ship used to construct and upgrade bases
extern uint set_construction_shiparch;

/// Mininmum distances for base deployment
extern bool enableDistanceCheck;
extern float minMiningDistance;
extern float minPlanetDistance;
extern float minStationDistance;
extern float minLaneDistance;
extern float minJumpDistance;
extern float minDistanceMisc;
extern float minOtherPOBDistance;
extern unordered_set<uint> lowTierMiningCommoditiesSet;

/// Deployment command cooldown trackimg
extern unordered_map<uint, uint> deploymentCooldownMap;
extern uint deploymentCooldownDuration;

/// Map of good to quantity for items required by construction ship
extern map<uint, uint> construction_items;

/// Construction cost in credits
extern int construction_credit_cost;

/// Damage to the base every 10 seconds
extern uint set_damage_per_10sec;

/// Damage to the base every tick
extern float set_damage_per_tick;

/// Additional damage penalty for stations without proper crew
extern float no_crew_damage_multiplier;

/// How much damage to repair per cycle
extern uint repair_per_repair_cycle;

/// The seconds per damage tick
extern uint set_damage_tick_time;

/// The seconds per tick
extern uint set_tick_time;

// set of configurable variables defining the diminishing returns on damage during POB siege
// POB starts at base_shield_strength, then every 'threshold' of damage taken, 
// shield goes up in absorption by the 'increment'
// threshold size is to be configured per core level.
extern unordered_map<int, float> shield_reinforcement_threshold_map;
extern float shield_reinforcement_increment;
extern float base_shield_strength;

extern const uint shield_fuse;

extern int vulnerability_window_length;

extern int vulnerability_window_change_cooldown;

extern int vulnerability_window_minimal_spread;

extern bool single_vulnerability_window;

/// Holiday mode
extern bool set_holiday_mode;

wstring HtmlEncode(wstring text);

wstring IntToStr(uint iValue);

extern string set_status_path_html;
extern string set_status_path_json;
extern string set_status_path_json_public_shop;

extern float damage_threshold;

extern float siege_mode_damage_trigger_level;

extern float siege_mode_chain_reaction_trigger_distance;

extern unordered_set<uint> customSolarList;

extern unordered_map<uint, float> siegeWeaponryMap;

extern unordered_set<uint> humanCargoList;

extern unordered_map<uint, unordered_set<CSolar*>> POBSolarsBySystemMap;

extern bool set_SkipUnchartedKill;

extern int defense_platform_activation_offset;

extern float rearmamentCostRatio;
extern vector<pair<uint, float>> rearmamentCreditRatio;

struct ScheduledRespawn
{
	string path;
	uint secondsUntil;
};

extern vector<ScheduledRespawn> basesToRespawn;

extern uint base_access_entry_limit;

extern unordered_map<uint, std::pair<uint, Costume>> factionCostumeMap;

// From EquipmentUtilities.cpp
namespace EquipmentUtilities
{
	void ReadIniNicknames();
	const char* FindNickname(uint hash);
}
#endif
