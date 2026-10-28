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
        throw std::runtime_error("expected a finite number: " + text);
    input >> std::ws;
    if (!input.eof()) throw std::runtime_error("expected a finite number: " + text);
    return value;
}

inline void SetSetting(Settings& settings, const std::string& key, double value)
{
    if (key == "daily_volume_per_core")
    {
        if (value < 0) throw std::runtime_error("daily_volume_per_core must be nonnegative");
        settings.dailyVolume = value;
    }
    else if (key == "flat_daily_volume_per_core")
    {
        if (value < 0 && value != -1) throw std::runtime_error("flat_daily_volume_per_core must be -1 or nonnegative");
        settings.flatDailyVolume = value;
    }
    else if (key == "starting_pressure") settings.starting = value;
    else if (key == "fixed_pressure" || key == "max_build_pressure")
    {
        if (value < 0 && value != -1) throw std::runtime_error(key + " must be -1 or nonnegative");
        (key == "fixed_pressure" ? settings.fixed : settings.ceiling) = value;
    }
    else throw std::runtime_error("unknown setting: " + key);
}

class Config
{
    Policy policy;
    std::map<std::string, std::map<std::string, double>> overrides;
    std::map<std::string, double> systemFields;
    std::set<std::string> seen;
    std::string section, system;
    bool haveTypes = false, haveCurve = false;

    void Unique(const std::string& key)
    {
        if (!seen.insert(key).second) throw std::runtime_error("duplicate setting: " + key);
    }

public:
    std::set<std::string> banned;

    void Header(const std::string& name)
    {
        if (section == "congestion_system")
        {
            if (system.empty()) throw std::runtime_error("congestion_system requires system");
            Unique("system:" + system);
            overrides.emplace(system, systemFields);
        }
        section = name;
        system.clear();
        systemFields.clear();
        if (section == "congestion") Unique("[congestion]");
    }

    void Row(const std::string& key, const std::vector<std::string>& values)
    {
        if (section != "congestion" && section != "congestion_system" && !(section == "general" && key == "banned_system")) return;
        if (values.empty()) throw std::runtime_error("missing value: " + key);
        if (section == "general")
        {
            if (values.size() != 1 || values[0].empty()) throw std::runtime_error("invalid banned_system");
            banned.insert(values[0]);
            return;
        }
        if (section == "congestion_system")
        {
            if (key == "system")
            {
                if (!system.empty() || values.size() != 1 || values[0].empty()) throw std::runtime_error("invalid system");
                system = values[0];
                return;
            }
            if (values.size() != 1) throw std::runtime_error("expected one value: " + key);
            Settings check;
            const double value = Number(values[0]);
            SetSetting(check, key, value);
            if (!systemFields.emplace(key, value).second) throw std::runtime_error("duplicate system setting: " + key);
            return;
        }
        if (key == "core_weight")
        {
            if (values.size() != 2) throw std::runtime_error("core_weight requires level, weight");
            const double level = Number(values[0]), weight = Number(values[1]);
            if (level < 1 || level > (std::numeric_limits<Id>::max)() || std::floor(level) != level || weight < 0)
                throw std::runtime_error("invalid core_weight");
            Unique("core:" + std::to_string(static_cast<Id>(level)));
            policy.weights[static_cast<Id>(level)] = weight;
        }
        else if (key == "curve_segment")
        {
            if (values.size() != 4) throw std::runtime_error("curve_segment requires four values");
            if (!haveCurve) { policy.curve.clear(); haveCurve = true; }
            policy.curve.push_back({ Number(values[0]), Number(values[1]), Number(values[2]), Number(values[3]) });
        }
        else if (key == "counted_type")
        {
            if (values.size() != 1 || values[0].empty()) throw std::runtime_error("invalid counted_type");
            if (!haveTypes) { policy.types.clear(); haveTypes = true; }
            policy.types.insert(values[0]);
        }
        else
        {
            if (values.size() != 1) throw std::runtime_error("expected one value: " + key);
            Unique("global:" + key);
            if (key == "reference_repair_item")
            {
                if (values[0].empty()) throw std::runtime_error("empty reference_repair_item");
                policy.reference = values[0];
            }
            else if (key == "enabled")
            {
                if (values[0] != "0" && values[0] != "1") throw std::runtime_error(key + " must be 0 or 1");
                policy.enabled = values[0] == "1";
            }
            else SetSetting(policy.defaults, key, Number(values[0]));
        }
    }

