#pragma once

#include <unordered_set>
#include <unordered_map>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

void ShipShieldDamageNaked();
void GuidedExplosionHitNaked();
void SolarExplosionHitNaked();
void ShipColGrpDmgNaked();
void ShipFuseLightNaked();
void __fastcall ShipEquipDamage(IObjRW* iobj, void* edx, CAttachedEquip* equip, float incDmg, DamageList* dmg);
void __fastcall ShipMunitionHit(IObjRW* iShip, void* edx, MunitionImpactData* data, DamageList* dmg);
bool __stdcall ShipExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg);
void LoadHookOverrides();
float __fastcall GetWeaponModifier(CEShield* shield, void* edx, uint& weaponType);
void FindAndDisableEquip(uint client, const string& hardpoint);
FireResult __fastcall CELauncherFire(CELauncher* gun, void* edx, const Vector& pos);
void __fastcall CShipInit(CShip* ship, void* edx, CShip::CreateParms& parms);

typedef void(__thiscall* TriggerExplosion)(StarSystem*, ExplosionDamageEvent*);
static TriggerExplosion TriggerExplosionFunc = TriggerExplosion(0x6D0B260);
static st6::map<uint, StarSystem>* StarSystemMap = (st6::map<uint, StarSystem>*)0x6D8DA2C;

constexpr uint shipObjType = (Fighter | Freighter | Transport | Gunboat | Cruiser | Capital);

struct BurstFireGunData
{
	int maxMagSize;
	int bulletsLeft;
	float reloadTime;
};

struct BurstFireData
{
	int magSize;
	float reloadTime;
};

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

struct ExplosionDamageData
{
	uint weaponType = 0;
	float percentageDamageHull = 0.0f;
	float percentageDamageShield = 0.0f;
	float percentageDamageEnergy = 0.0f;
	int armorPen = 0;
	float detDist = 0.0f;
	bool cruiseDisrupt = false;
	bool damageSolars = true;
	bool missileDestroy = false;
};

struct ShipData
{
	int engineCount = 0;
	bool internalEngine = false;
	unordered_map<string, unordered_set<string>> engineHpMap;
	unordered_map<ushort, vector<string>> colGrpHpMap;
	unordered_map<uint, vector<string>> fuseHpMap;
};

struct NewMissileUpdater
{
	uint system = 0;
	uint counter = 0;
	mstime mstimer = 0;
	double timer = 0;
};

struct InvulData
{
	float minHpPerc;
	bool hullOnlyInvul;
};

enum TRACKING_STATE {
	TRACK_ALERT,
	TRACK_NOALERT,
	NOTRACK_NOALERT
};

extern unordered_map<uint, ExplosionDamageData> explosionTypeMap;
extern unordered_map<uint, ShieldBoostFuseInfo> shieldFuseMap;
extern unordered_map<uint, GuidedData> guidedDataMap;
extern unordered_map<uint, ShipData> shipDataMap;
extern unordered_map<uint, InvulData> invulMap;
extern ShieldState playerShieldState[MAX_CLIENT_ID + 1];
extern PLUGIN_RETURNCODE returncode;
extern FARPROC ShipShieldDamageOrigFunc;
extern FARPROC GuidedExplosionHitOrigFunc;
extern FARPROC SolarExplosionHitOrigFunc;
extern FARPROC ShipMunitionHitOrigFunc;
extern FARPROC ShipColGrpDmgFunc;
extern FARPROC ShipFuseLightFunc;

extern vector<float> armorReductionVector;
extern int shipArmorRating;
extern uint shipArmorArch;
extern int weaponArmorPenValue;
extern uint weaponArmorPenArch;
extern bool armorEnabled;

extern unordered_map<uint, int> munitionArmorPenMap;
extern unordered_map<uint, int> shipArmorMap;
extern unordered_map<uint, unordered_map<ushort, BurstFireGunData>> shipGunData;
extern unordered_map<uint, BurstFireData> burstGunData;