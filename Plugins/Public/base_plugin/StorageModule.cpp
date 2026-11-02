#include "Main.h"


StorageModule::StorageModule(PlayerBase* the_base)
	: Module(TYPE_STORAGE), base(the_base)
{
	cargoSpace = 40000;
}

StorageModule::~StorageModule()
{
}

StorageModule::StorageModule(PlayerBase* the_base, uint nickname)
	: Module(TYPE_STORAGE_CUSTOM), base(the_base)
{
	const auto recipe = recipeMap.find(nickname);
	if (recipe != recipeMap.end() && recipe->second.shortcut_number == TYPE_STORAGE_CUSTOM)
	{
		moduleNickname = recipe->second.nicknameString;
		cargoSpace = recipe->second.moduleCargoStorage;
		crew = recipe->second.moduleCrew;
	}
}

wstring StorageModule::GetInfo(bool xml)
{
	if (type != TYPE_STORAGE_CUSTOM)
	{
		return L"Cargo Storage";
	}
	const auto recipe = recipeMap.find(CreateID(moduleNickname.c_str()));
	wstring info = recipe == recipeMap.end() ? L"Unknown storage module" : recipe->second.infotext;
	if (xml)
	{
		info = XMLText(info);
		const wstring staffing = GetActiveCrewInfo();
		if (!staffing.empty())
		{
			info += L"</TEXT><PARA/><TEXT>      " + XMLText(staffing);
		}
	}
	return info;
}

void StorageModule::LoadState(INI_Reader& ini)
{
	while (ini.read_value())
	{
		if (!ini.is_value("type"))
		{
			continue;
		}
		moduleNickname = ini.get_value_string(0);
		type = TYPE_STORAGE_CUSTOM;
		cargoSpace = 0;
		crew.clear();
		const auto recipe = recipeMap.find(CreateID(moduleNickname.c_str()));
		if (recipe != recipeMap.end() && (recipe->second.shortcut_number == TYPE_STORAGE_CUSTOM || recipe->second.shortcut_number == TYPE_FACTORY))
		{
			cargoSpace = recipe->second.moduleCargoStorage;
			crew = recipe->second.moduleCrew;
		}
		else
		{
			ConPrint(L"BASE ERROR Missing storage module recipe: %ls\n", stows(ini.get_value_string(0)).c_str());
		}
	}
}

void StorageModule::SaveState(FILE* file)
{
	fprintf(file, "[StorageModule]\n");
	if (type == TYPE_STORAGE_CUSTOM)
	{
		fprintf(file, "type = %s\n", moduleNickname.c_str());
	}
}
