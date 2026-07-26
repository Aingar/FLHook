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

using namespace std;

struct SpaceBuy
{
	uint goodId;
	int amount;
	int price;
};

struct SpaceBuyData
{
	uint affiliation;
	float minRep = 0.0f;
	vector<SpaceBuy> goods;
};