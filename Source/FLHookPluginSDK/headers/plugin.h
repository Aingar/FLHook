#ifndef __PLUGIN_H__
#define __PLUGIN_H__ 1

enum PLUGIN_RETURNCODE
{
	DEFAULT_RETURNCODE = 0,
	SKIPPLUGINS = 1,
	SKIPPLUGINS_NOFUNCTIONCALL = 2,
	NOFUNCTIONCALL = 3,
};



enum PLUGIN_CALLBACKS
{
	PLUGIN_HkIServerImpl_Update,
	PLUGIN_HkIServerImpl_SubmitChat,
	PLUGIN_HkIServerImpl_SubmitChat_AFTER,
	PLUGIN_HkIServerImpl_PlayerLaunch,
	PLUGIN_HkIServerImpl_PlayerLaunch_AFTER,
	PLUGIN_HkIServerImpl_FireWeapon,
	PLUGIN_HkIServerImpl_FireWeapon_AFTER,
	PLUGIN_HkIServerImpl_SPMunitionCollision,
	PLUGIN_HkIServerImpl_SPMunitionCollision_AFTER,
	PLUGIN_HkIServerImpl_SPObjUpdate,
	PLUGIN_HkIServerImpl_SPObjUpdate_AFTER,
	PLUGIN_HkIServerImpl_SPObjCollision,
	PLUGIN_HkIServerImpl_SPObjCollision_AFTER,
	PLUGIN_HkIServerImpl_LaunchComplete,
	PLUGIN_HkIServerImpl_LaunchComplete_AFTER,
	PLUGIN_HkIServerImpl_CharacterSelect,
	PLUGIN_HkIServerImpl_CharacterSelect_AFTER,
	PLUGIN_HkIServerImpl_BaseEnter,
	PLUGIN_HkIServerImpl_BaseEnter_AFTER,
	PLUGIN_HkIServerImpl_BaseExit,
	PLUGIN_HkIServerImpl_BaseExit_AFTER,
	PLUGIN_HkIServerImpl_OnConnect,
	PLUGIN_HkIServerImpl_OnConnect_AFTER,
	PLUGIN_HkIServerImpl_DisConnect,
	PLUGIN_HkIServerImpl_DisConnect_AFTER,
	PLUGIN_HkIServerImpl_TerminateTrade,
	PLUGIN_HkIServerImpl_TerminateTrade_AFTER,
	PLUGIN_HkIServerImpl_InitiateTrade,
	PLUGIN_HkIServerImpl_InitiateTrade_AFTER,
	PLUGIN_HkIServerImpl_ActivateEquip,
	PLUGIN_HkIServerImpl_ActivateEquip_AFTER,
	PLUGIN_HkIServerImpl_ActivateCruise,
	PLUGIN_HkIServerImpl_ActivateCruise_AFTER,
	PLUGIN_HkIServerImpl_ActivateThrusters,
	PLUGIN_HkIServerImpl_ActivateThrusters_AFTER,
	PLUGIN_HkIServerImpl_GFGoodSell,
	PLUGIN_HkIServerImpl_GFGoodSell_AFTER,
	PLUGIN_HkIServerImpl_CharacterInfoReq,
	PLUGIN_HkIServerImpl_CharacterInfoReq_AFTER,
	PLUGIN_HkIServerImpl_JumpInComplete,
	PLUGIN_HkIServerImpl_JumpInComplete_AFTER,
	PLUGIN_HkIServerImpl_SystemSwitchOutComplete,
	PLUGIN_HkIServerImpl_SystemSwitchOutComplete_AFTER,
	PLUGIN_HkIServerImpl_Login,
	PLUGIN_HkIServerImpl_Login_BEFORE,
	PLUGIN_HkIServerImpl_Login_AFTER,
	PLUGIN_HkIServerImpl_MineAsteroid,
	PLUGIN_HkIServerImpl_MineAsteroid_AFTER,
	PLUGIN_HkIServerImpl_GoTradelane,
	PLUGIN_HkIServerImpl_GoTradelane_AFTER,
	PLUGIN_HkIServerImpl_StopTradelane,
	PLUGIN_HkIServerImpl_StopTradelane_AFTER,
	PLUGIN_HkIServerImpl_AbortMission,
	PLUGIN_HkIServerImpl_AbortMission_AFTER,
	PLUGIN_HkIServerImpl_AcceptTrade,
	PLUGIN_HkIServerImpl_AcceptTrade_AFTER,
	PLUGIN_HkIServerImpl_AddTradeEquip,
	PLUGIN_HkIServerImpl_AddTradeEquip_AFTER,
	PLUGIN_HkIServerImpl_BaseInfoRequest,
	PLUGIN_HkIServerImpl_BaseInfoRequest_AFTER,
	PLUGIN_HkIServerImpl_CreateNewCharacter,
	PLUGIN_HkIServerImpl_CreateNewCharacter_AFTER,
	PLUGIN_HkIServerImpl_DelTradeEquip,
	PLUGIN_HkIServerImpl_DelTradeEquip_AFTER,
	PLUGIN_HkIServerImpl_DestroyCharacter,
	PLUGIN_HkIServerImpl_DestroyCharacter_AFTER,
	PLUGIN_HkIServerImpl_GFGoodBuy,
	PLUGIN_HkIServerImpl_GFGoodBuy_AFTER,
	PLUGIN_HkIServerImpl_GFGoodVaporized,
	PLUGIN_HkIServerImpl_GFGoodVaporized_AFTER,
	PLUGIN_HkIServerImpl_GFObjSelect,
	PLUGIN_HkIServerImpl_GFObjSelect_AFTER,
	PLUGIN_HkIServerImpl_Hail,
	PLUGIN_HkIServerImpl_Hail_AFTER,
	PLUGIN_HkIServerImpl_InterfaceItemUsed,
	PLUGIN_HkIServerImpl_InterfaceItemUsed_AFTER,
	PLUGIN_HkIServerImpl_JettisonCargo,
	PLUGIN_HkIServerImpl_JettisonCargo_AFTER,
	PLUGIN_HkIServerImpl_LocationEnter,
	PLUGIN_HkIServerImpl_LocationEnter_AFTER,
	PLUGIN_HkIServerImpl_LocationExit,
	PLUGIN_HkIServerImpl_LocationExit_AFTER,
	PLUGIN_HkIServerImpl_LocationInfoRequest,
	PLUGIN_HkIServerImpl_LocationInfoRequest_AFTER,
	PLUGIN_HkIServerImpl_MissionResponse,
	PLUGIN_HkIServerImpl_MissionResponse_AFTER,
	PLUGIN_HkIServerImpl_ReqAddItem,
	PLUGIN_HkIServerImpl_ReqAddItem_AFTER,
	PLUGIN_HkIServerImpl_ReqChangeCash,
	PLUGIN_HkIServerImpl_ReqChangeCash_AFTER,
	PLUGIN_HkIServerImpl_ReqCollisionGroups,
	PLUGIN_HkIServerImpl_ReqCollisionGroups_AFTER,
	PLUGIN_HkIServerImpl_ReqEquipment,
	PLUGIN_HkIServerImpl_ReqEquipment_AFTER,
	PLUGIN_HkIServerImpl_ReqHullStatus,
	PLUGIN_HkIServerImpl_ReqHullStatus_AFTER,
	PLUGIN_HkIServerImpl_ReqModifyItem,
	PLUGIN_HkIServerImpl_ReqModifyItem_AFTER,
	PLUGIN_HkIServerImpl_ReqRemoveItem,
	PLUGIN_HkIServerImpl_ReqRemoveItem_AFTER,
	PLUGIN_HkIServerImpl_ReqSetCash,
	PLUGIN_HkIServerImpl_ReqSetCash_AFTER,
	PLUGIN_HkIServerImpl_ReqShipArch,
	PLUGIN_HkIServerImpl_ReqShipArch_AFTER,
	PLUGIN_HkIServerImpl_RequestBestPath,
	PLUGIN_HkIServerImpl_RequestBestPath_AFTER,
	PLUGIN_HkIServerImpl_RequestCancel,
	PLUGIN_HkIServerImpl_RequestCancel_AFTER,
	PLUGIN_HkIServerImpl_RequestCreateShip,
	PLUGIN_HkIServerImpl_RequestCreateShip_AFTER,
	PLUGIN_HkIServerImpl_RequestEvent,
	PLUGIN_HkIServerImpl_RequestEvent_AFTER,
	PLUGIN_HkIServerImpl_RequestGroupPositions,
	PLUGIN_HkIServerImpl_RequestGroupPositions_AFTER,
	PLUGIN_HkIServerImpl_RequestPlayerStats,
	PLUGIN_HkIServerImpl_RequestPlayerStats_AFTER,
	PLUGIN_HkIServerImpl_RequestRankLevel,
	PLUGIN_HkIServerImpl_RequestRankLevel_AFTER,
	PLUGIN_HkIServerImpl_RequestTrade,
	PLUGIN_HkIServerImpl_RequestTrade_AFTER,
	PLUGIN_HkIServerImpl_SPRequestInvincibility,
	PLUGIN_HkIServerImpl_SPRequestInvincibility_AFTER,
	PLUGIN_HkIServerImpl_SPRequestUseItem,
	PLUGIN_HkIServerImpl_SPRequestUseItem_AFTER,
	PLUGIN_HkIServerImpl_SPScanCargo,
	PLUGIN_HkIServerImpl_SPScanCargo_AFTER,
	PLUGIN_HkIServerImpl_SetInterfaceState,
	PLUGIN_HkIServerImpl_SetInterfaceState_AFTER,
	PLUGIN_HkIServerImpl_SetManeuver,
	PLUGIN_HkIServerImpl_SetManeuver_AFTER,
	PLUGIN_HkIServerImpl_SetTarget,
	PLUGIN_HkIServerImpl_SetTarget_AFTER,
	PLUGIN_HkIServerImpl_SetTradeMoney,
	PLUGIN_HkIServerImpl_SetTradeMoney_AFTER,
	PLUGIN_HkIServerImpl_SetVisitedState,
	PLUGIN_HkIServerImpl_SetVisitedState_AFTER,
	PLUGIN_HkIServerImpl_SetWeaponGroup,
	PLUGIN_HkIServerImpl_SetWeaponGroup_AFTER,
	PLUGIN_HkIServerImpl_Shutdown,
	PLUGIN_HkIServerImpl_Startup,
	PLUGIN_HkIServerImpl_Startup_AFTER,
	PLUGIN_HkIServerImpl_StopTradeRequest,
	PLUGIN_HkIServerImpl_StopTradeRequest_AFTER,
	PLUGIN_HkIServerImpl_TractorObjects,
	PLUGIN_HkIServerImpl_TractorObjects_AFTER,
	PLUGIN_HkIServerImpl_TradeResponse,
	PLUGIN_HkIServerImpl_TradeResponse_AFTER,
	PLUGIN_ClearClientInfo,
	PLUGIN_LoadUserCharSettings,
	PLUGIN_HkCb_SendChat,
	PLUGIN_ExplosionHit,
	PLUGIN_HkCb_AddDmgEntry,
	PLUGIN_HkCb_AddDmgEntry_AFTER,
	PLUGIN_ShipHullDmg,
	PLUGIN_AllowPlayerDamage,
	PLUGIN_SendDeathMsg,
	PLUGIN_ShipDestroyed,
	PLUGIN_BaseDestroyed,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATESHIP,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATESHIP_AFTER,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATELOOT,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATELOOT_AFTER,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATESOLAR,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_LAUNCH,

