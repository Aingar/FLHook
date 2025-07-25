#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Main.h"
#include <hookext_exports.h>

CoreModule::CoreModule(PlayerBase* the_base) : Module(TYPE_CORE), base(the_base), space_obj(0), dont_eat(false),
dont_rust(false), baseHealthChanged(false), undergoingDestruction(false)
{
	cargoSpace = core_upgrade_storage[the_base->base_level];
}

CoreModule::~CoreModule()
{
	if (space_obj)
	{
		if (base->baseCSolar)
		{
			POBSolarsBySystemMap[base->system].erase(base->baseCSolar);
			base->baseCSolar = nullptr;
		}
		pub::SpaceObj::Destroy(space_obj, DestroyType::VANISH);
		spaceobj_modules.erase(space_obj);
		space_obj = 0;
	}
}

void CoreModule::Spawn()
{
	if (!space_obj)
	{
		pub::SpaceObj::SolarInfo si;
		memset(&si, 0, sizeof(si));
		si.iFlag = 4;

		char archname[100];
		if (base->basesolar == "legacy")
		{
			_snprintf(archname, sizeof(archname), "dsy_playerbase_%02u", base->base_level);
		}
		else if (base->basesolar == "modern")
		{
			_snprintf(archname, sizeof(archname), "dsy_playerbase_modern_%02u", base->base_level);
		}
		else
		{
			_snprintf(archname, sizeof(archname), base->basesolar.c_str());
		}
		si.iArchID = CreateID(archname);

		if (base->basesolar == "legacy" || base->basesolar == "modern")
		{
			si.iLoadoutID = CreateID(archname);
		}
		else
		{
			si.iLoadoutID = CreateID(base->baseloadout.c_str());
		}
		if (!base->archetype->isjump)
		{
			si.baseId = base->proxy_base;
		}
		else
		{
			si.baseId = base->destSystem;
		}
		si.iHitPointsLeft = -1;
		si.iSystemID = base->system;
		si.mOrientation = base->rotation;
		si.vPos = base->position;

		auto factionInfo = factionCostumeMap.find(base->affiliation);
		if (factionInfo != factionCostumeMap.end())
		{
			si.iVoiceID = factionInfo->second.first;
			si.costume = factionInfo->second.second;
		}
		else
		{
			si.costume.head = CreateID("pi_pirate2_head");
			si.costume.body = CreateID("pi_pirate8_body");
			si.iVoiceID = CreateID("atc_leg_m01");
		}
		strncpy_s(si.cNickName, sizeof(si.cNickName), base->nickname.c_str(), base->nickname.size());

		// Check to see if the hook IDS limit has been reached
		static uint solar_ids = 526000;
		static uint description_ids = 268000;
		if (++solar_ids > 526999)
		{
			solar_ids = 0;
			return;
		}
		++description_ids;

		// Send the base name to all players that are online
		base->solar_ids = solar_ids;
		base->description_ids = description_ids;
		base->description_text = base->BuildBaseDescription();

		wstring basename = base->basename;

		// Set the base name
		FmtStr infoname(solar_ids, 0);
		infoname.begin_mad_lib(solar_ids); // scanner name
		infoname.end_mad_lib();

		FmtStr infocard(solar_ids, 0);
		infocard.begin_mad_lib(solar_ids); // infocard
		infocard.end_mad_lib();
		pub::Reputation::Alloc(si.iRep, infoname, infocard);
		if (base->archetype && !base->archetype->isjump)
		{
			pub::Reputation::SetAffiliation(si.iRep, base->affiliation);
		}

		CreateSolar::SpawnSolar(space_obj, si);
		spaceobj_modules[space_obj] = this;
		
		base->baseCSolar = (CSolar*)CObject::Find(space_obj, CObject::CSOLAR_OBJECT);
		base->baseCSolar->Release();

		base->max_base_health = base->baseCSolar->archetype->fHitPoints;
		if (base->isFreshlyBuilt)
		{
			base->base_health = base->max_base_health * 0.5f;
		}
		else if (base->base_health > base->max_base_health)
		{
			base->base_health = base->max_base_health;
		}

		pub::SpaceObj::SetRelativeHealth(space_obj, base->base_health / base->max_base_health);

		if (base->archetype && !base->archetype->isjump)
		{
			if (base->baseCSolar)
			{
				POBSolarsBySystemMap[base->system].insert(base->baseCSolar);
			}
		}
		else
		{
			base->baseCSolar->jumpDestObj = base->destObject;
			base->baseCSolar->jumpDestSystem = base->destSystem;
		}

		struct PlayerData* pd = 0;
		while (pd = Players.traverse_active(pd))
		{
			HkChangeIDSString(pd->iOnlineID, base->solar_ids, base->basename);
			HkChangeIDSString(pd->iOnlineID, base->description_ids, base->description_text);
			if (base->baseCSolar)
			{
				SendBaseIDSList(pd->iOnlineID, base->baseCSolar->id, base->description_ids);
			}
		}

		if (shield_reinforcement_threshold_map.count(base->base_level))
			base->base_shield_reinforcement_threshold = shield_reinforcement_threshold_map[base->base_level];
		else
			base->base_shield_reinforcement_threshold = FLT_MAX;

		base->SyncReputationForBaseObject(space_obj);
		if (set_plugin_debug > 1)
			ConPrint(L"CoreModule::created space_obj=%u health=%f\n", space_obj, base->base_health);

		pub::AI::SetPersonalityParams pers = CreateSolar::MakePersonality();
		pub::AI::SubmitState(space_obj, &pers);
	}
}

