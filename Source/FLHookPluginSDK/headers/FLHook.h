#ifndef _FLHOOK_
#define _FLHOOK_

#pragma warning(disable: 4311 4786)

// include library files
#ifndef __NOINCFLHOOKLIBS__
#pragma comment(lib,"../../../Source/FLHookPluginSDK/libs/FLCoreDALib.lib")
#pragma comment(lib,"../../../Source/FLHookPluginSDK/libs/FLCoreCommon.lib")
#pragma comment(lib,"../../../Source/FLHookPluginSDK/libs/FLCoreDACom.lib")
#pragma comment(lib,"../../../Source/FLHookPluginSDK/libs/FLCoreFLServerEXE.lib")
#pragma comment(lib,"../../../Source/FLHookPluginSDK/libs/FLCoreRemoteClient.lib")
#pragma comment(lib,"../../../Source/FLHookPluginSDK/libs/FLCoreServer.lib")
#pragma comment(lib,"../../../Source/FLHookPluginSDK/libs/FLHook.lib")
//#if _MSC_VER == 1900 || _MSC_VER == 1910
//#pragma comment(lib,"../flhookplugin_sdk/libs/FLHook-vc14.lib")
//#elif _MSC_VER == 1800
//#pragma comment(lib,"../flhookplugin_sdk/libs/FLHook-vc12.lib")
//#else
//#pragma comment(lib,"../flhookplugin_sdk/libs/FLHook.lib")
//#endif
#endif

#include <windows.h>
#include <stdio.h>
#include <string>
#include <list>
#include <time.h>
#include <unordered_map>
using namespace std;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// defines

#define HKHKSUCCESS(a) ((a) == HKE_OK)
#define HKSUCCESS(a) ((hkLastErr = (a)) == HKE_OK)

#define SRV_ADDR(a) ((char*)hModServer + a)
#define DALIB_ADDR(a) ((char*)hModDaLib + a)
#define FLSERVER_ADDR(a) ((char*)hProcFL + a)
#define CONTENT_ADDR(a) ((char*)hModContent + a)
#define ARG_CLIENTID(a) (wstring(L"id ") + stows(itos(a)))

#define HK_GET_CLIENTID(a, b) \
	bool bIdString = false; \
	if(b.find(L"id ") == 0) bIdString = true; \
	uint a; \
	{ \
		HK_ERROR hkErr = HkResolveId(b, a); \
		if(hkErr != HKE_OK) \
		{ \
			if(hkErr == HKE_INVALID_ID_STRING) { \
				hkErr = HkResolveShortCut(b, a); \
				if((hkErr == HKE_AMBIGUOUS_SHORTCUT) || (hkErr == HKE_NO_MATCHING_PLAYER)) \
					return hkErr; \
				else if(hkErr == HKE_INVALID_SHORTCUT_STRING) \
					a = HkGetClientIdFromCharname(b); \
			} else \
				return hkErr; \
		} \
	} \

#define IMPORT __declspec(dllimport)
#define EXPORT __declspec(dllexport)

#define foreach(lst, type, var) for(list<type>::iterator var = lst.begin(); (var != lst.end()); var++)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// typedefs
typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long ulong;
typedef unsigned __int64 mstime;

// custom fl wstring (vc6 strings)
typedef class std::basic_string<unsigned short, struct ci_wchar_traits> flstr;
typedef class std::basic_string<char, struct ci_char_traits> flstrs;

typedef flstr* (*_CreateWString)(const wchar_t *wszStr);
typedef void(*_FreeWString)(flstr *wscStr);
typedef flstrs* (*_CreateString)(const char *szStr);
typedef void(*_FreeString)(flstrs *scStr);
typedef char* (*_GetCString)(flstrs *scStr);
typedef wchar_t* (*_GetWCString)(flstr *wscStr);
typedef wchar_t* (*_WStringAssign)(flstr *wscStr, const wchar_t *wszStr);
typedef wchar_t* (*_WStringAppend)(flstr *wscStr, const wchar_t *wszStr);

typedef flstr* (*_CPlayerAccount_GetServerSignature)(class CPlayerAccount* cpa, const char* sig);

#include "FLCoreCommon.h"
#include "FLCoreServer.h"
#include "FLCoreRemoteClient.h"
#include "FLCoreDALib.h"

#include "plugin.h"

typedef void(__stdcall *_RCSendChatMsg)(uint iId, uint iTo, uint iSize, void *pRDL);
typedef void(__stdcall *_CRCAntiCheat)();
typedef void(__stdcall *_CreateChar)(const wchar_t *wszName);
typedef int(__cdecl *_GetFLName)(char *szBuf, const wchar_t *wszStr);
typedef bool(__cdecl *_GetShipInspect)(uint &iShip, IObjRW* &inspect, StarSystem* &starSystem);

extern IMPORT _GetShipInspect GetShipInspect;
extern IMPORT FlMap<uint, MPlayerDataSaveStruct*>* mdataPlayerMap;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// enums

