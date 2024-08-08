#include <process.h>
#include "hook.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

PATCH_INFO piFLServerEXE =
{
	"flserver.exe", 0x0400000,
	{
		{0x041B094,		&HkIEngine::Update_Time,							4, 0,					false},
		{0x041BAB0,		&HkIEngine::Elapse_Time,							4, 0,					false},


		{0,0,0,0} // terminate
	}
};

PATCH_INFO piContentDLL =
{
	"content.dll", 0x6EA0000,
	{
		{0x6FB358C,		&HkIEngine::Dock_Call,								4, 0,			false},

		{0,0,0,0} // terminate
	}
};

PATCH_INFO piCommonDLL =
{
	"common.dll", 0x6260000,
	{

		{0x639D160,		&HkIEngine::CEGun_Update_naked,				4, &HkIEngine::fpOldUpdateCEGun,		false},
		//{0x639C138,		&HkIEngine::cshipInitNaked,				4, &HkIEngine::fpOldCshipInit,		false},
		//{0x639C138,		&HkIEngine::csolarInitNaked,				4, &HkIEngine::fpOldCsolarInit,		false},


		{0,0,0,0} // terminate
	}
};


PATCH_INFO piServerDLL =
{
	"server.dll", 0x6CE0000,
	{
		{0x6D672A4,		&ApplyShipDamageListNaked,				4, &ApplyShipDamageListOrigFunc,	false},
		//{0x6D67F4C,		&LootDestroyedNaked,				4, &LootDestroyedOrigFunc,		false},
		{0x6D661C4,		&MineDestroyedNaked,				4, &MineDestroyedOrigFunc,		false},
		{0x6D66694,		&GuidedDestroyedNaked,				4, &GuidedDestroyedOrigFunc,		false},
		{0x6D672D4,		&AllowPlayerDamageNaked,			4, &AllowPlayerDamageOrigFunc,		false},
		{0x6D6733C,		&ShipColGrpDestroyedHookNaked,		4, &ColGrpDeathOrigFunc,		false},
		{0x6D6768C,		&SolarColGrpDestroyedHookNaked,		4, 0,							false},
		{0x6D67274,		&ShipDestroyedNaked,				4, &fpOldShipDestroyed,			false},
		{0x6D675C4,		&SolarDestroyedNaked,				4, &fpOldSolarDestroyed,			false},
		{0x6D672A0,		&HookExplosionHitNaked,				4, &fpOldExplosionHit,		false},
		{0x6D672CC,		&ShipHullDamageNaked,				4, &ShipHullDamageOrigFunc,							false},
		{0x6D6761C,		&SolarHullDamageNaked,				4, &SolarHullDamageOrigFunc, false},
		{0x6D6420C,		&HkIEngine::_LaunchPos,				4, &HkIEngine::fpOldLaunchPos,	false},
		{0x6D648E0,		&HkIEngine::FreeReputationVibe,		4, 0,							false},

		{0x6D67F4C,		&HkIEngine::IObjDisconnectLoot,		4, &HkIEngine::IObjDisconnectLootFunc, false},
		{0x6D65124,		&HkIEngine::IObjDisconnectCM,		4, &HkIEngine::IObjDisconnectCMFunc, false},

		{0,0,0,0} // terminate
	}
};

PATCH_INFO piRemoteClientDLL =
{
	"remoteclient.dll", 0x6B30000,
	{
		{0x6B6BB80,		&HkCb_SendChat,								4, &RCSendChatMsg,			false},

		{0,0,0,0} // terminate
	}
};

