#include "Main.h"
#include <memory>

CongestionTotals maintenancePressure;

struct CongestionState
{
	Upkeep::Policy policy;
	map<uint, Upkeep::Settings> systems;

	double referenceVolume = 0;
	double damage = 0;
};

static unique_ptr<CongestionState> congestionState(new CongestionState);

vector<Upkeep::Material> RepairMaterials()
{
	vector<Upkeep::Material> result;
	for (const auto& item : set_base_repair_items)
	{
		result.push_back({ item.good, item.quantity, L"" });
	}
	return result;
}



static wstring FormatUpkeep(double bundles)
{
	auto items = RepairMaterials();
	for (auto& item : items)
	{
		const auto* good = GoodList::find_by_id(item.good);
		item.name = good ? HkGetWStringFromIDS(good->iIDSName) : L"Unknown";
	}
	return Upkeep::Format(bundles, items);
}

static wstring FormatAdditionalUpkeep(double bundles)
{
	const auto text = FormatUpkeep(bundles);
	return text == L"0" ? L"None" : text;
}

static UpkeepProjection FormatUpkeepProjection(double repairs, double additional)
{
	const bool valid = isfinite(repairs) && repairs >= 0 && isfinite(additional) && additional >= 0;
	UpkeepProjection text = { FormatUpkeep(repairs), FormatAdditionalUpkeep(additional), valid ? FormatUpkeep(repairs + additional) : L"Unavailable" };
	if (text.total != L"Unavailable" && repairs > 0 && additional > 0)
	{
		const double percent = additional / repairs * 100;
		if (isfinite(percent))
		{
			wostringstream amount;
			amount.imbue(locale::classic());
			amount.setf(ios::fixed, ios::floatfield);
			amount.precision(0);
			amount << round(percent);
			text.total += L" (" + amount.str() + L"% over baseline)";
		}
	}
	return text;
}

vector<uint> ConfiguredCoreLevels()
{
	return Upkeep::CoreLevels([](uint level)
	{
		const auto edge = core_upgrade_recipes.find(level);
		if (edge == core_upgrade_recipes.end())
		{
			return false;
		}
		const auto recipe = recipeMap.find(edge->second);
		return recipe != recipeMap.end() && recipe->second.shortcut_number == Module::TYPE_CORE && recipe->second.reqlevel <= level;
	});
}

static Upkeep::Settings CongestionSettings(uint system)
{
	const auto found = congestionState->systems.find(system);
	return found == congestionState->systems.end() ? congestionState->policy.defaults : found->second;
}

static bool ContributesPressure(PlayerBase* base)
{
	return Upkeep::Contributes(congestionState->policy, base->basetype, base->invulnerable != 0, base->logic != 0);
}

CongestionTotals BuildCongestionSnapshot(const Upkeep::Policy* policy)
{
	const auto& active = policy ? *policy : congestionState->policy;
	CongestionTotals totals;
	for (const auto& entry : player_bases)
	{
		PlayerBase* base = entry.second;
		if (!Upkeep::Contributes(active, base->basetype, base->invulnerable != 0, base->logic != 0))
		{
			continue;
		}
		const auto weight = active.weights.find(base->base_level);
		if (weight == active.weights.end())
		{
			totals[base->system] = numeric_limits<double>::quiet_NaN();
		}
		else
		{
			totals[base->system] += weight->second;
		}
	}
	return totals;
}

static double SystemPressure(uint system, const CongestionTotals& totals, PlayerBase* own = nullptr)
{
	const auto settings = CongestionSettings(system);
	if (settings.fixed >= 0)
	{
		return settings.fixed;
	}
	const auto total = totals.find(system);
	const double sum = total == totals.end() ? 0 : total->second;
	if (!isfinite(sum))
	{
		throw runtime_error("missing core weight or invalid system total");
	}
	double weight = 0;
	if (own && ContributesPressure(own))
	{
		weight = congestionState->policy.weights.at(own->base_level);
	}
	return Upkeep::Pressure(settings, sum, weight);
}