enum HK_ERROR
{
	HKE_OK,
	HKE_PLAYER_NOT_LOGGED_IN,
	HKE_CHAR_DOES_NOT_EXIST,
	HKE_INVALID_CLIENT_ID,
	HKE_COULD_NOT_DECODE_CHARFILE,
	HKE_COULD_NOT_ENCODE_CHARFILE,
	HKE_INVALID_BASENAME,
	HKE_INVALID_ID_STRING,
	HKE_INVALID_SYSTEM,
	HKE_PLAYER_NOT_IN_SPACE,
	HKE_PLAYER_NO_ADMIN,
	HKE_WRONG_XML_SYNTAX,
	HKE_INVALID_GOOD,
	HKE_NO_CHAR_SELECTED,
	HKE_CHARNAME_ALREADY_EXISTS,
	HKE_CHARNAME_TOO_LONG,
	HKE_CHARNAME_TOO_SHORT,
	HKE_AMBIGUOUS_SHORTCUT,
	HKE_NO_MATCHING_PLAYER,
	HKE_INVALID_SHORTCUT_STRING,
	HKE_MPNEWCHARACTERFILE_NOT_FOUND_OR_INVALID,
	HKE_INVALID_REP_GROUP,
	HKE_PLUGIN_UNLOADABLE,
	HKE_PLUGIN_UNPAUSABLE,
	HKE_PLUGIN_NOT_FOUND,
	HKE_UNKNOWN_ERROR,
	HKE_CUSTOM_1,
	HKE_CUSTOM_2,
	HKE_CUSTOM_3,
	HKE_CUSTOM_4,
	HKE_CUSTOM_5,
	HKE_CUSTOM_6,
	HKE_CUSTOM_7,
	HKE_CUSTOM_8,
	HKE_CUSTOM_9,
	HKE_CUSTOM_10,
	HKE_ANTICHEAT1,
	HKE_ANTICHEAT2,
	HKE_ANTICHEAT3,
	HKE_ANTICHEAT4,
};

enum DIEMSGTYPE
{
	DIEMSG_ALL_NOCONN = 0,
	DIEMSG_SYSTEM = 1,
	DIEMSG_NONE = 2,
	DIEMSG_SELF = 3,
	DIEMSG_ALL = 4,
};

enum CHATSIZE
{
	CS_DEFAULT = 0,
	CS_SMALL = 1,
	CS_BIG = 2,
};

enum CHATSTYLE
{
	CST_DEFAULT = 0,
	CST_BOLD = 1,
	CST_ITALIC = 2,
	CST_UNDERLINE = 3,
};

enum ENGINE_STATE
{
	ES_CRUISE,
	ES_THRUSTER,
	ES_ENGINE,
	ES_KILLED,
	ES_TRADELANE
};

enum EQ_TYPE
{
	ET_GUN,
	ET_TORPEDO,
	ET_CD,
	ET_MISSILE,
	ET_MINE,
	ET_CM,
	ET_SHIELDGEN,
	ET_THRUSTER,
	ET_SHIELDBAT,
	ET_NANOBOT,
	ET_MUNITION,
	ET_ENGINE,
	ET_OTHER,
	ET_SCANNER,
	ET_TRACTOR,
	ET_LIGHT
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// structs

struct HOOKENTRY
{
	FARPROC fpProc;
	long	dwRemoteAddress;
	FARPROC fpOldProc;
};

struct CARGO_INFO
{
	uint		iID;
	int			iCount;
	uint		iArchID;
	float		fStatus;
	bool		bMission;
	bool		bMounted;
	CacheString	hardpoint;
};


// money stuff
struct MONEY_FIX
{
	wstring		wscCharname;
	int			iAmount;

	bool operator==(MONEY_FIX mf1)
	{
		if (!wscCharname.compare(mf1.wscCharname))
			return true;

		return false;
	};
};

// ignore
struct IGNORE_INFO
{
	wstring wscCharname;
	wstring wscFlags;
};

struct CLIENT_INFO
{
	// kill msgs
	uint		iShip;
	uint		iShipOld;
	mstime		tmProtectedUntil;

	uint dmgLastPlayerId;
	DamageCause dmgLastCause;

	// money cmd
	list<MONEY_FIX> lstMoneyFix;

	// anticheat
	uint		iTradePartner;

	// change cruise disruptor behaviour
	bool		bCruiseActivated;
	bool		bThrusterActivated;
	bool		bEngineKilled;
	bool		bTradelane;

	// idle kicks
	uint		iBaseEnterTime;
	uint		iCharMenuEnterTime;

	// msg, wait and kick
	mstime		tmKickTime;

	// eventmode
	uint		iLastExitedBaseID;
	bool		bDisconnected;

	// f1 laming
	bool		bCharSelected;
	mstime		tmF1Time;
	mstime		tmF1TimeDisconnect;

	// ignore usercommand
	list<IGNORE_INFO> lstIgnore;

	// user settings
	DIEMSGTYPE	dieMsg;
	CHATSIZE	dieMsgSize;
	CHATSTYLE	dieMsgStyle;
	CHATSIZE	chatSize;
	CHATSTYLE	chatStyle;

	// autobuy
	/*
		bool		bAutoBuyMissiles;
		bool		bAutoBuyMunitions;
		bool		bAutoBuyMines;
		bool		bAutoBuyTorps;
		bool		bAutoBuyCD;
		bool		bAutoBuyCM;
		bool		bAutoBuyReload;
	*/
	// MultiKillMessages
	uint		iKillsInARow;

	// bans
	uint		iConnects; // incremented when player connects

	//cloak bool, this allows us to have a status on it shared between plugins to prevent possible bugs.
	bool bCloaked;

	bool		bSpawnProtected;
	bool		bUseServersideHitDetection; //used by AC Plugin
	CShip*		cship;
	uint		playerID;
	ushort		playerIDSID;
	float		fRadarRange;
	bool		isDocking;
	Vector		dockPosition;
	Vector		undockPosition;