PATCH_INFO piDaLibDLL =
{
	"dalib.dll", 0x65C0000,
	{
		{0x65C4BEC,		&_DisconnectPacketSent,						4, &fpOldDiscPacketSent,	false},

		{0,0,0,0} // terminate
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Patch(PATCH_INFO &pi)
{
	HMODULE hMod = GetModuleHandle(pi.szBinName);
	if (!hMod)
		return false;

	for (uint i = 0; (i < sizeof(pi.piEntries) / sizeof(PATCH_INFO_ENTRY)); i++)
	{
		if (!pi.piEntries[i].pAddress)
			break;

		char *pAddress = (char*)hMod + (pi.piEntries[i].pAddress - pi.pBaseAddress);
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RestorePatch(PATCH_INFO &pi)
{
	HMODULE hMod = GetModuleHandle(pi.szBinName);
	if (!hMod)
		return false;

	for (uint i = 0; (i < sizeof(pi.piEntries) / sizeof(PATCH_INFO_ENTRY)); i++)
	{
		if (!pi.piEntries[i].pAddress)
			break;

		char *pAddress = (char*)hMod + (pi.piEntries[i].pAddress - pi.pBaseAddress);
		WriteProcMem(pAddress, pi.piEntries[i].pOldValue, pi.piEntries[i].iSize);
		if (pi.piEntries[i].bAlloced)
			delete[] pi.piEntries[i].pOldValue;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

CDPClientProxy **g_cClientProxyArray;

CDPServer *cdpSrv;

HkIClientImpl* FakeClient;
HkIClientImpl* HookClient;
char* OldClient;

_CRCAntiCheat CRCAntiCheat;
_CreateChar CreateChar;

string scAcctPath;

CLIENT_INFO ClientInfo[MAX_CLIENT_ID + 1];

uint g_iServerLoad = 0;
uint g_iPlayerCount = 0;

char *g_FLServerDataPtr;

_GetShipInspect GetShipInspect;

unordered_map<uint, BASE_INFO> lstBases;

char szRepFreeFixOld[5];


/**************************************************************************************************************
clear the clientinfo
**************************************************************************************************************/

void ClearClientInfo(uint iClientID)
{
	ClientInfo[iClientID].dieMsg = DIEMSG_ALL_NOCONN;
	ClientInfo[iClientID].iShip = 0;
	ClientInfo[iClientID].iShipOld = 0;
	ClientInfo[iClientID].tmProtectedUntil = 0;
	ClientInfo[iClientID].lstMoneyFix.clear();
	ClientInfo[iClientID].iTradePartner = 0;
	ClientInfo[iClientID].iBaseEnterTime = 0;
	ClientInfo[iClientID].iCharMenuEnterTime = 0;
	ClientInfo[iClientID].bCruiseActivated = false;
	ClientInfo[iClientID].tmKickTime = 0;
	ClientInfo[iClientID].iLastExitedBaseID = 0;
	ClientInfo[iClientID].bDisconnected = false;
	ClientInfo[iClientID].bCharSelected = false;
	ClientInfo[iClientID].tmF1Time = 0;
	ClientInfo[iClientID].tmF1TimeDisconnect = 0;

	ClientInfo[iClientID].dmgLastCause = DamageCause::Unknown;
	ClientInfo[iClientID].dmgLastPlayerId = 0;
	ClientInfo[iClientID].dieMsgSize = CS_DEFAULT;
	ClientInfo[iClientID].chatSize = CS_DEFAULT;
	ClientInfo[iClientID].chatStyle = CST_DEFAULT;

	/*
	ClientInfo[iClientID].bAutoBuyMissiles = false;
	ClientInfo[iClientID].bAutoBuyMines = false;
	ClientInfo[iClientID].bAutoBuyTorps = false;
	ClientInfo[iClientID].bAutoBuyCD = false;
	ClientInfo[iClientID].bAutoBuyCM = false;
	ClientInfo[iClientID].bAutoBuyReload = false;
	*/

	ClientInfo[iClientID].lstIgnore.clear();
	ClientInfo[iClientID].iKillsInARow = 0;
	ClientInfo[iClientID].bEngineKilled = false;
	ClientInfo[iClientID].bThrusterActivated = false;
	ClientInfo[iClientID].bTradelane = false;

	CALL_PLUGINS_V(PLUGIN_ClearClientInfo, , (uint), (iClientID));
}

/**************************************************************************************************************
load settings from flhookhuser.ini
**************************************************************************************************************/

void LoadUserSettings(uint iClientID)
{
	CAccount *acc = Players.FindAccountFromClientID(iClientID);
	wstring wscDir;
	HkGetAccountDirName(acc, wscDir);
	string scUserFile = scAcctPath + wstos(wscDir) + "\\flhookuser.ini";

	// read diemsg settings
	ClientInfo[iClientID].dieMsg = (DIEMSGTYPE)IniGetI(scUserFile, "settings", "DieMsg", DIEMSG_ALL_NOCONN);
	ClientInfo[iClientID].dieMsgSize = (CHATSIZE)IniGetI(scUserFile, "settings", "DieMsgSize", CS_DEFAULT);

	// read chatstyle settings
	ClientInfo[iClientID].chatSize = (CHATSIZE)IniGetI(scUserFile, "settings", "ChatSize", CS_DEFAULT);
	ClientInfo[iClientID].chatStyle = (CHATSTYLE)IniGetI(scUserFile, "settings", "ChatStyle", CST_DEFAULT);

	// read ignorelist
	ClientInfo[iClientID].lstIgnore.clear();
	for (int i = 1; ; i++)
	{
		wstring wscIgnore = IniGetWS(scUserFile, "IgnoreList", itos(i), L"");
		if (!wscIgnore.length())
			break;

		IGNORE_INFO ii;
		ii.wscCharname = GetParam(wscIgnore, ' ', 0);
		ii.wscFlags = GetParam(wscIgnore, ' ', 1);
		ClientInfo[iClientID].lstIgnore.push_back(ii);
	}

}

/**************************************************************************************************************
load settings from flhookhuser.ini (specific to character)
**************************************************************************************************************/

void LoadUserCharSettings(uint iClientID)
{
	CAccount *acc = Players.FindAccountFromClientID(iClientID);
	wstring wscDir;
	HkGetAccountDirName(acc, wscDir);
	string scUserFile = scAcctPath + wstos(wscDir) + "\\flhookuser.ini";

	/*
	// read autobuy
	wstring wscFilename;
	HkGetCharFileName(ARG_CLIENTID(iClientID), wscFilename);
	string scSection = "autobuy_" + wstos(wscFilename);

	ClientInfo[iClientID].bAutoBuyMissiles = IniGetB(scUserFile, scSection, "missiles", false);
	ClientInfo[iClientID].bAutoBuyMines = IniGetB(scUserFile, scSection, "mines", false);
	ClientInfo[iClientID].bAutoBuyTorps = IniGetB(scUserFile, scSection, "torps", false);
	ClientInfo[iClientID].bAutoBuyCD = IniGetB(scUserFile, scSection, "cd", false);
	ClientInfo[iClientID].bAutoBuyCM = IniGetB(scUserFile, scSection, "cm", false);
	ClientInfo[iClientID].bAutoBuyReload = IniGetB(scUserFile, scSection, "reload", false);
	*/


	CALL_PLUGINS_V(PLUGIN_LoadUserCharSettings, , (uint), (iClientID));
}

/**************************************************************************************************************
install the callback hooks
**************************************************************************************************************/
static FARPROC radarDetour = FARPROC(&HkIEngine::Radar_Range_naked);

void Detour(void* pOFunc, void* pHkFunc)
{
	DWORD dwOldProtection = 0; // Create a DWORD for VirtualProtect calls to allow us to write.
	BYTE bPatch[5]; // We need to change 5 bytes and I'm going to use memcpy so this is the simplest way.
	bPatch[0] = 0xE9; // Set the first byte of the byte array to the op code for the JMP instruction.
	VirtualProtect(pOFunc, 5, PAGE_EXECUTE_READWRITE, &dwOldProtection); // Allow us to write to the memory we need to change
	DWORD dwRelativeAddress = (DWORD)pHkFunc - (DWORD)pOFunc - 5; // Calculate the relative JMP address.
	memcpy(&bPatch[1], &dwRelativeAddress, 4); // Copy the relative address to the byte array.
	memcpy(pOFunc, bPatch, 5); // Change the first 5 bytes to the JMP instruction.
	VirtualProtect(pOFunc, 5, dwOldProtection, 0); // Set the protection back to what it was.
}

void Detour(void* pOFunc, void* pHkFunc, unsigned char* originalData)
{
	DWORD dwOldProtection = 0; // Create a DWORD for VirtualProtect calls to allow us to write.
	BYTE bPatch[5]; // We need to change 5 bytes and I'm going to use memcpy so this is the simplest way.
	bPatch[0] = 0xE9; // Set the first byte of the byte array to the op code for the JMP instruction.
	VirtualProtect(pOFunc, 5, PAGE_EXECUTE_READWRITE, &dwOldProtection); // Allow us to write to the memory we need to change
	DWORD dwRelativeAddress = (DWORD)pHkFunc - (DWORD)pOFunc - 5; // Calculate the relative JMP address.
	memcpy(&bPatch[1], &dwRelativeAddress, 4); // Copy the relative address to the byte array.
	memcpy(originalData, pOFunc, 5);
	memcpy(pOFunc, bPatch, 5); // Change the first 5 bytes to the JMP instruction.
	VirtualProtect(pOFunc, 5, dwOldProtection, 0); // Set the protection back to what it was.
}

void UnDetour(void* pOFunc, unsigned char* originalData)
{
	DWORD dwOldProtection = 0; // Create a DWORD for VirtualProtect calls to allow us to write.
	VirtualProtect(pOFunc, 5, PAGE_EXECUTE_READWRITE, &dwOldProtection); // Allow us to write to the memory we need to change
	memcpy(pOFunc, originalData, 5);
	VirtualProtect(pOFunc, 5, dwOldProtection, 0); // Set the protection back to what it was.
}

bool InitHookExports()
{
	char	*pAddress;

	GetShipInspect = (_GetShipInspect)SRV_ADDR(ADDR_SRV_GETINSPECT);

	// install IServerImpl callbacks in remoteclient.dll
	char *pServer = (char*)&Server;
	memcpy(&pServer, pServer, 4);
	for (uint i = 0; (i < sizeof(HkIServerImpl::hookEntries) / sizeof(HOOKENTRY)); i++)
	{
		char *pAddress = pServer + HkIServerImpl::hookEntries[i].dwRemoteAddress;
		ReadProcMem(pAddress, &HkIServerImpl::hookEntries[i].fpOldProc, 4);
		WriteProcMem(pAddress, &HkIServerImpl::hookEntries[i].fpProc, 4);
	}

	// patch it
	Patch(piFLServerEXE);
	Patch(piContentDLL);
	Patch(piCommonDLL);
	Patch(piServerDLL);
	Patch(piRemoteClientDLL);
	Patch(piDaLibDLL);

	// patch rep array free
	char szNOPs[] = { '\x90','\x90','\x90','\x90','\x90' };
	pAddress = ((char*)hModServer + ADDR_SRV_REPARRAYFREE);
	ReadProcMem(pAddress, szRepFreeFixOld, 5);
	WriteProcMem(pAddress, szNOPs, 5);

	// reverse patch the client hanging fix
	BYTE patch[] = { 0xFF };
	WriteProcMem((char*)hModDaLib + 0x4BF4, patch, sizeof(patch));

	FARPROC FindStarListNaked2 = FARPROC(&HkIEngine::FindInStarListNaked2);
	WriteProcMem((char*)hModServer + 0x87CD4, &FindStarListNaked2, 4);
	PatchCallAddr((char*)hModServer, 0x2074A, (char*)HkIEngine::FindInStarListNaked);
	PatchCallAddr((char*)hModServer, 0x207BF, (char*)HkIEngine::FindInStarListNaked);

	// Simplified reimplementation of ShipRange.dll by Adoxa
	pAddress = SRV_ADDR(0x17272);
	FARPROC radarDetour2 = FARPROC(&radarDetour);
	WriteProcMem(pAddress, &radarDetour2, 4);

	// Optimize Server.dll sub_6CE61D0 that is called A LOT and crashes if it fails anwyay
	pAddress = SRV_ADDR(0x61D0);
	BYTE szOptimize[] = { 0x8B, 0x41, 0x10, 0x8b, 0x80, 0xb0, 0x00, 0x00, 0x00, 0xc3,
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
	WriteProcMem(pAddress, szOptimize, sizeof(szOptimize));

	// Optimize Server.dll sub_6CF4F00, also called a lot
	pAddress = SRV_ADDR(0x14F00);
	BYTE szOptimize2[] = { 0x52, 0x53, 0x57, 0x8B, 0x7C, 0x24, 0x10, 0x8B, 0x41, 0x04, 0xEB, 0x02, 0x8B, 0x00, 0x85, 0xC0, 0x74, 0x10, 0x8B, 0x50, 0x08, 0x8B, 0x5A, 0x10, 0x8B, 0x8B, 0xB0, 0x00, 0x00, 0x00, 0x39, 0xF9, 0x75, 0xEA, 0x5F, 0x5B, 0x5A, 0xC2, 0x04, 0x00,
		0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	WriteProcMem(pAddress, szOptimize2, sizeof(szOptimize2));

	// patch pub::Save method
	pAddress = SRV_ADDR(0x7EFA8);
	char szNop[2] = { '\x90', '\x90' };
	WriteProcMem(pAddress, szNop, sizeof(szNop)); // nop the SinglePlayer() check

	// patch flserver so it can better handle faulty house entries in char files

	// divert call to house load/save func
	pAddress = SRV_ADDR(0x679C6);
	char szDivertJump[] = { '\x6F' };

	WriteProcMem(pAddress, szDivertJump, 1);

	// jump out of the crash trap in TradeLane/SPObjUpdate related code
	pAddress = (char*)hModCommon + 0xF24A0;
	char szSkipCrash[2] = { '\xEB', '\x30' };
	WriteProcMem(pAddress, szSkipCrash, 2);

	// install hook at new address
	pAddress = SRV_ADDR(0x78B39);

	char szMovEAX[] = { '\xB8' };
	char szJMPEAX[] = { '\xFF', '\xE0' };

	FARPROC fpHkLoadRepFromCharFile = (FARPROC)HkIEngine::_HkLoadRepFromCharFile;

	WriteProcMem(pAddress, szMovEAX, 1);
	WriteProcMem(pAddress + 1, &fpHkLoadRepFromCharFile, 4);
	WriteProcMem(pAddress + 5, szJMPEAX, 2);

	HkIEngine::fpOldLoadRepCharFile = (FARPROC)SRV_ADDR(0x78B40);


	// crc anti-cheat
	CRCAntiCheat = (_CRCAntiCheat)((char*)hModServer + ADDR_CRCANTICHEAT);

	// get CDPServer
	pAddress = DALIB_ADDR(ADDR_CDPSERVER);
	ReadProcMem(pAddress, &cdpSrv, 4);

	// read g_FLServerDataPtr(used for serverload calc)
	pAddress = FLSERVER_ADDR(ADDR_DATAPTR);
	ReadProcMem(pAddress, &g_FLServerDataPtr, 4);

	// some setting relate hooks
	HookRehashed();

	// get client proxy array, used to retrieve player pings/ips
	pAddress = (char*)hModRemoteClient + ADDR_CPLIST;
	char *szTemp;
	ReadProcMem(pAddress, &szTemp, 4);
	szTemp += 0x10;
	memcpy(&g_cClientProxyArray, &szTemp, 4);

	// init variables
	char szDataPath[MAX_PATH];
	GetUserDataPath(szDataPath);
	scAcctPath = string(szDataPath) + "\\Accts\\MultiPlayer\\";

	// clear ClientInfo
	for (uint i = 0; (i < sizeof(ClientInfo) / sizeof(CLIENT_INFO)); i++)
	{
		ClientInfo[i].iConnects = 0; // only set to 0 on start
		ClearClientInfo(i);
	}

	return true;
}

void PatchClientImpl()
{
	// install HkIClientImpl callback

	FakeClient = new HkIClientImpl;
	HookClient = &Client;

	memcpy(&OldClient, &Client, 4);
	WriteProcMem(&Client, FakeClient, 4);
}

/**************************************************************************************************************
uninstall the callback hooks
**************************************************************************************************************/

void UnloadHookExports()
{
	char *pAddress;

	// uninstall IServerImpl callbacks in remoteclient.dll
	char *pServer = (char*)&Server;
	if (pServer) {
		memcpy(&pServer, pServer, 4);
		for (uint i = 0; (i < sizeof(HkIServerImpl::hookEntries) / sizeof(HOOKENTRY)); i++)
		{
			void *pAddress = (void*)((char*)pServer + HkIServerImpl::hookEntries[i].dwRemoteAddress);
			WriteProcMem(pAddress, &HkIServerImpl::hookEntries[i].fpOldProc, 4);
		}
	}


	// reset npc spawn setting
	HkChangeNPCSpawn(false);

	// restore other hooks
	RestorePatch(piFLServerEXE);
	RestorePatch(piContentDLL);
	RestorePatch(piCommonDLL);
	RestorePatch(piServerDLL);
	RestorePatch(piRemoteClientDLL);
	RestorePatch(piDaLibDLL);

	// unpatch rep array free
	pAddress = ((char*)GetModuleHandle("server.dll") + ADDR_SRV_REPARRAYFREE);
	WriteProcMem(pAddress, szRepFreeFixOld, 5);

	// unpatch flserver so it can better handle faulty house entries in char files

	// undivert call to house load/save func
	pAddress = SRV_ADDR(0x679C6);
	char szDivertJump[] = { '\x76' };

	// anti-death-msg
	char szOld[] = { '\x74' };
	pAddress = SRV_ADDR(ADDR_ANTIDIEMSG);
	WriteProcMem(pAddress, szOld, 1);

	// plugins
	PluginManager::UnloadPlugins();
	PluginManager::Destroy();

	// help
	lstHelpEntries.clear();
}

/**************************************************************************************************************
settings were rehashed
sometimes adjustments need to be made after a rehash
**************************************************************************************************************/

void HookRehashed()
{
	char *pAddress;

	// anti-deathmsg
	if (set_bDieMsg) { // disables the "old" "A Player has died: ..." messages
		char szJMP[] = { '\xEB' };
		pAddress = SRV_ADDR(ADDR_ANTIDIEMSG);
		WriteProcMem(pAddress, szJMP, 1);
	}
	else {
		char szOld[] = { '\x74' };
		pAddress = SRV_ADDR(ADDR_ANTIDIEMSG);
		WriteProcMem(pAddress, szOld, 1);
	}

	// charfile encyption(doesn't get disabled when unloading FLHook)
	if (set_bDisableCharfileEncryption) {
		char szBuf[] = { '\x14', '\xB3' };
		pAddress = SRV_ADDR(ADDR_DISCFENCR);
		WriteProcMem(pAddress, szBuf, 2);
		pAddress = SRV_ADDR(ADDR_DISCFENCR2);
		WriteProcMem(pAddress, szBuf, 2);
	}
	else {
		char szBuf[] = { '\xE4', '\xB4' };
		pAddress = SRV_ADDR(ADDR_DISCFENCR);
		WriteProcMem(pAddress, szBuf, 2);
		pAddress = SRV_ADDR(ADDR_DISCFENCR2);
		WriteProcMem(pAddress, szBuf, 2);
	}

	// maximum group size
	if (set_iMaxGroupSize > 0) {
		char cNewGroupSize = set_iMaxGroupSize & 0xFF;
		pAddress = SRV_ADDR(ADDR_SRV_MAXGROUPSIZE);
		WriteProcMem(pAddress, &cNewGroupSize, 1);
		pAddress = SRV_ADDR(ADDR_SRV_MAXGROUPSIZE2);
		WriteProcMem(pAddress, &cNewGroupSize, 1);
	}
	else { // default
		char cNewGroupSize = 8;
		pAddress = SRV_ADDR(ADDR_SRV_MAXGROUPSIZE);
		WriteProcMem(pAddress, &cNewGroupSize, 1);
		pAddress = SRV_ADDR(ADDR_SRV_MAXGROUPSIZE2);
		WriteProcMem(pAddress, &cNewGroupSize, 1);
	}

	// open debug log if necessary
	if (set_bDebug && !fLogDebug) {
		fLogDebug = fopen(sDebugLog.c_str(), "at");
	}
	else if (!set_bDebug && fLogDebug) {
		fclose(fLogDebug);
		fLogDebug = 0;
	}
}

