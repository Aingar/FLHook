#include "wildcards.hh"
#include "hook.h"
#include "CInGame.h"
#include <memory>

#ifdef SERVER_DEBUG_LOGGING
#define ISERVER_LOG() if(set_bDebug) AddDebugLog(__FUNCSIG__);
#define ISERVER_LOGARG_WS(a) if(set_bDebug) AddDebugLog("     " #a ": %s", wstos((const wchar_t*)a).c_str());
#define ISERVER_LOGARG_S(a) if(set_bDebug) AddDebugLog("     " #a ": %s", (const char*)a);
#define ISERVER_LOGARG_UI(a) if(set_bDebug) AddDebugLog("     " #a ": %u", (uint)a);
#define ISERVER_LOGARG_I(a) if(set_bDebug) AddDebugLog("     " #a ": %d", (int)a);
#define ISERVER_LOGARG_H(a) if(set_bDebug) AddDebugLog("     " #a ": 0x%08X", (int)a);
#define ISERVER_LOGARG_F(a) if(set_bDebug) AddDebugLog("     " #a ": %f", (float)a);
#define ISERVER_LOGARG_V(a) if(set_bDebug) AddDebugLog("     " #a ": %f %f %f", (float)a.x, (float)a.y, (float)a.z);
#define ISERVER_LOGARG_Q(a) if(set_bDebug) AddDebugLog("     " #a ": %f %f %f %f", (float)a.x, (float)a.y, (float)a.z, (float)a.w);
#else
#define ISERVER_LOG()
#define ISERVER_LOGARG_WS(a)
#define ISERVER_LOGARG_S(a)
#define ISERVER_LOGARG_UI(a)
#define ISERVER_LOGARG_I(a)
#define ISERVER_LOGARG_H(a)
#define ISERVER_LOGARG_F(a)
#define ISERVER_LOGARG_V(a)
#define ISERVER_LOGARG_Q(a)
#endif

#define EXECUTE_SERVER_CALL(args) \
	{ \
	TRY_HOOK { \
		args; \
	} CATCH_HOOK ({ AddLog("ERROR: Exception in " __FUNCTION__ " on server call"); } ) \
	}

#define EXECUTE_SERVER_CALL_DEBUG(args, clientId, arg) \
	{ \
	TRY_HOOK { \
		args; \
	} CATCH_HOOK({ const wchar_t* playerName = (const wchar_t*)Players.GetActiveCharacterName(clientId);\
		AddLog("ERROR: Exception in " __FUNCTION__ " on server call, charName=%s, arg2=%u", wstos(playerName).c_str(), arg); LOG_EXCEPTION; } \
	)}

#define CHECK_FOR_DISCONNECT \
	{ \
		if (ClientInfo[iClientID].bDisconnected) \
		{ AddLog("ERROR: Ignoring disconnected client in " __FUNCTION__ " id=%u", iClientID); return; }; \
	}

namespace HkIServerImpl
{

	/**************************************************************************************************************
	this is our "main" loop
	**************************************************************************************************************/

	// add timers here
	typedef void(*_TimerFunc)();

	struct TIMER
	{
		_TimerFunc	proc;
		mstime		tmIntervallMS;
		mstime		tmLastCall;
	};

	TIMER Timers[] =
	{
		{ProcessPendingCommands,		500,					0},
		{HkTimerCheckKick,			1000,					0},
		{HkTimerNPCAndF1Check,			100,					0},
	};

	int __stdcall Update(void)
	{
#ifdef HOOK_TIMER_LOGGING
		static auto lastUpdate = std::chrono::high_resolution_clock::now();
#endif
		static bool bFirstTime = true;
		if (bFirstTime)
		{
			FLHookInit();
			bFirstTime = false;
		}

		// call timers
		mstime currTime = timeInMS();
		for (uint i = 0; (i < sizeof(Timers) / sizeof(TIMER)); i++)
		{
			if ((currTime - Timers[i].tmLastCall) >= Timers[i].tmIntervallMS)
			{
				Timers[i].tmLastCall = currTime;
				Timers[i].proc();
			}
		}

		char *pData;
		memcpy(&pData, g_FLServerDataPtr + 0x40, 4);
		memcpy(&g_iServerLoad, pData + 0x204, 4);
		memcpy(&g_iPlayerCount, pData + 0x208, 4);

#ifdef HOOK_TIMER_LOGGING
		if (set_logPerfTimers)
		{
			auto currTime = std::chrono::high_resolution_clock::now();
			AddPerfTimer("serverUpdate %u", std::chrono::duration_cast<std::chrono::microseconds>(currTime - lastUpdate).count());
			lastUpdate = currTime;

			if (set_perfTimerLength < time(0))
			{
				set_logPerfTimers = false;
				set_perfTimerLength = 0;
			}
		}
		if (set_hookPerfTimerLength && set_hookPerfTimerLength < time(0))
		{
			set_hookPerfTimerLength = 0;
			set_perfTimedHookName = "";
		}
#endif

#ifdef CORE_TIMER_LOGGING
		if (set_corePerfTimerLength && set_corePerfTimerLength < time(0))
		{
			set_corePerfTimerLength = 0;
			PrintCorePerf();
		}
#endif

		CALL_PLUGINS(PLUGIN_HkIServerImpl_Update, int, __stdcall, (), ());

		int result = 0;
		EXECUTE_SERVER_CALL(result = Server.Update());
		return result;
	}

	/**************************************************************************************************************
	Chat-Messages are hooked here
	<Parameters>
	cId:       Sender's ClientID
	lP1:       size of rdlReader (used when extracting text from that buffer)
	rdlReader: RenderDisplayList which contains the chat-text
	cIdTo:     recipient's clientid(0x10000 = universe chat else when (cIdTo & 0x10000) = true -> system chat)
	iP2:       ???
	**************************************************************************************************************/

	constexpr unsigned short flufHeader = 0xF10F;
	struct FlufPayload
	{
		bool compressed{};
		char header[4]{};
		std::vector<char> data;

		std::vector<char> ToBytes() const
		{
			std::vector<char> bytes;
			bytes.resize(sizeof(flufHeader) + sizeof(header) + sizeof(compressed) + data.size());
			memcpy_s(bytes.data(), bytes.size(), &flufHeader, sizeof(flufHeader));
			memcpy_s(bytes.data() + sizeof(flufHeader), bytes.size() - sizeof(flufHeader), header, sizeof(header));
			memcpy_s(bytes.data() + sizeof(flufHeader) + sizeof(header), bytes.size() - sizeof(flufHeader) - sizeof(header), &compressed, sizeof(bool));
			memcpy_s(bytes.data() + sizeof(flufHeader) + sizeof(header) + sizeof(compressed),
				bytes.size() - sizeof(flufHeader) - sizeof(header) - sizeof(compressed),
				data.data(),
				data.size());

			return bytes;
		}

		static std::unique_ptr<FlufPayload> FromPayload(char* data, size_t size)
		{
			// Check if enough room for the fluf header and the body, and that the header matches
			if (size < sizeof(flufHeader) + sizeof(header) + sizeof(compressed) + 1 || *reinterpret_cast<ushort*>(data) != flufHeader)
			{
				return {};
			}

			FlufPayload payload;
			memcpy_s(payload.header, sizeof(payload.header), data + sizeof(flufHeader), sizeof(payload.header));
			memcpy_s(&payload.compressed, sizeof(payload.compressed), data + sizeof(flufHeader) + sizeof(header), sizeof(payload.compressed));
			const size_t newSize = size - sizeof(flufHeader) + sizeof(header) + sizeof(compressed);
			payload.data.resize(newSize);
			memcpy_s(payload.data.data(), newSize, data + sizeof(flufHeader) + sizeof(header) + sizeof(compressed), newSize);

			return std::make_unique<FlufPayload>(payload);
		}

		template <typename T>
		static FlufPayload ToPayload(const T& data, const char header[4])
		{
			FlufPayload payload;
			memcpy_s(payload.header, sizeof(payload.header), header, sizeof(header));

			auto msgPack = rfl::msgpack::write(data);

			const size_t maxPossibleSize = ZSTD_compressBound(msgPack.size());
			payload.data.resize(maxPossibleSize);
			const size_t newSize = ZSTD_compress(payload.data.data(), maxPossibleSize, msgPack.data(), msgPack.size(), 6);

			if (newSize > msgPack.size())
			{
				payload.data = msgPack;
				payload.compressed = false;
				return payload;
			}

			payload.compressed = true;
			payload.data.resize(newSize); // Cut down to exact size
			return payload;
		}

	private:
		FlufPayload() = default;
	};


	CInGame admin;
	bool g_bInSubmitChat = false;
	uint g_iTextLen = 0;

	void __stdcall SubmitChat(struct CHAT_ID cId, unsigned long size, void const *rdlReader, struct CHAT_ID cIdTo, int iP2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_I(cId.iID);
		ISERVER_LOGARG_I(cIdTo.iID);

		auto flufPayload = FlufPayload::FromPayload((char*)rdlReader, size);
		if (cIdTo.iID == 0x10000 && flufPayload.get())
		{
			/*
			if (strncmp(flufPayload.get()->header, "fluf", sizeof(flufPayload.get()->header)) == 0)
			{
				ClientId(cidFrom.id).GetData().usingFlufClientHook = true;
			}
			else
			{
				CallPlugins(&Plugin::OnPayloadReceived, ClientId(cidFrom.id), *flufPayload);
			}*/
			return;
		}

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SubmitChat, __stdcall, (struct CHAT_ID cId, unsigned long size, void const *rdlReader, struct CHAT_ID cIdTo, int iP2), (cId, size, rdlReader, cIdTo, iP2));