	wstring IP;
};

struct INISECTIONVALUE
{
	string scKey;
	string scValue;
};

struct MULTIKILLMESSAGE
{
	uint iKillsInARow;
	wstring wscMessage;
};

struct EVENT_PLUGIN_POB_TRANSFER
{
	string eventid;
	int count;
};

// taken from directplay
typedef struct _DPN_CONNECTION_INFO {
	DWORD   dwSize;
	DWORD   dwRoundTripLatencyMS;
	DWORD   dwThroughputBPS;
	DWORD   dwPeakThroughputBPS;
	DWORD   dwBytesSentGuaranteed;
	DWORD   dwPacketsSentGuaranteed;
	DWORD   dwBytesSentNonGuaranteed;
	DWORD   dwPacketsSentNonGuaranteed;
	DWORD   dwBytesRetried;
	DWORD   dwPacketsRetried;
	DWORD   dwBytesDropped;
	DWORD   dwPacketsDropped;
	DWORD   dwMessagesTransmittedHighPriority;
	DWORD   dwMessagesTimedOutHighPriority;
	DWORD   dwMessagesTransmittedNormalPriority;
	DWORD   dwMessagesTimedOutNormalPriority;
	DWORD   dwMessagesTransmittedLowPriority;
	DWORD   dwMessagesTimedOutLowPriority;
	DWORD   dwBytesReceivedGuaranteed;
	DWORD   dwPacketsReceivedGuaranteed;
	DWORD   dwBytesReceivedNonGuaranteed;
	DWORD   dwPacketsReceivedNonGuaranteed;
	DWORD   dwMessagesReceived;
} DPN_CONNECTION_INFO, *PDPN_CONNECTION_INFO;

struct HKPLAYERINFO
{
	uint iClientID;
	wstring wscCharname;
	wstring wscBase;
	wstring wscSystem;
	uint iSystem;
	uint iShip;
	DPN_CONNECTION_INFO ci;
	wstring wscIP;
};

// patch stuff
struct PATCH_INFO_ENTRY
{
	ulong pAddress;
	void *pNewValue;
	uint iSize;
	void *pOldValue;
	bool bAlloced;
};

struct PATCH_INFO
{
	char	*szBinName;
	ulong	pBaseAddress;

