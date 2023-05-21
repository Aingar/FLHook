#pragma once

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
#include <unordered_set>
#include <unordered_map>


struct CLIENT_DATA
{
	bool bDisplayDPOnLaunch = true;
	int DeathPenaltyCredits = 0;
};