double CongestionDaily(PlayerBase* base, const CongestionTotals& totals)
{
	if (!congestionState->policy.enabled || !ContributesPressure(base))
	{
		return 0;
	}
	if (!base->modules.empty() && base->modules[0] && base->modules[0]->type == Module::TYPE_CORE && static_cast<CoreModule*>(base->modules[0])->dont_rust)
	{
		return 0;
	}
	const auto settings = CongestionSettings(base->system);
	const double pressure = settings.flatDailyVolume >= 0 ? 0 : SystemPressure(base->system, totals, base);
	return Upkeep::DailyBundles(congestionState->policy, settings, base->base_level, pressure, congestionState->referenceVolume);
}

double CongestionDamagePerBundle()
{
	return congestionState->damage;
}

UpkeepProjection ProjectedUpkeep(PlayerBase* base, const CongestionTotals& totals)
{
	try
	{
		if (set_holiday_mode || (!base->logic && base->invulnerable == 1))
		{
			return { L"0", L"None", L"0" };
		}
		CoreModule* core = !base->modules.empty() && base->modules[0] && base->modules[0]->type == Module::TYPE_CORE ? static_cast<CoreModule*>(base->modules[0]) : nullptr;
		if (!core || !set_tick_time)
		{
			return { L"Unavailable", L"Unavailable", L"Unavailable" };
		}
		double repair = core->intendedRepairDaily;
		if (repair < 0)
		{
			const double deficit = base->max_base_health - base->base_health + (core->dont_rust ? 0 : set_damage_per_tick);
			repair = Upkeep::Intended(deficit, static_cast<double>(repair_per_repair_cycle) * base->base_level, set_base_repair_items.size()) * Upkeep::Day / set_tick_time;
		}
		return FormatUpkeepProjection(repair, CongestionDaily(base, totals));
	}
	catch (const exception&)
	{
		return { L"Unavailable", L"Unavailable", L"Unavailable" };
	}
}

bool SpaceSuppliesEnabled()
{
	return congestionState->policy.spaceSupplies;
}

vector<pair<uint, UpkeepProjection>> ProjectedSystemUpkeep(uint system)
{
	const auto type = mapArchs.find("legacy");
	const bool paused = set_holiday_mode || (type != mapArchs.end() && !type->second.logic && type->second.invulnerable == 1);
	const auto totals = paused ? CongestionTotals{} : BuildCongestionSnapshot();
	vector<pair<uint, UpkeepProjection>> result;
	for (uint level : ConfiguredCoreLevels())
	{
		if (paused)
		{
			result.push_back({ level, { L"0", L"None", L"0" } });
			continue;
		}
		UpkeepProjection text = { L"Unavailable", L"Unavailable", L"Unavailable" };
		try
		{
			double ordinary = Upkeep::Maintenance(set_damage_per_tick, static_cast<double>(repair_per_repair_cycle) * level, set_base_repair_items.size(), Upkeep::WearInterval(set_tick_time, set_damage_tick_time), set_tick_time);
			double extra = 0;
			if (congestionState->policy.enabled && type != mapArchs.end() && Upkeep::Contributes(congestionState->policy, "legacy", type->second.invulnerable != 0, type->second.logic != 0))
			{
				const auto settings = CongestionSettings(system);
				const double pressure = settings.flatDailyVolume >= 0 ? 0 : SystemPressure(system, totals);
				extra = Upkeep::DailyBundles(congestionState->policy, settings, level, pressure, congestionState->referenceVolume);
			}
			text = FormatUpkeepProjection(ordinary, extra);
		}
		catch (const exception&)
		{
		}
		result.push_back({ level, text });
	}
	return result;
}

wstring ConstructionPolicyError(uint system)
{
	if (bannedSystemList.count(system))
	{
		return L"ERR Deploying base in this system is not possible";
	}
	if (!congestionState->policy.enabled)
	{
		return L"";
	}
	try
	{
		const auto settings = CongestionSettings(system);
		if (settings.ceiling < 0)
		{
			return L"";
		}
		if (!Upkeep::BuildAllowed(true, settings, SystemPressure(system, BuildCongestionSnapshot()), false))
		{
			return L"ERR System congestion is too high to deploy a base";
		}
	}
	catch (const exception&)
	{
		return L"ERR System congestion is unavailable";
	}
	return L"";
}