	PATCH_INFO_ENTRY piEntries[128];
};

struct DATA_MARKETITEM
{
	uint iArchID;
	float fRep;
};

struct BASE_INFO
{
	uint	iBaseID;
	string	scBasename;
	uint	iObjectID;
	bool	bDestroyed;
	list<DATA_MARKETITEM> lstMarketMisc;
};

struct GROUP_MEMBER
{
	uint iClientID;
	wstring wscCharname;
};

struct PLUGIN_HOOKDATA
{
	string sName;
	HMODULE hDLL;
	int iPriority;
	bool bPaused;
};

struct PLUGIN_DATA
{
	string sName;
	string sShortName;
	HMODULE hDLL;
	string sDLL;
	bool bMayPause;
	bool bMayUnload;
	bool bPaused;
};

struct PathEntry
{
	Vector pos;
	uint objId = 0;
	uint systemId;
};

template<uint count>
struct RequestPathStruct
{
	int repId;
	uint waypointCount;
	bool noPathFound = false;
	PathEntry pathEntries[count];
};

// plugin
IMPORT void Plugin_Communication(PLUGIN_MESSAGE msgtype, void* msg);

// HkFuncTools
IMPORT uint CreateLootSimple(uint system, uint ownerShip, uint commodityId, uint amount, Vector pos, bool canAITractor);
IMPORT uint HkGetClientIdFromAccount(CAccount *acc);
IMPORT uint HkGetClientIdFromPD(struct PlayerData *pPD);
IMPORT CAccount* HkGetAccountByCharname(const wstring &wscCharname);
IMPORT uint HkGetClientIdFromCharname(const wstring &wscCharname);
IMPORT wstring HkGetAccountID(CAccount *acc);
IMPORT bool HkIsEncoded(const string &scFilename);
IMPORT bool HkIsInCharSelectMenu(const wstring &wscCharname);
IMPORT bool HkIsInCharSelectMenu(uint iClientID);
IMPORT bool HkIsValidClientID(uint iClientID);
IMPORT HK_ERROR HkResolveId(const wstring &wscCharname, uint &iClientID);
IMPORT HK_ERROR HkResolveShortCut(const wstring &wscShortcut, uint &iClientID);
IMPORT uint HkGetClientIDByShip(uint iShip);
IMPORT HK_ERROR HkGetAccountDirName(CAccount *acc, wstring &wscDir);
IMPORT HK_ERROR HkGetAccountDirName(const wstring &wscCharname, wstring &wscDir);
IMPORT HK_ERROR HkGetCharFileName(const wstring &wscCharname, wstring &wscFilename);
IMPORT wstring HkGetBaseNickByID(uint iBaseID);
IMPORT wstring HkGetPlayerSystem(uint iClientID);
IMPORT wstring HkGetSystemNickByID(uint iSystemID);
IMPORT void HkLockAccountAccess(CAccount *acc, bool bKick);
IMPORT void HkUnlockAccountAccess(CAccount *acc);
IMPORT void HkGetItemsForSale(uint iBaseID, list<uint> &lstItems);
IMPORT IObjRW* HkGetInspectObj(uint objId);
IMPORT IObjRW* HkGetInspect(uint iClientID);
IMPORT ENGINE_STATE HkGetEngineState(uint iClientID);
IMPORT EQ_TYPE HkGetEqType(Archetype::Equipment *eq);
IMPORT bool HkIsOnDeathMenu(uint iClientID);

// HkFuncMsg
IMPORT HK_ERROR HkMsg(int iClientID, const wstring &wscMessage);
IMPORT HK_ERROR HkMsg(const wstring &wscCharname, const wstring &wscMessage);
IMPORT HK_ERROR HkMsgS(const wstring &wscSystemname, const wstring &wscMessage);
IMPORT void HkMsgS(uint systemId, const wstring &wscMessage);
IMPORT HK_ERROR HkMsgU(const wstring &wscMessage);
IMPORT HK_ERROR HkFMsgEncodeXML(const wstring &wscXML, char *szBuf, uint iSize, uint &iRet);
IMPORT HK_ERROR HkFMsgSendChat(uint iClientID, char *szBuf, uint iSize);
IMPORT HK_ERROR HkFMsg(uint iClientID, const wstring &wscXML);
IMPORT HK_ERROR HkFMsg(const wstring &wscCharname, const wstring &wscXML);
IMPORT HK_ERROR HkFMsgS(const wstring &wscSystemname, const wstring &wscXML);
IMPORT HK_ERROR HkFMsgU(const wstring &wscXML);

// HkFuncPlayers
IMPORT HK_ERROR HkGetCash(const wstring &wscCharname, int &iCash);
IMPORT HK_ERROR HkAddCash(const wstring &wscCharname, int iAmount);
IMPORT HK_ERROR HkKick(CAccount *acc);
IMPORT HK_ERROR HkKick(const wstring &wscCharname);
IMPORT HK_ERROR HkKickReason(const wstring &wscCharname, const wstring &wscReason);
IMPORT HK_ERROR HkBan(const wstring &wscCharname, bool bBan);
IMPORT HK_ERROR HkBeam(const wstring &wscCharname, const wstring &wscBasename);
IMPORT HK_ERROR HkBeamById(const uint clientId, const uint baseId);
IMPORT HK_ERROR HkSaveChar(const wstring &wscCharname);
IMPORT HK_ERROR HkEnumCargo(const wstring &wscCharname, list<CARGO_INFO> &lstCargo, int &iRemainingHoldSize);
IMPORT HK_ERROR HkRemoveCargo(const wstring &wscCharname, uint iID, uint iCount);
IMPORT HK_ERROR HkAddCargo(const wstring &wscCharname, uint iGoodID, int iCount, bool bMission);
IMPORT HK_ERROR HkAddCargo(const wstring &wscCharname, const wstring &wscGood, int iCount, bool bMission);
IMPORT HK_ERROR HkRename(const wstring &wscCharname, const wstring &wscNewCharname, bool bOnlyDelete);
IMPORT HK_ERROR HkMsgAndKick(uint iClientID, const wstring &wscReason, uint iIntervall);
IMPORT HK_ERROR HkKill(const wstring &wscCharname);
IMPORT HK_ERROR HkGetReservedSlot(const wstring &wscCharname, bool &bResult);
IMPORT HK_ERROR HkSetReservedSlot(const wstring &wscCharname, bool bReservedSlot);
//IMPORT void HkPlayerAutoBuy(uint iClientID, uint iBaseID);
IMPORT HK_ERROR HkResetRep(const wstring &wscCharname);
IMPORT HK_ERROR HkGetGroupMembers(const wstring &wscCharname, list<GROUP_MEMBER> &lstMembers);
IMPORT HK_ERROR HkSetRep(const wstring &wscCharname, const wstring &wscRepGroup, float fValue);
IMPORT HK_ERROR HkGetRep(const wstring &wscCharname, const wstring &wscRepGroup, float &fValue);
IMPORT HK_ERROR HkReadCharFile(const wstring &wscCharname, list<wstring> &lstOutput);
IMPORT HK_ERROR HkWriteCharFile(const wstring &wscCharname, wstring wscData);

// HkFuncLog
IMPORT void AddDebugLog(const char *szString, ...);
IMPORT void AddLog(const char *szString, ...);
IMPORT void AddChatLog(const char *szString, ...);
IMPORT void HkHandleCheater(uint iClientID, bool bBan, wstring wscReason, ...);
IMPORT bool HkAddCheaterLog(const wstring &wscCharname, const wstring &wscReason);
IMPORT bool HkAddCheaterLog(const uint &iClientID, const wstring &wscReason);
IMPORT bool HkAddKickLog(uint iClientID, wstring wscReason, ...);
IMPORT bool HkAddConnectLog(uint iClientID, wstring wscReason, ...);
IMPORT void HkAddAdminCmdLog(const char *szString, ...);
IMPORT void HkAddUserCmdLog(const char *szString, ...);
IMPORT void HkAddPerfTimerLog(const char *szString, ...);

// HkFuncOther
IMPORT void HkGetPlayerIP(uint iClientID, wstring &wscIP);
IMPORT HK_ERROR HkGetPlayerInfo(const wstring &wscCharname, HKPLAYERINFO &pi, bool bAlsoCharmenu);
IMPORT list<HKPLAYERINFO> HkGetPlayers();
IMPORT HK_ERROR HkGetConnectionStats(uint iClientID, DPN_CONNECTION_INFO &ci);
IMPORT HK_ERROR HkSetAdmin(const wstring &wscCharname, const wstring &wscRights);
IMPORT HK_ERROR HkGetAdmin(const wstring &wscCharname, wstring &wscRights);
IMPORT HK_ERROR HkDelAdmin(const wstring &wscCharname);
IMPORT HK_ERROR HkChangeNPCSpawn(bool bDisable);
IMPORT HK_ERROR HkGetBaseStatus(const wstring &wscBasename, float &fHealth, float &fMaxHealth);

// HkFLIni
IMPORT HK_ERROR HkFLIniGet(const wstring &wscCharname, const wstring &wscKey, wstring &wscRet);
IMPORT HK_ERROR HkFLIniWrite(const wstring &wscCharname, const wstring &wscKey, const wstring &wscValue);

IMPORT wstring HkErrGetText(HK_ERROR hkErr);

IMPORT void UserCmd_SetDieMsg(uint iClientID, const wstring &wscParam);
IMPORT void UserCmd_SetChatFont(uint iClientID, const wstring &wscParam);

IMPORT void ProcessEvent(wstring wscText, ...);
IMPORT void SendChatEvent(uint iClientID, uint iToID, wstring &wscMsg);

IMPORT void PrintUserCmdText(uint iClientID, wstring wscText, ...);

// tools
IMPORT wstring stows(const string &scText);
IMPORT string wstos(const wstring &wscText);
IMPORT string itos(int i);
EXPORT wstring itows(int i);
IMPORT string IniGetS(const string &scFile, const string &scApp, const string &scKey, const string &scDefault);
IMPORT int IniGetI(const string &scFile, const string &scApp, const string &scKey, int iDefault);
IMPORT bool IniGetB(const string &scFile, const string &scApp, const string &scKey, bool bDefault);
IMPORT void IniWrite(const string &scFile, const string &scApp, const string &scKey, const string &scValue);
IMPORT void WriteProcMem(void *pAddress, void *pMem, int iSize);
IMPORT void ReadProcMem(void *pAddress, void *pMem, int iSize);
IMPORT wstring ToLower(const wstring &wscStr);
IMPORT string ToLower(const string &scStr);
IMPORT int ToInt(const wstring &wscStr);
IMPORT void ConPrint(wstring wscText, ...);
IMPORT wstring XMLText(const wstring &wscText);
IMPORT wstring GetParam(const wstring &wscLine, wchar_t wcSplitChar, uint iPos);
IMPORT wstring ReplaceStr(const wstring &wscSource, const wstring &wscSearchFor, const wstring &wscReplaceWith);
IMPORT void IniDelSection(const string &scFile, const string &scApp);
IMPORT void IniWriteW(const string &scFile, const string &scApp, const string &scKey, const wstring &wscValue);
IMPORT wstring IniGetWS(const string &scFile, const string &scApp, const string &scKey, const wstring &wscDefault);
IMPORT wstring ToMoneyStr(int iCash);
IMPORT float IniGetF(const string &scFile, const string &scApp, const string &scKey, float fDefault);
IMPORT void IniGetSection(const string &scFile, const string &scApp, list<INISECTIONVALUE> &lstValues);
IMPORT float ToFloat(const wstring &wscStr);
IMPORT mstime timeInMS();
IMPORT wstring TimeString(uint seconds);
IMPORT void SwapBytes(void *ptr, uint iLen);
IMPORT FARPROC PatchCallAddr(char *hMod, DWORD dwInstallAddress, char *dwHookFunction);

// flcodec
IMPORT bool flc_decode(const char *ifile, const char *ofile);
IMPORT bool flc_encode(const char *ifile, const char *ofile);

// admin commands

// enums
enum CCMDS_RIGHTS
{
	RIGHT_NOTHING = 0,
	RIGHT_SUPERADMIN = 0xFFFFFFFF,
	RIGHT_MSG = (1 << 0),
	RIGHT_BAN = (1 << 1),
	RIGHT_EVENTMODE = (1 << 2),
	RIGHT_CASH = (1 << 3),
	RIGHT_BEAMKILL = (1 << 4),
	RIGHT_REPUTATION = (1 << 5),
	RIGHT_CARGO = (1 << 6),
	RIGHT_CHARACTERS = (1 << 7),
	RIGHT_SETTINGS = (1 << 8),
	RIGHT_PLUGINS = (1 << 9),
	RIGHT_OTHER = (1 << 10),
	RIGHT_SPECIAL1 = (1 << 11),
	RIGHT_SPECIAL2 = (1 << 12),
	RIGHT_SPECIAL3 = (1 << 13),
	RIGHT_KICK = (1 << 14),
	RIGHT_CHASEPULL = (1 << 15),
	RIGHT_AICONTROL = (1 << 16),
	RIGHT_CLOAK = (1 << 17),
	RIGHT_BASES = (1 << 18),
};

class CTimer
{
public:
	IMPORT CTimer(string sFunction, uint iWarning);
	IMPORT void start();
	IMPORT uint stop();

private:
	mstime tmStart;
	uint iMax;
	string sFunction;
	uint iWarning;
};

// admin stuff
class CCmds
{
	bool bID;
	bool bShortCut;
	bool bSelf;
	bool bTarget;

public:
	DWORD rights;
	HK_ERROR hkLastErr;

