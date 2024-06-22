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
			cship->isAlive = true;
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


void __fastcall ShipRadiationDamage(IObjRW* ship, void* edx, float incDamage, DamageList* dmg)
{
	if (ship->cobj->ownerPlayer)
	{
		ship->damage_hull(15.f, dmg);
		ConPrint(L"%f %f\n", ship->timer, ship->timeSinceLastUpdate);
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

