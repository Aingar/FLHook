#include "RecipeLoading.h"

static uint ModuleRecipeId(Module* module)
{
	if (module->type == Module::TYPE_FACTORY)
	{
		return static_cast<FactoryModule*>(module)->factoryNickname;
	}
	if (module->type == Module::TYPE_STORAGE_CUSTOM)
	{
		return CreateID(static_cast<StorageModule*>(module)->moduleNickname.c_str());
	}
	return 0;
}

static void ValidateBaseRecipes(PlayerBase* base, const RecipeTables& preparedTables)
{
	for (Module* module : base->modules)
	{
		if (!module)
		{
			continue;
		}
		if (module->type == Module::TYPE_BUILD)
		{
			const RECIPE& active = static_cast<BuildModule*>(module)->active_recipe;
			if (active.nickname && preparedTables.RequireRecipe(active.nickname, true).shortcut_number != active.shortcut_number)
			{
				throw runtime_error("cannot change the class of a module under construction");
			}
		}
		else if (module->type == Module::TYPE_FACTORY || module->type == Module::TYPE_STORAGE_CUSTOM)
		{
			const uint id = ModuleRecipeId(module);
			const RECIPE& definition = preparedTables.RequireRecipe(id, true);
			if (definition.shortcut_number != static_cast<uint>(module->type))
			{
				throw runtime_error("cannot change an installed module's class");
			}
			if (module->type == Module::TYPE_FACTORY && static_cast<FactoryModule*>(module)->active_recipe.nickname)
			{
				preparedTables.RequireRecipe(static_cast<FactoryModule*>(module)->active_recipe.nickname, false);
			}
		}
	}
}

static void ValidatePermanentCrew(uint level, const vector<Module*>& modules, const vector<PreparedModuleProperties>& properties)
{
	map<uint, uint64_t> counts;
	counts[set_base_crew_type] = static_cast<uint64_t>(level) * 200;
	for (const Module* module : modules)
	{
		if (!module || (module->type != Module::TYPE_FACTORY && module->type != Module::TYPE_STORAGE_CUSTOM))
		{
			continue;
		}
		const auto next = find_if(properties.begin(), properties.end(), [module](const PreparedModuleProperties& value)
		{
			return value.module == module;
		});
		const auto& crew = next == properties.end() ? module->crew : next->crew;
		for (const auto& worker : crew)
		{
			counts[worker.first] += worker.second;
		}
	}
	for (const auto& count : counts)
	{
		if (count.second > (numeric_limits<uint>::max)())
		{
			throw runtime_error("module crew total exceeds the supported amount");
		}
	}
}

static PreparedBaseRecipes PrepareRecipeBase(PlayerBase* base, const unordered_map<uint, RECIPE>& recipes, const unordered_map<uint, vector<wstring>>& craftLists)
{
	PreparedBaseRecipes next;
	next.base = base;
	for (Module* module : base->modules)
	{
		if (!module || (module->type != Module::TYPE_FACTORY && module->type != Module::TYPE_STORAGE_CUSTOM))
		{
			continue;
		}
		const RECIPE& definition = recipes.at(ModuleRecipeId(module));
		next.properties.push_back({ module, static_cast<int>(definition.moduleCargoStorage), definition.moduleCrew });
		if (module->type != Module::TYPE_FACTORY)
		{
			continue;
		}
		FactoryModule* factory = static_cast<FactoryModule*>(module);
		const auto groups = craftLists.find(factory->factoryNickname);
		if (groups != craftLists.end())
		{
			for (const wstring& group : groups->second)
			{
				next.lists.insert(group);
				next.factories[group] = factory;
			}
		}
	}
	ValidatePermanentCrew(base->base_level, base->modules, next.properties);
	return next;
}

vector<PreparedBaseRecipes> PrepareRecipeReload(const RecipeTables& preparedTables)
{
	for (const auto& upgrade : core_upgrade_recipes)
	{
		if (preparedTables.RequireRecipe(upgrade.second, true).shortcut_number != Module::TYPE_CORE)
		{
			throw runtime_error("core upgrade must reference a core recipe");
		}
	}
	vector<PreparedBaseRecipes> access;
	for (const auto& entry : player_bases)
	{
		PlayerBase* base = entry.second;
		if (!base)
		{
			continue;
		}
		ValidateBaseRecipes(base, preparedTables);
		access.push_back(PrepareRecipeBase(base, preparedTables.recipeMap, preparedTables.factoryNicknameToCraftTypeMap));
	}
	return access;
}