	IMPORT void PrintError();

	// commands
	void CmdGetCash(const wstring &wscCharname);
	void CmdSetCash(const wstring &wscCharname, int iAmount);
	void CmdSetCashSec(const wstring &wscCharname, int iAmountCheck, int iAmount);
	void CmdAddCash(const wstring &wscCharname, int iAmount);
	void CmdAddCashSec(const wstring &wscCharname, int iAmountCheck, int iAmount);

	void CmdKick(const wstring &wscCharname, const wstring &wscReason);
	void CmdBan(const wstring &wscCharname);
	void CmdUnban(const wstring &wscCharname);
	void CmdKickBan(const wstring &wscCharname, const wstring &wscReason);

	void CmdBeam(const wstring &wscCharname, const wstring &wscBasename);
	void CmdKill(const wstring &wscCharname);
	void CmdResetRep(const wstring &wscCharname);
	void CmdSetRep(const wstring &wscCharname, const wstring &wscRepGroup, float fValue);
	void CmdGetRep(const wstring &wscCharname, const wstring &wscRepGroup);

	void CmdMsg(const wstring &wscCharname, const wstring &wscText);
	void CmdMsgS(const wstring &wscSystemname, const wstring &wscText);
	void CmdMsgU(const wstring &wscText);
	void CmdFMsg(const wstring &wscCharname, const wstring &wscXML);
	void CmdFMsgS(const wstring &wscSystemname, const wstring &wscXML);
	void CmdFMsgU(const wstring &wscXML);