		TRY_HOOK {

			// Group join/leave commands
			if (cIdTo.iID == 0x10004)
			{
				g_bInSubmitChat = true;
				EXECUTE_SERVER_CALL(Server.SubmitChat(cId, size, rdlReader, cIdTo, iP2));
				g_bInSubmitChat = false;
				return;
			}

			// extract text from rdlReader
			BinaryRDLReader rdl;
			wchar_t wszBuf[1024] = L"";
			uint iRet1;
			rdl.extract_text_from_buffer((unsigned short*)wszBuf, sizeof(wszBuf), iRet1, (const char*)rdlReader, size);
			wstring wscBuf = wszBuf;
			uint iClientID = cId.iID;

			// if this is a message in system chat then convert it to local unless
			// explicitly overriden by the player using /s.
			if (set_bDefaultLocalChat && cIdTo.iID == 0x10001)
			{
				cIdTo.iID = 0x10002;
			}

			// fix flserver commands and change chat to id so that event logging is
			// accurate.
			g_iTextLen = (uint)wscBuf.length();
			if (!wscBuf.find(L"/g "))
			{
				cIdTo.iID = 0x10003;
				g_iTextLen -= 3;
			}
			else if (!wscBuf.find(L"/l "))
			{
				cIdTo.iID = 0x10002;
				g_iTextLen -= 3;
			}
			else if (!wscBuf.find(L"/s "))
			{
				cIdTo.iID = 0x10001;
				g_iTextLen -= 3;
			}
			else if (!wscBuf.find(L"/u "))
			{
				cIdTo.iID = 0x10000;
				g_iTextLen -= 3;
			}
			else if (!wscBuf.find(L"/group "))
			{
				cIdTo.iID = 0x10003;
				g_iTextLen -= 7;
			}
			else if (!wscBuf.find(L"/local "))
			{
				cIdTo.iID = 0x10002;
				g_iTextLen -= 7;
			}
			else if (!wscBuf.find(L"/system "))
			{
				cIdTo.iID = 0x10001;
				g_iTextLen -= 8;
			}
			else if (!wscBuf.find(L"/universe "))
			{
				cIdTo.iID = 0x10000;
				g_iTextLen -= 10;
			}

			ISERVER_LOGARG_WS(wszBuf);
			ISERVER_LOGARG_I(g_iTextLen);

			// check for user cmds
			if (wszBuf[0] == '/' 
			&& UserCmd_Process(iClientID, wscBuf))
				return;

			if (wszBuf[0] == '.')
			{ // flhook admin command
				CAccount *acc = Players.FindAccountFromClientID(iClientID);
				wstring wscAccDirname;

				HkGetAccountDirName(acc, wscAccDirname);
				string scAdminFile = scAcctPath + wstos(wscAccDirname) + "\\flhookadmin.ini";
				WIN32_FIND_DATA fd;
				HANDLE hFind = FindFirstFile(scAdminFile.c_str(), &fd);
				if (hFind != INVALID_HANDLE_VALUE)
				{ // is admin
					FindClose(hFind);
					admin.ReadRights(scAdminFile);
					admin.iClientID = iClientID;
					admin.wscAdminName = (wchar_t*)Players.GetActiveCharacterName(iClientID);
					admin.ExecuteCommandString(wszBuf + 1);
					return;
				}
			}

			// process chat event
			SendChatEvent(iClientID, cIdTo.iID, wscBuf);

			// check if chat should be suppressed
			foreach(set_lstChatSuppress, wstring, i)
			{
				if ((ToLower(wscBuf)).find(ToLower(*i)) == 0)
					return;
			}
		} CATCH_HOOK({})

