#include "Main.h"

PlayerBase::PlayerBase(uint client, const wstring &password, const wstring &the_basename)
	: basename(the_basename),
	base(0), money(0), base_health(0), baseCSolar(nullptr), preferred_food(0),
	base_level(1), defense_mode(DEFENSE_MODE::NODOCK_NEUTRAL), proxy_base(0), affiliation(DEFAULT_AFFILIATION), vulnerableWindowStatus(BASE_VULNERABILITY_STATE::INVULNERABLE),
	shield_timeout(0), isShieldOn(false), isFreshlyBuilt(true), pinned_item_updated(false),
	shield_strength_multiplier(base_shield_strength), damage_taken_since_last_threshold(0), isPublic(false)
{
	nickname = CreateBaseNickname(wstos(basename));
	base = CreateID(nickname.c_str());
	archetype = &mapArchs["legacy"];

	// The creating ship is an ally by default.
	BasePassword bp;
	bp.pass = password;
	bp.admin = true;
	passwords.emplace_back(bp);
	ally_names.insert((const wchar_t*)Players.GetActiveCharacterName(client));

	// Setup the base in the current system and at the location 
	// of the player. Rotate the base so that the docking ports
	// face the ship and move the base to just in front of the ship
	uint ship;
	pub::Player::GetShip(client, ship);
	pub::SpaceObj::GetSystem(ship, system);
	pub::SpaceObj::GetLocation(ship, position, rotation);
	Rotate180(rotation);
	TranslateZ(position, rotation, 1000);

	// Create the default module and spawn space obj.
	modules.emplace_back((Module*)new CoreModule(this));

	// Setup derived fields
	SetupDefaults();

}

PlayerBase::PlayerBase(const string &the_path)
	: path(the_path), base(0), money(0), baseCSolar(nullptr), preferred_food(0),
	base_health(0), base_level(0), defense_mode(DEFENSE_MODE::NODOCK_NEUTRAL), proxy_base(0), affiliation(DEFAULT_AFFILIATION), vulnerableWindowStatus(BASE_VULNERABILITY_STATE::INVULNERABLE),
	shield_timeout(0), isShieldOn(false), isFreshlyBuilt(false), pinned_item_updated(false),
	shield_strength_multiplier(base_shield_strength), damage_taken_since_last_threshold(0), isPublic(false)
{
	// Load and spawn base modules
	Load();

	// Setup derived fields
	SetupDefaults();

}

PlayerBase::~PlayerBase()
{
	for (Module* module : modules)
	{
		if (module)
		{
			delete module;
		}
	}
}

void PlayerBase::Spawn()
{
	for (vector<Module*>::iterator i = modules.begin(); i != modules.end(); ++i)
	{
		if (*i)
		{
			(*i)->Spawn();
		}
	}

	if (archetype)
	{
		has_shield = archetype->hasShield;
		siege_gun_only = archetype->siegeGunOnly;
		use_vulnerability_window = archetype->vulnerabilityWindowUse;
	}

	SyncReputationForBase();

	if (archetype && archetype->isjump)
	{
		if (destObject)
		{
			auto obj = HkGetInspectObj(destObject);
			if (obj)
			{
				destSystem = obj->cobj->system;
				HyperJump::InitJumpHole(base, destSystem, destObject);
			}
		}
	}
}

PlayerBase::BASE_VULNERABILITY_STATE IsVulnerabilityWindowActive(BASE_VULNERABILITY_WINDOW window, int timeOfDay, int startOffset)
{
	if ((window.start < window.end
		&& window.start <= timeOfDay && window.end > timeOfDay)
		|| (window.start > window.end
			&& (window.start <= timeOfDay || window.end > timeOfDay)))
	{
		return PlayerBase::BASE_VULNERABILITY_STATE::VULNERABLE;
	}

	window.start -= startOffset;
	if ((window.start < window.end
		&& window.start <= timeOfDay && window.end > timeOfDay)
		|| (window.start > window.end
			&& (window.start <= timeOfDay || window.end > timeOfDay)))
	{
		return PlayerBase::BASE_VULNERABILITY_STATE::PREVULNERABLE;
	}

	return PlayerBase::BASE_VULNERABILITY_STATE::INVULNERABLE;
}

void PlayerBase::CheckVulnerabilityWindow(uint currTime)
{
	if (set_holiday_mode)
	{
		return;
	}

	int timeOfDay = (currTime % (3600 * 24)) / 60;

	auto currVulnState = IsVulnerabilityWindowActive(vulnerabilityWindow1, timeOfDay, defense_platform_activation_offset);
	if (!single_vulnerability_window)
	{
		currVulnState = max(currVulnState, IsVulnerabilityWindowActive(vulnerabilityWindow1, timeOfDay, 60));
	}

	if (currVulnState == vulnerableWindowStatus)
	{
		return;
	}
	
	switch (currVulnState)
	{
	case BASE_VULNERABILITY_STATE::VULNERABLE:
	{
		shield_strength_multiplier = base_shield_strength;
		damage_taken_since_last_threshold = 0;
		if (shield_reinforcement_threshold_map.count(base_level))
		{
			base_shield_reinforcement_threshold = shield_reinforcement_threshold_map[base_level];
		}
		else
		{
			base_shield_reinforcement_threshold = FLT_MAX;
		}
		if (baseCSolar && base_health <= max_base_health)
		{
			baseCSolar->set_hit_pts(base_health);
		}
		break;
	}
	case BASE_VULNERABILITY_STATE::PREVULNERABLE: break;
	case BASE_VULNERABILITY_STATE::INVULNERABLE:
	{
		if (baseCSolar && base_health <= max_base_health)
		{
			baseCSolar->set_hit_pts(base_health);
		}
		LogDamageDealers();
		break;
	}
	}

	vulnerableWindowStatus = currVulnState;
	SyncReputationForBase();
	
}