wstring CoreModule::GetInfo(bool xml)
{
	return L"Core";
}

void CoreModule::LoadState(INI_Reader& ini)
{
	while (ini.read_value())
	{
		if (ini.is_value("dont_eat"))
		{
			dont_eat = (ini.get_value_int(0) == 1);
		}
		else if (ini.is_value("dont_rust"))
		{
			dont_rust = (ini.get_value_int(0) == 1);
		}
	}
}

void CoreModule::SaveState(FILE* file)
{
	fprintf(file, "[CoreModule]\n");
	fprintf(file, "dont_eat = %d\n", dont_eat);
	fprintf(file, "dont_rust = %d\n", dont_rust);
}

void CoreModule::RepairDamage()
{
	// no food & no water & no oxygen = RIOTS
	if (!base->isCrewSupplied)
	{
		return;
	}

	// The bigger the base the more damage can be repaired.
	for (REPAIR_ITEM& item : set_base_repair_items)
	{
		if (base->base_health >= base->max_base_health)
			return;

		if (base->HasMarketItem(item.good) >= item.quantity)
		{
			base->RemoveMarketGood(item.good, item.quantity);
			base->base_health += repair_per_repair_cycle * base->base_level;
			baseHealthChanged = true;
		}
	}
}

void CoreModule::EnableShieldFuse(bool shieldEnabled)
{
	if (space_obj)
	{
		StarSystem* dummy;
		IObjRW* inspect;
		if (GetShipInspect(space_obj, inspect, dummy))
		{
			HkUnLightFuse(inspect, shield_fuse, 0);
			if (shieldEnabled)
			{
				HkLightFuse(inspect, shield_fuse, 0.0f, 0.0f, 0.0f);
			}
		}
	}
}

bool CoreModule::Timer(uint time)
{
	// Disable shield if time elapsed
	if (base->shield_timeout && base->shield_timeout < time)
	{
		base->shield_timeout = 0;
		base->isShieldOn = false;
		EnableShieldFuse(false);
	}

	if (set_holiday_mode)
	{
		return false;
	}

	if ((time % set_tick_time) != 0)
	{
		return false;
	}

	if (!space_obj)
	{
		return false;
	}

	if ((base->logic == 0) && (base->invulnerable == 1))
	{
		return false;
	}

	uint number_of_crew = base->HasMarketItem(set_base_crew_type);
	bool isCrewSufficient = number_of_crew >= (base->base_level * 200);

	if (baseHealthChanged && base->baseCSolar)
	{
		base->base_health = base->baseCSolar->get_hit_pts();
	}
	if (!dont_rust && ((time % set_damage_tick_time) == 0))
	{
		float no_crew_penalty = isCrewSufficient ? 1.0f : no_crew_damage_multiplier;
		// Reduce hitpoints to reflect wear and tear. This will eventually
		// destroy the base unless it is able to repair itself.
		float damage_taken = set_damage_per_tick * no_crew_penalty;
		base->base_health -= damage_taken;
	}

	// Repair damage if we have sufficient crew on the base.

	if (isCrewSufficient)
	{
		RepairDamage();
	}

	if (base->base_health > base->max_base_health)
	{
		base->base_health = base->max_base_health;
	}
	else if (base->base_health <= 0)
	{
		base->base_health = 0;
		return SpaceObjDestroyed(space_obj);
	}

	if (baseHealthChanged && base->baseCSolar)
	{
		baseHealthChanged = false;
		base->baseCSolar->set_hit_pts(base->base_health);
	}

	// Humans use commodity_oxygen, commodity_water. Consume these for
	// the crew or kill 10 crew off and repeat this every 12 hours.
	if (time % set_crew_check_frequency == 0)
	{
		base->fed_workers.clear();
		if (dont_eat	)
		{
			base->isCrewSupplied = true;
			base->fed_workers[set_base_crew_type] = base->base_level * 200;
		}
		else
		{
			base->isCrewSupplied = base->FeedCrew(set_base_crew_type, base->base_level * 200);
		}
	}

	return false;
}

