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

static bool ShouldReplaceModule(Module* module, const RECIPE& recipe)
{
	return (module->type != static_cast<int>(recipe.shortcut_number) || ModuleRecipeId(module) != recipe.nickname)
		&& !(module->type == Module::TYPE_FACTORY && recipe.shortcut_number == Module::TYPE_STORAGE_CUSTOM
			&& static_cast<FactoryModule*>(module)->active_recipe.nickname);
}

static unique_ptr<Module> ConvertedModule(PlayerBase* base, Module* module, const RECIPE& recipe)
{
	if (!ShouldReplaceModule(module, recipe))
	{
		return nullptr;
	}
	unique_ptr<Module> replacement;
	if (recipe.shortcut_number == Module::TYPE_FACTORY)
	{
		unique_ptr<FactoryModule> factory(new FactoryModule(base));
		factory->factoryNickname = recipe.nickname;
		if (module->type == Module::TYPE_FACTORY)
		{
			const auto* previous = static_cast<FactoryModule*>(module);
			factory->active_recipe = previous->active_recipe;
			factory->amassedCookingRate = previous->amassedCookingRate;
			factory->build_queue = previous->build_queue;
			factory->Paused = previous->Paused;
			factory->sufficientCatalysts = previous->sufficientCatalysts;
			factory->errorMessage = previous->errorMessage;
		}
		replacement = move(factory);
	}
	else if (recipe.shortcut_number == Module::TYPE_STORAGE_CUSTOM)
	{
		unique_ptr<StorageModule> storage(new StorageModule(base));
		storage->type = Module::TYPE_STORAGE_CUSTOM;
		storage->moduleNickname = recipe.nicknameString;
		replacement = move(storage);
	}
	else
	{
		throw runtime_error("replacement must be a factory or repeatable storage module");
	}
	replacement->cargoSpace = recipe.moduleCargoStorage;
	replacement->crew = recipe.moduleCrew;
	return replacement;
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
			if (active.nickname && preparedTables.RequireRecipe(active.nickname, true).shortcut_number != active.shortcut_number
				&& !preparedTables.replacements.count(active.nickname))
			{
				throw runtime_error("cannot change the class of a module under construction");
			}
		}
		else if (module->type == Module::TYPE_FACTORY || module->type == Module::TYPE_STORAGE_CUSTOM)
		{
			const uint id = ModuleRecipeId(module);
			const RECIPE& definition = preparedTables.RequireRecipe(id, true);
			if (definition.shortcut_number != static_cast<uint>(module->type) && !preparedTables.replacements.count(id)
				&& !(module->type == Module::TYPE_FACTORY && definition.shortcut_number == Module::TYPE_STORAGE_CUSTOM
					&& ::recipeMap.count(id) && ::recipeMap.at(id).shortcut_number == Module::TYPE_STORAGE_CUSTOM))
			{
				throw runtime_error("cannot change an installed module's class without replace_with");
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
	if (level > (numeric_limits<uint>::max)() / 200)
	{
		throw runtime_error("core crew total exceeds the supported amount");
	}
	unordered_map<uint, uint> counts;
	counts[set_base_crew_type] = level * 200;
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
			if (worker.second > (numeric_limits<uint>::max)() - counts[worker.first])
			{
				throw runtime_error("module crew total exceeds the supported amount");
			}
			counts[worker.first] += worker.second;
		}
	}
}

