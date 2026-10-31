#include "Main.h"

vector<REPAIR_ITEM> OrderedRepairMaterials(PlayerBase* base)
{
	vector<REPAIR_ITEM> result;
	for (const auto& item : set_base_repair_items)
	{
		if (item.good == base->preferred_repair && !base->disabled_repair.count(item.good))
		{
			result.push_back(item);
			break;
		}
	}
	for (const auto& item : set_base_repair_items)
	{
		if (item.good != base->preferred_repair && !base->disabled_repair.count(item.good))
		{
			result.push_back(item);
		}
	}
	return result;
}