void PlayerBase::LogDamageDealers()
{
	if (!damageTakenMap.empty())
	{
		string siegeMsg = wstos(basename) + "Damage taken: \n";
		for (auto& item : damageTakenMap)
		{
			char buf[50];
			sprintf(buf, "%s - %0.0f\n", wstos(item.first).c_str(), item.second);
			siegeMsg += buf;
		}
		BaseLogging(siegeMsg.c_str());
	}
}

// Dispatch timer to modules and exit immediately if the timer indicates
// that this base has been deleted.
void PlayerBase::Timer(uint curr_time)
{
	if (failed_update_counter >= 3)
	{
		AddLog("Base %s error count exceeded, respawning", wstos(basename).c_str());
		basesToRespawn.push_back({ path, 60 });
		base_health = 0;
		CoreModule* core = reinterpret_cast<CoreModule*>(modules[0]);
		uint spaceObjId = core->space_obj;
		core->SpaceObjDestroyed(core->space_obj, false, false);
		pub::SpaceObj::Destroy(spaceObjId, DestroyType::VANISH);
		return;
	}

	if (set_plugin_debug_special && (curr_time % 60 == 0))
	{
		AddLog("Started processing %s\n", wstos(this->basename).c_str());
	}
	if ((curr_time % set_tick_time) == 0 && logic)
	{
		failed_update_counter++;
		reservedCatalystMap.clear();
		reservedCatalystMap[set_base_crew_type] = base_level * 200;
	}
	if ((curr_time % 60) == 0 && !invulnerable)
	{
		this->CheckVulnerabilityWindow(curr_time);
	}
	for (Module* pobModule : modules)
	{
		if (pobModule)
		{
			bool is_deleted = pobModule->Timer(curr_time);
			if (is_deleted)
				break;
		}
	}
	if (curr_time % set_tick_time == 0 && logic)
	{
		failed_update_counter = 0;
	}

	return;
}

void PlayerBase::SetupDefaults()
{
	// Calculate the hash of the nickname
	if (!proxy_base)
	{
		char system_nick[1024];
		pub::GetSystemNickname(system_nick, sizeof(system_nick), system);

		char proxy_base_nick[1024];
		sprintf(proxy_base_nick, "%s_proxy_base", system_nick);

		proxy_base = CreateID(proxy_base_nick);
	}

	// The path to the save file for the base.
	if (!path.size())
	{
		char datapath[MAX_PATH];
		GetUserDataPath(datapath);

		char tpath[1024];
		sprintf(tpath, R"(%s\Accts\MultiPlayer\player_bases\base_%08x.ini)", datapath, base);
		path = tpath;
	}

	// Build the infocard text
	infocardHeader.clear();

	if (!infocard_para[1].empty())
	{
		infocardHeader = L"<TEXT>" + ReplaceStr(infocard_para[1], L"\n", L"</TEXT><PARA/><TEXT>") + L"</TEXT><PARA/><PARA/>";
	}

	infocard.clear();
	for (int i = 2; i <= MAX_PARAGRAPHS; i++)
	{
		wstring& wscXML = infocard_para[i];

		if (wscXML.length())
			infocard += L"<TEXT>" + ReplaceStr(wscXML, L"\n", L"</TEXT><PARA/><TEXT>") + L"</TEXT><PARA/><PARA/>";
	}

	// Validate the affiliation and clear it if there is no infocard
	// name assigned to it. We assume that this would be an corrupted affiliation.
	if (affiliation)
	{
		uint name;
		pub::Reputation::GetGroupName(affiliation, name);
		if (!name)
		{
			affiliation = 0;
		}
	}

	if (vulnerabilityWindow1.start == -1 || vulnerabilityWindow2.start == -1)
	{
		vulnerabilityWindow1 = { 10 * 60, ((10 * 60) + vulnerability_window_length) % (60 * 24) };
		vulnerabilityWindow2 = { 20 * 60, ((20 * 60) + vulnerability_window_length) % (60 * 24) };
	}
	CheckVulnerabilityWindow((uint)time(nullptr));

	if (modules.size() < (base_level * 3) + 1)
	{
		modules.resize((base_level * 3) + 1);
	}

	RecalculateCargoSpace();

	if (base_level >= 2)
	{
		isRearmamentAvailable = true;
	}
}