	PLUGIN_HkIClientImpl_Send_FLPACKET_COMMON_UPDATEOBJECT,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_ACTIVATEOBJECT,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_DESTROYOBJECT,
	PLUGIN_HkIClientImpl_Send_FLPACKET_COMMON_FIREWEAPON,
	PLUGIN_HkIClientImpl_Send_FLPACKET_COMMON_ACTIVATEEQUIP,
	PLUGIN_HkIClientImpl_Send_FLPACKET_COMMON_ACTIVATECRUISE,
	PLUGIN_HkIClientImpl_Send_FLPACKET_COMMON_ACTIVATETHRUSTERS,

	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_MISCOBJUPDATE_3,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_MISCOBJUPDATE_3_AFTER,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_MISCOBJUPDATE_5,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_REQUESTCREATESHIPRESP,
	PLUGIN_HkIEngine_CShip_init,
	PLUGIN_HkIEngine_CShip_destroy,
	PLUGIN_HkCb_Update_Time,
	PLUGIN_HkCb_Update_Time_AFTER,
	PLUGIN_HkCb_Dock_Call,
	PLUGIN_HkCb_Dock_Call_AFTER,
	PLUGIN_HkCb_Elapse_Time,
	PLUGIN_HkCb_Elapse_Time_AFTER,
	PLUGIN_LaunchPosHook,
	PLUGIN_HkTimerCheckKick,
	PLUGIN_HkTimerNPCAndF1Check,
	PLUGIN_UserCmd_Help,
	PLUGIN_UserCmd_Process,
	PLUGIN_CmdHelp_Callback,
	PLUGIN_ExecuteCommandString_Callback,
	PLUGIN_ProcessEvent_BEFORE,
	PLUGIN_LoadSettings,
	PLUGIN_Plugin_Communication,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATEGUIDED, // adding here to avoid breaking private plugins due to enum mismatch, can be moved in case of global plugin recompile
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SYSTEM_SWITCH_OUT,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_LAND,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SETSHIPARCH,
	PLUGIN_DelayedDisconnect,
	PLUGIN_ShipColGrpDestroyed,
	PLUGIN_SolarColGrpDestroyed,
	PLUGIN_SolarHullDmg,
	PLUGIN_GuidedDestroyed,
	PLUGIN_MineDestroyed,
	PLUGIN_HKIServerImpl_PopUpDialog,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATESHIP_PLAYER,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SETEQUIPMENT,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SETHULLSTATUS,
	PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SETCOLLISIONGROUPS,
	PLUGIN_HkIEngine_CGuided_init,
	PLUGIN_HkIEngine_SetReputation,
	PLUGIN_HkIEngine_SendComm,
	PLUGIN_ShipShieldDmg,
	PLUGIN_ServerCrash,
	PLUGIN_CALLBACKS_AMOUNT,
};