		// send
		g_bInSubmitChat = true;
		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.SubmitChat(cId, size, rdlReader, cIdTo, iP2));
		LOG_CORE_TIMER_END
		g_bInSubmitChat = false;

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SubmitChat_AFTER, __stdcall, (struct CHAT_ID cId, unsigned long size, void const *rdlReader, struct CHAT_ID cIdTo, int iP2), (cId, size, rdlReader, cIdTo, iP2));
	}

	/**************************************************************************************************************
	Called when player ship was created in space (after undock or login)
	**************************************************************************************************************/

	void __stdcall PlayerLaunch(unsigned int iShip, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iShip);
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

		ClientInfo[iClientID].iShip = iShip;
		ClientInfo[iClientID].iKillsInARow = 0;
		ClientInfo[iClientID].bCruiseActivated = false;
		ClientInfo[iClientID].bThrusterActivated = false;
		ClientInfo[iClientID].bEngineKilled = false;
		ClientInfo[iClientID].bTradelane = false;
		ClientInfo[iClientID].isDocking = false;

		// adjust cash, this is necessary when cash was added while use was in charmenu/had other char selected
		wstring wscCharname = ToLower((wchar_t*)Players.GetActiveCharacterName(iClientID));
		foreach(ClientInfo[iClientID].lstMoneyFix, MONEY_FIX, i)
		{
			if (!(*i).wscCharname.compare(wscCharname))
			{
				HkAddCash(wscCharname, (*i).iAmount);
				ClientInfo[iClientID].lstMoneyFix.remove(*i);
				break;
			}
		}

		ClientInfo[iClientID].playerID = 0;
		ClientInfo[iClientID].playerIDSID = 0;
		for (auto& item : Players[iClientID].equipDescList.equip)
		{
			if (!item.bMounted)
			{
				continue;
			}
			Archetype::Equipment* equip = Archetype::GetEquipment(item.iArchID);
			if (!equip || equip->get_class_type() != Archetype::AClassType::TRACTOR)
			{
				continue;
			}
			ClientInfo[iClientID].playerID = item.iArchID;
			ClientInfo[iClientID].playerIDSID = item.sID;
			break;
		}

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_PlayerLaunch, __stdcall, (unsigned int iShip, unsigned int iClientID), (iShip, iClientID));

		HkIEngine::playerShips.insert(iShip);
		EXECUTE_SERVER_CALL(Server.PlayerLaunch(iShip, iClientID));

		if (!ClientInfo[iClientID].iLastExitedBaseID)
		{
			ClientInfo[iClientID].iLastExitedBaseID = 1;

			// event
			ProcessEvent(L"spawn char=%s id=%d system=%s",
				(wchar_t*)Players.GetActiveCharacterName(iClientID),
				iClientID,
				HkGetPlayerSystem(iClientID).c_str());
		}

		pub::SpaceObj::SetInvincible2(iShip, false, false, 0.0f);

		CShip* playerCship = reinterpret_cast<CShip*>(CObject::Find(iShip, CObject::CSHIP_OBJECT));
		if (!playerCship)
		{
			AddLog("Player %s failed to launch!", wstos((wchar_t*)Players.GetActiveCharacterName(iClientID)));
			return;
		}
		CEScanner* scanner = reinterpret_cast<CEScanner*>(playerCship->equip_manager.FindFirst(Scanner));
		ClientInfo[iClientID].cship = playerCship;
		ClientInfo[iClientID].fRadarRange = scanner->GetRadarRange();
		ClientInfo[iClientID].fRadarRange *= ClientInfo[iClientID].fRadarRange;
		playerCship->Release();

		ClientInfo[iClientID].undockPosition = playerCship->vPos;

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, __stdcall, (unsigned int iShip, unsigned int iClientID), (iShip, iClientID));

	}

	/**************************************************************************************************************
	Called when player fires a weapon
	**************************************************************************************************************/

	void __stdcall FireWeapon(unsigned int iClientID, struct XFireWeaponInfo const &wpn)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

			CALL_PLUGINS_V(PLUGIN_HkIServerImpl_FireWeapon, __stdcall, (unsigned int iClientID, struct XFireWeaponInfo const &wpn), (iClientID, wpn));

		EXECUTE_SERVER_CALL(Server.FireWeapon(iClientID, wpn));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_FireWeapon_AFTER, __stdcall, (unsigned int iClientID, struct XFireWeaponInfo const &wpn), (iClientID, wpn));
	}

	/**************************************************************************************************************
	Called when one player hits a target with a gun
	<Parameters>
	ci:  only figured out where dwTargetShip is ...
	**************************************************************************************************************/

	void __stdcall SPMunitionCollision(SSPMunitionCollisionInfo const & ci, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

		iDmgMunitionID = ci.projectileArchID;

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPMunitionCollision, __stdcall, (SSPMunitionCollisionInfo const & ci, unsigned int iClientID), (ci, iClientID));

		LOG_CORE_TIMER_START
		Server.SPMunitionCollision(ci, iClientID);
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPMunitionCollision_AFTER, __stdcall, (SSPMunitionCollisionInfo const & ci, unsigned int iClientID), (ci, iClientID));
	}

	/**************************************************************************************************************
	Called when player moves his ship
	**************************************************************************************************************/


	void __stdcall SPObjUpdate(struct SSPObjUpdateInfo const &ui, unsigned int iClientID)
	{
		/*ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		ISERVER_LOGARG_UI(ui.iShip);
		ISERVER_LOGARG_V(ui.vPos);
		ISERVER_LOGARG_Q(ui.vDir);
		ISERVER_LOGARG_F(ui.throttle);*/

		CHECK_FOR_DISCONNECT;

		// NAN check
		if (isnan(ui.vPos.x) || isnan(ui.vPos.y) || isnan(ui.vPos.z)
			|| isnan(ui.vDir.x) || isnan(ui.vDir.y) || isnan(ui.vDir.z) || isnan(ui.vDir.w)
			|| isnan(ui.throttle))
		{
			AddLog("ERROR: NAN found in " __FUNCTION__ " for id=%u", iClientID);
			HkKick(Players[iClientID].Account);
			return;
		}

		float n = ui.vDir.w * ui.vDir.w + ui.vDir.x * ui.vDir.x + ui.vDir.y * ui.vDir.y + ui.vDir.z * ui.vDir.z;
		if (n > 1.05f || n < 0.95f)
		{
			//AddLog("ERROR: Non-normalized quaternion found in " __FUNCTION__ " for id=%u, value: %0.4f", iClientID, n);
			//HkKick(Players[iClientID].Account);
			return;
		}

		// Far check
		if (abs(ui.vPos.x) > 1e7f || abs(ui.vPos.y) > 1e7f || abs(ui.vPos.z) > 1e7f)
		{
			AddLog("ERROR: Ship position out of bounds in " __FUNCTION__ " for id=%u", iClientID);
			HkKick(Players[iClientID].Account);
			return;
		}

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPObjUpdate, __stdcall, (struct SSPObjUpdateInfo const &ui, unsigned int iClientID), (ui, iClientID));

		EXECUTE_SERVER_CALL(Server.SPObjUpdate(ui, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPObjUpdate_AFTER, __stdcall, (struct SSPObjUpdateInfo const &ui, unsigned int iClientID), (ui, iClientID));

	}
	/**************************************************************************************************************
	Called when one player collides with a space object
	**************************************************************************************************************/

	void __stdcall SPObjCollision(struct SSPObjCollisionInfo const &ci, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT


			CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPObjCollision, __stdcall, (struct SSPObjCollisionInfo const &ci, unsigned int iClientID), (ci, iClientID));

		EXECUTE_SERVER_CALL(Server.SPObjCollision(ci, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPObjCollision_AFTER, __stdcall, (struct SSPObjCollisionInfo const &ci, unsigned int iClientID), (ci, iClientID));
	}

	/**************************************************************************************************************
	Called when player has undocked and is now ready to fly
	**************************************************************************************************************/

	void __stdcall LaunchComplete(unsigned int iBaseID, unsigned int iShip)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iBaseID);
		ISERVER_LOGARG_UI(iShip);

		TRY_HOOK
		{

			uint iClientID = HkGetClientIDByShip(iShip);
			if (set_iAntiDockKill && iClientID)
			{
				ClientInfo[iClientID].tmProtectedUntil = timeInMS() + set_iAntiDockKill; // save for anti-dockkill
			}

			// event
			ProcessEvent(L"launch char=%s id=%d base=%s system=%s",
				(wchar_t*)Players.GetActiveCharacterName(iClientID),
				iClientID,
				HkGetBaseNickByID(ClientInfo[iClientID].iLastExitedBaseID).c_str(),
				HkGetPlayerSystem(iClientID).c_str());
		} CATCH_HOOK({})

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LaunchComplete, __stdcall, (unsigned int iBaseID, unsigned int iShip), (iBaseID, iShip));

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.LaunchComplete(iBaseID, iShip));
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LaunchComplete_AFTER, __stdcall, (unsigned int iBaseID, unsigned int iShip), (iBaseID, iShip));
	}

	/**************************************************************************************************************
	Called when player selects a character
	**************************************************************************************************************/

	void __stdcall CharacterSelect(struct CHARACTER_ID const & cId, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_S(&cId);
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

		ClientInfo[iClientID].iCharMenuEnterTime = 0;

			CALL_PLUGINS_V(PLUGIN_HkIServerImpl_CharacterSelect, __stdcall, (struct CHARACTER_ID const & cId, unsigned int iClientID), (cId, iClientID));

		wstring wscCharBefore;
		LOG_CORE_TIMER_START
		TRY_HOOK {
			const wchar_t *wszCharname = (wchar_t*)Players.GetActiveCharacterName(iClientID);
			wscCharBefore = wszCharname ? wszCharname : L"";
			ClientInfo[iClientID].iLastExitedBaseID = 0;
			ClientInfo[iClientID].iTradePartner = 0;
			Server.CharacterSelect(cId, iClientID);

			wstring wscCharname = (wchar_t*)Players.GetActiveCharacterName(iClientID);

			if (wscCharBefore != wscCharname) {
				LoadUserCharSettings(iClientID);

				if (set_bUserCmdHelp)
					PrintUserCmdText(iClientID, L"To get a list of available commands, type \"/help\" in chat.");

				// event
				CAccount *acc = Players.FindAccountFromClientID(iClientID);
				wstring wscDir;
				HkGetAccountDirName(acc, wscDir);
				wstring playerIP;
				HkGetPlayerIP(iClientID, playerIP);
				ProcessEvent(L"login char=%s accountdirname=%s id=%d ip=%s",
					wscCharname.c_str(),
					wscDir.c_str(),
					iClientID,
					playerIP.c_str());
			}
		} CATCH_HOOK({
			HkAddKickLog(iClientID, L"Corrupt charfile?");
			HkKick(ARG_CLIENTID(iClientID));
			return;
			})
			LOG_CORE_TIMER_END

		ClientInfo[iClientID].playerID = 0;
		ClientInfo[iClientID].playerIDSID = 0;
		for (auto& eq : Players[iClientID].equipDescList.equip)
		{
			if (!eq.bMounted)
			{
				continue;
			}
			if (strcmp(eq.szHardPoint.value, "BAY") != 0)
			{
				continue;
			}
			Archetype::Equipment* equip = Archetype::GetEquipment(eq.iArchID);
			if (!equip)
			{
				continue;
			}

			if (equip->get_class_type() == Archetype::AClassType::TRACTOR)
			{
				ClientInfo[iClientID].playerID = eq.iArchID;
				ClientInfo[iClientID].playerIDSID = eq.sID;
				break;
			}
		}

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_CharacterSelect_AFTER, __stdcall, (struct CHARACTER_ID const & cId, unsigned int iClientID), (cId, iClientID));
	}

	/**************************************************************************************************************
	Called when player enters base
	**************************************************************************************************************/

	void __stdcall BaseEnter(unsigned int iBaseID, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iBaseID);
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

		if (ClientInfo[iClientID].cship)
		{
			HkIEngine::playerShips.erase(ClientInfo[iClientID].cship->id);
		}
		ClientInfo[iClientID].cship = nullptr;

		//Resync shadow equip desc to avoid deallocation issues.
		Players[iClientID].lShadowEquipDescList = Players[iClientID].equipDescList;

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_BaseEnter, __stdcall, (unsigned int iBaseID, unsigned int iClientID), (iBaseID, iClientID));

		ClientInfo[iClientID].isDocking = false;

		LOG_CORE_TIMER_START
		TRY_HOOK {
			Server.BaseEnter(iBaseID, iClientID);
		} CATCH_HOOK({ 
			string playerName = wstos((const wchar_t*)Players.GetActiveCharacterName(iClientID));
			AddLog("BaseEnterException: %x %s",iBaseID, playerName.c_str());  AddLog("ERROR: Exception in " __FUNCTION__ " on server call"); })
		LOG_CORE_TIMER_END

		TRY_HOOK {
			// adjust cash, this is necessary when cash was added while use was in charmenu/had other char selected
			wstring wscCharname = ToLower((wchar_t*)Players.GetActiveCharacterName(iClientID));
			foreach(ClientInfo[iClientID].lstMoneyFix, MONEY_FIX, i)
			{
				if (!(*i).wscCharname.compare(wscCharname))
				{
					HkAddCash(wscCharname, (*i).iAmount);
					ClientInfo[iClientID].lstMoneyFix.remove(*i);
					break;
				}
			}

			// anti base-idle
			ClientInfo[iClientID].iBaseEnterTime = (uint)time(0);

			// event
			ProcessEvent(L"baseenter char=%s id=%d base=%s system=%s",
				(wchar_t*)Players.GetActiveCharacterName(iClientID),
				iClientID,
				HkGetBaseNickByID(iBaseID).c_str(),
				HkGetPlayerSystem(iClientID).c_str());
		} CATCH_HOOK({})

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_BaseEnter_AFTER, __stdcall, (unsigned int iBaseID, unsigned int iClientID), (iBaseID, iClientID));
	}

	/**************************************************************************************************************
	Called when player exits base
	**************************************************************************************************************/

	void __stdcall BaseExit(unsigned int iBaseID, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iBaseID);
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

		ClientInfo[iClientID].iBaseEnterTime = 0;
		ClientInfo[iClientID].iLastExitedBaseID = iBaseID;

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_BaseExit, __stdcall, (unsigned int iBaseID, unsigned int iClientID), (iBaseID, iClientID));

		EXECUTE_SERVER_CALL(Server.BaseExit(iBaseID, iClientID));

		const wchar_t *wszCharname = (wchar_t*)Players.GetActiveCharacterName(iClientID);

		// event
		ProcessEvent(L"baseexit char=%s id=%d base=%s system=%s",
			(wchar_t*)Players.GetActiveCharacterName(iClientID),
			iClientID,
			HkGetBaseNickByID(iBaseID).c_str(),
			HkGetPlayerSystem(iClientID).c_str());

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_BaseExit_AFTER, __stdcall, (unsigned int iBaseID, unsigned int iClientID), (iBaseID, iClientID));
	}
	/**************************************************************************************************************
	Called when player connects
	**************************************************************************************************************/

	unordered_set<wstring> connectingPlayerIPs;
	void __stdcall OnConnect(unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		wstring wscIP;
		HkGetPlayerIP(iClientID, wscIP);
		if (connectingPlayerIPs.count(wscIP))
		{
			AddLog("potential dos attempt: %s", wstos(wscIP).c_str());
			return;
		}

		TRY_HOOK {
			// If ID is too high due to disconnect buffer time then manually drop the connection.
			if (iClientID > MAX_CLIENT_ID)
			{
				AddLog("INFO: Blocking connect in " __FUNCTION__ " due to invalid id, id=%u", iClientID);
				CDPClientProxy *cdpClient = g_cClientProxyArray[iClientID - 1];
				if (!cdpClient)
					return;
				cdpClient->Disconnect();
				return;
			}

			// If this client is in the anti-F1 timeout then force the disconnect.
			if (ClientInfo[iClientID].tmF1TimeDisconnect > timeInMS())
			{
				// manual disconnect
				CDPClientProxy *cdpClient = g_cClientProxyArray[iClientID - 1];
				if (!cdpClient)
					return;
				cdpClient->Disconnect();
				return;
			}

			ClientInfo[iClientID].iConnects++;
			ClearClientInfo(iClientID);
		} CATCH_HOOK({})


		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_OnConnect, __stdcall, (unsigned int iClientID), (iClientID));

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.OnConnect(iClientID));
		LOG_CORE_TIMER_END

		connectingPlayerIPs.insert(wscIP);
		ClientInfo[iClientID].IP = wscIP;
		// event
		ProcessEvent(L"connect id=%d ip=%s",
			iClientID,
			wscIP.c_str());

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_OnConnect_AFTER, __stdcall, (unsigned int iClientID), (iClientID));
	}

	/**************************************************************************************************************
	Called when player disconnects
	**************************************************************************************************************/

	void __stdcall DisConnect(unsigned int iClientID, enum EFLConnection p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		ISERVER_LOGARG_UI(p2);

		connectingPlayerIPs.erase(ClientInfo[iClientID].IP);

		if (Players[iClientID].iMissionID)
		{
			Server.AbortMission(iClientID, 0);
			Players[iClientID].iMissionID = 0;
			Players[iClientID].iMissionSetBy = 0;
		}

		wstring wscCharname;
		TRY_HOOK
		{
			if (!ClientInfo[iClientID].bDisconnected)
			{
				ClientInfo[iClientID].bDisconnected = true;
				ClientInfo[iClientID].lstMoneyFix.clear();
				ClientInfo[iClientID].iTradePartner = 0;

				// event
				const wchar_t* wszCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
				if (wszCharname)
					wscCharname = wszCharname;
				ProcessEvent(L"disconnect char=%s id=%d", wscCharname.c_str(), iClientID);

				CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DisConnect, __stdcall, (unsigned int iClientID, enum EFLConnection p2), (iClientID, p2));
				LOG_CORE_TIMER_START
				EXECUTE_SERVER_CALL(Server.DisConnect(iClientID, p2));
				LOG_CORE_TIMER_END
				CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DisConnect_AFTER, __stdcall, (unsigned int iClientID, enum EFLConnection p2), (iClientID, p2));
			}
		} CATCH_HOOK({ AddLog("ERROR: Exception in " __FUNCTION__ "@loc2 charname=%s iClientID=%u", wstos(wscCharname).c_str(), iClientID); })
	}

	/**************************************************************************************************************
	Called when trade is being terminated
	**************************************************************************************************************/

	void __stdcall TerminateTrade(unsigned int iClientID, int iAccepted)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		ISERVER_LOGARG_I(iAccepted);

		CHECK_FOR_DISCONNECT

			CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TerminateTrade, __stdcall, (unsigned int iClientID, int iAccepted), (iClientID, iAccepted));

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.TerminateTrade(iClientID, iAccepted));
		LOG_CORE_TIMER_END


			if (ClientInfo[iClientID].iTradePartner)
				ClientInfo[ClientInfo[iClientID].iTradePartner].iTradePartner = 0;
			ClientInfo[iClientID].iTradePartner = 0;

		

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TerminateTrade_AFTER, __stdcall, (unsigned int iClientID, int iAccepted), (iClientID, iAccepted));
	}

	/**************************************************************************************************************
	Called when new trade request
	**************************************************************************************************************/

	void __stdcall InitiateTrade(unsigned int iClientID1, unsigned int iClientID2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID1);
		ISERVER_LOGARG_UI(iClientID2);

		// save traders client-ids
		ClientInfo[iClientID1].iTradePartner = iClientID2;
		ClientInfo[iClientID2].iTradePartner = iClientID1;

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_InitiateTrade, __stdcall, (unsigned int iClientID1, unsigned int iClientID2), (iClientID1, iClientID2));

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.InitiateTrade(iClientID1, iClientID2));
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_InitiateTrade_AFTER, __stdcall, (unsigned int iClientID1, unsigned int iClientID2), (iClientID1, iClientID2));
	}

	/**************************************************************************************************************
	Called when equipment is being activated/disabled
	**************************************************************************************************************/

	void __stdcall ActivateEquip(unsigned int iClientID, XActivateEquip const &aq)
	{
		CHECK_FOR_DISCONNECT

		TRY_HOOK{

			CShip* cship = ClientInfo[iClientID].cship;
			if (cship && cship->objectClass == CObject::CSHIP_OBJECT)
			{
				CEquip* equip = cship->equip_manager.FindByID(aq.sID);
				if (equip && equip->CEquipType == Engine)
				{
					ClientInfo[iClientID].bEngineKilled = !aq.bActivate;
					if (!aq.bActivate)
						ClientInfo[iClientID].bCruiseActivated = false; // enginekill enabled
				}
			}
		} CATCH_HOOK({ 
			ConPrint(L"ActivateEquipException: %u, %u, %d\n", iClientID, Players[iClientID].iShipID, aq.sID); 
			auto eq = Players[iClientID].equipDescList.find_equipment_item(aq.sID);
			if (eq)
			{
				ConPrint(L"Activate %u\n", eq->iArchID);
			}

			})

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateEquip, __stdcall, (unsigned int iClientID, struct XActivateEquip const &aq), (iClientID, aq));

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.ActivateEquip(iClientID, aq));
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateEquip_AFTER, __stdcall, (unsigned int iClientID, struct XActivateEquip const &aq), (iClientID, aq));
	}

	/**************************************************************************************************************
	Called when cruise engine is being activated/disabled
	**************************************************************************************************************/

	void __stdcall ActivateCruise(unsigned int iClientID, struct XActivateCruise const &ac)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

		ClientInfo[iClientID].bCruiseActivated = ac.bActivate;
		if (ac.bActivate)
		{
			CShip* cship = ClientInfo[iClientID].cship;
			if (cship)
			{
				CEquip* equip;
				CEquipTraverser tr(Engine);
				while (equip = cship->equip_manager.Traverse(tr))
				{
					equip->Activate(true);
				}
			}
		}
		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateCruise, __stdcall, (unsigned int iClientID, struct XActivateCruise const &ac), (iClientID, ac));

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.ActivateCruise(iClientID, ac));
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateCruise_AFTER, __stdcall, (unsigned int iClientID, struct XActivateCruise const &ac), (iClientID, ac));
	}

	/**************************************************************************************************************
	Called when thruster is being activated/disabled
	**************************************************************************************************************/

	void __stdcall ActivateThrusters(unsigned int iClientID, struct XActivateThrusters const &at)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

		ClientInfo[iClientID].bThrusterActivated = at.bActivate;

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateThrusters, __stdcall, (unsigned int iClientID, struct XActivateThrusters const &at), (iClientID, at));

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.ActivateThrusters(iClientID, at));
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateThrusters_AFTER, __stdcall, (unsigned int iClientID, struct XActivateThrusters const &at), (iClientID, at));

	}

	/**************************************************************************************************************
	Called when player sells good on a base
	**************************************************************************************************************/

	void __stdcall GFGoodSell(struct SGFGoodSellInfo const &gsi, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

			TRY_HOOK {
			// anti-cheat check
			list <CARGO_INFO> lstCargo;
			int iHold;
			HkEnumCargo(ARG_CLIENTID(iClientID), lstCargo, iHold);
			bool bLegalSell = false;
			foreach(lstCargo, CARGO_INFO, it)
			{
				if ((*it).iArchID == gsi.iArchID)
				{
					bLegalSell = true;
					if (abs(gsi.iCount) > it->iCount)
					{
						wchar_t wszBuf[1000];

						const wchar_t *wszCharname = (wchar_t*)Players.GetActiveCharacterName(iClientID);
						swprintf(wszBuf, L"Sold more good than possible item=%08x count=%u", gsi.iArchID, gsi.iCount);
						HkAddCheaterLog(wszCharname, wszBuf);

						swprintf(wszBuf, L"Possible cheating detected (%s)", wszCharname);
						HkMsgU(wszBuf);
						HkBan(ARG_CLIENTID(iClientID), true);
						HkKick(ARG_CLIENTID(iClientID));
						return;
					}
					break;
				}
			}
			if (!bLegalSell)
			{
				wchar_t wszBuf[1000];
				const wchar_t *wszCharname = (wchar_t*)Players.GetActiveCharacterName(iClientID);
				swprintf(wszBuf, L"Sold good player does not have (buggy test), item=%08x", gsi.iArchID);
				HkAddCheaterLog(wszCharname, wszBuf);

				//swprintf(wszBuf, L"Possible cheating detected (%s)", wszCharname);
				//HkMsgU(wszBuf);
				//HkBan(ARG_CLIENTID(iClientID), true);
				//HkKick(ARG_CLIENTID(iClientID));
				return;
			}
		}
		CATCH_HOOK ({ AddLog("Exception in %s (iClientID=%u (%x))", __FUNCTION__, iClientID, Players.GetActiveCharacterName(iClientID)); } )

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodSell, __stdcall, (struct SGFGoodSellInfo const &gsi, unsigned int iClientID), (gsi, iClientID));

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.GFGoodSell(gsi, iClientID));
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodSell_AFTER, __stdcall, (struct SGFGoodSellInfo const &gsi, unsigned int iClientID), (gsi, iClientID));
	}

	/**************************************************************************************************************
	Called when player connects or pushes f1
	**************************************************************************************************************/

	void __stdcall CharacterInfoReq(unsigned int iClientID, bool p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		ISERVER_LOGARG_UI(p2);

		if (Players[iClientID].iMissionID)
		{
			Server.AbortMission(iClientID, 0);
			Players[iClientID].iMissionID = 0;
			Players[iClientID].iMissionSetBy = 0;
		}

		CHECK_FOR_DISCONNECT

			CALL_PLUGINS_V(PLUGIN_HkIServerImpl_CharacterInfoReq, __stdcall, (unsigned int iClientID, bool p2), (iClientID, p2));

		try {
			if (!ClientInfo[iClientID].bCharSelected)
				ClientInfo[iClientID].bCharSelected = true;
			else { // pushed f1
				uint iShip = 0;
				pub::Player::GetShip(iClientID, iShip);
				if (iShip)
				{ // in space
					ClientInfo[iClientID].tmF1Time = timeInMS() + set_iAntiF1;
					return;
				}
			}

			Server.CharacterInfoReq(iClientID, p2);
		}
		catch (...) { // something is wrong with charfile
			HkAddKickLog(iClientID, L"Corrupt charfile?");
			HkKick(ARG_CLIENTID(iClientID));
			return;
		}

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_CharacterInfoReq_AFTER, __stdcall, (unsigned int iClientID, bool p2), (iClientID, p2));
	}

	/**************************************************************************************************************
	Called when player jumps in system
	**************************************************************************************************************/

	void __stdcall JumpInComplete(unsigned int iSystemID, unsigned int iShip)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iSystemID);
		ISERVER_LOGARG_UI(iShip);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_JumpInComplete, __stdcall, (unsigned int iSystemID, unsigned int iShip), (iSystemID, iShip));

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.JumpInComplete(iSystemID, iShip));
		LOG_CORE_TIMER_END

		TRY_HOOK {
			uint iClientID = HkGetClientIDByShip(iShip);
			if (!iClientID)
				return;

			// event
			ProcessEvent(L"jumpin char=%s id=%d system=%s",
				(wchar_t*)Players.GetActiveCharacterName(iClientID),
				iClientID,
				HkGetSystemNickByID(iSystemID).c_str());

			ClientInfo[iClientID].undockPosition = ClientInfo[iClientID].cship->vPos;
		} CATCH_HOOK({})



		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_JumpInComplete_AFTER, __stdcall, (unsigned int iSystemID, unsigned int iShip), (iSystemID, iShip));
	}

	/**************************************************************************************************************
	Called when player jumps out of system
	**************************************************************************************************************/

	void __stdcall SystemSwitchOutComplete(unsigned int iShip, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iShip);
		ISERVER_LOGARG_UI(iClientID);

		CHECK_FOR_DISCONNECT

			CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SystemSwitchOutComplete, __stdcall, (unsigned int iShip, unsigned int iClientID), (iShip, iClientID));

		wstring wscSystem = HkGetPlayerSystem(iClientID);

		LOG_CORE_TIMER_START
		EXECUTE_SERVER_CALL(Server.SystemSwitchOutComplete(iShip, iClientID));
		LOG_CORE_TIMER_END

		TRY_HOOK {
			// event
			ProcessEvent(L"switchout char=%s id=%d system=%s",
				(wchar_t*)Players.GetActiveCharacterName(iClientID),
				iClientID,
				wscSystem.c_str());
		} CATCH_HOOK({})

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SystemSwitchOutComplete_AFTER, __stdcall, (unsigned int iShip, unsigned int iClientID), (iShip, iClientID));
	}

	/**************************************************************************************************************
	Called when player logs in
	**************************************************************************************************************/

	void __stdcall Login(struct SLoginInfo const &li, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_WS(&li);
		ISERVER_LOGARG_UI(iClientID);

		if (!connectingPlayerIPs.erase(ClientInfo[iClientID].IP))
		{
			return;
		}

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Login_BEFORE, __stdcall, (struct SLoginInfo const& li, unsigned int iClientID), (li, iClientID));

		LOG_CORE_TIMER_START
		TRY_HOOK {

			Server.Login(li, iClientID);

			if (iClientID > MAX_CLIENT_ID)
				return; // lalala DisconnectDelay bug

			if (!HkIsValidClientID(iClientID))
				return; // player was kicked

			// Kick the player if the account ID doesn't exist. This is caused
			// by a duplicate log on.
			CAccount *acc = Players.FindAccountFromClientID(iClientID);
			if (acc && !acc->wszAccID)
			{
				acc->ForceLogout();
				return;
			}

			LoadUserSettings(iClientID);


		} CATCH_HOOK({
			CAccount *acc = Players.FindAccountFromClientID(iClientID);
			if (acc)
			{
				acc->ForceLogout();
			}
		})
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Login, __stdcall, (struct SLoginInfo const& li, unsigned int iClientID), (li, iClientID));
	}

	/**************************************************************************************************************
	Called on item spawn
	**************************************************************************************************************/


	void __stdcall MineAsteroid(unsigned int p1, class Vector const &vPos, unsigned int iLookID, unsigned int iGoodID, unsigned int iCount, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		//	ISERVER_LOGARG_UI(vPos);
		ISERVER_LOGARG_UI(iLookID);
		ISERVER_LOGARG_UI(iGoodID);
		ISERVER_LOGARG_UI(iCount);
		ISERVER_LOGARG_UI(iClientID);


		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_MineAsteroid, __stdcall, (unsigned int p1, class Vector const &vPos, unsigned int iLookID, unsigned int iGoodID, unsigned int iCount, unsigned int iClientID), (p1, vPos, iLookID, iGoodID, iCount, iClientID));

		EXECUTE_SERVER_CALL(Server.MineAsteroid(p1, vPos, iLookID, iGoodID, iCount, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_MineAsteroid_AFTER, __stdcall, (unsigned int p1, class Vector const &vPos, unsigned int iLookID, unsigned int iGoodID, unsigned int iCount, unsigned int iClientID), (p1, vPos, iLookID, iGoodID, iCount, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall GoTradelane(unsigned int iClientID, struct XGoTradelane const &gtl)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		ClientInfo[iClientID].bTradelane = true;

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GoTradelane, __stdcall, (unsigned int iClientID, struct XGoTradelane const &gtl), (iClientID, gtl));

		LOG_CORE_TIMER_START
		TRY_HOOK
		{
			Server.GoTradelane(iClientID, gtl);
		} CATCH_HOOK(
		{
			uint iSystem;
			pub::Player::GetSystem(iClientID, iSystem);
			AddLog("ERROR: Exception in HkIServerImpl::GoTradelane charname=%s sys=%08x arch=%08x arch2=%08x",
				wstos((const wchar_t*)Players.GetActiveCharacterName(iClientID)).c_str(), iSystem, gtl.iTradelaneSpaceObj1, gtl.iTradelaneSpaceObj2);
		})
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GoTradelane_AFTER, __stdcall, (unsigned int iClientID, struct XGoTradelane const &gtl), (iClientID, gtl));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall StopTradelane(unsigned int iClientID, unsigned int p2, unsigned int p3, unsigned int p4)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		ISERVER_LOGARG_UI(p2);
		ISERVER_LOGARG_UI(p3);
		ISERVER_LOGARG_UI(p4);

		LOG_CORE_TIMER_START
		TRY_HOOK {
			ClientInfo[iClientID].bTradelane = false;
		} CATCH_HOOK({})
		LOG_CORE_TIMER_END

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_StopTradelane, __stdcall, (unsigned int iClientID, unsigned int p2, unsigned int p3, unsigned int p4), (iClientID, p2, p3, p4));

		EXECUTE_SERVER_CALL(Server.StopTradelane(iClientID, p2, p3, p4));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_StopTradelane_AFTER, __stdcall, (unsigned int iClientID, unsigned int p2, unsigned int p3, unsigned int p4), (iClientID, p2, p3, p4));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall AbortMission(unsigned int iClientID, unsigned int p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		ISERVER_LOGARG_UI(p2);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AbortMission, __stdcall, (unsigned int iClientID, unsigned int p2), (iClientID, p2));

		EXECUTE_SERVER_CALL(Server.AbortMission(iClientID, p2));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AbortMission_AFTER, __stdcall, (unsigned int iClientID, unsigned int p2), (iClientID, p2));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall AcceptTrade(unsigned int iClientID, bool p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		ISERVER_LOGARG_UI(p2);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AcceptTrade, __stdcall, (unsigned int iClientID, bool p2), (iClientID, p2));

		EXECUTE_SERVER_CALL(Server.AcceptTrade(iClientID, p2));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AcceptTrade_AFTER, __stdcall, (unsigned int iClientID, bool p2), (iClientID, p2));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall AddTradeEquip(unsigned int iClientID, struct EquipDesc const &ed)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AddTradeEquip, __stdcall, (unsigned int iClientID, struct EquipDesc const &ed), (iClientID, ed));

		EXECUTE_SERVER_CALL(Server.AddTradeEquip(iClientID, ed));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AddTradeEquip_AFTER, __stdcall, (unsigned int iClientID, struct EquipDesc const &ed), (iClientID, ed));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall BaseInfoRequest(unsigned int p1, unsigned int p2, bool p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(p2);
		ISERVER_LOGARG_UI(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_BaseInfoRequest, __stdcall, (unsigned int p1, unsigned int p2, bool p3), (p1, p2, p3));

		EXECUTE_SERVER_CALL(Server.BaseInfoRequest(p1, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_BaseInfoRequest_AFTER, __stdcall, (unsigned int p1, unsigned int p2, bool p3), (p1, p2, p3));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall CharacterSkipAutosave(unsigned int iClientID)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall CommComplete(unsigned int p1, unsigned int p2, unsigned int p3, enum CommResult cr)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall CreateNewCharacter(struct SCreateCharacterInfo const & scci, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_CreateNewCharacter, __stdcall, (struct SCreateCharacterInfo const & scci, unsigned int iClientID), (scci, iClientID));

		EXECUTE_SERVER_CALL(Server.CreateNewCharacter(scci, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_CreateNewCharacter_AFTER, __stdcall, (struct SCreateCharacterInfo const & scci, unsigned int iClientID), (scci, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall DelTradeEquip(unsigned int iClientID, struct EquipDesc const &ed)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DelTradeEquip, __stdcall, (unsigned int iClientID, struct EquipDesc const &ed), (iClientID, ed));

		EXECUTE_SERVER_CALL(Server.DelTradeEquip(iClientID, ed));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DelTradeEquip_AFTER, __stdcall, (unsigned int iClientID, struct EquipDesc const &ed), (iClientID, ed));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall DestroyCharacter(struct CHARACTER_ID const &cId, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		ISERVER_LOGARG_S(&cId);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DestroyCharacter, __stdcall, (struct CHARACTER_ID const &cId, unsigned int iClientID), (cId, iClientID));

		EXECUTE_SERVER_CALL(Server.DestroyCharacter(cId, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DestroyCharacter_AFTER, __stdcall, (struct CHARACTER_ID const &cId, unsigned int iClientID), (cId, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall Dock(unsigned int const &p1, unsigned int const &p2)
	{
		// anticheat - never let the client manually dock somewhere
		return;
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall DumpPacketStats(char const *p1)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ElapseTime(float p1)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall GFGoodBuy(struct SGFGoodBuyInfo const &gbi, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodBuy, __stdcall, (struct SGFGoodBuyInfo const &gbi, unsigned int iClientID), (gbi, iClientID));

		EXECUTE_SERVER_CALL(Server.GFGoodBuy(gbi, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodBuy_AFTER, __stdcall, (struct SGFGoodBuyInfo const &gbi, unsigned int iClientID), (gbi, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall GFGoodVaporized(struct SGFGoodVaporizedInfo const &gvi, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodVaporized, __stdcall, (struct SGFGoodVaporizedInfo const &gvi, unsigned int iClientID), (gvi, iClientID));

		EXECUTE_SERVER_CALL(Server.GFGoodVaporized(gvi, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodVaporized_AFTER, __stdcall, (struct SGFGoodVaporizedInfo const &gvi, unsigned int iClientID), (gvi, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall GFObjSelect(unsigned int p1, unsigned int p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(p2);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFObjSelect, __stdcall, (unsigned int p1, unsigned int p2), (p1, p2));

		EXECUTE_SERVER_CALL(Server.GFObjSelect(p1, p2));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFObjSelect_AFTER, __stdcall, (unsigned int p1, unsigned int p2), (p1, p2));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	unsigned int __stdcall GetServerID(void)
	{
		ISERVER_LOG();

		return Server.GetServerID();
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	char const * __stdcall GetServerSig(void)
	{
		ISERVER_LOG();

		return Server.GetServerSig();
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall GetServerStats(struct ServerStats &ss)
	{
		ISERVER_LOG();

		Server.GetServerStats(ss);
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall Hail(unsigned int p1, unsigned int p2, unsigned int p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(p2);
		ISERVER_LOGARG_UI(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Hail, __stdcall, (unsigned int p1, unsigned int p2, unsigned int p3), (p1, p2, p3));

		EXECUTE_SERVER_CALL(Server.Hail(p1, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Hail_AFTER, __stdcall, (unsigned int p1, unsigned int p2, unsigned int p3), (p1, p2, p3));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall InterfaceItemUsed(unsigned int p1, unsigned int p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(p2);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_InterfaceItemUsed, __stdcall, (unsigned int p1, unsigned int p2), (p1, p2));

		EXECUTE_SERVER_CALL(Server.InterfaceItemUsed(p1, p2));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_InterfaceItemUsed_AFTER, __stdcall, (unsigned int p1, unsigned int p2), (p1, p2));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall JettisonCargo(unsigned int iClientID, struct XJettisonCargo const &jc)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_JettisonCargo, __stdcall, (unsigned int iClientID, struct XJettisonCargo const &jc), (iClientID, jc));

		EXECUTE_SERVER_CALL(Server.JettisonCargo(iClientID, jc));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_JettisonCargo_AFTER, __stdcall, (unsigned int iClientID, struct XJettisonCargo const &jc), (iClientID, jc));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall LocationEnter(unsigned int p1, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationEnter, __stdcall, (unsigned int p1, unsigned int iClientID), (p1, iClientID));

		EXECUTE_SERVER_CALL(Server.LocationEnter(p1, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationEnter_AFTER, __stdcall, (unsigned int p1, unsigned int iClientID), (p1, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall LocationExit(unsigned int p1, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationExit, __stdcall, (unsigned int p1, unsigned int iClientID), (p1, iClientID));

		EXECUTE_SERVER_CALL(Server.LocationExit(p1, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationExit_AFTER, __stdcall, (unsigned int p1, unsigned int iClientID), (p1, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall LocationInfoRequest(unsigned int p1, unsigned int p2, bool p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(p2);
		ISERVER_LOGARG_UI(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationInfoRequest, __stdcall, (unsigned int p1, unsigned int p2, bool p3), (p1, p2, p3));

		EXECUTE_SERVER_CALL(Server.LocationInfoRequest(p1, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationInfoRequest_AFTER, __stdcall, (unsigned int p1, unsigned int p2, bool p3), (p1, p2, p3));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall MissionResponse(unsigned int p1, unsigned long p2, bool p3, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(p2);
		ISERVER_LOGARG_UI(p3);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_MissionResponse, __stdcall, (unsigned int p1, unsigned long p2, bool p3, unsigned int iClientID), (p1, p2, p3, iClientID));

		EXECUTE_SERVER_CALL(Server.MissionResponse(p1, p2, p3, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_MissionResponse_AFTER, __stdcall, (unsigned int p1, unsigned long p2, bool p3, unsigned int iClientID), (p1, p2, p3, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/


	void __stdcall MissionSaveB(unsigned int iClientID, unsigned long p2)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall PopUpDialog(unsigned int iClientID, unsigned int buttonPressed)
	{
		CALL_PLUGINS_V(PLUGIN_HKIServerImpl_PopUpDialog, , (uint, uint), (iClientID, buttonPressed));

		return;
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall RTCDone(unsigned int p1, unsigned int p2)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ReqAddItem(unsigned int goodId, char const *hardpoint, int count, float status, bool mounted, unsigned int clientId)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(goodId);
		//	ISERVER_LOGARG_S(p2);
		ISERVER_LOGARG_UI(count);
		ISERVER_LOGARG_F(status);
		ISERVER_LOGARG_UI(mounted);
		ISERVER_LOGARG_UI(clientId);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqAddItem, __stdcall, (unsigned int& goodId, char const* hardpoint, int count, float status, bool& mounted, unsigned int clientId),
			(goodId, hardpoint, count, status, mounted, clientId));

		EXECUTE_SERVER_CALL(Server.ReqAddItem(goodId, hardpoint, count, status, mounted, clientId));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqAddItem_AFTER, __stdcall, (unsigned int goodId, char const * hardpoint, int count, float status, bool mounted, unsigned int clientId),
			(goodId, hardpoint, count, status, mounted, clientId));

		if (mounted && !ClientInfo[clientId].playerID)
		{
			uint archId = Good2Arch(goodId);
			auto eqArch = Archetype::GetEquipment(archId);
			if (eqArch && eqArch->get_class_type() == Archetype::AClassType::TRACTOR)
			{
				for (auto& equip : Players[clientId].equipDescList.equip)
				{
					if (equip.iArchID != archId)
					{
						continue;
					}

					ClientInfo[clientId].playerID = equip.iArchID;
					ClientInfo[clientId].playerIDSID = equip.sID;
				}
			}
		}
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ReqChangeCash(int p1, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqChangeCash, __stdcall, (int p1, unsigned int iClientID), (p1, iClientID));

		EXECUTE_SERVER_CALL(Server.ReqChangeCash(p1, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqChangeCash_AFTER, __stdcall, (int p1, unsigned int iClientID), (p1, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ReqCollisionGroups(class std::list<struct CollisionGroupDesc, class std::allocator<struct CollisionGroupDesc> > const &p1, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqCollisionGroups, __stdcall, (class std::list<struct CollisionGroupDesc, class std::allocator<struct CollisionGroupDesc> > const &p1, unsigned int iClientID), (p1, iClientID));

		EXECUTE_SERVER_CALL(Server.ReqCollisionGroups(p1, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqCollisionGroups_AFTER, __stdcall, (class std::list<struct CollisionGroupDesc, class std::allocator<struct CollisionGroupDesc> > const &p1, unsigned int iClientID), (p1, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ReqDifficultyScale(float p1, unsigned int iClientID)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ReqEquipment(class EquipDescList const &edl, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqEquipment, __stdcall, (class EquipDescList const &edl, unsigned int iClientID), (edl, iClientID));

		EXECUTE_SERVER_CALL(Server.ReqEquipment(edl, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqEquipment_AFTER, __stdcall, (class EquipDescList const &edl, unsigned int iClientID), (edl, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ReqHullStatus(float p1, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_F(p1);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqHullStatus, __stdcall, (float p1, unsigned int iClientID), (p1, iClientID));

		EXECUTE_SERVER_CALL(Server.ReqHullStatus(p1, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqHullStatus_AFTER, __stdcall, (float p1, unsigned int iClientID), (p1, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/


	void __stdcall ReqModifyItem(ushort sid, char const *hardpoint, int count, float hitPts, bool mounted, uint client)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		//	ISERVER_LOGARG_S(p2);
		ISERVER_LOGARG_I(p3);
		ISERVER_LOGARG_F(p4);
		ISERVER_LOGARG_UI(p5);
		ISERVER_LOGARG_UI(iClientID);

		if (mounted && !ClientInfo[client].playerID)
		{
			for (auto& equip : Players[client].equipDescList.equip)
			{
				if (equip.sID != sid)
				{
					continue;
				}
				if (equip.bMounted)
				{
					break;
				}

				auto eqArch = Archetype::GetEquipment(equip.iArchID);
				if (eqArch && eqArch->get_class_type() == Archetype::AClassType::TRACTOR)
				{
					ClientInfo[client].playerIDSID = equip.sID;
					ClientInfo[client].playerID = equip.iArchID;
				}
			}
		}
		else if(sid == ClientInfo[client].playerIDSID)
		{
			ClientInfo[client].playerIDSID = 0;
			ClientInfo[client].playerID = 0;
		}

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqModifyItem, __stdcall, (ushort, char const*, int, float, bool, uint), (sid, hardpoint, count, hitPts, mounted, client));

		EXECUTE_SERVER_CALL(Server.ReqModifyItem(sid, hardpoint, count, hitPts, mounted, client));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqModifyItem_AFTER, __stdcall, (ushort, char const*, int, float, bool, uint), (sid, hardpoint, count, hitPts, mounted, client));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ReqRemoveItem(unsigned short sid, int count, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_I(p2);
		ISERVER_LOGARG_UI(iClientID);

		if (sid == ClientInfo[iClientID].playerIDSID)
		{
			ClientInfo[iClientID].playerIDSID = 0;
			ClientInfo[iClientID].playerID = 0;
		}

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqRemoveItem, __stdcall, (unsigned short, int, unsigned int iClientID), (sid, count, iClientID));

		EXECUTE_SERVER_CALL(Server.ReqRemoveItem(sid, count, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqRemoveItem_AFTER, __stdcall, (unsigned short, int, unsigned int iClientID), (sid, count, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ReqSetCash(int p1, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_I(p1);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqSetCash, __stdcall, (int p1, unsigned int iClientID), (p1, iClientID));

		EXECUTE_SERVER_CALL(Server.ReqSetCash(p1, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqSetCash_AFTER, __stdcall, (int p1, unsigned int iClientID), (p1, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall ReqShipArch(unsigned int p1, unsigned int p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(p2);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqShipArch, __stdcall, (unsigned int p1, unsigned int p2), (p1, p2));

		EXECUTE_SERVER_CALL(Server.ReqShipArch(p1, p2));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqShipArch_AFTER, __stdcall, (unsigned int p1, unsigned int p2), (p1, p2));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall RequestBestPath(unsigned int p1, unsigned char *p2, int p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		//	ISERVER_LOGARG_S(p2);
		ISERVER_LOGARG_I(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestBestPath, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

		EXECUTE_SERVER_CALL(Server.RequestBestPath(p1, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestBestPath_AFTER, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/


	// Cancel a ship maneuver (goto, dock, formation).
	// p1 = iType? ==0 if docking, ==1 if formation
	void __stdcall RequestCancel(int iType, unsigned int iShip, unsigned int p3, unsigned long p4, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_I(iType);
		ISERVER_LOGARG_UI(iShip);
		ISERVER_LOGARG_UI(p3);
		ISERVER_LOGARG_UI(p4);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestCancel, __stdcall, (int iType, unsigned int iShip, unsigned int p3, unsigned long p4, unsigned int iClientID), (iType, iShip, p3, p4, iClientID));

		EXECUTE_SERVER_CALL(Server.RequestCancel(iType, iShip, p3, p4, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestCancel_AFTER, __stdcall, (int iType, unsigned int iShip, unsigned int p3, unsigned long p4, unsigned int iClientID), (iType, iShip, p3, p4, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall RequestCreateShip(unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestCreateShip, __stdcall, (unsigned int iClientID), (iClientID));

		EXECUTE_SERVER_CALL(Server.RequestCreateShip(iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestCreateShip_AFTER, __stdcall, (unsigned int iClientID), (iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/


	/// Called upon flight maneuver (goto, dock, formation).
	/// p1 = iType? ==0 if docking, ==1 if formation
	/// p2 = iShip of person docking
	/// p3 = iShip of dock/formation target
	/// p4 seems to be 0 all the time
	/// p5 seems to be 0 all the time
	/// p6 = iClientID
	void __stdcall RequestEvent(int iType, unsigned int iShip, unsigned int iShipTarget, unsigned int p4, unsigned long p5, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_I(iType);
		ISERVER_LOGARG_UI(iShip);
		ISERVER_LOGARG_UI(iShipTarget);
		ISERVER_LOGARG_UI(p4);
		ISERVER_LOGARG_UI(p5);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestEvent, __stdcall, (int iType, unsigned int iShip, unsigned int iShipTarget, unsigned int p4, unsigned long p5, unsigned int iClientID), (iType, iShip, iShipTarget, p4, p5, iClientID));

		EXECUTE_SERVER_CALL(Server.RequestEvent(iType, iShip, iShipTarget, p4, p5, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestEvent_AFTER, __stdcall, (int iType, unsigned int iShip, unsigned int iShipTarget, unsigned int p4, unsigned long p5, unsigned int iClientID), (iType, iShip, iShipTarget, p4, p5, iClientID));
		
		//If Dock_Call plugin turns a successful dock into a failed one, we need to cancel the event
		if (HkIEngine::bAbortEventRequest)
		{
			HkIEngine::bAbortEventRequest = false;
			Server.RequestCancel(iType, iShip, 0, UINT_MAX, iClientID);
		}
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall RequestGroupPositions(unsigned int p1, unsigned char *p2, int p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		//	ISERVER_LOGARG_S(p2);
		ISERVER_LOGARG_I(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestGroupPositions, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

		EXECUTE_SERVER_CALL(Server.RequestGroupPositions(p1, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestGroupPositions_AFTER, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall RequestPlayerStats(unsigned int p1, unsigned char *p2, int p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		//	ISERVER_LOGARG_S(p2);
		ISERVER_LOGARG_I(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestPlayerStats, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

		EXECUTE_SERVER_CALL(Server.RequestPlayerStats(p1, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestPlayerStats_AFTER, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall RequestRankLevel(unsigned int p1, unsigned char *p2, int p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		//	ISERVER_LOGARG_S(p2);
		ISERVER_LOGARG_I(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestRankLevel, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

		EXECUTE_SERVER_CALL(Server.RequestRankLevel(p1, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestRankLevel_AFTER, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall RequestTrade(unsigned int p1, unsigned int p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		ISERVER_LOGARG_UI(p2);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestTrade, __stdcall, (unsigned int p1, unsigned int p2), (p1, p2));

		EXECUTE_SERVER_CALL(Server.RequestTrade(p1, p2));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestTrade_AFTER, __stdcall, (unsigned int p1, unsigned int p2), (p1, p2));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SPBadLandsObjCollision(struct SSPBadLandsObjCollisionInfo const &p1, unsigned int iClientID)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	/// Called when ship starts jump gate/hole acceleration but before system switch out.
	void __stdcall SPRequestInvincibility(unsigned int iShip, bool p2, enum InvincibilityReason p3, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iShip);
		ISERVER_LOGARG_UI(p2);
		ISERVER_LOGARG_UI(p3);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPRequestInvincibility, __stdcall, (unsigned int iShip, bool p2, enum InvincibilityReason p3, unsigned int iClientID), (iShip, p2, p3, iClientID));

		EXECUTE_SERVER_CALL(Server.SPRequestInvincibility(iShip, p2, p3, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPRequestInvincibility_AFTER, __stdcall, (unsigned int iShip, bool p2, enum InvincibilityReason p3, unsigned int iClientID), (iShip, p2, p3, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SPRequestUseItem(struct SSPUseItem const &p1, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPRequestUseItem, __stdcall, (struct SSPUseItem const &p1, unsigned int iClientID), (p1, iClientID));

		EXECUTE_SERVER_CALL(Server.SPRequestUseItem(p1, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPRequestUseItem_AFTER, __stdcall, (struct SSPUseItem const &p1, unsigned int iClientID), (p1, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SPScanCargo(unsigned int const &iScanInstigator, unsigned int const &iScanTarget, unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iScanInstigator);
		ISERVER_LOGARG_UI(iScanTarget);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPScanCargo, __stdcall, (unsigned int const &iScanInstigator, unsigned int const &iScanTarget, unsigned int iClientID), (iScanInstigator, iScanTarget, iClientID));

		EXECUTE_SERVER_CALL(Server.SPScanCargo(iScanInstigator, iScanTarget, iClientID));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPScanCargo_AFTER, __stdcall, (unsigned int const &iScanInstigator, unsigned int const &iScanTarget, unsigned int iClientID), (iScanInstigator, iScanTarget, iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SaveGame(struct CHARACTER_ID const &cId, unsigned short const *p2, unsigned int p3)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SetInterfaceState(unsigned int p1, unsigned char *p2, int p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(p1);
		//	ISERVER_LOGARG_S(p2);
		ISERVER_LOGARG_I(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetInterfaceState, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

		EXECUTE_SERVER_CALL(Server.SetInterfaceState(p1, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetInterfaceState_AFTER, __stdcall, (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SetManeuver(unsigned int iClientID, struct XSetManeuver const &p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetManeuver, __stdcall, (unsigned int iClientID, struct XSetManeuver const &p2), (iClientID, p2));

		EXECUTE_SERVER_CALL(Server.SetManeuver(iClientID, p2));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetManeuver_AFTER, __stdcall, (unsigned int iClientID, struct XSetManeuver const &p2), (iClientID, p2));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SetMissionLog(unsigned int iClientID, unsigned char *p2, int p3)
	{
		return; // not used
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SetTarget(unsigned int iClientID, struct XSetTarget const &p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetTarget, __stdcall, (unsigned int iClientID, struct XSetTarget const &p2), (iClientID, p2));

		EXECUTE_SERVER_CALL(Server.SetTarget(iClientID, p2));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetTarget_AFTER, __stdcall, (unsigned int iClientID, struct XSetTarget const &p2), (iClientID, p2));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SetTradeMoney(unsigned int iClientID, unsigned long p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		ISERVER_LOGARG_UI(p2);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetTradeMoney, __stdcall, (unsigned int iClientID, unsigned long p2), (iClientID, p2));

		EXECUTE_SERVER_CALL(Server.SetTradeMoney(iClientID, p2));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetTradeMoney_AFTER, __stdcall, (unsigned int iClientID, unsigned long p2), (iClientID, p2));

	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SetVisitedState(unsigned int iClientID, unsigned char *p2, int p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		//	ISERVER_LOGARG_S(p2);
		ISERVER_LOGARG_I(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetVisitedState, __stdcall, (unsigned int iClientID, unsigned char *p2, int p3), (iClientID, p2, p3));

		EXECUTE_SERVER_CALL(Server.SetVisitedState(iClientID, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetVisitedState_AFTER, __stdcall, (unsigned int iClientID, unsigned char *p2, int p3), (iClientID, p2, p3));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall SetWeaponGroup(unsigned int iClientID, unsigned char *p2, int p3)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);
		//	ISERVER_LOGARG_S(p2);
		ISERVER_LOGARG_I(p3);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetWeaponGroup, __stdcall, (unsigned int iClientID, unsigned char *p2, int p3), (iClientID, p2, p3));

		EXECUTE_SERVER_CALL(Server.SetWeaponGroup(iClientID, p2, p3));

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetWeaponGroup_AFTER, __stdcall, (unsigned int iClientID, unsigned char *p2, int p3), (iClientID, p2, p3));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall Shutdown()
	{
		ISERVER_LOG();

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Shutdown, __stdcall, (), ());

		Server.Shutdown();

		FLHookShutdown();
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	bool __stdcall Startup(struct SStartupInfo const &p1)
	{
		FLHookInit_Pre();

		// The maximum number of players we can support is MAX_CLIENT_ID
		// Add one to the maximum number to allow renames
		int iMaxPlayers = MAX_CLIENT_ID + 1;

		// Startup the server with this number of players.
		char* pAddress = ((char*)hModServer + ADDR_SRV_PLAYERDBMAXPLAYERSPATCH);
		char szNOP[] = { '\x90' };
		char szMOVECX[] = { '\xB9' };
		WriteProcMem(pAddress, szMOVECX, sizeof(szMOVECX));
		WriteProcMem(pAddress + 1, &iMaxPlayers, sizeof(iMaxPlayers));
		WriteProcMem(pAddress + 5, szNOP, sizeof(szNOP));

		CALL_PLUGINS_NORET(PLUGIN_HkIServerImpl_Startup, __stdcall, (struct SStartupInfo const &p1), (p1));

		bool bRet = Server.Startup(p1);

		// Patch to set maximum number of players to connect. This is normally 
		// less than MAX_CLIENT_ID
		pAddress = ((char*)hModServer + ADDR_SRV_PLAYERDBMAXPLAYERS);
		WriteProcMem(pAddress, (void*)&p1.iMaxPlayers, sizeof(iMaxPlayers));

		// read base market data from ini
		HkLoadBaseMarket();

		ISERVER_LOG();

		CALL_PLUGINS_NORET(PLUGIN_HkIServerImpl_Startup_AFTER, __stdcall, (struct SStartupInfo const &p1), (p1));

		return bRet;
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall StopTradeRequest(unsigned int iClientID)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_StopTradeRequest, __stdcall, (unsigned int iClientID), (iClientID));

		Server.StopTradeRequest(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_StopTradeRequest_AFTER, __stdcall, (unsigned int iClientID), (iClientID));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall TractorObjects(unsigned int iClientID, XTractorObjects const &p2)
	{
		ISERVER_LOG();
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TractorObjects, __stdcall, (unsigned int iClientID, struct XTractorObjects const &p2), (iClientID, p2));

		Server.TractorObjects(iClientID, p2);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TractorObjects_AFTER, __stdcall, (unsigned int iClientID, struct XTractorObjects const &p2), (iClientID, p2));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __stdcall TradeResponse(unsigned char const *p1, int p2, unsigned int iClientID)
	{
		ISERVER_LOG();
		///	ISERVER_LOGARG_S(p1);
		ISERVER_LOGARG_I(p2);
		ISERVER_LOGARG_UI(iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TradeResponse, __stdcall, (unsigned char const *p1, int p2, unsigned int iClientID), (p1, p2, iClientID));

		Server.TradeResponse(p1, p2, iClientID);

		CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TradeResponse_AFTER, __stdcall, (unsigned char const *p1, int p2, unsigned int iClientID), (p1, p2, iClientID));
	}

	/**************************************************************************************************************
	IServImpl hook entries
	**************************************************************************************************************/

	HOOKENTRY hookEntries[85] =
	{
		{(FARPROC)SubmitChat,				-0x08, 0},
		{(FARPROC)FireWeapon,				0x000, 0},
		{(FARPROC)ActivateEquip,			0x004, 0},
		{(FARPROC)ActivateCruise,			0x008, 0},
		{(FARPROC)ActivateThrusters,		0x00C, 0},
		{(FARPROC)SetTarget,				0x010, 0},
		{(FARPROC)TractorObjects,			0x014, 0},
		{(FARPROC)GoTradelane,				0x018, 0},
		{(FARPROC)StopTradelane,			0x01C, 0},
		{(FARPROC)JettisonCargo,			0x020, 0},
		{(FARPROC)ElapseTime,				0x030, 0},
		{(FARPROC)DisConnect,				0x040, 0},
		{(FARPROC)OnConnect,				0x044, 0},
		{(FARPROC)Login,					0x048, 0},
		{(FARPROC)CharacterInfoReq,			0x04C, 0},
		{(FARPROC)CharacterSelect,			0x050, 0},
		{(FARPROC)CreateNewCharacter,		0x058, 0},
		{(FARPROC)DestroyCharacter,			0x05C, 0},
		{(FARPROC)CharacterSkipAutosave,	0x060, 0},
		{(FARPROC)ReqShipArch,				0x064, 0},
		{(FARPROC)ReqHullStatus,			0x068, 0},
		{(FARPROC)ReqCollisionGroups,		0x06C, 0},
		{(FARPROC)ReqEquipment,				0x070, 0},
		{(FARPROC)ReqAddItem,				0x078, 0},
		{(FARPROC)ReqRemoveItem,			0x07C, 0},
		{(FARPROC)ReqModifyItem,			0x080, 0},
		{(FARPROC)ReqSetCash,				0x084, 0},
		{(FARPROC)ReqChangeCash,			0x088, 0},
		{(FARPROC)BaseEnter,				0x08C, 0},
		{(FARPROC)BaseExit,					0x090, 0},
		{(FARPROC)LocationEnter,			0x094, 0},
		{(FARPROC)LocationExit,				0x098, 0},
		{(FARPROC)BaseInfoRequest,			0x09C, 0},
		{(FARPROC)LocationInfoRequest,		0x0A0, 0},
		{(FARPROC)GFObjSelect,				0x0A4, 0},
		{(FARPROC)GFGoodVaporized,			0x0A8, 0},
		{(FARPROC)MissionResponse,			0x0AC, 0},
		{(FARPROC)TradeResponse,			0x0B0, 0},
		{(FARPROC)GFGoodBuy,				0x0B4, 0},
		{(FARPROC)GFGoodSell,				0x0B8, 0},
		{(FARPROC)SystemSwitchOutComplete,	0x0BC, 0},
		{(FARPROC)PlayerLaunch,				0x0C0, 0},
		{(FARPROC)LaunchComplete,			0x0C4, 0},
		{(FARPROC)JumpInComplete,			0x0C8, 0},
		{(FARPROC)Hail,						0x0CC, 0},
		{(FARPROC)SPObjUpdate,				0x0D0, 0},
		{(FARPROC)SPMunitionCollision,		0x0D4, 0},
		{(FARPROC)SPBadLandsObjCollision,	0x0D8, 0},
		{(FARPROC)SPObjCollision,			0x0DC, 0},
		{(FARPROC)SPRequestUseItem,			0x0E0, 0},
		{(FARPROC)SPRequestInvincibility,	0x0E4, 0},
		{(FARPROC)SaveGame,					0x0E8, 0},
		{(FARPROC)MissionSaveB,				0x0EC, 0},
		{(FARPROC)RequestEvent,				0x0F0, 0},
		{(FARPROC)RequestCancel,			0x0F4, 0},
		{(FARPROC)MineAsteroid,				0x0F8, 0},
		{(FARPROC)CommComplete,				0x0FC, 0},
		{(FARPROC)RequestCreateShip,		0x100, 0},
		{(FARPROC)SPScanCargo,				0x104, 0},
		{(FARPROC)SetManeuver,				0x108, 0},
		{(FARPROC)InterfaceItemUsed,		0x10C, 0},
		{(FARPROC)AbortMission,				0x110, 0},
		{(FARPROC)RTCDone,					0x114, 0},
		{(FARPROC)SetWeaponGroup,			0x118, 0},
		{(FARPROC)SetVisitedState,			0x11C, 0},
		{(FARPROC)RequestBestPath,			0x120, 0},
		{(FARPROC)RequestPlayerStats,		0x124, 0},
		{(FARPROC)PopUpDialog,				0x128, 0},
		{(FARPROC)RequestGroupPositions,	0x12C, 0},
		{(FARPROC)SetMissionLog,			0x130, 0},
		{(FARPROC)SetInterfaceState,		0x134, 0},
		{(FARPROC)RequestRankLevel,			0x138, 0},
		{(FARPROC)InitiateTrade,			0x13C, 0},
		{(FARPROC)TerminateTrade,			0x140, 0},
		{(FARPROC)AcceptTrade,				0x144, 0},
		{(FARPROC)SetTradeMoney,			0x148, 0},
		{(FARPROC)AddTradeEquip,			0x14C, 0},
		{(FARPROC)DelTradeEquip,			0x150, 0},
		{(FARPROC)RequestTrade,				0x154, 0},
		{(FARPROC)StopTradeRequest,			0x158, 0},
		{(FARPROC)ReqDifficultyScale,		0x15C, 0},
		{(FARPROC)GetServerID,				0x160, 0},
		{(FARPROC)GetServerSig,				0x164, 0},
		{(FARPROC)DumpPacketStats,			0x168, 0},
		{(FARPROC)Dock,						0x16C, 0},
	};

}
