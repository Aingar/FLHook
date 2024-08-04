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

bool __stdcall ShipShieldExplosionDamage(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmgList)
{

	typedef bool(__thiscall* ShipShieldExlosionHit)(IObjRW*, ExplosionDamageEvent*, DamageList*);
	static ShipShieldExlosionHit ShipShieldExlosionHitFunc = ShipShieldExlosionHit(0x6CE9A90);

	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);
	CEShield* shield = reinterpret_cast<CEShield*>(cship->equip_manager.FindFirst(Shield));
	if (!shield)
	{
		return ShipShieldExlosionHitFunc(iobj, explosion, dmgList);
	}

	auto explosionIter = explosionTypeMap.find(explosion->explosionArchetype->iID);
	if (explosionIter == explosionTypeMap.end())
	{
		return ShipShieldExlosionHitFunc(iobj, explosion, dmgList);
	}

	float modifier = GetWeaponModifier(shield, nullptr, explosionIter->second.type);

	float originalHullDmg = explosion->explosionArchetype->fHullDamage;
	float originalEnergyDmg = explosion->explosionArchetype->fEnergyDamage;

	explosion->explosionArchetype->fHullDamage *= modifier;
	explosion->explosionArchetype->fEnergyDamage *= modifier;

	bool retVal = ShipShieldExlosionHitFunc(iobj, explosion, dmgList);

	explosion->explosionArchetype->fHullDamage = originalHullDmg;
	explosion->explosionArchetype->fEnergyDamage = originalEnergyDmg;
	
	return retVal;
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
	if (!iobj->cobj->ownerPlayer)
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
	}
	else
	{
		shieldState.boostUntil = 0;
		shieldState.damageReduction = 0;
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