struct PLUGIN_HOOKINFO
{
	PLUGIN_HOOKINFO(FARPROC* pFunc, PLUGIN_CALLBACKS eCallbackID, int iPriority)
	{
		this->pFunc = pFunc;
		this->eCallbackID = eCallbackID;
		this->iPriority = iPriority;
	}

	FARPROC* pFunc;
	PLUGIN_CALLBACKS eCallbackID;
	int iPriority;
};

struct PLUGIN_INFO
{
	string sName;
	string sShortName;
	bool bMayPause;
	bool bMayUnload;
	PLUGIN_RETURNCODE* ePluginReturnCode;
	list<PLUGIN_HOOKINFO> lstHooks;
};

enum PLUGIN_MESSAGE
{
	DEFAULT_MESSAGE = 0,
	CONDATA_EXCEPTION = 10,
	CONDATA_DATA = 11,
	TEMPBAN_BAN = 20,
	ANTICHEAT_TELEPORT = 30,
	ANTICHEAT_CHEATER = 31,
	DSACE_CHANGE_INFOCARD = 40,
	DSACE_SPEED_EXCEPTION = 41,
	CUSTOM_BASE_BEAM = 42,
	CUSTOM_BASE_IS_DOCKED = 43,
	CLIENT_CLOAK_INFO = 44,
	COMBAT_DAMAGE_OVERRIDE = 45,
	CUSTOM_BASE_LAST_DOCKED = 46,
	CUSTOM_JUMP = 47,
	CUSTOM_REVERSE_TRANSACTION = 48,
	CUSTOM_JUMP_CALLOUT = 49,
	CUSTOM_IS_IT_POB = 50,
	CUSTOM_SPAWN_SOLAR = 52,
	CUSTOM_MOBILE_DOCK_CHECK = 53,
	CUSTOM_IN_WARP_CHECK = 54,
	CUSTOM_DESPAWN_SOLAR = 55,
	CUSTOM_CLOAK_CHECK = 56,
	CUSTOM_RENAME_NOTIFICATION = 57,
	CUSTOM_RESTART_NOTIFICATION = 58,
	CUSTOM_CLOAK_ALERT = 60,
	CUSTOM_POB_DOCK_ALERT = 61,
	CUSTOM_SHIELD_STATE_CHANGE = 62,
	CUSTOM_EVENT_ECON_UPDATE = 63,
	CUSTOM_POB_EVENT_NOTIFICATION_INIT = 64,
	CUSTOM_POB_EVENT_NOTIFICATION_BUY = 65,
	CUSTOM_POB_EVENT_NOTIFICATION_SELL = 66,
	CUSTOM_POPUP_INIT = 67,
	CUSTOM_AUTOBUY_CART = 68,
	CUSTOM_BEAM_LAST_BASE = 69,
	CUSTOM_CHECK_POB_SRP_ACCESS = 70,
	CUSTOM_CHECK_EQUIP_VOLUME = 71,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// message structs

struct ANTICHEAT_TELEPORT_STRUCT // in
{
	uint iClientID;
	Vector vNewPos;
	Matrix mNewOrient;
};

enum ANTICHEAT_CHEAT_CODE
{
	AC_CODE_POWER,
	AC_CODE_TIMER,
	AC_CODE_SPEED,
	AC_CODE_MINING,
};

struct ANTICHEAT_CHEATER_STRUCT
{
	uint iClientID;
	wstring wscCharname;
	ANTICHEAT_CHEAT_CODE CheatCode;
	wstring wscLog;
	bool bKilled;
};

struct CONDATA_DATA_STRUCT
{
	uint		iClientID; // in
	uint		iAveragePing; // out
	uint		iAverageLoss; // out
	uint		iPingFluctuation; // out
	uint		iLags; // out
};

struct	CONDATA_EXCEPTION_STRUCT // in
{
	uint iClientID;
	bool bException;
	string sReason;
};

struct	TEMPBAN_BAN_STRUCT // in
{
	uint iClientID;
	uint iDuration;
};

struct DSACE_CHANGE_INFOCARD_STRUCT
{
	uint iClientID;
	uint ids;
	wstring text;
};

struct DSACE_SPEED_EXCEPTION_STRUCT
{
	uint iClientID;
};

struct CUSTOM_BASE_BEAM_STRUCT
{
	uint iClientID;
	uint iTargetBaseID;
	bool bBeamed = false;
};

struct CUSTOM_BASE_IS_DOCKED_STRUCT
{
	uint iClientID;
	uint iDockedBaseID = 0;
};

struct CUSTOM_BASE_IS_IT_POB_STRUCT
{
	uint iBase;
	bool bAnswer = false;
};

struct CLIENT_CLOAK_STRUCT
{
	uint iClientID;
	bool isChargingCloak = false;
	bool isCloaked = false;
};

struct COMBAT_DAMAGE_OVERRIDE_STRUCT
{
	uint iMunitionID;
	uint iTargetTypeID;
	float fDamageMultiplier = 1.0;
};

const enum JUMP_TYPE {
	BEAM_JUMP,
	JUMPGATE_HOLE_JUMP
};

struct CUSTOM_JUMP_STRUCT
{
	uint iShipID;
	uint iSystemID;
};

struct CUSTOM_JUMP_CALLOUT_STRUCT
{
	uint iClientID;
	uint iSystemID;
	Vector pos;
	Matrix ori;
	JUMP_TYPE jumpType = BEAM_JUMP;
};

struct CUSTOM_REVERSE_TRANSACTION_STRUCT
{
	uint iClientID;
};


struct SPAWN_SOLAR_STRUCT
{
	uint solarArchetypeId = 0;
	uint loadoutArchetypeId = 0;
	string nickname;
	uint solar_ids = 0;
	wstring overwrittenName = L"";
	Vector pos;
	Matrix ori;
	uint iSystemId = 0;
	uint iSpaceObjId = 0;
	uint destSystem = 0;
	uint destObj = 0;
	uint affiliation = 0;
	float percentageHp = 1.0f;
};
struct CUSTOM_MOBILE_DOCK_CHECK_STRUCT
{
	uint iClientID;
	bool isMobileDocked = false;
};

struct LAST_PLAYER_BASE_NAME_STRUCT
{
	uint clientID;
	wstring lastBaseName;
};

struct CUSTOM_IN_WARP_CHECK_STRUCT
{
	uint clientId;
	bool inWarp = false;

};

struct DESPAWN_SOLAR_STRUCT
{
	uint spaceObjId;
	DestroyType destroyType;
};

struct CUSTOM_CLOAK_ALERT_STRUCT
{
	vector<uint> alertedGroupMembers;
};

struct CUSTOM_CLOAK_CHECK_STRUCT
{
	uint clientId;
	bool isCloaked = false;
};

struct CUSTOM_RENAME_NOTIFICATION_STRUCT
{
	wstring currentName;
};

struct CUSTOM_RESTART_NOTIFICATION_STRUCT
{
	wstring playerName;
};

struct CUSTOM_POB_DOCK_ALERT_STRUCT
{
	uint client;
	float range;
	wstring* msg;
};

enum class ShieldSource
{
	UNSET,
	CLOAK,
	MISC
};

struct CUSTOM_SHIELD_CHANGE_STATE_STRUCT
{
	uint client;
	bool newState;
	bool success = false;
	ShieldSource source;
};

struct CUSTOM_POB_EVENT_NOTIFICATION_INIT_STRUCT
{
	unordered_map<uint, unordered_set<uint>> data;
};

struct CUSTOM_POB_EVENT_NOTIFICATION_BUY_STRUCT
{
	uint clientId;
	SGFGoodBuyInfo info;
};

struct CUSTOM_POB_EVENT_NOTIFICATION_SELL_STRUCT
{
	uint clientId;
	SGFGoodSellInfo info;
};

struct AUTOBUY_CARTITEM
{
	uint iArchID;
	int iCount;
	wstring wscDescription;
};

struct CUSTOM_AUTOBUY_CARTITEMS
{
	list<AUTOBUY_CARTITEM> cartItems;
	uint clientId;
	int remHoldSize;
};

struct POB_SRP_ACCESS_STRUCT
{
	uint baseId;
	uint clientId;
	bool dockAllowed = false;
};

struct EQUIP_VOLUME_STRUCT
{
	uint shipArch = 0;
	uint equipArch = 0;
	float volume;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif
