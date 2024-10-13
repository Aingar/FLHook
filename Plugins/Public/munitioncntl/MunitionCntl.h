#pragma once

#include <unordered_set>
#include <unordered_map>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

void ShipShieldDamageNaked();
void GuidedExplosionHitNaked();
bool __stdcall ExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg);
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
	float armorPen = 0.0f;
	bool detonateOnEndLifetime = false;
	bool stopSpin = false;
};

struct GuidedData
{
	bool noTrackingAlert = false;
	uint trackingBlacklist = 0;
	float armingTime = 0.0f;
	float topSpeed = 0.0f;
	float armorPen = 0.0f;
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
};

struct ExplosionDamageData
{
	uint weaponType = 0;
	float percentageDamageHull = 0.0f;
	float percentageDamageShield = 0.0f;
	float percentageDamageEnergy = 0.0f;
	float armorPen = 0.0f;
	bool cruiseDisrupt = false;
	bool missileDestroy = true;
};

enum TRACKING_STATE {
	TRACK_ALERT,
	TRACK_NOALERT,
	NOTRACK_NOALERT
};

extern unordered_map<uint, ExplosionDamageData> explosionTypeMap;
extern unordered_map<uint, ShieldBoostFuseInfo> shieldFuseMap;
extern unordered_map<uint, GuidedData> guidedDataMap;
extern ShieldState playerShieldState[MAX_CLIENT_ID + 1];
extern PLUGIN_RETURNCODE returncode;
extern FARPROC ShipShieldDamageOrigFunc;
extern FARPROC GuidedExplosionHitOrigFunc;

extern float shipArmorValue;
extern uint shipArmorArch;
extern float weaponArmorPenValue;
extern uint weaponArmorPenArch;
extern bool armorEnabled;

extern unordered_map<uint, float> munitionArmorPenMap;