	void CmdEnumCargo(const wstring &wscCharname);
	void CmdRemoveCargo(const wstring &wscCharname, uint iID, uint iCount);
	void CmdAddCargo(const wstring &wscCharname, const wstring &wscGood, uint iCount, uint iMission);

	void CmdRename(const wstring &wscCharname, const wstring &wscNewCharname);
	void CmdDeleteChar(const wstring &wscCharname);

	void CmdReadCharFile(const wstring &wscCharname);
	void CmdWriteCharFile(const wstring &wscCharname, const wstring &wscData);

	void CmdGetBaseStatus(const wstring &wscBasename);
	void CmdGetClientId(const wstring &wscCharname);
	void PrintPlayerInfo(HKPLAYERINFO pi);
	void CmdGetPlayerInfo(const wstring &wscCharname);
	void CmdGetPlayers();
	void XPrintPlayerInfo(HKPLAYERINFO pi);
	void CmdXGetPlayerInfo(const wstring &wscCharname);
	void CmdXGetPlayers();
	void CmdGetPlayerIDs();
	void CmdHelp();
	void CmdGetAccountDirName(const wstring &wscCharname);
	void CmdGetCharFileName(const wstring &wscCharname);
	void CmdIsOnServer(const wstring &wscCharname);
	void CmdIsLoggedIn(const wstring &wscCharname);
	void CmdMoneyFixList();
	void CmdServerInfo();
	void CmdGetGroupMembers(const wstring &wscCharname);

	void CmdSaveChar(const wstring &wscCharname);

	void CmdGetReservedSlot(const wstring &wscCharname);
	void CmdSetReservedSlot(const wstring &wscCharname, int iReservedSlot);
	void CmdSetAdmin(const wstring &wscCharname, wstring wscRights);
	void CmdGetAdmin(const wstring &wscCharname);
	void CmdDelAdmin(const wstring &wscCharname);
	void CmdRehash();
	void CmdUnload(const wstring &wscParam);

	void CmdLoadPlugins();
	void CmdListPlugins();
	void CmdUnloadPlugin(const wstring &wscPlugin);
	void CmdPausePlugin(const wstring &wscPlugin);
	void CmdUnpausePlugin(const wstring &wscPlugin);

	void CmdTest(int iArg, int iArg2, int iArg3);
	//
	IMPORT wstring ArgCharname(uint iArg);
	IMPORT int ArgInt(uint iArg);
	IMPORT uint ArgUInt(uint iArg);
	IMPORT float ArgFloat(uint iArg);
	IMPORT wstring ArgStr(uint iArg);
	IMPORT wstring ArgStrToEnd(uint iArg);
	void ExecuteCommandString(const wstring &wscCmd);

	void SetRightsByString(string scRightStr);
	IMPORT void Print(wstring wscText, ...);
	virtual void DoPrint(wstring wscText) {};
	IMPORT virtual wstring GetAdminName() { return L""; };

