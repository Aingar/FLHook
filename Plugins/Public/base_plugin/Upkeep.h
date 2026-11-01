#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Upkeep
{
	using Id = std::uint32_t;
	constexpr float Day = 86400.0f;

	struct Settings
	{
		float dailyVolume = 1000;
		float flatDailyVolume = -1;
		float starting = 0;
		float fixed = -1;
		float ceiling = -1;
	};

	struct Segment
	{
		float start, percent, linear, quadratic;
	};

	struct Policy
	{
		bool enabled = false;
		bool spaceSupplies = true;
		std::unordered_set<std::string> countedTypes = { "legacy" };
		Settings defaults;
		std::unordered_map<Id, Settings> systems;
		std::unordered_map<Id, float> weights = { {1, 0}, {2, 1}, {3, 3}, {4, 5}, {5, 10} };
		std::vector<Segment> curve = { {0, 0, 10.0f / 3, 0}, {3, 10, 15.0f / 7, 0}, {10, 25, 6.25f, 0.125f} };
	};

	inline double Number(const std::string& text)
	{
		std::istringstream input(text);
		input.imbue(std::locale::classic());
		double value = 0;
		input >> value;
		if (!input || !std::isfinite(value))
		{
			throw std::runtime_error("expected a finite number: " + text);
		}
		input >> std::ws;
		if (!input.eof())
		{
			throw std::runtime_error("expected a finite number: " + text);
		}
		return value;
	}

	inline float FloatNumber(const std::string& text)
	{
		const double value = Number(text);
		if (value < -(std::numeric_limits<float>::max)() || value > (std::numeric_limits<float>::max)())
		{
			throw std::runtime_error("number exceeds float range: " + text);
		}
		return static_cast<float>(value);
	}

	inline void SetSetting(Settings& settings, const std::string& key, float value)
	{
		if (key == "daily_volume_per_core")
		{
			if (value < 0)
			{
				throw std::runtime_error("daily_volume_per_core must be nonnegative");
			}
			settings.dailyVolume = value;
		}
		else if (key == "flat_daily_volume_per_core")
		{
			if (value < 0 && value != -1)
			{
				throw std::runtime_error("flat_daily_volume_per_core must be -1 or nonnegative");
			}
			settings.flatDailyVolume = value;
		}
		else if (key == "starting_pressure")
		{
			settings.starting = value;
		}
		else if (key == "fixed_pressure" || key == "max_build_pressure")
		{
			if (value < 0 && value != -1)
			{
				throw std::runtime_error(key + " must be -1 or nonnegative");
			}
			(key == "fixed_pressure" ? settings.fixed : settings.ceiling) = value;
		}
		else
		{
			throw std::runtime_error("unknown setting: " + key);
		}
	}

	

	inline bool Contributes(const Policy& policy, const std::string& type, bool invulnerable, bool logic)
	{
		return !invulnerable && logic && policy.countedTypes.count(type) != 0;
	}

	inline float Pressure(const Settings& settings, float total, float own = 0)
	{
		return settings.fixed >= 0 ? settings.fixed : (std::max)(0.0f, settings.starting + (std::max)(0.0f, total - own));
	}

	inline float Fraction(const Policy& policy, float pressure)
	{
		for (auto segment = policy.curve.rbegin(); segment != policy.curve.rend(); ++segment)
		{
			const Segment& selected = *segment;
			if (selected.start > pressure)
			{
				continue;
			}
			const float d = pressure - selected.start;
			return (selected.percent + selected.linear * d + selected.quadratic * d * d) / 100;
		}
		return 0;
	}

	inline float DailyBundles(const Policy& policy, const Settings& settings, Id core, float pressure, float referenceVolume)
	{
		if (!policy.enabled)
		{
			return 0;
		}
		float volume = settings.flatDailyVolume;
		if (volume < 0)
		{
			volume = settings.dailyVolume == 0 ? 0 : settings.dailyVolume * Fraction(policy, pressure);
		}
		if (volume == 0)
		{
			return 0;
		}
		const float result = volume * core / referenceVolume;
		if (!std::isfinite(result) || result < 0)
		{
			throw std::runtime_error("congestion amount exceeds numeric range");
		}
		return result;
	}

	inline bool BuildAllowed(bool enabled, const Settings& settings, float pressure, bool banned)
	{
		return !banned && (!enabled || settings.ceiling < 0 || pressure <= settings.ceiling);
	}

	struct Material
	{
		Id good, quantity;
		std::wstring name;
	};



	inline double Accrue(float daily, Id seconds, double& carry)
	{
		const double amount = static_cast<double>(daily) * seconds / Day + carry;
		if (!std::isfinite(amount) || amount < 0)
		{
			throw std::runtime_error("congestion accrual exceeds numeric range");
		}
		const double whole = std::floor(amount);
		carry = amount - whole;
		return whole;
	}

	inline float Intended(float deficit, float heal, size_t alternatives)
	{
		if (!std::isfinite(deficit) || !std::isfinite(heal) || heal <= 0)
		{
			return std::numeric_limits<float>::quiet_NaN();
		}
		if (deficit > 0 && alternatives == 0)
		{
			return std::numeric_limits<float>::quiet_NaN();
		}
		return (std::min)(static_cast<float>(alternatives), std::ceil((std::max)(0.0f, deficit) / heal));
	}

	inline float WearInterval(Id tick, Id damageTick)
	{
		if (!tick || !damageTick)
		{
			return 0;
		}
		Id a = tick, b = damageTick;
		while (b)
		{
			const Id remainder = a % b;
			a = b;
			b = remainder;
		}
		return static_cast<float>(tick / a) * damageTick;
	}

	inline float Maintenance(float wear, float heal, size_t alternatives, float interval, float repairInterval)
	{
		if (!std::isfinite(wear) || !std::isfinite(heal) || !std::isfinite(interval) || !std::isfinite(repairInterval) ||
			wear < 0 || heal <= 0 || repairInterval <= 0 || interval < repairInterval)
		{
			return std::numeric_limits<float>::quiet_NaN();
		}
		const float required = std::ceil(wear / heal);
		if (required > alternatives * (interval / repairInterval))
		{
			return std::numeric_limits<float>::quiet_NaN();
		}
		return required * Day / interval;
	}

	inline std::wstring Format(float bundles, const std::vector<Material>& items)
	{
		if (!std::isfinite(bundles) || bundles < 0)
		{
			return L"Unavailable";
		}
		if (bundles == 0)
		{
			return L"0";
		}
		struct Group
		{
			float amount;
			std::wstring names;
		};
		std::vector<Group> groups;
		for (const auto& item : items)
		{
			const float amount = bundles * item.quantity;
			if (!std::isfinite(amount))
			{
				return L"Unavailable";
			}
			auto group = std::find_if(groups.begin(), groups.end(), [amount](const Group& g) { return g.amount == amount; });
			if (group == groups.end())
			{
				groups.push_back({ amount, item.name });
			}
			else
			{
				group->names += L"/" + item.name;
			}
		}
		if (groups.empty())
		{
			return L"Unavailable";
		}
		std::wstring result;
		for (const auto& group : groups)
		{
			if (!result.empty())
			{
				result += L" or ";
			}
			std::wostringstream amount;
			amount.imbue(std::locale::classic());
			amount.setf(std::ios::fixed, std::ios::floatfield);
			amount.precision(0);
			amount << std::ceil(group.amount);
			result += amount.str() + L" " + group.names;
		}
		return result;
	}
}
