#include "MunitionCntl.h"

FARPROC GuidedExplosionHitOrigFunc, SolarExplosionHitOrigFunc, ShipMunitionHitOrigFunc, ShipColGrpDmgFunc, ShipFuseLightFunc, ShipEquipDestroyedFunc, SolarHullDmgFunc, SolarColGrpDmgFunc;

PATCH_INFO piServerDLL =
{
	"server.dll", 0x6CE0000,
	{
		{0x6D6761C,		&SolarHullDamageNaked,		4, &SolarHullDmgFunc,	false},
		{0x6D67684,		&SolarColGrpDamageNaked,		4, &SolarColGrpDmgFunc,	false},
		{0x6D675EC,		&SolarMunitionHit,		4, nullptr,	false},
		{0x6D6732C,		&ShipEquipDamage,		4, nullptr,	false},
		{0x6D67338,		&ShipEnergyDamage,		4, nullptr,	false},
		{0x6D666C0,		&GuidedExplosionHitNaked,	4, &GuidedExplosionHitOrigFunc, false},
		{0x6D675F0,		&SolarExplosionHitNaked,	4, &SolarExplosionHitOrigFunc, false},
		{0x6D6729C,		&ShipMunitionHit,		4, &ShipMunitionHitOrigFunc, false},
		{0x6D67334,		&ShipColGrpDmgNaked,		4, &ShipColGrpDmgFunc, false},
		{0x6D67300,		&ShipFuseLightNaked,		4, &ShipFuseLightFunc, false},
		{0x6D67340,		&ShipEquipmentDestroyedNaked,	4, &ShipEquipDestroyedFunc, false},

		{0,0,0,0} // terminate
	}
};

PATCH_INFO piCommonDLL =
{
	"common.dll", 0x6260000,
	{
		{0x639D1CC,			&CELauncherFire,		4, nullptr,	false},
		{0x639C138,			&CShipInit,		4, nullptr,	false},
		//{0x639C07C,			&CShipUpdate,		4, nullptr,	false},
		{0x639E4AC,			&CMineUpdate,		4, nullptr,	false},
		{0x639BCBC,			&CGuidedUpdate,		4, nullptr,	false},

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
	Patch(piCommonDLL);
}