	wstring wscCurCmdString;
};


// namespaces
namespace HkIServerImpl
{
	IMPORT extern bool g_bInSubmitChat;
	IMPORT extern uint g_iTextLen;
}
namespace PluginManager {
	IMPORT void LoadPlugins(bool, CCmds*);
	IMPORT void LoadPlugin(const string &sFileName, CCmds*);
	IMPORT HK_ERROR PausePlugin(const string &sShortName, bool bPause);
	IMPORT HK_ERROR UnloadPlugin(const string &sShortName);
	IMPORT void UnloadPlugins();
}

// variables
extern IMPORT std::map<string, list<PLUGIN_HOOKDATA>*> mpPluginHooks;
extern IMPORT list<PLUGIN_DATA> lstPlugins;

extern IMPORT HkIClientImpl* FakeClient;
extern IMPORT HkIClientImpl* HookClient;
extern IMPORT char* OldClient;

extern IMPORT uint iDmgMunitionID;
extern IMPORT unordered_map<uint, uint> npcToDropLoot;

extern IMPORT bool g_bMsg;
extern IMPORT bool g_bMsgS;
extern IMPORT bool g_bMsgU;

extern IMPORT HANDLE hProcFL;
extern IMPORT HMODULE hModServer;
extern IMPORT HMODULE hModCommon;
extern IMPORT HMODULE hModRemoteClient;
extern IMPORT HMODULE hModDPNet;
extern IMPORT HMODULE hModDaLib;
extern IMPORT HMODULE hModContent;
extern IMPORT _CreateWString CreateWString;
extern IMPORT _FreeWString FreeWString;
extern IMPORT _CreateString CreateString;
extern IMPORT _FreeString FreeString;
extern IMPORT _GetCString GetCString;
extern IMPORT _GetWCString GetWCString;
extern IMPORT _WStringAssign WStringAssign;
extern IMPORT _WStringAppend WStringAppend;
extern IMPORT _CPlayerAccount_GetServerSignature CPlayerAccount_GetServerSignature;
extern IMPORT FILE *fLog;
extern IMPORT FILE *fChatLog;
extern IMPORT FILE *fLogDebug;
extern IMPORT FARPROC fpOldUpdate;
extern IMPORT string sDebugLog;

// setting variables
extern IMPORT bool set_bLoadedSettings;
extern IMPORT string set_scCfgFile;
extern IMPORT uint set_iAntiDockKill;
extern IMPORT list<uint> set_lstNoPVPSystems;
extern IMPORT list<wstring> set_lstChatSuppress;
extern IMPORT bool set_bSocketActivated;
extern IMPORT bool set_bDebug;
extern IMPORT bool set_bLogConnects;
extern IMPORT wstring set_wscDeathMsgStyle;
extern IMPORT wstring set_wscDeathMsgStyleSys;
extern IMPORT bool	set_bDieMsg;
extern IMPORT bool	set_bDisableCharfileEncryption;
extern IMPORT uint	set_iAntiBaseIdle;
extern IMPORT uint	set_iAntiCharMenuIdle;
extern IMPORT bool set_bChangeCruiseDisruptorBehaviour;
extern IMPORT bool	set_bUserCmdSetDieMsg;
extern IMPORT bool	set_bUserCmdSetChatFont;
extern IMPORT bool	set_bUserCmdIgnore;
extern IMPORT bool	set_bUserCmdHelp;
extern IMPORT uint	set_iDisableNPCSpawns;
extern IMPORT int	set_iPort;
extern IMPORT int	set_iWPort;
extern IMPORT wstring set_wscKickMsg;
extern IMPORT wstring set_wscUserCmdStyle;
extern IMPORT wstring set_wscAdminCmdStyle;
extern IMPORT uint	set_iKickMsgPeriod;
extern IMPORT wstring set_wscDeathMsgTextPlayerKill;
extern IMPORT wstring set_wscDeathMsgTextSelfKill;
extern IMPORT wstring set_wscDeathMsgTextNPC;
extern IMPORT wstring set_wscDeathMsgTextSuicide;
extern IMPORT uint	set_iAntiF1;
extern IMPORT wstring set_wscDeathMsgTextAdminKill;
extern IMPORT uint	set_iUserCmdMaxIgnoreList;
extern IMPORT uint	set_iReservedSlots;
extern IMPORT uint	set_iDisconnectDelay;
//extern IMPORT bool	set_bAutoBuy;
extern IMPORT float set_fTorpMissileBaseDamageMultiplier;
extern IMPORT bool set_MKM_bActivated;
extern IMPORT wstring set_MKM_wscStyle;
extern IMPORT list<MULTIKILLMESSAGE> set_MKM_lstMessages;
extern IMPORT bool	set_bUserCmdSetDieMsgSize;
extern IMPORT uint	set_iMaxGroupSize;
extern IMPORT list<wstring> set_lstBans;
extern IMPORT bool	set_bBanAccountOnMatch;
extern IMPORT uint set_iTimerThreshold;
extern IMPORT uint set_iTimerDebugThreshold;
extern IMPORT uint set_iDebugMaxSize;
extern IMPORT bool	set_bLogAdminCmds;
extern IMPORT bool	set_bLogSocketCmds;
extern IMPORT bool	set_bLogLocalSocketCmds;
extern IMPORT bool	set_bLogUserCmds;
extern IMPORT bool	set_bPerfTimer;


extern IMPORT unordered_map<uint, BASE_INFO> lstBases;

extern IMPORT CDPClientProxy **g_cClientProxyArray;
extern IMPORT void *pClient;

extern IMPORT _RCSendChatMsg RCSendChatMsg;
extern IMPORT _CRCAntiCheat CRCAntiCheat;
extern IMPORT _CreateChar CreateChar;

extern IMPORT string scAcctPath;

#define MAX_CLIENT_ID 249
extern IMPORT CLIENT_INFO ClientInfo[MAX_CLIENT_ID + 1];
extern IMPORT CDPServer *cdpSrv;
extern IMPORT uint g_iServerLoad;
extern IMPORT uint g_iPlayerCount;
extern IMPORT bool g_bNPCDisabled;
extern IMPORT char *g_FLServerDataPtr;
extern IMPORT bool g_gNonGunHitsBase;
extern IMPORT float g_LastHitPts;

// help

typedef bool(*_HelpEntryDisplayed)(uint);
extern IMPORT void HkAddHelpEntry(const wstring &c, const wstring &t, const wstring &shelp, const wstring &lhelp, _HelpEntryDisplayed check);
extern IMPORT void HkRemoveHelpEntry(const wstring &c, const wstring &t);
extern IMPORT bool get_bTrue(uint iClientID);

//blowfish

typedef struct {
	unsigned long P[16 + 2];
	unsigned long S[4][256];
} BLOWFISH_CTX;

IMPORT void Blowfish_Init(BLOWFISH_CTX *ctx, unsigned char *key, int keyLen);
IMPORT char Blowfish_Encrypt(BLOWFISH_CTX *ctx, void *ptr, unsigned long dataLen);
IMPORT char Blowfish_Decrypt(BLOWFISH_CTX *ctx, void *ptr, unsigned long dataLen);


enum class TextColor
{
	Default = 0x00FF00,
	SystemBlue = 0x84C6E6,
	DeathMessage = 0x1919BD,
	DeathMessageRed = 0xBF1D1D,
	GroupMessagePink = 0xFF7BFF,
	PrivateGreen = 0x3ABD19,

