/**
 * @date Jan, 2023
 * @author Raikkonen
 * @defgroup Betting Betting
 * @brief
 * A plugin that allows players to place bets and then duel, the winner getting the pot.
 *
 * @paragraph cmds Player Commands
 * All commands are prefixed with '/' unless explicitly specified.
 * - acceptduel - Accepts the current duel request.
 * - acceptffa - Accept the current ffa request.
 * - cancel - Cancel the current duel/ffa request.
 * - duel <amount> - Create a duel request to the targeted player. Winner gets the pot.
 * - ffa <amount> - Create an ffa and send an invite to everyone in the system. Winner gets the pot.
 *
 * @paragraph adminCmds Admin Commands
 * There are no admin commands in this plugin.
 *
 * @paragraph configuration Configuration
 * This plugin has no configuration file.
 *
 * @paragraph ipc IPC Interfaces Exposed
 * This plugin does not expose any functionality.
 *
 * @paragraph optional Optional Plugin Dependencies
 * This plugin has no dependencies.
 */

#include "Main.h"

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Main.h"
#include <sstream>
#include <iostream>
#include <hookext_exports.h>

 //! A struct to hold a duel between two players. This holds the amount of cash they're betting on, and whether it's been accepted or not
struct Duel
{
	uint client;
	uint client2;
	int amount;
	bool accepted;
};

//! A struct to hold a contestant for a Free-For-All
struct Contestant
{
	bool accepted;
	bool loser;
};

//! A struct to hold a Free-For-All competition. This holds the contestants, how much it costs to enter, and the total pot to be won by the eventual winner
struct FreeForAll
{
	std::map<uint, Contestant> contestants;
	int entryAmount;
	int pot;
};

PLUGIN_RETURNCODE returncode;

std::vector<Duel> duels;
std::unordered_map<uint, FreeForAll> freeForAlls; // uint is iSystemId

/** @ingroup Betting
 * @brief If the player who died is in an FreeForAll, mark them as a loser. Also handles payouts to winner.
 */
void processFFA(uint client)
{
	for (const auto& entry : freeForAlls)
	{
		const uint system = entry.first;
		const auto& freeForAll = entry.second;
		if (freeForAlls[system].contestants[client].accepted && !freeForAlls[system].contestants[client].loser)
		{
			if (freeForAlls[system].contestants.count(client))
			{
				freeForAlls[system].contestants[client].loser = true;
				PrintLocalUserCmdText(client,
				    std::wstring(reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(client))) + L" has been knocked out the FFA.",
				    100000);
			}

			// Is the FreeForAll over?
			int count = 0;
			uint contestantId = 0;
			for (const auto& contestant : freeForAlls[system].contestants)
			{
				if (contestant.second.loser == false && contestant.second.accepted == true)
				{
					count++;
					contestantId = contestant.first;
				}
			}

			// Has the FreeForAll been won?
			if (count <= 1)
			{
				if (HkIsValidClientID(contestantId))
				{
					// Announce and pay winner
					std::wstring winner = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(contestantId));
					pub::Player::AdjustCash(contestantId, freeForAlls[system].pot);
					const std::wstring message =
					    winner + L" has won the FFA and receives " + std::to_wstring(freeForAlls[system].pot) + L" credits.";
					PrintLocalUserCmdText(contestantId, message, 100000);
				}
				else
				{
					struct PlayerData* playerData = nullptr;
					while ((playerData = Players.traverse_active(playerData)))
					{
						uint systemId;
						pub::Player::GetSystem(playerData->iOnlineID, systemId);
						if (system == systemId)
							PrintUserCmdText(playerData->iOnlineID, L"No one has won the FFA.");
					}
				}
				// Delete event
				freeForAlls.erase(system);
				return;
			}
		}
	}
}

/** @ingroup Betting
 * @brief This method is called when a player types /ffa in an attempt to start a pvp event
 */