float CoreModule::SpaceObjDamaged(uint space_obj, uint attacking_space_obj, float incoming_damage)
{
	if (!base->has_shield)
	{
		base->SpaceObjDamaged(space_obj, attacking_space_obj, incoming_damage);
		if (base->base_health > incoming_damage)
		{
			base->base_health -= incoming_damage;
			return incoming_damage;
		}
		else
		{
			SpaceObjDestroyed(space_obj);
			return 0.0f;
		}
	}
	base->shield_timeout = (int)time(nullptr) + 60;
	if (!base->isShieldOn)
	{
		base->isShieldOn = true;
		EnableShieldFuse(true);
	}
	if ((base->use_vulnerability_window && base->vulnerableWindowStatus != PlayerBase::BASE_VULNERABILITY_STATE::VULNERABLE) || base->invulnerable == 1 || base->shield_strength_multiplier >= 1.0f)
	{
		// base invulnerable, keep current health value
		return 0;
	}

	if (base->siege_gun_only)
	{
		const auto& siegeDamageIter = siegeWeaponryMap.find(iDmgMunitionID);
		if (siegeDamageIter == siegeWeaponryMap.end())
		{
			//Siege gun(s) defined, but this is not one of them, no damage dealt
			return 0;
		}
		else
		{
			//Even with siege gun damage override, it still takes shield strength into the account
			incoming_damage = siegeDamageIter->second * (1.0f - base->shield_strength_multiplier);
		}
	}

	base->damage_taken_since_last_threshold += incoming_damage;
	if (base->damage_taken_since_last_threshold >= base->base_shield_reinforcement_threshold)
	{
		base->damage_taken_since_last_threshold -= base->base_shield_reinforcement_threshold;
		base->shield_strength_multiplier += shield_reinforcement_increment;

		PrintLocalMsgAroundObject(space_obj, base->basename + L" shield reinforced!", 50000.f);
		if (base->shield_strength_multiplier >= 1.0f)
		{
			PrintLocalMsgAroundObject(space_obj, base->basename + L" is now impervious to damage!", 50000.f);
		}
	}

	if (baseHealthChanged && base->baseCSolar)
	{
		base->baseCSolar->set_hit_pts(base->base_health);
		baseHealthChanged = false;
	}

	base->SpaceObjDamaged(space_obj, attacking_space_obj, incoming_damage);

	if (base->base_health > incoming_damage)
	{
		base->base_health -= incoming_damage;
		return incoming_damage;
	}
	else
	{
		SpaceObjDestroyed(space_obj);
		return 0.0f;
	}
}

bool CoreModule::SpaceObjDestroyed(uint space_obj, bool moveFile, bool broadcastDeath)
{
	POBSolarsBySystemMap[base->system].erase(base->baseCSolar);
	base->baseCSolar = nullptr;
	if (this->space_obj == space_obj && !undergoingDestruction)
	{
		undergoingDestruction = true;
		if (set_plugin_debug > 1)
			ConPrint(L"CoreModule::destroyed space_obj=%u\n", space_obj);
		pub::SpaceObj::Destroy(space_obj, DestroyType::VANISH);
		spaceobj_modules.erase(space_obj);
		this->space_obj = 0;

		//List all players in the system at the time
		list<string> CharsInSystem;
		struct PlayerData* pd = 0;
		while (pd = Players.traverse_active(pd))
		{
			if (broadcastDeath)
			{
				PrintUserCmdText(pd->iOnlineID, L"Base %s destroyed", base->basename.c_str());
			}
			if (pd->iSystemID == base->system)
			{
				const wstring& charname = (const wchar_t*)Players.GetActiveCharacterName(pd->iOnlineID);
				CharsInSystem.emplace_back(wstos(charname));
			}
		}

		// Logging
		wstring wscMsg = L": Base %b destroyed";
		wscMsg = ReplaceStr(wscMsg, L"%b", base->basename.c_str());
		string scText = wstos(wscMsg);
		BaseLogging("%s", scText.c_str());

		//Base specific logging
		string msg = "Base destroyed. Players in system: ";
		for each (string player in CharsInSystem)
		{
			msg += (player + "; ");
		}

		base->LogDamageDealers();
		Log::LogBaseAction(wstos(base->basename), msg.c_str());

		ConPrint(L"BASE: Base %s destroyed\n", base->basename.c_str());

		// Unspawn, delete base and save file.
		DeleteBase(base, moveFile);

		// Careful not to access this as this object will have been deleted by now.
		return true;
	}
	return false;
}

void CoreModule::SetReputation(int player_rep, float attitude)
{
	if (space_obj)
	{
		EnableShieldFuse(base->isShieldOn);

		int obj_rep;
		pub::SpaceObj::GetRep(this->space_obj, obj_rep);
		if (set_plugin_debug > 1)
			ConPrint(L"CoreModule::SetReputation player_rep=%u obj_rep=%u attitude=%f base=%08x\n",
				player_rep, obj_rep, attitude, base->base);
		pub::Reputation::SetAttitude(obj_rep, player_rep, attitude);
	}
}