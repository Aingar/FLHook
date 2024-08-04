#pragma once

#include <unordered_set>
#include <unordered_map>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

void ShipShieldDamageNaked();
void ShipShieldExplosionDamageNaked();
void LoadHookOverrides();
float __fastcall GetWeaponModifier(CEShield* shield, void* edx, uint& weaponType);

constexpr uint shipObjType = (Fighter | Freighter | Transport | Gunboat | Cruiser | Capital);

struct MineInfo
{
	float armingTime = 0.0f;
	float dispersionAngle = 0.0f;
	bool detonateOnEndLifetime = false;
	bool stopSpin = false;
};


struct ShieldState
{
	bool shieldState;
	ShieldSource changeSource;
	mstime boostUntil;
	float damageReduction;
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
};


struct ShieldBoostFuseInfo
{
	uint fuseId;
	mstime lastUntil;
};


struct EngineProperties
{
	bool ignoreCDWhenEKd = false;
	float engineKillCDSpeedLimit;
};

struct ExplosionDamageType
{
	uint type = 0;
};


enum TRACKING_STATE {
	TRACK_ALERT,
	TRACK_NOALERT,
	NOTRACK_NOALERT
};

extern unordered_map<uint, ExplosionDamageType> explosionTypeMap;
extern ShieldState playerShieldState[MAX_CLIENT_ID + 1];