bool UserCmdStartFreeForAll(uint client, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	// Convert string to uint
	int amount = ToInt(GetParam(wscParam, ' ', 0));

	// Check its a valid amount of cash
	if (amount == 0)
	{
		PrintUserCmdText(client, L"Must specify a cash amount. Usage: /ffa <amount> e.g. /ffa 5000");
		return true;
	}

	// Check the player can afford it
	std::wstring characterName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(client));
	int cash;
	pub::Player::InspectCash(client, cash);
	if (amount > 0 && cash < amount)
	{
		PrintUserCmdText(client, L"You don't have enough credits to create this FFA.");
		return true;
	}

	// Get the player's current system and location in the system.
	uint systemId;
	pub::Player::GetSystem(client, systemId);

	// Look in FreeForAll map, is an ffa happening in this system already?
	// If system doesn't have an ongoing ffa
	if (!freeForAlls.count(systemId))
	{
		// Get a list of other players in the system
		// Add them and the player into the ffa map
		struct PlayerData* playerData = nullptr;
		while ((playerData = Players.traverse_active(playerData)))
		{
			// Get the this player's current system
			if (systemId != playerData->iSystemID)
				continue;

			uint client2 = playerData->iOnlineID;

			// Add them to the contestants freeForAlls
			freeForAlls[systemId].contestants[client2].loser = false;

			if (client == client2)
				freeForAlls[systemId].contestants[client2].accepted = true;
			else
			{
				freeForAlls[systemId].contestants[client2].accepted = false;
				PrintUserCmdText(client2,
				        L"%ls has started a Free-For-All tournament. Cost to enter is %u credits. Type \"/acceptffa\" to enter.", characterName.c_str(), amount);
			}
		}

		// Are there any other players in this system?
		if (!freeForAlls[systemId].contestants.empty())
		{
			PrintUserCmdText(client, L"Challenge issued. Waiting for others to accept.");
			freeForAlls[systemId].entryAmount = amount;
			freeForAlls[systemId].pot = amount;
			pub::Player::AdjustCash(client, -amount);
		}
		else
		{
			freeForAlls.erase(systemId);
			PrintUserCmdText(client, L"There are no other players in this system.");
		}
	}
	else
		PrintUserCmdText(client, L"There is an FFA already happening in this system.");
	return true;
}

/** @ingroup Betting
 * @brief This method is called when a player types /acceptffa
 */

bool UserCmd_AcceptFFA(uint client, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	// Is player in space?
	uint ship;
	pub::Player::GetShip(client, ship);
	if (!ship)
	{
		PrintUserCmdText(client, L"You must be in space to accept this.");
		return true;
	}

	// Get the player's current system and location in the system.
	uint systemId = Players[client].iSystemID;

	if (!freeForAlls.count(systemId))
	{
		PrintUserCmdText(client, L"There isn't an FFA in this system. Use /ffa to create one.");
	}
	else
	{
		std::wstring characterName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(client));

		// Check the player can afford it
		int cash;
		pub::Player::InspectCash(client, cash);
		if (freeForAlls[systemId].entryAmount > 0 && cash < freeForAlls[systemId].entryAmount)
		{
			PrintUserCmdText(client, L"You don't have enough credits to join this FFA.");
			return true;
		}

		// Accept
		if (freeForAlls[systemId].contestants[client].accepted == false)
		{
			freeForAlls[systemId].contestants[client].accepted = true;
			freeForAlls[systemId].contestants[client].loser = false;
			freeForAlls[systemId].pot = freeForAlls[systemId].pot + freeForAlls[systemId].entryAmount;
			PrintUserCmdText(client,
			    std::to_wstring(freeForAlls[systemId].entryAmount) +
			        L" credits have been deducted from "
			        L"your Neural Net account.");
			const std::wstring msg =
			    characterName + L" has joined the FFA. Pot is now at " + std::to_wstring(freeForAlls[systemId].pot) + L" credits.";
			PrintLocalUserCmdText(client, msg, 100000);

			// Deduct cash
			pub::Player::AdjustCash(client, -freeForAlls[systemId].entryAmount);
		}
		else
			PrintUserCmdText(client, L"You have already accepted the FFA.");
	}
	return true;
}

