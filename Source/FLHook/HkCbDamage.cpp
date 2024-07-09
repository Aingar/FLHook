#include "hook.h"

EXPORT uint iDmgMunitionID = 0;
DamageList	LastDmgList;

bool g_gNonGunHitsBase = false;
float g_LastHitPts;

/**************************************************************************************************************
Called when a torp/missile/mine/wasp hits a ship
return 0 -> pass on to server.dll
return 1 -> suppress
**************************************************************************************************************/

FARPROC GuidedCreatedOrigFunc;


FARPROC fpOldExplosionHit;

void __stdcall ExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{
	CSimple* simple = reinterpret_cast<CSimple*>(iobj->cobj);
	if (simple->ownerPlayer && simple->objectClass == CObject::CSHIP_OBJECT)
	{
		CALL_PLUGINS_V(PLUGIN_ExplosionHit, __stdcall, (IObjRW * iobj, ExplosionDamageEvent * explosion, DamageList * dmg), (iobj, explosion, dmg));
	}
}

__declspec(naked) void HookExplosionHitNaked()
{
	__asm {
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call ExplosionHit
		pop ecx
		jmp [fpOldExplosionHit]
	}
}

/**************************************************************************************************************
Called when ship was damaged
**************************************************************************************************************/

FARPROC ApplyShipDamageListOrigFunc;

void __stdcall SetDamageToOne(IObjRW* ship, DamageList* dmg, uint source)
{
	if (source != 0x6cfe254) // only work when dmg source is a damage fuse
	{
		return;
	}
	for (auto& dmgEntry : dmg->damageentries)
	{
		if (dmgEntry.subobj == 1 && dmgEntry.health <= 0.0f)
		{
			dmgEntry.health = 0.1f;
			CShip* cship = (CShip*)ship->cobj;
			cship->isUndergoingDeathFuse = true;
		}
	}
}

__declspec(naked) void ApplyShipDamageListNaked()
{
	__asm {
		push ecx
		push[esp + 0x4]
		push[esp + 0xC]
		push ecx
		call SetDamageToOne
		pop ecx
		jmp ApplyShipDamageListOrigFunc
	}
}

typedef float(__thiscall* GetZoneDistanceFunc)(Universe::IZone* zone, Vector& pos);
GetZoneDistanceFunc GetZoneDistance = GetZoneDistanceFunc(0x6339B00);

unordered_map<uint, ZoneSpecialData> zoneSpecialData;
void __fastcall ShipRadiationDamage(IObjRW* ship, void* edx, float incDamage, DamageList* dmg)
{
	if (!ship->cobj->ownerPlayer)
	{
		return;
	}

	auto zoneDataIter = zoneSpecialData.find(ship->cobj->currentDamageZone->iZoneID);
	if (zoneDataIter == zoneSpecialData.end())
	{
		return;
	}
	const ZoneSpecialData& zd = zoneDataIter->second;
	uint zoneType = zd.dmgType;

	CShip* cship = reinterpret_cast<CShip*>(ship->cobj);

	if (zoneType & ZONEDMG_CRUISE)
	{
		dmg->damageCause = DamageCause::CruiseDisrupter;
		dmg->add_damage_entry(1, cship->hitPoints, DamageEntry::SubObjFate(0));
		zoneType -= ZONEDMG_CRUISE;
		if (!zoneType)
		{
			return;
		}
	}

	float damage = zd.flatDamage;

	if (damage <= 0.0f)
	{
		return;
	}

	float dmgMultiplier = 1.0f;

	if (zd.distanceScaling != 0.0f)
	{
		float edgeDistance = -GetZoneDistance(ship->cobj->currentDamageZone, ship->cobj->vPos);
		if (zd.distanceScaling > 0.0f)
		{
			if (edgeDistance <= zd.distanceScaling)
			{
				dmgMultiplier = powf(edgeDistance / zd.distanceScaling, zd.logScale);
			}
		}
		else
		{
			if (edgeDistance <= -zd.distanceScaling)
			{
				dmgMultiplier = powf(1.0 - (edgeDistance / -zd.distanceScaling), zd.logScale);
			}
			else
			{
				return;
			}
		}
	}

	if (zoneType & ZONEDMG_SHIELD)
	{
		CEShield* shield = reinterpret_cast<CEShield*>(cship->equip_manager.FindFirst(Shield));
		if (shield)
		{
			float shielddamage = (damage + zd.percentageDamage * shield->maxShieldHitPoints) * dmgMultiplier * zd.shieldMult;
			ship->damage_shield_direct(shield, shielddamage, dmg);
			zoneType -= ZONEDMG_SHIELD;
			if (!zoneType)
			{
				return;
			}
		}
	}

	if (zoneType & ZONEDMG_ENERGY)
	{
		float energydamage = (damage + zd.percentageDamage * cship->maxPower) * dmgMultiplier * zd.energyMult;
		ship->damage_energy(energydamage, dmg);
		zoneType -= ZONEDMG_ENERGY;
		if (!zoneType)
		{
			return;
		}
	}

	CArchGroupManager& carchMan = cship->archGroupManager;
	CArchGrpTraverser tr2;

	CArchGroup* carch = nullptr;
	uint colGrpCount = 1;
	while (carch = carchMan.Traverse(tr2))
	{
		if (carch->colGrp->hitPts < 100.f)
		{
			continue;
		}
		colGrpCount++;
	}
	tr2.Restart();

	damage /= colGrpCount;

	while (carch = carchMan.Traverse(tr2))
	{
		if (carch->colGrp->hitPts < 100.f)
		{
			continue;
		}
		float colGrpDamage = min(carch->hitPts, ((damage + (carch->colGrp->hitPts * zd.percentageDamage)) * dmgMultiplier) / colGrpCount);
		if (colGrpDamage <= 0.0f)
		{
			continue;
		}

		DamageEntry::SubObjFate fate;
		if (colGrpDamage >= carch->hitPts)
		{
			fate = DamageEntry::SubObjFate(1);
		}
		else
		{
			fate = DamageEntry::SubObjFate(0);
		}
		dmg->add_damage_entry(carch->colGrp->id, carch->hitPts - colGrpDamage, fate);
	}

	float hulldamage = min(cship->hitPoints, dmgMultiplier * (damage + (zd.percentageDamage * ship->cobj->archetype->fHitPoints)));

	if (hulldamage > 0.0f)
	{
		dmg->add_damage_entry(1, cship->hitPoints - hulldamage, DamageEntry::SubObjFate(0));
	}
}