wstring PlayerBase::GetBaseHeaderText()
{
	if (archetype && archetype->isjump)
	{
		return LR"(<?xml version="1.0" encoding="UTF-16"?><RDL><PUSH/><TRA data="65281" mask="-31" def="30"/><TEXT>***GRAVITATIONAL ANOMALY DETECTED***</TEXT><PARA/><TRA data="96" mask="-31" def="-1"/><TEXT> </TEXT><PARA/><TEXT>Magnetic and gravimetric readings are consistent with a Jump Hole -- a natural phenomenon that functions similar to a Jump Gate, though the hazards associated with traveling through a Jump Hole remain largely unknown. Like Jump Gates, jump holes are semi-permeable areas in real-space that have a tendency to breach the natural boundaries of linear time and space. Unlike Jump Gates, though, these anomalies occur naturally and so are both unpredictable and unstable. Traveling through jump holes is a risky proposition at best. Though some have been charted and, based on accounts from ships that have accidentally encountered them, have had their exit points logged, just as many ships have never been heard from again. It is believed that these ships were either crushed in the violent, cataclysmic vortex that lies outside the narrow corridor of a jump tunnel, or were sent to a place far enough away to make communication or a return trip infeasible. In either case, those missing are presumed dead. Ageira Technologies and Deep Space Engineering urge all citizens to use the prescribed Trade Lanes and Jump Gates and to avoid all contact with jump holes.</TEXT><PARA/><POP/></RDL>)";
	}
	const Universe::ISystem* sys = Universe::get_system(system);

	wstring base_status = L"<RDL><PUSH/>";
	base_status += L"<TEXT>" + XMLText(basename) + L", " + HkGetWStringFromIDS(sys->strid_name) + L"</TEXT><PARA/>";

	wstring affiliation_string = L"";
	if (affiliation && affiliation != DEFAULT_AFFILIATION)
	{
		affiliation_string = HkGetWStringFromIDS(Reputation::get_name(affiliation));
	}
	else
	{
		affiliation_string = L"Unaffiliated";
	}

	base_status += L"<TEXT>Core " + IntToStr(base_level) + L" " + affiliation_string + L" Installation</TEXT><PARA/><PARA/>";

	if (!infocardHeader.empty())
	{
		base_status += infocardHeader;
	}
	else
	{
		base_status += L"<PARA/>";
	}

	if (!pinned_market_items.empty())
	{
		base_status += L"<TEXT>Highlighted commodities:</TEXT>";
		for (auto& goodId : pinned_market_items)
		{
			const auto& item = market_items.at(goodId);
			wchar_t buf[240];
			const GoodInfo* gi = GoodList::find_by_id(goodId);
			if (!gi)
			{
				continue;
			}
			if (gi->iType == GOODINFO_TYPE_SHIP)
			{
				gi = GoodList::find_by_id(gi->iHullGoodID);
			}
			wstring name = HkGetWStringFromIDS(gi->iIDSName);
			wstring stock = UIntToPrettyStr(item.quantity);
			wstring buyPrice = UIntToPrettyStr(item.price);
			wstring sellPrice = UIntToPrettyStr(item.sellPrice);
			wstring minStock = UIntToPrettyStr(item.min_stock);
			wstring maxStock = UIntToPrettyStr(item.max_stock);
			swprintf(buf, _countof(buf), L"<PARA/><TEXT>- %ls: x%ls | Buys at $%ls Sells at $%ls | Min: %ls Max: %ls</TEXT>",
				name.c_str(), stock.c_str(), sellPrice.c_str(), buyPrice.c_str(), minStock.c_str(), maxStock.c_str());
			base_status += buf;
		}
		base_status += L"<PARA/><PARA/>";
	}

	if (!infocard.empty())
	{
		base_status += infocard;
	}

	return base_status;
}

wstring PlayerBase::BuildBaseDescription()
{
	wstring base_info = GetBaseHeaderText();

	if (archetype && archetype->isjump)
	{
		return base_info;
	}

	if (single_vulnerability_window)
	{
		wchar_t buf[75];
		swprintf(buf, _countof(buf), L"<TEXT>Vulnerability Window: %u:00 - %u:%02u</TEXT><PARA/>", vulnerabilityWindow1.start / 60, vulnerabilityWindow1.end / 60, vulnerabilityWindow1.end % 60);
		base_info += buf;
	}
	else
	{
		wchar_t buf[125];
		swprintf(buf, _countof(buf), L"<TEXT>Vulnerability Windows: %u:00 - %u:%02u, %u:00 - %u:%02u</TEXT><PARA/>",
			vulnerabilityWindow1.start / 60, vulnerabilityWindow1.end / 60, vulnerabilityWindow1.end % 60,
			vulnerabilityWindow2.start / 60, vulnerabilityWindow2.end / 60, vulnerabilityWindow2.end % 60);
		base_info += buf;
	}

	base_info += L"<POP/></RDL>";

	return base_info;
}

void PlayerBase::UpdateBaseInfoText()
{
	description_text = BuildBaseDescription();
	PlayerData* pd = nullptr;
	while (pd = Players.traverse_active(pd))
	{
		HkChangeIDSString(pd->iOnlineID, description_ids, description_text);
		SendBaseIDSList(pd->iOnlineID, baseCSolar->id, description_ids);
	}
}