static PreparedBaseRecipes PrepareRecipeBase(PlayerBase* base, const unordered_map<uint, RECIPE>& recipes, const unordered_map<uint, vector<wstring>>& craftLists)
{
	PreparedBaseRecipes next;
	next.base = base;
	next.modules = base->modules;
	for (size_t i = 0; i < next.modules.size(); ++i)
	{
		Module* module = next.modules[i];
		if (!module || (module->type != Module::TYPE_FACTORY && module->type != Module::TYPE_STORAGE_CUSTOM))
		{
			continue;
		}
		const auto recipe = recipes.find(ModuleRecipeId(module));
		if (recipe == recipes.end() || recipe->second.nicknameString.empty())
		{
			continue;
		}
		auto replacement = ConvertedModule(base, module, recipe->second);
		if (replacement)
		{
			next.modules[i] = replacement.get();
			next.owned.push_back(move(replacement));
			next.retired.push_back(module);
		}
		module = next.modules[i];
		const RECIPE& definition = recipe->second;
		if (module->type == static_cast<int>(definition.shortcut_number))
		{
			next.properties.push_back({ module, static_cast<int>(definition.moduleCargoStorage), definition.moduleCrew });
		}
		if (module->type != Module::TYPE_FACTORY)
		{
			continue;
		}
		FactoryModule* factory = static_cast<FactoryModule*>(module);
		const auto groups = craftLists.find(factory->factoryNickname);
		if (groups == craftLists.end())
		{
			continue;
		}
		for (const wstring& group : groups->second)
		{
			next.lists.insert(group);
			next.factories[group] = factory;
		}
	}
	ValidatePermanentCrew(base->base_level, next.modules, next.properties);
	return next;
}

void ApplyModuleReplacements(PlayerBase* base)
{
	for (Module* module : base->modules)
	{
		if (!module || (module->type != Module::TYPE_FACTORY && module->type != Module::TYPE_STORAGE_CUSTOM))
		{
			continue;
		}
		const auto recipe = recipeMap.find(ModuleRecipeId(module));
		if (recipe == recipeMap.end() || recipe->second.nicknameString.empty())
		{
			continue;
		}
		if (!ShouldReplaceModule(module, recipe->second))
		{
			continue;
		}
		try
		{
			auto next = PrepareRecipeBase(base, recipeMap, factoryNicknameToCraftTypeMap);
			next.ApplyToBase();
		}
		catch (const exception& e)
		{
			ConPrint(L"BASE module conversion failed for %ls: %ls\n", base->basename.c_str(), stows(e.what()).c_str());
		}
		return;
	}
}

void ResolveModuleReplacements(RecipeTables& preparedTables)
{
	for (const auto& replacement : preparedTables.replacements)
	{
		if (preparedTables.recipeMap.count(replacement.first))
		{
			throw runtime_error("replacement nickname duplicates a module recipe");
		}
		unordered_set<uint> visited;
		uint target = replacement.first;
		while (preparedTables.replacements.count(target))
		{
			if (!visited.insert(target).second)
			{
				throw runtime_error("module replacement cycle");
			}
			target = preparedTables.replacements.at(target);
		}
		const RECIPE& recipe = preparedTables.RequireRecipe(target, true);
		if (recipe.shortcut_number != Module::TYPE_FACTORY && recipe.shortcut_number != Module::TYPE_STORAGE_CUSTOM)
		{
			throw runtime_error("replacement must target a factory or repeatable storage module");
		}
		preparedTables.recipeMap.emplace(replacement.first, recipe);
		preparedTables.moduleIds.insert(replacement.first);
		const auto lists = preparedTables.factoryNicknameToCraftTypeMap.find(target);
		if (lists != preparedTables.factoryNicknameToCraftTypeMap.end())
		{
			preparedTables.factoryNicknameToCraftTypeMap.emplace(replacement.first, lists->second);
		}
	}
}

static void PrepareBuildReplacements(PreparedBaseRecipes& next, const RecipeTables& preparedTables)
{
	for (Module* module : next.base->modules)
	{
		if (!module || module->type != Module::TYPE_BUILD)
		{
			continue;
		}
		BuildModule* build = static_cast<BuildModule*>(module);
		if (!preparedTables.replacements.count(build->active_recipe.nickname))
		{
			continue;
		}
		RECIPE active = build->active_recipe;
		const RECIPE& target = preparedTables.RequireRecipe(active.nickname, true);
		active.nickname = target.nickname;
		active.nicknameString = target.nicknameString;
		active.infotext = target.infotext;
		active.shortcut_number = target.shortcut_number;
		active.moduleCargoStorage = target.moduleCargoStorage;
		active.moduleCrew = target.moduleCrew;
		next.builds.emplace_back(build, move(active));
	}
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
		PreparedBaseRecipes next = PrepareRecipeBase(base, preparedTables.recipeMap, preparedTables.factoryNicknameToCraftTypeMap);
		PrepareBuildReplacements(next, preparedTables);
		access.push_back(move(next));
	}
	return access;
}
