#pragma once

#include <unordered_set>
#include <unordered_map>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

void ShipShieldDamageNaked();
void ShipShieldExplosionDamageNaked();
void SolarExplosionHitNaked();
void LoadHookOverrides();
float __fastcall GetWeaponModifier(CEShield* shield, void* edx, uint& weaponType);

typedef void(__thiscall* TriggerExplosion)(StarSystem*, ExplosionDamageEvent*);
static TriggerExplosion TriggerExplosionFunc = TriggerExplosion(0x6D0B260);
static st6::map<uint, StarSystem>* StarSystemMap = (st6::map<uint, StarSystem>*)0x6D8DA2C;

constexpr uint shipObjType = (Fighter | Freighter | Transport | Gunboat | Cruiser | Capital);

struct MineInfo
{
	float armingTime = 0.0f;
	float dispersionAngle = 0.0f;
	bool detonateOnEndLifetime = false;
	bool stopSpin = false;
};

struct GuidedData
{
	bool noTrackingAlert = false;
	uint trackingBlacklist = 0;
	float armingTime = 0.0f;
	float topSpeed = 0.0f;
};

struct ShieldState
{
	bool shieldState = true;
	ShieldSource changeSource = ShieldSource::UNSET;
	mstime boostUntil = 0;
	float damageReduction = 0.0f;
	float damageTaken = 0.0f;
};

struct ShieldSyncData
{
	uint client;
	uint targetClient;
	uint count = 0;
};

struct ShieldBoostData
{
	float durationPerBattery;
	float minimumDuration;
	float maximumDuration;
	float damageReduction;
	uint fuseId;

	float hullBaseDamage = 0.0f;
	float hullReflectDamagePercentage = 0.0f;
	float hullDamageCap = 0.0f;
	float energyBaseDamage = 0.0f;
	float energyReflectDamagePercentage = 0.0f;
	float energyDamageCap = 0.0f;
	float radius = 0.0f;
	uint explosionFuseId = 0;
};

struct ShieldBoostFuseInfo
{
	mstime lastUntil;
	const ShieldBoostData* boostData;
};

struct EngineProperties
{
	bool ignoreCDWhenEKd = false;
	float engineKillCDSpeedLimit;
	string hpType;
};

struct ExplosionDamageType
{
	uint type = 0;
	bool damageSolars = true;
};

struct ShipData
{
	int engineCount = 0;
	bool internalEngine = false;
	unordered_map<string, unordered_set<string>> hpMap;
};

enum TRACKING_STATE {
	TRACK_ALERT,
	TRACK_NOALERT,
	NOTRACK_NOALERT
};

extern unordered_map<uint, ExplosionDamageType> explosionTypeMap;
extern unordered_map<uint, ShieldBoostFuseInfo> shieldFuseMap;
extern ShieldState playerShieldState[MAX_CLIENT_ID + 1];
