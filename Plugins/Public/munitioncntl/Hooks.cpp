#include "MunitionCntl.h"

FARPROC ShipShieldDamageOrigFunc, ShipShieldExplosionDamageOrigFunc;

PATCH_INFO piServerDLL =
{
	"server.dll", 0x6CE0000,
	{
		{0x6D67330,		&ShipShieldDamageNaked,				4, &ShipShieldDamageOrigFunc,							false},
		{0x6D67320,		&ShipShieldExplosionDamageNaked,	4, &ShipShieldExplosionDamageOrigFunc,							false},

		{0,0,0,0} // terminate
	}
};

bool Patch(PATCH_INFO& pi)
{
	HMODULE hMod = GetModuleHandle(pi.szBinName);
	if (!hMod)
		return false;

	for (uint i = 0; (i < sizeof(pi.piEntries) / sizeof(PATCH_INFO_ENTRY)); i++)
	{
		if (!pi.piEntries[i].pAddress)
			break;

		char* pAddress = (char*)hMod + (pi.piEntries[i].pAddress - pi.pBaseAddress);
		if (!pi.piEntries[i].pOldValue) {
			pi.piEntries[i].pOldValue = new char[pi.piEntries[i].iSize];
			pi.piEntries[i].bAlloced = true;
		}
		else
			pi.piEntries[i].bAlloced = false;

		ReadProcMem(pAddress, pi.piEntries[i].pOldValue, pi.piEntries[i].iSize);
		WriteProcMem(pAddress, &pi.piEntries[i].pNewValue, pi.piEntries[i].iSize);
	}

	return true;
}

void LoadHookOverrides()
{
	Patch(piServerDLL);
}

float GetRangeModifier(CShip* cship, ExplosionDamageEvent* explosion)
{
	float squaredRadius = explosion->explosionArchetype->fRadius * explosion->explosionArchetype->fRadius;
	float squaredRadiusTwoThirds = squaredRadius * 0.44444444f;
	float squaredRadiusOneThirds = squaredRadius * 0.11111111f;

	const Vector& explPos = explosion->explosionPosition;
	float sq1 = explPos.x - cship->vPos.x, sq2 = explPos.y - cship->vPos.y, sq3 = explPos.z - cship->vPos.z;
	float distance = sq1 * sq1 + sq2 * sq2 + sq3 * sq3;

	if (distance <= squaredRadiusOneThirds || cship->shiparch()->iShipClass >= 6)
	{
		return 1.0f;
	}
	if (distance <= squaredRadiusTwoThirds)
	{
		return 0.66666666f;
	}
	if (distance <= squaredRadius)
	{
		return 0.33333333f;
	}

	return 0.0f;
}

bool __stdcall ShipShieldExplosionDamage(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmgList)
{

	typedef bool(__thiscall* ShipShieldExlosionHit)(IObjRW*, ExplosionDamageEvent*, DamageList*);
	static ShipShieldExlosionHit ShipShieldExlosionHitFunc = ShipShieldExlosionHit(0x6CE9A90);

	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);
	CEShield* shield = reinterpret_cast<CEShield*>(cship->equip_manager.FindFirst(Shield));
	if (!shield || !shield->IsFunctioning())
	{
		return false;
	}

	float shieldDamage = (explosion->explosionArchetype->fHullDamage * ShieldEquipConsts::HULL_DAMAGE_FACTOR) + explosion->explosionArchetype->fEnergyDamage;
	shieldDamage *= GetRangeModifier(cship, explosion);

	auto explosionIter = explosionTypeMap.find(explosion->explosionArchetype->iID);
	if (explosionIter != explosionTypeMap.end())
	{
		shieldDamage *= GetWeaponModifier(shield, nullptr, explosionIter->second.type);
	}

	iobj->damage_shield_direct(shield, shieldDamage, dmgList);
	
	return true;
}

__declspec(naked) void ShipShieldExplosionDamageNaked()
{
	__asm {
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call ShipShieldExplosionDamage
		pop ecx
		retn 8
	}
}

void __stdcall ShipShieldDamage(IObjRW* iobj, float& incDmg)
{
	uint clientId = iobj->cobj->ownerPlayer;
	if (!clientId)
	{
		return;
	}

	auto& shieldState = playerShieldState[clientId];
	if (!shieldState.damageReduction)
	{
		return;
	}
	if (!shieldState.boostUntil)
	{
		return;
	}
	mstime currTime = timeInMS();
	if (shieldState.boostUntil > currTime)
	{
		incDmg *= 1.0f - shieldState.damageReduction;
		shieldState.damageTaken += incDmg;

		CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);
		CEShield* shield = reinterpret_cast<CEShield*>(cship->equip_manager.FindFirst(Shield));
		if (shield && shield->IsFunctioning())
		{
			uint fuseId = shieldFuseMap[clientId].boostData->fuseId;

			HkUnLightFuse(iobj, fuseId, 0.0f);

			shieldFuseMap.erase(clientId);

			shieldState.boostUntil = 0;
			shieldState.damageReduction = 0;
			shieldState.damageTaken = 0;
		}
	}
	else
	{
		shieldState.boostUntil = 0;
		shieldState.damageReduction = 0;
		shieldState.damageTaken = 0;
	}
}

__declspec(naked) void ShipShieldDamageNaked()
{
	__asm {
		push ecx
		lea eax, [esp + 0xC]
		push eax
		push ecx
		call ShipShieldDamage
		pop ecx
		jmp[ShipShieldDamageOrigFunc]
	}
}