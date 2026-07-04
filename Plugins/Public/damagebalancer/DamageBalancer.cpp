// MunitionControl Plugin - Handle tracking/alert notifications for missile projectiles
// By Aingar
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "DamageBalancer.h"

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	return true;
}

/// Hook will call this function after calling a plugin function to see if we the
/// processing to continue
EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

bool enabled = false;

BalanceData balanceData[256];
unordered_map<uint, BalanceData> groupData;

void HandleDamageMods(IObjRW* iobj, float& dmg, uint inflictorPlayer, DamageType dmgType)
{
	if (!enabled)
	{
		return;
	}

	CShip* ship = reinterpret_cast<CShip*>(iobj->cobj);
	if (ship->ownerPlayer)
	{
		auto& data = balanceData[ship->ownerPlayer];
		ArmorData* currBalanceData = nullptr;
		if (data.enabled)
		{
			currBalanceData = &data.armor;
		}
		else if (ship->groupId)
		{
			auto iter = groupData.find(ship->groupId);
			if (iter != groupData.end())
			{
				currBalanceData = &iter->second.armor;
			}
		}

		if (currBalanceData)
		{
			dmg *= currBalanceData->damageTakenMultArray[dmgType];
		}
	}

	if (inflictorPlayer)
	{
		auto& data = balanceData[inflictorPlayer];
		DamageData* damageData = nullptr;

		if (data.enabled)
		{
			damageData = &balanceData[inflictorPlayer].damage;
		}
		else
		{
			auto group = Players[inflictorPlayer].PlayerGroup;
			if (group)
			{
				auto iter = groupData.find(group->GetID());
				if (iter != groupData.end())
				{
					damageData = &iter->second.damage;
				}
			}
		}

		if (damageData)
		{
			dmg *= damageData->damageDoneMultArray[dmgType];
		}
	}
}

void __stdcall ShipShieldDamage(IObjRW* iobj, CEShield* shield, float& incDmg, DamageList* dmg)
{
	

	HandleDamageMods(iobj, incDmg, dmg->iInflictorPlayerID, DamageType::SHIELD);
}

void __stdcall ShipHullDamage(IObjRW* iobj, float& incDmg, DamageList* dmg)
{
	

	HandleDamageMods(iobj, incDmg, dmg->iInflictorPlayerID, DamageType::HULL);
}

void __stdcall ShipColGrpDmg(IObjRW* iobj, CArchGroup* colGrp, float& incDmg, DamageList* dmg)
{
	

	HandleDamageMods(iobj, incDmg, dmg->iInflictorPlayerID, DamageType::COLGRP);
}

void __stdcall ShipEquipDamage(IObjRW* iobj, CAttachedEquip* equip, float& incDmg, DamageList* dmg)
{
	

	HandleDamageMods(iobj, incDmg, dmg->iInflictorPlayerID, DamageType::EQUIP);
}

void __stdcall CharacterSelect_AFTER(struct CHARACTER_ID const& cId, unsigned int client)
{
	

	balanceData[client].Reset();
}

void __stdcall DisConnect(unsigned int client, enum  EFLConnection state)
{
	

	balanceData[client].Reset();

	auto group = Players[client].PlayerGroup;
	if (group && group->GetMemberCount() >= 1)
	{
		groupData.erase(group->GetID());
	}
}

void PrintBalance(CCmds* cmd)
{
	cmd->Print(L"Printing balanced players:\n");

	for (auto& data : balanceData)
	{
		if (!data.enabled) { continue; }

		cmd->Print(L"Player %u:\n", data.id);
		cmd->Print(L"Damage: hull %0.2f shield %0.2f colgrp %0.2f equip %0.2f\n", data.damage.damageDoneMultArray[DamageType::HULL], data.damage.damageDoneMultArray[DamageType::SHIELD], data.damage.damageDoneMultArray[DamageType::COLGRP], data.damage.damageDoneMultArray[DamageType::EQUIP]);
		cmd->Print(L"Armor: hull %0.2f shield %0.2f colgrp %0.2f equip %0.2f\n", data.armor.damageTakenMultArray[DamageType::HULL], data.armor.damageTakenMultArray[DamageType::SHIELD], data.armor.damageTakenMultArray[DamageType::COLGRP], data.armor.damageTakenMultArray[DamageType::EQUIP]);
	}

	cmd->Print(L"Printing balanced groups:\n");
	for (auto& group : groupData)
	{
		cmd->Print(L"Group %u:\n", group.first);
		cmd->Print(L"Damage: hull %0.2f shield %0.2f colgrp %0.2f equip %0.2f\n", group.second.damage.damageDoneMultArray[DamageType::HULL], group.second.damage.damageDoneMultArray[DamageType::SHIELD], group.second.damage.damageDoneMultArray[DamageType::COLGRP], group.second.damage.damageDoneMultArray[DamageType::EQUIP]);
		cmd->Print(L"Armor: hull %0.2f shield %0.2f colgrp %0.2f equip %0.2f\n", group.second.armor.damageTakenMultArray[DamageType::HULL], group.second.armor.damageTakenMultArray[DamageType::SHIELD], group.second.armor.damageTakenMultArray[DamageType::COLGRP], group.second.armor.damageTakenMultArray[DamageType::EQUIP]);
	}
}

