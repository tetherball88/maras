// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Fateless.Ai. SkyrimNet's plugin API headers are MIT licensed: include them
// in any mod, open or closed source. SkyrimNet itself is not open source.

#pragma once
#include "PublicAPIMemoryQuery.h"  // shared JSON serialization helpers

/**
 * SkyrimNet Public API: typed diary query (v11+). Companion header to PublicAPI.h, the diary counterpart of
 * PublicAPIMemoryQuery.h; copy all three into your plugin. Read one actor's entries in write order:
 *
 * @code
 *   DiaryQuery q;
 *   q.minId    = lastIdIHave + 1;      // ids are AUTOINCREMENT, see PublicQueryDiaryEntries
 *   q.orderBy  = DiaryOrder::IdAsc;
 *   q.maxCount = 20;
 *
 *   std::string json = QueryDiaryEntries(formId, q);
 * @endcode
 */

/** Sort order for QueryDiaryEntries. Ties break on id. */
enum class DiaryOrder : uint32_t {
    EntryDateDesc = 0,  // newest in-game date first (as PublicGetDiaryEntries)
    EntryDateAsc,
    ImportanceDesc,
    ImportanceAsc,
    IdDesc,             // most recently written first
    IdAsc,              // write order
};

/**
 * Filter and ordering for an actor's diary entries. Include lists are OR, exclude lists drop any match and win
 * over include; an empty list is no constraint. Ranges are inclusive; unset = unbounded on that side.
 * Defaults behave like PublicGetDiaryEntries(formId, 50, 0, 0).
 */
struct DiaryQuery {
    int maxCount = 50;

    std::vector<std::string> includeTags;
    std::vector<std::string> excludeTags;
    bool matchAllIncludeTags = false;  // true = entry must carry ALL of includeTags

    std::vector<std::string> includeEmotions;
    std::vector<std::string> excludeEmotions;

    std::vector<std::string> includeLocations;
    std::vector<std::string> excludeLocations;

    std::optional<float> minImportance;
    std::optional<float> maxImportance;
    std::optional<double> entryDateAfter;   // as reported in the "entry_date" field
    std::optional<double> entryDateBefore;
    std::optional<int> minId;               // pass lastId + 1 to read the entries after one you have
    std::optional<int> maxId;

    DiaryOrder orderBy = DiaryOrder::EntryDateDesc;
};

namespace SkyrimNetDiaryQueryDetail {

inline const char* OrderName(DiaryOrder order) {
    switch (order) {
        case DiaryOrder::EntryDateAsc:   return "EntryDateAsc";
        case DiaryOrder::ImportanceDesc: return "ImportanceDesc";
        case DiaryOrder::ImportanceAsc:  return "ImportanceAsc";
        case DiaryOrder::IdDesc:         return "IdDesc";
        case DiaryOrder::IdAsc:          return "IdAsc";
        case DiaryOrder::EntryDateDesc:
        default:                         return "EntryDateDesc";
    }
}

} // namespace SkyrimNetDiaryQueryDetail

/** Serialize a DiaryQuery to the JSON object the DLL export expects. Fields left at their defaults are omitted. */
inline std::string DiaryQueryToJSON(const DiaryQuery& q) {
    using namespace SkyrimNetMemoryQueryDetail;

    std::string json = "{\"maxCount\":" + NumberToJSON(q.maxCount);

    AppendStringArray(json, "includeTags", q.includeTags);
    AppendStringArray(json, "excludeTags", q.excludeTags);
    if (q.matchAllIncludeTags) json += ",\"matchAllIncludeTags\":true";

    AppendStringArray(json, "includeEmotions", q.includeEmotions);
    AppendStringArray(json, "excludeEmotions", q.excludeEmotions);
    AppendStringArray(json, "includeLocations", q.includeLocations);
    AppendStringArray(json, "excludeLocations", q.excludeLocations);

    AppendOptionalNumber(json, "minImportance", q.minImportance);
    AppendOptionalNumber(json, "maxImportance", q.maxImportance);
    AppendOptionalNumber(json, "entryDateAfter", q.entryDateAfter);
    AppendOptionalNumber(json, "entryDateBefore", q.entryDateBefore);
    AppendOptionalNumber(json, "minId", q.minId);
    AppendOptionalNumber(json, "maxId", q.maxId);

    json += ",\"orderBy\":\"";
    json += SkyrimNetDiaryQueryDetail::OrderName(q.orderBy);
    json += "\"}";

    return json;
}