FARPROC ShipHullDamageOrigFunc, SolarHullDamageOrigFunc;

void __stdcall ShipHullDamage(IObjRW* iobj, float& incDmg, DamageList* dmg)
{
	CSimple* simple = reinterpret_cast<CSimple*>(iobj->cobj);
	if (simple->ownerPlayer && dmg->iInflictorPlayerID)
	{
		CALL_PLUGINS_V(PLUGIN_ShipHullDmg, __stdcall, (IObjRW * iobj, float& incDmg, DamageList * dmg), (iobj, incDmg, dmg));

		if (incDmg > 0)
		{
			ClientInfo[simple->ownerPlayer].dmgLastPlayerId = dmg->iInflictorPlayerID;
			ClientInfo[simple->ownerPlayer].dmgLastCause = dmg->damageCause;
		}
	}
}

__declspec(naked) void ShipHullDamageNaked()
{
	__asm {
		push ecx
		push[esp + 0xC]
		lea eax, [esp + 0xC]
		push eax
		push ecx
		call ShipHullDamage
		pop ecx
		jmp [ShipHullDamageOrigFunc]
	}
}

void __stdcall SolarHullDamage(IObjRW* iobj, float& incDmg, DamageList* dmg)
{
	CALL_PLUGINS_V(PLUGIN_SolarHullDmg, __stdcall, (IObjRW* iobj, float& incDmg, DamageList* dmg), (iobj, incDmg, dmg));
}

__declspec(naked) void SolarHullDamageNaked()
{
	__asm {
		push ecx
		push[esp + 0xC]
		lea eax, [esp + 0xC]
		push eax
		push ecx
		call SolarHullDamage
		pop ecx
		jmp [SolarHullDamageOrigFunc]
	}
}

/**************************************************************************************************************
Called when player ship is damaged
**************************************************************************************************************/

bool AllowPlayerDamageIds(const uint targetClientId, const uint attackerClient)
{
	if (targetClientId)
	{
		// anti-dockkill check
		if (ClientInfo[targetClientId].tmProtectedUntil)
		{
			if (timeInMS() <= ClientInfo[targetClientId].tmProtectedUntil)
				return false; // target is protected
			else
				ClientInfo[targetClientId].tmProtectedUntil = 0;
		}
		if (ClientInfo[attackerClient].tmProtectedUntil)
		{
			if (timeInMS() <= ClientInfo[attackerClient].tmProtectedUntil)
				return false; // target may not shoot
			else
				ClientInfo[attackerClient].tmProtectedUntil = 0;
		}
	}

	return true;
}

FARPROC AllowPlayerDamageOrigFunc;
bool __stdcall AllowPlayerDamage(const IObjRW* iobj, const DamageList* dmgList)
{
	if (!dmgList->iInflictorPlayerID)
	{
		return true;
	}
	if (iobj->cobj->objectClass != CObject::CSHIP_OBJECT)
	{
		return true;
	}
	uint targetClientId = ((CShip*)iobj->cobj)->ownerPlayer;
	if (!targetClientId)
	{
		return true;
	}

	CALL_PLUGINS(PLUGIN_AllowPlayerDamage, bool, , (uint, uint), (dmgList->iInflictorPlayerID, targetClientId));

	return AllowPlayerDamageIds(targetClientId, dmgList->iInflictorPlayerID);
}
__declspec(naked) void AllowPlayerDamageNaked()
{
	__asm
	{
		push ecx
		push[esp + 0x8]
		push ecx
		call AllowPlayerDamage
		pop ecx
		test al, al
		jz abort_lbl
		jmp [AllowPlayerDamageOrigFunc]
	abort_lbl:
		mov al, 1
		retn 0x4
	}
}


///////////////////////////