/** @ingroup Betting
 * @brief Removes any duels with this client and handles payouts.
 */
void ProcessDuel(uint client)
{
	auto duel = duels.begin();
	while (duel != duels.end())
	{
		uint clientKiller = 0;

		if (duel->client == client)
			clientKiller = duel->client2;

		if (duel->client2 == client)
			clientKiller = duel->client;

		if (clientKiller == 0)
		{
			duel++;
			continue;
		}

		if (duel->accepted)
		{
			// Get player names
			std::wstring victim = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(client));
			std::wstring killer = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(clientKiller));

			// Prepare and send message
			const std::wstring msg = killer + L" has won a duel against " + victim + L" for " + std::to_wstring(duel->amount) + L" credits.";
			PrintLocalUserCmdText(clientKiller, msg, 10000);

			// Change cash
			pub::Player::AdjustCash(clientKiller, duel->amount);
			pub::Player::AdjustCash(client, -duel->amount);
		}
		else
		{
			PrintUserCmdText(duel->client, L"Duel cancelled.");
			PrintUserCmdText(duel->client2, L"Duel cancelled.");
		}
		duel = duels.erase(duel);
		return;
	}
}

/** @ingroup Betting
 * @brief This method is called when a player types /duel in an attempt to start a duel
 */
bool UserCmd_Duel(uint client, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	// Convert string to uint
	const int amount = ToInt(GetParam(wscParam, ' ', 0));

	// Check its a valid amount of cash
	if (amount == 0)
	{
		PrintUserCmdText(client,
			L"Must specify a cash amount. Usage: /duel "
			L"<amount> e.g. /duel 5000");
		return true;
	}

	uint ship;
	pub::Player::GetShip(client, ship);
	if (!ship)
	{
		PrintUserCmdText(client, L"ERR You're not in space!");
		return true;
	}
	// Get the object the player is targetting
	uint targetShip;
	pub::SpaceObj::GetTarget(ship, targetShip);
	if (targetShip)
	{
		PrintUserCmdText(client, L"ERR No target");
		return true;
	}

	// Check ship is a player
	uint clientTarget = HkGetClientIDByShip(targetShip);
	if (!clientTarget)
	{
		PrintUserCmdText(client, L"ERR Target is not a player");
		return true;
	}

	std::wstring characterName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(client));

	// Check the player can afford it
	int cash;
	pub::Player::InspectCash(client, cash);
	if (amount > 0 && cash < amount)
	{
		PrintUserCmdText(client, L"You don't have enough credits to issue this challenge.");
		return true;
	}

	// Do either players already have a duel?
	for (const auto& duel : duels)
	{
		// Target already has a bet
		if (duel.client == clientTarget || duel.client2 == clientTarget)
		{
			PrintUserCmdText(client, L"This player already has an ongoing duel.");
			return true;
		}
		// Player already has a bet
		if (duel.client == client || duel.client2 == client)
		{
			PrintUserCmdText(client, L"You already have an ongoing duel. Type /cancel");
			return true;
		}
	}

	// Create duel
	Duel duel;
	duel.client = client;
	duel.client2 = clientTarget;
	duel.amount = amount;
	duel.accepted = false;
	duels.push_back(duel);

	// Message players
	const std::wstring characterName2 = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(clientTarget));
	const std::wstring message = characterName + L" has challenged " + characterName2 + L" to a duel for " + std::to_wstring(amount) + L" credits.";
	PrintLocalUserCmdText(client, message, 10000);
	PrintUserCmdText(clientTarget, L"Type \"/acceptduel\" to accept.");

	return true;
}

/** @ingroup Betting
 * @brief This method is called when a player types /acceptduel to accept a duel request.
 */

