#ifndef __MAIN_H__
#define __MAIN_H__ 1

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

using namespace std;

typedef std::map<uint, struct MarketGoodInfo, struct std::less<uint>, class std::allocator<struct MarketGoodInfo> > market_map_t;
typedef std::map<uint, struct MarketGoodInfo, struct std::less<uint>, class std::allocator<struct MarketGoodInfo> >::const_iterator market_map_iter_t;
typedef std::map<uint, struct MarketGoodInfo, struct std::less<uint>, class std::allocator<struct MarketGoodInfo> >::value_type market_map_pair_t;

#endif