void HandleBalanceData(CCmds* cmd, BalanceData& data, bool isArmor, const wstring& types, float value)
{
	auto targetArray = isArmor ? &data.armor.damageTakenMultArray : &data.damage.damageDoneMultArray;
	if (types == L"all")
	{
		data.enabled = true;
		(*targetArray)[DamageType::HULL] = value;
		(*targetArray)[DamageType::SHIELD] = value;
		(*targetArray)[DamageType::COLGRP] = value;
		(*targetArray)[DamageType::EQUIP] = value;
		cmd->Print(L"OK!\n");
	}
	else if (types == L"hull")
	{
		data.enabled = true;
		(*targetArray)[DamageType::HULL] = value;
		cmd->Print(L"OK!\n");
	}
	else if (types == L"equip")
	{
		data.enabled = true;
		(*targetArray)[DamageType::EQUIP] = value;
		cmd->Print(L"OK!\n");
	}
	else if (types == L"colgrp")
	{
		data.enabled = true;
		(*targetArray)[DamageType::COLGRP] = value;
		cmd->Print(L"OK!\n");
	}
	else if (types == L"shield")
	{
		data.enabled = true;
		(*targetArray)[DamageType::SHIELD] = value;
		cmd->Print(L"OK!\n");
	}
	else
	{
		cmd->Print(L"Multiplier must be greater than zero!\n");
		return;
	}
}

void SetGroupBalance(CCmds* cmd)
{
	auto groupId = cmd->ArgUInt(2);
	auto damageOrArmor = cmd->ArgStr(3);
	auto types = cmd->ArgStr(4);
	auto value = cmd->ArgFloat(5);

	if (groupId == 0)
	{
		cmd->Print(L"No valid groupId provided!\n");
		return;
	}

	if (damageOrArmor == L"reset")
	{
		auto& groupIter = groupData.find(groupId);
		if (groupIter == groupData.end())
		{
			cmd->Print(L"Group has no balance overrides defined!\n");
			return;
		}
		else
		{
			groupData.erase(groupIter);
			cmd->Print(L"Balance reset for group %u\n", groupId);
		}
	}

	if (damageOrArmor != L"armor" && damageOrArmor != L"damage")
	{
		cmd->Print(L"You must specify if you define armor or damage!\n");
		return;
	}

	if (value <= 0.f)
	{
		cmd->Print(L"Multiplier must be greater than zero!\n");
		return;
	}

	auto& data = groupData[groupId];
	HandleBalanceData(cmd, data, damageOrArmor == L"armor", types, value);
}

void SetPlayerBalance(CCmds* cmd)
{
	auto charName = cmd->ArgCharname(2);
	auto damageOrArmor = cmd->ArgStr(3);
	auto types = cmd->ArgStr(4);
	auto value = cmd->ArgFloat(5);

	uint client = HkGetClientIdFromCharname(charName);
	if (!client || client == -1)
	{
		cmd->Print(L"Character not found online!\n");
		return;
	}

	if (damageOrArmor == L"reset")
	{
		balanceData[client].Reset();
		cmd->Print(L"Balance reset for %s\n", charName.c_str());
	}

	if (damageOrArmor != L"armor" && damageOrArmor != L"damage")
	{
		cmd->Print(L"You must specify if you define armor or damage!\n");
		return;
	}

	if (value <= 0.f)
	{
		cmd->Print(L"Multiplier must be greater than zero!\n");
		return;
	}

	auto& data = balanceData[client];
	data.id = client;
	HandleBalanceData(cmd, data, damageOrArmor == L"armor", types, value);
}

#define IS_CMD(a) !args.compare(L##a)
#define RIGHT_CHECK(a) if(!(cmd->rights & a)) { cmd->Print(L"ERR No permission\n"); return true; }
bool ExecuteCommandString_Callback(CCmds* cmd, const wstring& args)
{
	

	if (IS_CMD("balance"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		RIGHT_CHECK(RIGHT_SPECIAL2);

		auto currCmd = cmd->ArgStr(1);

		if (currCmd == L"enable")
		{
			enabled = true;
			cmd->Print(L"OK balancer enabled\n");
			return true;
		}
		else if (currCmd == L"disable")
		{
			enabled = false;
			cmd->Print(L"OK balancer disabled\n");
			return true;
		}
		else if (currCmd == L"list")
		{
			PrintBalance(cmd);
			return true;
		}
		else if (currCmd == L"group")
		{
			SetGroupBalance(cmd);
			return true;
		}
		else if (currCmd == L"player")
		{
			SetPlayerBalance(cmd);
			return true;
		}
		else
		{
			cmd->Print(L"Syntax: .balance [enable|disable|list]");
			cmd->Print(L"or      .balance group groupId [damage|armor|reset] [all|hull|shield|colgrp|equip] [0.01-100.00]");
			cmd->Print(L"or      .balance player playerName [damage|armor|reset] [all|hull|shield|colgrp|equip] [0.01-100.00]");
		}
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Damage Balancer";
	p_PI->sShortName = "damagebalancer";
	p_PI->bMayPause = false;
	p_PI->bMayUnload = false;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipColGrpDmg, PLUGIN_ShipColGrpDmg, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipEquipDamage, PLUGIN_ShipEquipDmg, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipHullDamage, PLUGIN_ShipHullDmg, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipShieldDamage, PLUGIN_ShipShieldDmg, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DisConnect, PLUGIN_HkIServerImpl_DisConnect, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect_AFTER, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