    Policy Finish()
    {
        Header("");
        std::sort(policy.curve.begin(), policy.curve.end(), [](const Segment& a, const Segment& b) { return a.start < b.start; });
        double previous = 0;
        for (size_t i = 0; i < policy.curve.size(); ++i)
        {
            const auto& s = policy.curve[i];
            if (s.start < 0 || s.percent < 0 || (i == 0 && (s.start != 0 || s.percent != 0)) ||
                (i && s.start == policy.curve[i - 1].start)) throw std::runtime_error("invalid curve segment start");
            if (s.percent + 1e-9 < previous || s.linear < 0) throw std::runtime_error("curve must be nondecreasing");
            if (i + 1 < policy.curve.size())
            {
                const double span = policy.curve[i + 1].start - s.start;
                previous = s.percent + s.linear * span + s.quadratic * span * span;
                if (!std::isfinite(previous) || s.linear + 2 * s.quadratic * span < 0)
                    throw std::runtime_error("curve must remain finite and nondecreasing");
            }
            else if (s.quadratic < 0) throw std::runtime_error("last curve segment must remain nondecreasing");
        }
        for (const auto& entry : overrides)
        {
            Settings settings = policy.defaults;
            for (const auto& field : entry.second) SetSetting(settings, field.first, field.second);
            policy.systems.emplace(entry.first, settings);
        }
        return std::move(policy);
    }
};

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
        if (segment.start <= pressure) selected = &segment;
    const double d = pressure - selected->start;
    return (selected->percent + selected->linear * d + selected->quadratic * d * d) / 100;
}

inline double DailyBundles(const Policy& policy, const Settings& settings, Id core, double pressure, double referenceVolume)
{
    if (!policy.enabled) return 0;
    double volume = settings.flatDailyVolume;
    if (volume < 0) volume = settings.dailyVolume == 0 ? 0 : settings.dailyVolume * Fraction(policy, pressure);
    if (volume == 0) return 0;
    const double result = volume * core / referenceVolume;
    if (!std::isfinite(result) || result < 0) throw std::runtime_error("congestion amount exceeds numeric range");
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

inline std::vector<Material> Ordered(const std::vector<Material>& items, Id preferred, const std::unordered_set<Id>& disabled)
{
    std::vector<Material> result;
    for (const auto& item : items)
        if (item.good == preferred && !disabled.count(item.good)) { result.push_back(item); break; }
    for (const auto& item : items)
        if (item.good != preferred && !disabled.count(item.good)) result.push_back(item);
    return result;
}

template<class Stock, class Consume>
double Pay(double bundles, const std::vector<Material>& ordered, Stock stock, Consume consume)
{
    for (const auto& item : ordered)
    {
        if (bundles < 1) break;
        if (item.quantity == 0) return 0;
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
    if (!std::isfinite(amount) || amount < 0) throw std::runtime_error("congestion accrual exceeds numeric range");
    const double whole = std::floor(amount);
    carry = amount - whole;
    return whole;
}



inline double WearInterval(Id tick, Id damageTick)
{
    if (!tick || !damageTick) return 0;
    Id a = tick, b = damageTick;
    while (b) { const Id remainder = a % b; a = b; b = remainder; }
    return static_cast<double>(tick / a) * damageTick;
}

inline double Maintenance(double wear, double heal, size_t alternatives, double interval, double repairInterval)
{
    if (!std::isfinite(wear) || !std::isfinite(heal) || !std::isfinite(interval) || !std::isfinite(repairInterval) ||
        wear < 0 || heal <= 0 || repairInterval <= 0 || interval < repairInterval) return std::numeric_limits<double>::quiet_NaN();
    const double required = std::ceil(wear / heal);
    if (required > alternatives * (interval / repairInterval)) return std::numeric_limits<double>::quiet_NaN();
    return required * Day / interval;
}

template<class Reachable>
std::vector<Id> CoreLevels(Reachable next)
{
    std::vector<Id> levels = { 1 };
    while (levels.back() < (std::numeric_limits<Id>::max)() && next(levels.back())) levels.push_back(levels.back() + 1);
    return levels;
}


}
