#include "MunitionCntl.h"

FARPROC ShipShieldDamageOrigFunc;

PATCH_INFO piServerDLL =
{
	"server.dll", 0x6CE0000,
	{
		{0x6D67330,		&ShipShieldDamageNaked,				4, &ShipShieldDamageOrigFunc,							false},

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