void RefreshUpkeepStatus(PlayerBase* base, const CongestionTotals& totals)
{
	CoreModule* core = !base->modules.empty() && base->modules[0] && base->modules[0]->type == Module::TYPE_CORE ? static_cast<CoreModule*>(base->modules[0]) : nullptr;
	try
	{
		UpkeepProjection upkeep = ProjectedUpkeep(base, totals);
		if (core && core->lastUpkeepText == upkeep.total)
		{
			return;
		}
		SendBaseStatus(base, &upkeep);
		if (core)
		{
			core->lastUpkeepText = std::move(upkeep.total);
		}
	}
	catch (const exception& e)
	{
		ConPrint(L"BASE upkeep status refresh failed for %ls: %ls\n", base->basename.c_str(), stows(e.what()).c_str());
	}
}

static Upkeep::Policy ReadCongestionPolicy(INI_Reader& ini, set<string>& banned, string& location)
{
	Upkeep::Policy policy;
	map<string, map<string, double>> overrides;
	set<string> seen;
	bool haveTypes = false;
	bool haveCurve = false;
	while (ini.read_header())
	{
		location = "base.cfg:" + to_string(ini.get_line_num());
		if (ini.is_header("general"))
		{
			while (ini.read_value())
			{
				if (ini.is_value("banned_system"))
				{
					location = "base.cfg:" + to_string(ini.get_line_num());
					if (ini.get_num_parameters() != 1 || !*ini.get_value_string(0))
					{
						throw runtime_error("invalid banned_system");
					}
					banned.insert(ToLower(ini.get_value_string(0)));
				}
			}
		}
		else if (ini.is_header("congestion_system"))
		{
			string system;
			map<string, double> fields;
			while (ini.read_value())
			{
				location = "base.cfg:" + to_string(ini.get_line_num());
				const string key = ToLower(ini.get_name_ptr());
				if (ini.get_num_parameters() != 1)
				{
					throw runtime_error("expected one value: " + key);
				}
				if (key == "system")
				{
					if (!system.empty() || !*ini.get_value_string(0))
					{
						throw runtime_error("invalid system");
					}
					system = ToLower(ini.get_value_string(0));
				}
				else
				{
					Upkeep::Settings check;
					const double value = Upkeep::Number(ini.get_value_string(0));
					Upkeep::SetSetting(check, key, value);
					if (!fields.emplace(key, value).second)
					{
						throw runtime_error("duplicate system setting: " + key);
					}
				}
			}
			if (system.empty())
			{
				throw runtime_error("congestion_system requires system");
			}
			if (!overrides.emplace(system, move(fields)).second)
			{
				throw runtime_error("duplicate setting: system:" + system);
			}
		}
		else if (ini.is_header("congestion"))
		{
			if (!seen.insert("[congestion]").second)
			{
				throw runtime_error("duplicate setting: [congestion]");
			}
			while (ini.read_value())
			{
				location = "base.cfg:" + to_string(ini.get_line_num());
				const string key = ToLower(ini.get_name_ptr());
				if (ini.is_value("core_weight"))
				{
					if (ini.get_num_parameters() != 2)
					{
						throw runtime_error("core_weight requires level, weight");
					}
					const double level = Upkeep::Number(ini.get_value_string(0));
					const double weight = Upkeep::Number(ini.get_value_string(1));
					if (level < 1 || level > (numeric_limits<Upkeep::Id>::max)() || floor(level) != level || weight < 0)
					{
						throw runtime_error("invalid core_weight");
					}
					if (!seen.insert("core:" + to_string(static_cast<Upkeep::Id>(level))).second)
					{
						throw runtime_error("duplicate core_weight");
					}
					policy.weights[static_cast<Upkeep::Id>(level)] = weight;
				}
				else if (ini.is_value("curve_segment"))
				{
					if (ini.get_num_parameters() != 4)
					{
						throw runtime_error("curve_segment requires four values");
					}
					if (!haveCurve)
					{
						policy.curve.clear();
						haveCurve = true;
					}
					policy.curve.push_back({ Upkeep::Number(ini.get_value_string(0)), Upkeep::Number(ini.get_value_string(1)),
						Upkeep::Number(ini.get_value_string(2)), Upkeep::Number(ini.get_value_string(3)) });
				}
				else if (ini.is_value("counted_type"))
				{
					if (ini.get_num_parameters() != 1 || !*ini.get_value_string(0))
					{
						throw runtime_error("invalid counted_type");
					}
					if (!haveTypes)
					{
						policy.types.clear();
						haveTypes = true;
					}
					policy.types.insert(ini.get_value_string(0));
				}
				else
				{
					if (ini.get_num_parameters() != 1)
					{
						throw runtime_error("expected one value: " + key);
					}
					if (!seen.insert("global:" + key).second)
					{
						throw runtime_error("duplicate setting: " + key);
					}
					if (ini.is_value("reference_repair_item"))
					{
						if (!*ini.get_value_string(0))
						{
							throw runtime_error("empty reference_repair_item");
						}
						policy.reference = ToLower(ini.get_value_string(0));
					}
					else if (key == "enabled" || key == "space_upkeep_projection")
					{
						const string value = ini.get_value_string(0);
						if (value != "0" && value != "1")
						{
							throw runtime_error(key + " must be 0 or 1");
						}
						(key == "enabled" ? policy.enabled : policy.spaceSupplies) = value == "1";
					}
					else
					{
						Upkeep::SetSetting(policy.defaults, key, Upkeep::Number(ini.get_value_string(0)));
					}
				}
			}
		}
	}
	location = "base.cfg congestion policy";
	std::sort(policy.curve.begin(), policy.curve.end(), [](const Upkeep::Segment& a, const Upkeep::Segment& b)
	{
		return a.start < b.start;
	});
	double previous = 0;
	for (size_t i = 0; i < policy.curve.size(); ++i)
	{
		const auto& s = policy.curve[i];
		if (s.start < 0 || s.percent < 0 || (i == 0 && (s.start != 0 || s.percent != 0)) ||
			(i && s.start == policy.curve[i - 1].start))
		{
			throw std::runtime_error("invalid curve segment start");
		}
		if (s.percent + 1e-9 < previous || s.linear < 0)
		{
			throw std::runtime_error("curve must be nondecreasing");
		}
		if (i + 1 < policy.curve.size())
		{
			const double span = policy.curve[i + 1].start - s.start;
			previous = s.percent + s.linear * span + s.quadratic * span * span;
			if (!std::isfinite(previous) || s.linear + 2 * s.quadratic * span < 0)
			{
				throw std::runtime_error("curve must remain finite and nondecreasing");
			}
		}
		else if (s.quadratic < 0)
		{
			throw std::runtime_error("last curve segment must remain nondecreasing");
		}
	}
	for (const auto& entry : overrides)
	{
		Upkeep::Settings settings = policy.defaults;
		for (const auto& field : entry.second)
		{
			Upkeep::SetSetting(settings, field.first, field.second);
		}
		policy.systems.emplace(entry.first, settings);
	}
	return policy;
}