void PlayerBase::Load()
{
	INI_Reader ini;
	BuildModule* coreConstruction = nullptr;
	uint moduleCounter = 0;
	modules.resize(1);
	if (ini.open(path.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("Base"))
			{
				int paraindex = 0;
				invulnerable = 0;
				logic = 1;
				string defaultsystem = "iw09";

				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						nickname = ini.get_value_string();
					}
					else if (ini.is_value("basetype"))
					{
						basetype = ini.get_value_string();
						auto arch = mapArchs.find(basetype);
						if (arch == mapArchs.end())
						{
							ConPrint(L"ERROR: UNABLE TO FIND BASE ARCHETYPE \"%s\"!\n", stows(basetype).c_str());
							archetype = &mapArchs[basetype];
						}
						else
						{
							archetype = &arch->second;
						}
					}
					else if (ini.is_value("basesolar"))
					{
						basesolar = ini.get_value_string();
					}
					else if (ini.is_value("baseloadout"))
					{
						baseloadout = ini.get_value_string();
					}
					else if (ini.is_value("upgrade"))
					{
						base_level = ini.get_value_int(0);
						modules.resize((base_level * 3) + 1);
					}
					else if (ini.is_value("affiliation"))
					{
						affiliation = ini.get_value_int(0);
						if (!affiliation)
						{
							affiliation = DEFAULT_AFFILIATION;
						}
					}
					else if (ini.is_value("system"))
					{
						string sysNickname = ini.get_value_string(0);
						uint systemId = Universe::get_system_id(sysNickname.c_str());
						if (systemId)
						{
							system = systemId;
						}
						else
						{
							system = ini.get_value_int(0);
						}
					}
					else if (ini.is_value("pos"))
					{
						position.x = ini.get_value_float(0);
						position.y = ini.get_value_float(1);
						position.z = ini.get_value_float(2);
					}
					else if (ini.is_value("rot"))
					{
						Vector erot;
						erot.x = ini.get_value_float(0);
						erot.y = ini.get_value_float(1);
						erot.z = ini.get_value_float(2);
						rotation = EulerMatrix(erot);
					}
					else if (ini.is_value("destobject"))
					{
						destObjectName = ini.get_value_string(0);
						destObject = CreateID(destObjectName.c_str());
					}
					else if (ini.is_value("destsystem"))
					{
						string sysNickname = ini.get_value_string(0);
						uint systemId = Universe::get_system_id(sysNickname.c_str());
						if (systemId)
						{
							destSystem = systemId;
						}
						else
						{
							destSystem = ini.get_value_int(0);
						}
					}
					else if (ini.is_value("destpos"))
					{
						destPos = { ini.get_value_float(0), ini.get_value_float(1), ini.get_value_float(2) };
					}
					else if (ini.is_value("destori"))
					{
						Vector ori = { ini.get_value_float(0), ini.get_value_float(1), ini.get_value_float(2) };
						destOri = EulerMatrix(ori);
					}
					else if (ini.is_value("logic"))
					{
						logic = ini.get_value_int(0);
					}
					else if (ini.is_value("public"))
					{
						isPublic = ini.get_value_bool(0);
					}
					else if (ini.is_value("invulnerable"))
					{
						invulnerable = ini.get_value_int(0);
					}
					else if (ini.is_value("shieldstrength"))
					{
						shield_strength_multiplier = ini.get_value_float(0);
					}
					else if (ini.is_value("shielddmgtaken"))
					{
						damage_taken_since_last_threshold = ini.get_value_float(0);
					}
					else if (ini.is_value("last_vulnerability_change"))
					{
						lastVulnerabilityWindowChange = ini.get_value_int(0);
					}
					else if (ini.is_value("vulnerability_windows"))
					{
						vulnerabilityWindow1 = { ini.get_value_int(0) * 60, ((ini.get_value_int(0) * 60) + vulnerability_window_length) % (60 * 24) };
						vulnerabilityWindow2 = { ini.get_value_int(1) * 60, ((ini.get_value_int(1) * 60) + vulnerability_window_length) % (60 * 24) };
					}
					else if (ini.is_value("infoname"))
					{
						ini_get_wstring(ini, basename);
					}
					else if (ini.is_value("infocardpara"))
					{
						ini_get_wstring(ini, infocard_para[++paraindex]);
					}
					else if (ini.is_value("infocardpara2"))
					{
						wstring infopara2;
						ini_get_wstring(ini, infopara2);
						infocard_para[paraindex] += infopara2;
					}
					else if (ini.is_value("money"))
					{
						sscanf(ini.get_value_string(), "%I64d", &money);
					}
					else if (ini.is_value("preferred_food"))
					{
						preferred_food = ini.get_value_int(0);
					}
					else if (ini.is_value("commodity"))
					{
						MARKET_ITEM mi;
						UINT good = ini.get_value_int(0);
						mi.quantity = ini.get_value_int(1);
						mi.price = ini.get_value_int(2);
						mi.min_stock = ini.get_value_int(3);
						mi.max_stock = ini.get_value_int(4);
						mi.is_public = bool(ini.get_value_int(5));
						mi.sellPrice = ini.get_value_int(6);
						mi.is_pinned = bool(ini.get_value_int(7));
						if (mi.is_pinned)
						{
							pinned_market_items.insert(good);
						}
						if (mi.is_public)
						{
							public_market_items.insert(good);
						}
						if (!mi.sellPrice)
						{
							mi.sellPrice = mi.price;
						}


						const GoodInfo* gi = GoodList_get()->find_by_id(good);
						if (gi)
						{
							if (gi->iType == GOODINFO_TYPE_SHIP)
							{
								mi.shipHullId = gi->iHullGoodID;
							}
							market_items[good] = mi;
						}
					}
					else if (ini.is_value("health"))
					{
						base_health = ini.get_value_float(0);
					}
					else if (ini.is_value("defensemode"))
					{
						defense_mode = (DEFENSE_MODE)ini.get_value_int(0);
					}
					else if (ini.is_value("srp_tag"))
					{
						wstring tag;
						ini_get_wstring(ini, tag);
						if (!tag.empty())
						{
							srp_tags.emplace_back(tag);
						}
					}
					else if (ini.is_value("srp_name"))
					{
						wstring tag;
						ini_get_wstring(ini, tag);
						if (!tag.empty())
						{
							srp_names.insert(tag);
						}
					}
					else if (ini.is_value("faction_srp_tag"))
					{
						srp_factions.insert(ini.get_value_int(0));
					}
					else if (ini.is_value("ally_tag"))
					{
						wstring tag;
						ini_get_wstring(ini, tag);
						if (!tag.empty())
						{
							ally_tags.emplace_back(tag);
						}
					}
					else if (ini.is_value("ally_name"))
					{
						wstring tag;
						ini_get_wstring(ini, tag);
						if (!tag.empty())
						{
							ally_names.insert(tag);
						}
					}
					else if (ini.is_value("faction_ally_tag"))
					{
						ally_factions.insert(ini.get_value_int(0));
					}
					else if (ini.is_value("nodock_tag"))
					{
						wstring tag;
						ini_get_wstring(ini, tag);
						if (!tag.empty())
						{
							nodock_tags.emplace_back(tag);
						}
					}
					else if (ini.is_value("nodock_name"))
					{
						wstring tag;
						ini_get_wstring(ini, tag);
						if (!tag.empty())
						{
							nodock_names.insert(tag);
						}
					}
					else if (ini.is_value("nodock_faction"))
					{
						nodock_factions.insert(ini.get_value_int(0));
					}
					else if (ini.is_value("hostile_tag"))
					{
						wstring tag;
						ini_get_wstring(ini, tag);
						if (!tag.empty())
						{
							hostile_tags.push_back(tag);
						}
					}
					else if (ini.is_value("hostile_name"))
					{
						wstring tag;
						ini_get_wstring(ini, tag);
						if (!tag.empty())
						{
							hostile_names.insert(tag);
						}
					}
					else if (ini.is_value("faction_hostile_tag"))
					{
						hostile_factions.insert(ini.get_value_int(0));
					}
					else if (ini.is_value("passwd"))
					{
						wstring passwd;
						ini_get_wstring(ini, passwd);
						BasePassword bp;
						bp.pass = GetParam(passwd, ' ', 0);
						if (GetParam(passwd, ' ', 1) == L"viewshop")
						{
							bp.viewshop = true;
						}
						else {
							bp.admin = true;
						}
						passwords.emplace_back(bp);
					}
					else if (ini.is_value("crew_supplied"))
					{
						isCrewSupplied = ini.get_value_bool(0);
					}
					else if (ini.is_value("crew_fed"))
					{
						fed_workers[ini.get_value_int(0)] = ini.get_value_int(1);
					}
					else if (ini.is_value("dock_key"))
					{
						dockKeyList.insert(ini.get_value_int(0));
					}
					else if (ini.is_value("no_dock_key_msg"))
					{
						noDockKeyMessage = stows(ini.get_value_string());
					}
					else if (ini.is_value("rearmament_margin"))
					{
						rearmamentCostPerCredit = ini.get_value_float(0);
					}
				}
				if (basetype.empty())
				{
					basetype = "legacy";
					archetype = &mapArchs[basetype];
				}
				if (basesolar.empty())
				{
					basesolar = "legacy";
				}
				if (baseloadout.empty())
				{
					baseloadout = "legacy";
				}
				base = CreateID(nickname.c_str());
			}
			else if (ini.is_header("CoreModule"))
			{
				CoreModule* mod = new CoreModule(this);
				mod->LoadState(ini);
				modules.at(moduleCounter) = mod;
				moduleCounter++;
			}
			else if (ini.is_header("BuildModule"))
			{
				BuildModule* mod = new BuildModule(this);
				mod->LoadState(ini);
				if (mod->active_recipe.shortcut_number == Module::TYPE_CORE)
				{
					coreConstruction = mod;
				}
				else
				{
					if (moduleCounter >= modules.size())
					{
						ConPrint(L"ERR TOO MANY MODULES ON %ls, SKIPPING BUID MODULE", this->basename.c_str());
						continue;
					}
					modules.at(moduleCounter) = mod;
					moduleCounter++;
				}
			}
			else if (ini.is_header("StorageModule"))
			{
				if (moduleCounter >= modules.size())
				{
					ConPrint(L"ERR TOO MANY MODULES ON %ls, SKIPPING STORAGE MODULE", this->basename.c_str());
					continue;
				}
				StorageModule* mod = new StorageModule(this);
				mod->LoadState(ini);
				modules.at(moduleCounter) = mod;
				moduleCounter++;
			}
			else if (ini.is_header("DefenseModule"))
			{
				if (moduleCounter >= modules.size())
				{
					ConPrint(L"ERR TOO MANY MODULES ON %ls, SKIPPING DEFENSE MODULE", this->basename.c_str());
					continue;
				}
				DefenseModule* mod = new DefenseModule(this);
				mod->LoadState(ini);
				modules.at(moduleCounter) = mod;
				moduleCounter++;
			}
			else if (ini.is_header("FactoryModule"))
			{
				if (moduleCounter >= modules.size())
				{
					ConPrint(L"ERR TOO MANY MODULES ON %ls, SKIPPING FACTORY MODULE", this->basename.c_str());
					continue;
				}
				FactoryModule* mod = new FactoryModule(this);
				mod->LoadState(ini);
				modules.at(moduleCounter) = mod;
				moduleCounter++;
			}
			else if (ini.is_header("RearmamentModule"))
			{
				if (moduleCounter >= modules.size())
				{
					ConPrint(L"ERR TOO MANY MODULES ON %ls, SKIPPING REARMAMENT MODULE", this->basename.c_str());
					continue;
				}
				RearmamentModule* mod = new RearmamentModule(this);
				mod->LoadState(ini);
				modules.at(moduleCounter) = mod;
				moduleCounter++;
			}
		}
		if (coreConstruction)
		{
			modules.emplace_back(coreConstruction);
		}
		ini.close();
	}
}