bool UserCmdAcceptDuel(uint client, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	uint ship;
	pub::Player::GetShip(client, ship);
	if (!ship)
	{
		PrintUserCmdText(client, L"ERR You're not in space!");
		return true;
	}

	for (auto& duel : duels)
	{
		if (duel.client2 == client)
		{
			// Has player already accepted the bet?
			if (duel.accepted == true)
			{
				PrintUserCmdText(client, L"You have already accepted the challenge.");
				return true;
			}

			// Check the player can afford it
			std::wstring characterName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(client));
			int cash;
			pub::Player::InspectCash(duel.client2, cash);

			if (cash < duel.amount)
			{
				PrintUserCmdText(client, L"You don't have enough credits to accept this challenge");
				return true;
			}

			duel.accepted = true;
			const std::wstring message = characterName + L" has accepted the duel with " +
			    reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(duel.client)) + L" for " + std::to_wstring(duel.amount) + L" credits.";
			PrintLocalUserCmdText(client, message, 10000);
			return true;
		}
	}
	PrintUserCmdText(client,
	    L"You have no duel requests. To challenge "
		L"someone, target them and type /duel <amount>");
	
	return true;
}

/** @ingroup Betting
 * @brief This method is called when a player types /cancel to cancel a duel/ffa request.
 */
bool UserCmd_Cancel(uint client, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	processFFA(client);
	ProcessDuel(client);
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Client command processing
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct USERCMD
{
	wchar_t *wszCmd;
	_UserCmdProc proc;
	wchar_t *usage;
};

USERCMD UserCmds[] =
{
	{L"/acceptduel", UserCmdAcceptDuel, L"Accepts the current duel request."},
	{L"/acceptffa", UserCmd_AcceptFFA, L"Accept the current ffa request."},
	{L"/cancel", UserCmd_Cancel, L"Cancel the current duel/ffa request."},
	{L"/duel", UserCmd_Duel, L"Create a duel request to the targeted player. Winner gets the pot."},
	{L"/ffa", UserCmdStartFreeForAll, L"Create an ffa and send an invite to everyone in the system. Winner gets the pot."},
};

bool __stdcall UserCmd_Process(uint iClientID, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	// If the chat string does not match the USER_CMD then we do not handle the
	// command, so let other plugins or FLHook kick in. We require an exact match
	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{

		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			// Extract the parameters string from the chat string. It should
			// be immediately after the command and a space.
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			// Dispatch the command to the appropriate processing function.
			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				// We handled the command tell FL hook to stop processing this
				// chat string.
				returncode = SKIPPLUGINS_NOFUNCTIONCALL; // we handled the command, return immediatly
				return true;
			}
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hooks
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** @ingroup Betting
 * @brief Hook for dock call. Treats a player as if they died if they were part of a duel
 */
int __stdcall DockCall(unsigned int const& ship, unsigned int const& d, const int& cancel,
    const enum DOCK_HOST_RESPONSE& response)
{
	returncode = DEFAULT_RETURNCODE;
	uint client = HkGetClientIDByShip(ship);
	if (client)
	{
		processFFA(client);
		ProcessDuel(client);
	}
	return 0;
}

/** @ingroup Betting
 * @brief Hook for disconnect. Treats a player as if they died if they were part of a duel
 */
void __stdcall DisConnect(uint client, const enum EFLConnection& state)
{
	returncode = DEFAULT_RETURNCODE;
	processFFA(client);
	ProcessDuel(client);
}

/** @ingroup Betting
 * @brief Hook for char info request (F1). Treats a player as if they died if they were part of a duel
 */
void CharacterInfoReq(uint client, const bool& p2)
{
	returncode = DEFAULT_RETURNCODE;
	processFFA(client);
	ProcessDuel(client);
}

/** @ingroup Betting
 * @brief Hook for death to kick player out of duel
 */
void __stdcall SendDeathMessage( const std::wstring& message, const uint system, uint clientVictim, uint clientKiller)
{
	returncode = DEFAULT_RETURNCODE;
	ProcessDuel(clientVictim);
	processFFA(clientVictim);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Betting";
	p_PI->sShortName = "betting";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SendDeathMessage, PLUGIN_SendDeathMsg, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterInfoReq, PLUGIN_HkIServerImpl_CharacterInfoReq, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DockCall, PLUGIN_HkCb_Dock_Call, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DisConnect, PLUGIN_HkIServerImpl_DisConnect, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));

	return p_PI;
}