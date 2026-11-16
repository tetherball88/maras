// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Fateless.Ai. SkyrimNet's plugin API headers are MIT licensed: include them
// in any mod, open or closed source. SkyrimNet itself is not open source.

#pragma once
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

/**
 * SkyrimNet Public API: typed memory query (v10+). Companion header to PublicAPI.h; copy both into your plugin.
 * The DLL export takes a JSON string (stable ABI); this header serializes a typed struct to it inline:
 *
 * @code
 *   MemoryQuery q;
 *   q.excludeTags        = {"mymod_writeback"};
 *   q.excludeMemoryTypes = {"KNOWLEDGE"};
 *   q.minImportance      = 0.4f;
 *   q.orderBy            = MemoryOrder::ImportanceDesc;
 *   q.maxCount           = 10;
 *
 *   std::string json = QueryMemoriesForActor(formId, q);
 * @endcode
 */

/**
 * Sort order for QueryMemoriesForActor. Relevance needs a non-empty contextQuery (falls back to GameTimeDesc)
 * and only sees memories already in the vector index; the other orders run in SQL and see every eligible row.
 */
enum class MemoryOrder : uint32_t {
    GameTimeDesc = 0,  // newest first
    GameTimeAsc,
    ImportanceDesc,
    ImportanceAsc,
    IdDesc,
    IdAsc,
    Relevance,
};

/**
 * Filter and ordering for an actor's memories. Include lists are OR, exclude lists drop any match and win over
 * include; an empty list is no constraint. Defaults behave like PublicGetMemoriesForActor(formId, 50, "").
 */
struct MemoryQuery {
    int maxCount = 50;

    std::vector<std::string> includeTags;
    std::vector<std::string> excludeTags;
    bool matchAllIncludeTags = false;  // true = row must carry ALL of includeTags

    std::vector<std::string> includeMemoryTypes;  // "EXPERIENCE", "KNOWLEDGE", ...
    std::vector<std::string> excludeMemoryTypes;

    std::vector<std::string> includeEmotions;
    std::vector<std::string> excludeEmotions;

    std::vector<std::string> includeLocations;
    std::vector<std::string> excludeLocations;

    // Unset = unbounded on that side. Ranges are inclusive.
    std::optional<float> minImportance;
    std::optional<float> maxImportance;
    std::optional<double> gameTimeAfter;   // game-seconds, as reported in the "game_time" field
    std::optional<double> gameTimeBefore;

    // Unset = no constraint; true = active only; false = inactive only.
    std::optional<bool> isActive = true;

    std::string contextQuery;  // empty = non-semantic
    MemoryOrder orderBy = MemoryOrder::GameTimeDesc;
};

namespace SkyrimNetMemoryQueryDetail {

inline std::string EscapeJSON(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (unsigned char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(c >> 4) & 0xF];
                    out += kHex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

inline void AppendStringArray(std::string& json, const char* key, const std::vector<std::string>& values) {
    if (values.empty()) return;
    json += ",\"";
    json += key;
    json += "\":[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) json += ",";
        json += "\"" + EscapeJSON(values[i]) + "\"";
    }
    json += "]";
}

// std::to_chars, not std::to_string: the latter is locale-dependent and a setlocale(LC_NUMERIC) anywhere
// in the process would emit "0,4" and break the JSON.
template <typename T>
inline std::string NumberToJSON(T value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) return "0";
    return std::string(buffer, result.ptr);
}

template <typename T>
inline void AppendOptionalNumber(std::string& json, const char* key, const std::optional<T>& value) {
    if (!value.has_value()) return;
    json += ",\"";
    json += key;
    json += "\":";
    json += NumberToJSON(*value);
}

inline const char* OrderName(MemoryOrder order) {
    switch (order) {
        case MemoryOrder::GameTimeAsc:    return "GameTimeAsc";
        case MemoryOrder::ImportanceDesc: return "ImportanceDesc";
        case MemoryOrder::ImportanceAsc:  return "ImportanceAsc";
        case MemoryOrder::IdDesc:         return "IdDesc";
        case MemoryOrder::IdAsc:          return "IdAsc";
        case MemoryOrder::Relevance:      return "Relevance";
        case MemoryOrder::GameTimeDesc:
        default:                          return "GameTimeDesc";
    }
}

} // namespace SkyrimNetMemoryQueryDetail

/** Serialize a MemoryQuery to the JSON object the DLL export expects. Fields left at their defaults are omitted. */
inline std::string MemoryQueryToJSON(const MemoryQuery& q) {
    using namespace SkyrimNetMemoryQueryDetail;

    std::string json = "{\"maxCount\":" + NumberToJSON(q.maxCount);

    AppendStringArray(json, "includeTags", q.includeTags);
    AppendStringArray(json, "excludeTags", q.excludeTags);
    if (q.matchAllIncludeTags) json += ",\"matchAllIncludeTags\":true";

    AppendStringArray(json, "includeMemoryTypes", q.includeMemoryTypes);
    AppendStringArray(json, "excludeMemoryTypes", q.excludeMemoryTypes);
    AppendStringArray(json, "includeEmotions", q.includeEmotions);
    AppendStringArray(json, "excludeEmotions", q.excludeEmotions);
    AppendStringArray(json, "includeLocations", q.includeLocations);
    AppendStringArray(json, "excludeLocations", q.excludeLocations);

    AppendOptionalNumber(json, "minImportance", q.minImportance);
    AppendOptionalNumber(json, "maxImportance", q.maxImportance);
    AppendOptionalNumber(json, "gameTimeAfter", q.gameTimeAfter);
    AppendOptionalNumber(json, "gameTimeBefore", q.gameTimeBefore);

    // Always emitted: explicit null distinguishes "no constraint" from the active-only default.
    json += ",\"isActive\":";
    json += q.isActive.has_value() ? (*q.isActive ? "true" : "false") : "null";

    if (!q.contextQuery.empty()) {
        json += ",\"contextQuery\":\"" + EscapeJSON(q.contextQuery) + "\"";
    }
    json += ",\"orderBy\":\"";
    json += OrderName(q.orderBy);
    json += "\"}";

    return json;
}