void PlayerBase::Save()
{
	FILE* file = fopen(path.c_str(), "w");
	if (file)
	{
		fprintf(file, "[Base]\n");
		fprintf(file, "nickname = %s\n", nickname.c_str());
		fprintf(file, "basetype = %s\n", basetype.c_str());
		fprintf(file, "basesolar = %s\n", basesolar.c_str());
		fprintf(file, "baseloadout = %s\n", baseloadout.c_str());
		fprintf(file, "upgrade = %u\n", base_level);
		fprintf(file, "affiliation = %u\n", affiliation);
		fprintf(file, "logic = %u\n", logic);
		fprintf(file, "invulnerable = %u\n", invulnerable);
		fprintf(file, "crew_supplied = %u\n", isCrewSupplied ? 1 : 0);
		for (auto& workers : fed_workers)
		{
			fprintf(file, "crew_fed = %u, %u\n", workers.first, workers.second);
		}
		if (preferred_food)
		{
			fprintf(file, "preferred_food = %u\n", preferred_food);
		}
		if (isPublic)
		{
			fprintf(file, "public = %u\n", isPublic ? 1 : 0);
		}
		fprintf(file, "shieldstrength = %f\n", shield_strength_multiplier);
		fprintf(file, "shielddmgtaken = %f\n", damage_taken_since_last_threshold);
		fprintf(file, "last_vulnerability_change = %u\n", lastVulnerabilityWindowChange);
		fprintf(file, "vulnerability_windows = %u, %u\n", vulnerabilityWindow1.start / 60, vulnerabilityWindow2.start / 60);

		if (isRearmamentAvailable)
		{
			fprintf(file, "rearmament_margin = %0.3f\n", rearmamentCostPerCredit);
		}
		fprintf(file, "money = %I64d\n", money);
		auto sysInfo = Universe::get_system(system);
		fprintf(file, "system = %s\n", sysInfo->nickname.value);
		fprintf(file, "pos = %0.0f, %0.0f, %0.0f\n", position.x, position.y, position.z);

		Vector vRot = MatrixToEuler(rotation);
		fprintf(file, "rot = %0.0f, %0.0f, %0.0f\n", vRot.x, vRot.y, vRot.z);
		if (archetype && archetype->ishubreturn)
		{
			const auto& destSystemInfo = Universe::get_system(destSystem);
			fprintf(file, "destsystem = %s\n", destSystemInfo->nickname.value);

			fprintf(file, "destpos = %0.0f, %0.0f, %0.0f\n", destPos.x, destPos.y, destPos.z);

			Vector destRot = MatrixToEuler(destOri);
			fprintf(file, "destori = %0.0f, %0.0f, %0.0f\n", destRot.x, destRot.y, destRot.z);
		}
		else if (archetype && archetype->isjump && destObject && pub::SpaceObj::ExistsAndAlive(destObject) == 0) //0 means alive, -2 dead
		{
			uint destSystemId;
			pub::SpaceObj::GetSystem(destObject, destSystemId);
			const auto& destSystemInfo = Universe::get_system(destSystemId);
			fprintf(file, "destsystem = %s\n", destSystemInfo->nickname.value);
			fprintf(file, "destobject = %s\n", destObjectName.c_str());
		}

		ini_write_wstring(file, "infoname", basename);
		for (int i = 1; i <= MAX_PARAGRAPHS; i++)
		{
			ini_write_wstring(file, "infocardpara", infocard_para[i].substr(0, 252));
			if (infocard_para[i].length() >= 252)
				ini_write_wstring(file, "infocardpara2", infocard_para[i].substr(252, 252));
		}
		for (const auto& i : market_items)
		{
			fprintf(file, "commodity = %u, %u, %u, %u, %u, %u, %u, %u\n",
				i.first, i.second.quantity, i.second.price, i.second.min_stock,
				i.second.max_stock, int(i.second.is_public), i.second.sellPrice, int(i.second.is_pinned));
		}

		fprintf(file, "defensemode = %u\n", defense_mode);
		for (const auto& i : srp_tags)
		{
			ini_write_wstring(file, "srp_tag", i);
		}
		for (const auto& i : srp_names)
		{
			ini_write_wstring(file, "srp_name", i);
		}
		for (const auto& i : srp_factions)
		{
			fprintf(file, "faction_srp_tag = %d\n", i);
		}
		for(const auto& i : ally_tags)
		{
			ini_write_wstring(file, "ally_tag", i);
		}
		for (const auto& i : ally_names)
		{
			ini_write_wstring(file, "ally_name", i);
		}
		for (const auto& i : ally_factions)
		{
			fprintf(file, "faction_ally_tag = %d\n", i);
		}
		for (const auto& i : nodock_factions)
		{
			fprintf(file, "nodock_faction = %d\n", i);
		}
		for (const auto& i : nodock_tags)
		{
			ini_write_wstring(file, "nodock_tag", i);
		}
		for (const auto& i : nodock_names)
		{
			ini_write_wstring(file, "nodock_name", i);
		}
		for (const auto& i : hostile_factions)
		{
			fprintf(file, "faction_hostile_tag = %d\n", i);
		}
		for(const auto& i : hostile_tags)
		{
			ini_write_wstring(file, "hostile_tag", i);
		}
		for (const auto& i : hostile_names)
		{
			ini_write_wstring(file, "hostile_name", i);
		}
		for (const auto& i : dockKeyList)
		{
			fprintf(file, "dock_key = %u\n", i);
		}
		if (!noDockKeyMessage.empty())
		{
			fprintf(file, "no_dock_key_msg = %s\n", wstos(noDockKeyMessage).c_str());
		}
		foreach(passwords, BasePassword, i)
		{
			BasePassword bp = *i;
			wstring l = bp.pass;
			if (!bp.admin && bp.viewshop)
			{
				l += L" viewshop";
			}
			ini_write_wstring(file, "passwd", l);
		}
		fprintf(file, "health = %0.0f\n", base_health);

		for (vector<Module*>::iterator i = modules.begin(); i != modules.end(); ++i)
		{
			if (*i)
			{
				(*i)->SaveState(file);
			}
		}

		fclose(file);
	}

	SendBaseStatus(this);
}


