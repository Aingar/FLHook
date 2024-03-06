#include "hook.h"

EXPORT uint iDmgTo = 0;
EXPORT uint iDmgToSpaceID = 0;
EXPORT uint iDmgMunitionID = 0;
DamageList	LastDmgList;

bool g_gNonGunHitsBase = false;
float g_LastHitPts;

/**************************************************************************************************************
Called when a torp/missile/mine/wasp hits a ship
return 0 -> pass on to server.dll
return 1 -> suppress
**************************************************************************************************************/

FARPROC fpOldExplosionHit;

void __stdcall ExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{

	CALL_PLUGINS_V(PLUGIN_ExplosionHit, __stdcall, (IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg), (iobj, explosion, dmg));

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
		mov eax, [fpOldExplosionHit]
		jmp eax
	}
}

/**************************************************************************************************************
Called when ship was damaged
**************************************************************************************************************/

FARPROC ShipHullDamageOrigFunc, SolarHullDamageOrigFunc;

bool __stdcall ShipHullDamage(IObjRW* iobj, float incDmg, DamageList* dmg)
{
	CALL_PLUGINS(PLUGIN_ShipHullDmg, bool, __stdcall, (IObjRW* iobj, float incDmg, DamageList* dmg), (iobj, incDmg, dmg));

	CSimple * simple = (CSimple*)iobj->cobj;
	if (simple->ownerPlayer)
	{
		ClientInfo[simple->ownerPlayer].dmgLastPlayerId = dmg->iInflictorPlayerID;
		ClientInfo[simple->ownerPlayer].dmgLastCause = dmg->damageCause;
	}

	return true;
}

__declspec(naked) void ShipHullDamageNaked()
{
	__asm {
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call ShipHullDamage
		pop ecx
		test al, al
		jz skipLabel
		mov eax, [ShipHullDamageOrigFunc]
		jmp eax
	skipLabel:
		ret 0x8
	}
}

bool __stdcall SolarHullDamage(IObjRW* iobj, float incDmg, DamageList* dmg)
{
	CALL_PLUGINS(PLUGIN_SolarHullDmg, bool, __stdcall, (IObjRW* iobj, float incDmg, DamageList* dmg), (iobj, incDmg, dmg));

	return true;
}

__declspec(naked) void SolarHullDamageNaked()
{
	__asm {
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call SolarHullDamage
		pop ecx
		test al, al
		jz skipLabel
		mov eax, [SolarHullDamageOrigFunc]
		jmp eax
	skipLabel:
		ret 0x8
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

/**************************************************************************************************************
**************************************************************************************************************/

FARPROC fpOldNonGunWeaponHitsBase;

float fHealthBefore;
uint iHitObject;
uint iClientIDInflictor;

void __stdcall HkCb_NonGunWeaponHitsBaseBefore(char *ECX, char *p1, DamageList *dmg)
{

	CSimple *simple;
	memcpy(&simple, ECX + 0x10, 4);
	g_LastHitPts = simple->get_hit_pts();


	g_gNonGunHitsBase = true;
}

void HkCb_NonGunWeaponHitsBaseAfter()
{
	g_gNonGunHitsBase = false;

}

ulong lRetAddress;

__declspec(naked) void _HkCb_NonGunWeaponHitsBase()
{
	__asm
	{
		mov eax, [esp + 4]
		mov edx, [esp + 8]
		push ecx
		push edx
		push eax
		push ecx
		call HkCb_NonGunWeaponHitsBaseBefore
		pop ecx

		mov eax, [esp]
		mov[lRetAddress], eax
		lea eax, return_here
		mov[esp], eax
		jmp[fpOldNonGunWeaponHitsBase]
		return_here:
		pushad
			call HkCb_NonGunWeaponHitsBaseAfter
			popad
			jmp[lRetAddress]
	}
}

///////////////////////////