	AliceBlue = 0xF0F8FF,
	AntiqueWhite = 0xFAEBD7,
	Aqua = 0x00FFFF,
	Aquamarine = 0x7FFFD4,
	Azure = 0xF0FFFF,
	Beige = 0xF5F5DC,
	Bisque = 0xFFE4C4,
	Black = 0x000000,
	BlanchedAlmond = 0xFFEBCD,
	Blue = 0x0000FF,
	BlueViolet = 0x8A2BE2,
	Brown = 0xA52A2A,
	BurlyWood = 0xDEB887,
	CadetBlue = 0x5F9EA0,
	Chartreuse = 0x7FFF00,
	Chocolate = 0xD2691E,
	Coral = 0xFF7F50,
	CornflowerBlue = 0x6495ED,
	Cornsilk = 0xFFF8DC,
	Crimson = 0xDC143C,
	Cyan = 0x00FFFF,
	DarkBlue = 0x00008B,
	DarkCyan = 0x008B8B,
	DarkGoldenrod = 0xB8860B,
	DarkGray = 0xA9A9A9,
	DarkGreen = 0x006400,
	DarkKhaki = 0xBDB76B,
	DarkMagenta = 0x8B008B,
	DarkOliveGreen = 0x556B2F,
	DarkOrange = 0xFF8C00,
	DarkOrchid = 0x9932CC,
	DarkRed = 0x8B0000,
	DarkSalmon = 0xE9967A,
	DarkSeaGreen = 0x8FBC8F,
	DarkSlateBlue = 0x483D8B,
	DarkSlateGray = 0x2F4F4F,
	DarkTurquoise = 0x00CED1,
	DarkViolet = 0x9400D3,
	DeepPink = 0xFF1493,
	DeepSkyBlue = 0x00BFFF,
	DimGray = 0x696969,
	DodgerBlue = 0x1E90FF,
	Firebrick = 0xB22222,
	FloralWhite = 0xFFFAF0,
	ForestGreen = 0x228B22,
	Fuchsia = 0xFF00FF,
	Gainsboro = 0xDCDCDC,
	GhostWhite = 0xF8F8FF,
	Gold = 0xFFD700,
	Goldenrod = 0xDAA520,
	Gray = 0x808080,
	Green = 0x008000,
	GreenYellow = 0xADFF2F,
	Honeydew = 0xF0FFF0,
	HotPink = 0xFF69B4,
	IndianRed = 0xCD5C5C,
	Indigo = 0x4B0082,
	Ivory = 0xFFFFF0,
	Khaki = 0xF0E68C,
	Lavender = 0xE6E6FA,
	LavenderBlush = 0xFFF0F5,
	LawnGreen = 0x7CFC00,
	LemonChiffon = 0xFFFACD,
	LightBlue = 0xADD8E6,
	LightCoral = 0xF08080,
	LightCyan = 0xE0FFFF,
	LightGoldenrodYellow = 0xFAFAD2,
	LightGray = 0xD3D3D3,
	LightGreen = 0x90EE90,
	LightPink = 0xFFB6C1,
	LightSalmon = 0xFFA07A,
	LightSeaGreen = 0x20B2AA,
	LightSkyBlue = 0x87CEFA,
	LightSlateGray = 0x778899,
	LightSteelBlue = 0xB0C4DE,
	LightYellow = 0xFFFFE0,
	Lime = 0x00FF00,
	LimeGreen = 0x32CD32,
	Linen = 0xFAF0E6,
	Magenta = 0xFF00FF,
	Maroon = 0x800000,
	MediumAquamarine = 0x66CDAA,
	MediumBlue = 0x0000CD,
	MediumOrchid = 0xBA55D3,
	MediumPurple = 0x9370DB,
	MediumSeaGreen = 0x3CB371,
	MediumSlateBlue = 0x7B68EE,
	MediumSpringGreen = 0x00FA9A,
	MediumTurquoise = 0x48D1CC,
	MediumVioletRed = 0xC71585,
	MidnightBlue = 0x191970,
	MintCream = 0xF5FFFA,
	MistyRose = 0xFFE4E1,
	Moccasin = 0xFFE4B5,
	NavajoWhite = 0xFFDEAD,
	Navy = 0x000080,
	OldLace = 0xFDF5E6,
	Olive = 0x808000,
	OliveDrab = 0x6B8E23,
	Orange = 0xFFA500,
	OrangeRed = 0xFF4500,
	Orchid = 0xDA70D6,
	PaleGoldenrod = 0xEEE8AA,
	PaleGreen = 0x98FB98,
	PaleTurquoise = 0xAFEEEE,
	PaleVioletRed = 0xDB7093,
	PapayaWhip = 0xFFEFD5,
	PeachPuff = 0xFFDAB9,
	Peru = 0xCD853F,
	Pink = 0xFFC0CB,
	Plum = 0xDDA0DD,
	PowderBlue = 0xB0E0E6,
	Purple = 0x800080,
	Red = 0xFF0000,
	RosyBrown = 0xBC8F8F,
	RoyalBlue = 0x4169E1,
	SaddleBrown = 0x8B4513,
	Salmon = 0xFA8072,
	SandyBrown = 0xF4A460,
	SeaGreen = 0x2E8B57,
	SeaShell = 0xFFF5EE,
	Sienna = 0xA0522D,
	Silver = 0xC0C0C0,
	SkyBlue = 0x87CEEB,
	SlateBlue = 0x6A5ACD,
	SlateGray = 0x708090,
	Snow = 0xFFFAFA,
	SpringGreen = 0x00FF7F,
	SteelBlue = 0x4682B4,
	Tan = 0xD2B48C,
	Teal = 0x008080,
	Thistle = 0xD8BFD8,
	Tomato = 0xFF6347,
	Turquoise = 0x40E0D0,
	Violet = 0xEE82EE,
	Wheat = 0xF5DEB3,
	White = 0xFFFFFF,
	WhiteSmoke = 0xF5F5F5,
	Yellow = 0xFFFF00,
	YellowGreen = 0x9ACD32,
};

#endif