bool PlayerBase::AddMarketGood(uint good, uint quantity)
{
	if (quantity == 0)
	{
		return true;
	}

	float vol, mass;
	pub::GetGoodProperties(good, vol, mass);

	if (GetRemainingCargoSpace() < (quantity * vol)
		|| (market_items.count(good) && market_items[good].max_stock < market_items[good].quantity + quantity))
	{
		return false;
	}

	market_items[good].quantity += quantity;
	const GoodInfo* gi = GoodList::find_by_id(good);

	if (gi->iType == GOODINFO_TYPE_SHIP)
	{
		market_items[good].shipHullId = gi->iHullGoodID;
	}
	SendMarketGoodUpdated(this, good, market_items[good]);
	return true;
}

void PlayerBase::RemoveMarketGood(uint good, uint quantity)
{
	auto iter = market_items.find(good);
	if (iter != market_items.end())
	{
		iter->second.quantity = max(0, iter->second.quantity - quantity);
		SendMarketGoodUpdated(this, good, iter->second);
	}
}

void PlayerBase::ChangeMoney(INT64 the_money)
{
	money += the_money;
	if (money < 0)
	{
		money = 0;
	}
}

int PlayerBase::GetRemainingCargoSpace()
{
	uint used = 0;
	for (auto i = market_items.begin(); i != market_items.end(); ++i)
	{
		float vol, mass;
		pub::GetGoodProperties(i->first, vol, mass);
		used += (uint)((float)i->second.quantity * vol);
	}

	if (used > storage_space)
	{
		return 0;
	}
	return storage_space - used;
}

