#pragma once

#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include <unordered_map>

enum DamageType
{
	HULL,
	SHIELD,
	COLGRP,
	EQUIP,
	DamageTypeSIZE
};

struct ArmorData
{
	float damageTakenMultArray[DamageType::DamageTypeSIZE];
};

struct DamageData
{
	float damageDoneMultArray[DamageType::DamageTypeSIZE];
};

struct BalanceData
{
	bool enabled = false;
	uint id;
	ArmorData armor;
	DamageData damage;

	void Reset()
	{
		enabled = false;
		armor.damageTakenMultArray[DamageType::HULL] = 1.f;
		armor.damageTakenMultArray[DamageType::SHIELD] = 1.f;
		armor.damageTakenMultArray[DamageType::COLGRP] = 1.f;
		armor.damageTakenMultArray[DamageType::EQUIP] = 1.f;

		damage.damageDoneMultArray[DamageType::HULL] = 1.f;
		damage.damageDoneMultArray[DamageType::SHIELD] = 1.f;
		damage.damageDoneMultArray[DamageType::COLGRP] = 1.f;
		damage.damageDoneMultArray[DamageType::EQUIP] = 1.f;
	}
};