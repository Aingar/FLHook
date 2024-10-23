#pragma once

#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

using namespace std;

typedef std::map<uint, struct MarketGoodInfo, struct std::less<uint>, class std::allocator<struct MarketGoodInfo> > market_map_t;
typedef std::map<uint, struct MarketGoodInfo, struct std::less<uint>, class std::allocator<struct MarketGoodInfo> >::const_iterator market_map_iter_t;
typedef std::map<uint, struct MarketGoodInfo, struct std::less<uint>, class std::allocator<struct MarketGoodInfo> >::value_type market_map_pair_t;

extern PLUGIN_RETURNCODE returncode;

namespace AlleyMF
{
	void LoadSettings();
	void GFGoodSell(struct SGFGoodSellInfo const& gsi, unsigned int clientId);
	bool GFGoodBuy(struct SGFGoodBuyInfo const& gbi, unsigned int clientId);
	void BaseEnter_AFTER(unsigned int baseId, unsigned int clientId);
	void PlayerLaunch(unsigned int iShip, unsigned int client);
}

namespace CommodityLimit
{

	void LoadSettings();
	void ClearClientInfo(uint iClientID);
	bool GFGoodBuy(struct SGFGoodBuyInfo const& gbi, unsigned int iClientID);
	void __stdcall ReqAddItem(uint& goodID, char const* hardpoint, int count, float status, bool& mounted, uint iClientID);
	void ReqChangeCash(int iMoneyDiff, unsigned int iClientID);

}