void PlayerBase::RecalculateCargoSpace()
{
	storage_space = 0;
	for (Module* mod : modules)
	{
		if (mod)
		{
			storage_space += mod->cargoSpace;
		}
	}
}

string PlayerBase::CreateBaseNickname(const string& basename)
{
	return string("pb_") + basename;
}

uint PlayerBase::HasMarketItem(uint good)
{
	auto i = market_items.find(good);
	if (i != market_items.end())
	{
		return i->second.quantity;
	}
	return 0;
}

uint PlayerBase::HasFedWorkerItem(uint good)
{
	auto i = fed_workers.find(good);
	if (i != fed_workers.end())
	{
		return i->second;
	}
	return 0;
}

bool PlayerBase::IsOnSRPList(const wstring& charname, uint affiliation)
{
	if (srp_factions.count(affiliation))
	{
		return true;
	}
	if (srp_names.count(charname))
	{
		return true;
	}
	for (auto& i : srp_tags)
	{
		if (charname.find(i) == 0)
		{
			return true;
		}
	}

	return false;
}

bool PlayerBase::IsOnAllyList(const wstring& charname, uint affiliation)
{
	if (ally_factions.count(affiliation))
	{
		return true;
	}
	if (ally_names.count(charname))
	{
		return true;
	}
	for (auto& i : ally_tags)
	{
		if (charname.find(i) == 0)
		{
			return true;
		}
	}

	return false;
}

bool PlayerBase::IsOnNodockList(const wstring& charname, uint affiliation)
{
	if (nodock_factions.count(affiliation))
	{
		return true;
	}
	if (nodock_names.count(charname))
	{
		return true;
	}
	for (auto& i : nodock_tags)
	{
		if (charname.find(i) == 0)
		{
			return true;
		}
	}

	return false;
}

bool PlayerBase::IsOnHostileList(const wstring& charname, uint affiliation)
{
	if (hostile_factions.count(affiliation))
	{
		return true;
	}
	if (hostile_names.count(charname))
	{
		return true;
	}
	for (auto& i : hostile_names)
	{
		if (charname.find(i) == 0)
		{
			return true;
		}
	}

	return false;
}