bool ReloadCongestion(string& error, bool fullLoad)
{
	INI_Reader ini;
	error.clear();
	string location = "base.cfg";
	try
	{
		char directory[MAX_PATH];
		GetCurrentDirectory(sizeof(directory), directory);
		const string path = string(directory) + R"(\flhook_plugins\base.cfg)";
		if (!ini.open(path.c_str(), false))
		{
			throw runtime_error("cannot open base.cfg");
		}
		set<string> banned;
		unique_ptr<CongestionState> next(new CongestionState);
		next->policy = ReadCongestionPolicy(ini, banned, location);
		auto& candidate = next->policy;
		auto& systems = next->systems;
		unordered_set<uint> bans;
		for (const auto& entry : candidate.systems)
		{
			const uint system = CreateID(entry.first.c_str());
			if (!Universe::get_system(system))
			{
				throw runtime_error("unknown system: " + entry.first);
			}
			if (!systems.emplace(system, entry.second).second)
			{
				throw runtime_error("duplicate system ID: " + entry.first);
			}
		}
		for (const auto& name : banned)
		{
			const uint system = CreateID(name.c_str());
			if (!Universe::get_system(system))
			{
				throw runtime_error("unknown banned system: " + name);
			}
			bans.insert(system);
		}
		for (const auto& type : candidate.types)
		{
			if (!mapArchs.count(type))
			{
				throw runtime_error("unknown counted_type: " + type);
			}
		}
		auto totals = BuildCongestionSnapshot(&candidate);
		double& referenceVolume = next->referenceVolume;
		double& damage = next->damage;
		if (candidate.enabled)
		{
			const auto configuredLevels = ConfiguredCoreLevels();
			set<uint> levels(configuredLevels.begin(), configuredLevels.end());
			for (const auto& entry : player_bases)
			{
				if (Upkeep::Contributes(candidate, entry.second->basetype, entry.second->invulnerable != 0, entry.second->logic != 0))
				{
					levels.insert(entry.second->base_level);
				}
			}
			for (uint level : levels)
			{
				if (!candidate.weights.count(level))
				{
					throw runtime_error("missing core_weight: " + to_string(level));
				}
			}
			const auto* reference = GoodList_get()->find_by_name(candidate.reference.c_str());
			if (!reference)
			{
				throw runtime_error("unknown reference_repair_item: " + candidate.reference);
			}
			set<uint> goods;
			for (const auto& item : set_base_repair_items)
			{
				if (!GoodList::find_by_id(item.good) || !goods.insert(item.good).second || item.quantity > static_cast<uint>((numeric_limits<int>::max)()))
				{
					throw runtime_error("invalid or duplicate repair item");
				}
				if (item.good != CreateID(candidate.reference.c_str()))
				{
					continue;
				}
				float volume = 0, mass = 0;
				pub::GetGoodProperties(item.good, volume, mass);
				referenceVolume = volume * static_cast<double>(item.quantity);
			}
			if (!isfinite(referenceVolume) || referenceVolume <= 0)
			{
				throw runtime_error("reference repair bundle must have positive volume");
			}
			const double interval = Upkeep::WearInterval(set_tick_time, set_damage_tick_time);
			const double maintenance = Upkeep::Maintenance(set_damage_per_tick, repair_per_repair_cycle, set_base_repair_items.size(), interval, set_tick_time);
			if (!isfinite(maintenance) || maintenance <= 0)
			{
				throw runtime_error("Core 1 wear has no usable repair reference");
			}
			damage = set_damage_per_tick * (Upkeep::Day / interval) / maintenance;
			auto checks = totals;
			for (const auto& entry : systems)
			{
				checks.emplace(entry.first, 0);
			}
			checks.emplace(0, 0);
			for (const auto& total : checks)
			{
				if (!isfinite(total.second))
				{
					throw runtime_error("system pressure exceeds numeric range");
				}
				const auto found = systems.find(total.first);
				const auto settings = found == systems.end() ? candidate.defaults : found->second;
				for (uint level : levels)
				{
					const double daily = Upkeep::DailyBundles(candidate, settings, level, Upkeep::Pressure(settings, total.second), referenceVolume);
					for (const auto& item : set_base_repair_items)
					{
						if (!isfinite(daily * item.quantity))
						{
							throw runtime_error("congestion material amount exceeds numeric range");
						}
					}
					double carry = 0;
					if (!isfinite(Upkeep::Accrue(daily, set_tick_time, carry) * damage))
					{
						throw runtime_error("congestion damage exceeds numeric range");
					}
				}
			}
		}
		ini.close();
		congestionState.swap(next);
		maintenancePressure.swap(totals);
		bannedSystemList.swap(bans);
	}
	catch (const exception& e)
	{
		error = location + ": " + e.what();
		ini.close();
		if (fullLoad)
		{
			congestionState->policy.enabled = false;
			congestionState->policy.spaceSupplies = false;
			congestionState->referenceVolume = congestionState->damage = 0;
		}
		return false;
	}
	for (const auto& entry : player_bases)
	{
		PlayerBase* base = entry.second;
		CoreModule* core = !base->modules.empty() && base->modules[0] && base->modules[0]->type == Module::TYPE_CORE ? static_cast<CoreModule*>(base->modules[0]) : nullptr;
		if (core)
		{
			core->congestionCarry = 0;
			core->lastUpkeepText.clear();
		}
		RefreshUpkeepStatus(base, maintenancePressure);
	}
	return true;
}
