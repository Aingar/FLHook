#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Upkeep
{
	using Id = std::uint32_t;
	constexpr double Day = 86400.0;

	struct Settings
	{
		double dailyVolume = 1000;
		double flatDailyVolume = -1;
		double starting = 0;
		double fixed = -1;
		double ceiling = -1;
	};

	struct Segment
	{
		double start, percent, linear, quadratic;
	};

	struct Policy
	{
		bool enabled = false;
		bool spaceSupplies = true;
		std::string reference = "commodity_basic_alloys";
		std::set<std::string> types = { "legacy" };
		Settings defaults;
		std::map<std::string, Settings> systems;
		std::map<Id, double> weights = { {1, 0}, {2, 1}, {3, 3}, {4, 5}, {5, 10} };
		std::vector<Segment> curve = { {0, 0, 10.0 / 3, 0}, {3, 10, 15.0 / 7, 0}, {10, 25, 6.25, 0.125} };
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

	inline void SetSetting(Settings& settings, const std::string& key, double value)
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
		return !invulnerable && logic && policy.types.count(type) != 0;
	}

	inline double Pressure(const Settings& settings, double total, double own = 0)
	{
		return settings.fixed >= 0 ? settings.fixed : (std::max)(0.0, settings.starting + (std::max)(0.0, total - own));
	}

	inline double Fraction(const Policy& policy, double pressure)
	{
		const Segment* selected = &policy.curve.front();
		for (const auto& segment : policy.curve)
		{
			if (segment.start <= pressure)
			{
				selected = &segment;
			}
		}
		const double d = pressure - selected->start;
		return (selected->percent + selected->linear * d + selected->quadratic * d * d) / 100;
	}

	inline double DailyBundles(const Policy& policy, const Settings& settings, Id core, double pressure, double referenceVolume)
	{
		if (!policy.enabled)
		{
			return 0;
		}
		double volume = settings.flatDailyVolume;
		if (volume < 0)
		{
			volume = settings.dailyVolume == 0 ? 0 : settings.dailyVolume * Fraction(policy, pressure);
		}
		if (volume == 0)
		{
			return 0;
		}
		const double result = volume * core / referenceVolume;
		if (!std::isfinite(result) || result < 0)
		{
			throw std::runtime_error("congestion amount exceeds numeric range");
		}
		return result;
	}

	inline bool BuildAllowed(bool enabled, const Settings& settings, double pressure, bool banned)
	{
		return !banned && (!enabled || settings.ceiling < 0 || pressure <= settings.ceiling);
	}

	struct Material
	{
		Id good, quantity;
		std::wstring name;
	};



	template<class Materials, class Stock, class Consume>
	double Pay(double bundles, const Materials& ordered, Stock stock, Consume consume)
	{
		for (const auto& item : ordered)
		{
			if (bundles < 1)
			{
				break;
			}
			if (item.quantity == 0)
			{
				return 0;
			}
			const Id available = stock(item.good) / item.quantity;
			const double paid = (std::min)(bundles, static_cast<double>(available));
			if (paid > 0)
			{
				consume(item.good, static_cast<Id>(paid) * item.quantity);
				bundles -= paid;
			}
		}
		return bundles;
	}

	inline double Accrue(double daily, double seconds, double& carry)
	{
		const double amount = daily * seconds / Day + carry;
		if (!std::isfinite(amount) || amount < 0)
		{
			throw std::runtime_error("congestion accrual exceeds numeric range");
		}
		const double whole = std::floor(amount);
		carry = amount - whole;
		return whole;
	}

	inline double Intended(double deficit, double heal, size_t alternatives)
	{
		if (!std::isfinite(deficit) || !std::isfinite(heal) || heal <= 0)
		{
			return std::numeric_limits<double>::quiet_NaN();
		}
		if (deficit > 0 && alternatives == 0)
		{
			return std::numeric_limits<double>::quiet_NaN();
		}
		return (std::min)(static_cast<double>(alternatives), std::ceil((std::max)(0.0, deficit) / heal));
	}

	inline double WearInterval(Id tick, Id damageTick)
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
		return static_cast<double>(tick / a) * damageTick;
	}

	inline double Maintenance(double wear, double heal, size_t alternatives, double interval, double repairInterval)
	{
		if (!std::isfinite(wear) || !std::isfinite(heal) || !std::isfinite(interval) || !std::isfinite(repairInterval) ||
			wear < 0 || heal <= 0 || repairInterval <= 0 || interval < repairInterval)
		{
			return std::numeric_limits<double>::quiet_NaN();
		}
		const double required = std::ceil(wear / heal);
		if (required > alternatives * (interval / repairInterval))
		{
			return std::numeric_limits<double>::quiet_NaN();
		}
		return required * Day / interval;
	}

	template<class Reachable>
	std::vector<Id> CoreLevels(Reachable next)
	{
		std::vector<Id> levels = { 1 };
		while (levels.back() < (std::numeric_limits<Id>::max)() && next(levels.back()))
		{
			levels.push_back(levels.back() + 1);
		}
		return levels;
	}

	inline std::wstring Format(double bundles, const std::vector<Material>& items)
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
			double amount;
			std::wstring names;
		};
		std::vector<Group> groups;
		for (const auto& item : items)
		{
			const double amount = bundles * item.quantity;
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