float PlayerBase::GetAttitudeTowardsClient(uint client)
{
	if (archetype && archetype->isjump)
	{
		return 0.0f;
	}

	// By default all bases are hostile to everybody.
	float attitude = 1.0f;

	// If an affiliation is defined then use the player's attitude.
	if (affiliation)
	{
		int rep = Players[client].iReputation;
		pub::Reputation::GetGroupFeelingsTowards(rep, affiliation, attitude);
	}

	wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
	uint playeraff = GetAffliationFromClient(client);

	if (IsOnSRPList(charname, playeraff))
	{
		return 1.0f;
	}

	if (attitude <= -0.6f)
	{
		return -1.0f;
	}

	if (vulnerableWindowStatus != BASE_VULNERABILITY_STATE::INVULNERABLE 
		&& temp_hostile_names.count(charname))
	{
		return -1.0f;
	}

	if (defense_mode == DEFENSE_MODE::IFF)
	{
		if (IsOnHostileList(charname, playeraff))
		{
			if (vulnerableWindowStatus != BASE_VULNERABILITY_STATE::INVULNERABLE)
			{
				return -1.0f;
			}
			return -0.59f;
		}

		return 1.0f;
	}

	float no_access_rep = -1.0f;
	if (defense_mode == DEFENSE_MODE::NODOCK_NEUTRAL)
	{
		no_access_rep = -0.59f;
	}

	if (IsOnNodockList(charname, playeraff))
	{
		return -0.59f;
	}

	if (IsOnHostileList(charname, playeraff))
	{
		if (vulnerableWindowStatus != BASE_VULNERABILITY_STATE::INVULNERABLE)
		{
			return -1.0f;
		}
		return no_access_rep;
	}

	if (IsOnAllyList(charname, playeraff))
	{
		return 1.0f;
	}

	// if a player has no standing at all, be neutral otherwise newbies all get shot
	return no_access_rep;
}

// For all players in the base's system, resync their reps towards all objects
// of this base.
void PlayerBase::SyncReputationForBase()
{
	struct PlayerData* pd = nullptr;
	while (pd = Players.traverse_active(pd))
	{
		if (pd->iShipID && pd->iSystemID == system)
		{
			int player_rep;
			pub::SpaceObj::GetRep(pd->iShipID, player_rep);
			float attitude = GetAttitudeTowardsClient(pd->iOnlineID);
			for (auto& i : modules)
			{
				if (i)
				{
					i->SetReputation(player_rep, attitude);
				}
			}
		}
	}
}

// For all players in the base's system, resync their reps towards this object.
void PlayerBase::SyncReputationForBaseObject(uint space_obj)
{
	struct PlayerData* pd = 0;
	while (pd = Players.traverse_active(pd))
	{
		if (pd->iShipID && pd->iSystemID == system)
		{
			int player_rep;
			pub::SpaceObj::GetRep(pd->iShipID, player_rep);
			float attitude = GetAttitudeTowardsClient(pd->iOnlineID);

			int obj_rep;
			pub::SpaceObj::GetRep(space_obj, obj_rep);
			pub::Reputation::SetAttitude(obj_rep, player_rep, attitude);
		}
	}
}

void ReportAttack(wstring basename, wstring charname, uint system, wstring alert_phrase = L"is under attack by")
{
	wstring wscMsg = L"Base %b %s %p!";
	wscMsg = ReplaceStr(wscMsg, L"%b", basename);
	wscMsg = ReplaceStr(wscMsg, L"%p", charname);
	wscMsg = ReplaceStr(wscMsg, L"%s", alert_phrase);

	const Universe::ISystem* iSys = Universe::get_system(system);
	wstring sysname = stows(iSys->nickname.value);

	HkMsgS(sysname.c_str(), wscMsg.c_str());

	// Logging
	wstring wscMsgLog = L": Base %b is under attack by %p!";
	wscMsgLog = ReplaceStr(wscMsgLog, L"%p", charname);
	wscMsgLog = ReplaceStr(wscMsgLog, L"%b", basename);
	string scText = wstos(wscMsgLog);
	BaseLogging("%s", scText.c_str());

	return;
}

// Return true if 
void PlayerBase::SpaceObjDamaged(uint space_obj, uint attacking_space_obj, float incoming_damage)
{
	if (invulnerable)
	{
		return;
	}

	// Make sure that the attacking player is hostile.
	uint client = HkGetClientIDByShip(attacking_space_obj);
	if (!client)
	{
		return;
	}
	const wstring& charname = (const wchar_t*)Players.GetActiveCharacterName(client);

	if (!temp_hostile_names.count(charname))
	{
		temp_hostile_names.insert(charname);
		ReportAttack(this->basename, charname, this->system);
		SyncReputationForBase();
	}

	damageTakenMap[charname] += incoming_damage;
}

bool PlayerBase::FeedCrew(uint crewId, uint count)
{
	uint crewToFeed = count;
	bool passedFoodCheck = true;

	bool canFeedCrew = true;

	for (uint item : set_base_crew_consumption_items)
	{
		// Use water and oxygen.
		uint itemCount = HasMarketItem(item);
		if (itemCount < crewToFeed)
		{
			return false;
		}
	}

	uint foodItemCount = 0;
	for (uint item : set_base_crew_food_items)
	{
		foodItemCount += HasMarketItem(item);
	}

	if (foodItemCount < crewToFeed)
	{
		return false;
	}

	for (uint item : set_base_crew_consumption_items)
	{
		// Use water and oxygen.
		RemoveMarketGood(item, crewToFeed);
	}

	// Humans use food but may eat one of a number of types.

	if (preferred_food)
	{
		uint foodCount = HasMarketItem(preferred_food);
		uint food_to_use = min(foodCount, crewToFeed);
		RemoveMarketGood(preferred_food, food_to_use);
		crewToFeed -= food_to_use;
	}

	for (uint item : set_base_crew_food_items)
	{
		if (!crewToFeed)
		{
			break;
		}

		if (item == preferred_food)
		{
			continue;
		}

		uint food_available = HasMarketItem(item);
		if (food_available)
		{
			uint food_to_use = min(food_available, crewToFeed);
			RemoveMarketGood(item, food_to_use);
			crewToFeed -= food_to_use;
		}
	}

	fed_workers[crewId] += count;

	return true;
}