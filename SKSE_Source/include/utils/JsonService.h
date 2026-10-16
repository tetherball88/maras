#pragma once

#include "PCH.h"
#include "core/NPCIndex.h"
#include "utils/StringUtils.h"

namespace MARAS::JsonService {

// Navigate a JSON tree by a dot-separated path (e.g. "initiator" or "data.actors.0").
// Returns nullptr if any segment is missing or type mismatches.
inline const nlohmann::json* Traverse(const nlohmann::json& root, std::string_view path) {
    const nlohmann::json* cur = &root;
    if (path.empty()) return cur;
    std::string_view sv = path;
    while (!sv.empty() && cur) {
        auto dot = sv.find('.');
        auto key = (dot == std::string_view::npos) ? sv : sv.substr(0, dot);
        sv       = (dot == std::string_view::npos) ? std::string_view{} : sv.substr(dot + 1);
        if (cur->is_object()) {
            // Fast path: exact key match
            if (auto exactIt = cur->find(key); exactIt != cur->end()) {
                cur = &exactIt.value();
                continue;
            }
            // Fallback: zero-allocation case-insensitive key match
            const nlohmann::json* found = nullptr;
            for (auto it = cur->begin(); it != cur->end(); ++it) {
                if (StringUtils::CaseInsensitiveEquals(it.key(), key)) {
                    found = &it.value();
                    break;
                }
            }
            cur = found;
        } else if (cur->is_array()) {
            std::size_t idx = 0;
            const char* begin = key.data();
            const char* end = begin + key.size();
            auto [ptr, ec] = std::from_chars(begin, end, idx, 10);
            cur = (!key.empty() && ec == std::errc{} && ptr == end && idx < cur->size()) ? &(*cur)[idx] : nullptr;
        } else {
            cur = nullptr;
        }
    }
    return cur;
}

inline const nlohmann::json* Traverse(const nlohmann::json& root, const char* path) {
    return Traverse(root, path ? std::string_view(path) : std::string_view{});
}

namespace detail {
    // Thread-local single-entry parse cache so sequential GetString/GetActor/GetInt calls
    // on the same JSON payload do not re-parse the JSON string on every field lookup.
    inline const nlohmann::json* GetParsedJson(const char* json) {
        if (!json || json[0] == '\0') return nullptr;
        std::string_view sv(json);

        struct CacheEntry {
            std::string raw;
            nlohmann::json parsed{nlohmann::json::value_t::discarded};
        };
        static thread_local CacheEntry s_cache;

        if (s_cache.raw != sv) {
            s_cache.raw.assign(sv);
            s_cache.parsed = nlohmann::json::parse(sv, nullptr, false);
        }
        return s_cache.parsed.is_discarded() ? nullptr : &s_cache.parsed;
    }

    inline RE::FormID ExtractFormID(const nlohmann::json& val) noexcept {
        if (val.is_number_unsigned()) {
            return static_cast<RE::FormID>(val.get<std::uint32_t>());
        }
        if (val.is_number_integer()) {
            return static_cast<RE::FormID>(static_cast<std::uint32_t>(val.get<std::int64_t>()));
        }
        if (val.is_string()) {
            const auto& s = val.get_ref<const std::string&>();
            std::string_view sv = StringUtils::TrimView(s);
            if (sv.empty()) return 0;

            bool hasHexPrefix = (sv.size() >= 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X'));
            if (hasHexPrefix) {
                sv.remove_prefix(2);
            }
            if (sv.empty()) return 0;

            RE::FormID formID = 0;
            const char* begin = sv.data();
            const char* end = begin + sv.size();
            auto [ptr, ec] = std::from_chars(begin, end, formID, 16);
            if (ec == std::errc{} && ptr == end) {
                return formID;
            }
            if (!hasHexPrefix) {
                auto [decPtr, decEc] = std::from_chars(begin, end, formID, 10);
                if (decEc == std::errc{} && decPtr == end) {
                    return formID;
                }
            }
        }
        return 0;
    }
}  // namespace detail

inline std::string GetString(const char* json, const char* path, const char* def = "") {
    const std::string fallback = def ? def : "";
    try {
        const auto* j = detail::GetParsedJson(json);
        if (!j) return fallback;
        const auto* val = Traverse(*j, path);
        if (!val || val->is_null()) return fallback;
        if (val->is_string()) return val->get<std::string>();
        return val->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (...) {
        return fallback;
    }
}

inline RE::Actor* GetActor(const char* json, const char* path) {
    try {
        const auto* j = detail::GetParsedJson(json);
        if (!j) return nullptr;
        const auto* val = Traverse(*j, path);
        if (!val) return nullptr;

        RE::FormID formID = detail::ExtractFormID(*val);
        if (!HasPhysicalActor(formID)) return nullptr;
        return NPCIndex::GetSingleton().GetActorFromFormId(formID);
    } catch (...) {
        return nullptr;
    }
}

inline int32_t GetInt(const char* json, const char* path, int32_t def = 0) {
    try {
        const auto* j = detail::GetParsedJson(json);
        if (!j) return def;
        const auto* val = Traverse(*j, path);
        if (!val) return def;
        if (val->is_number()) return val->get<int32_t>();
        if (val->is_string()) {
            std::string_view sv = StringUtils::TrimView(val->get_ref<const std::string&>());
            int32_t parsed = def;
            auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsed, 10);
            return (ec == std::errc{} && ptr == sv.data() + sv.size()) ? parsed : def;
        }
        return def;
    } catch (...) {
        return def;
    }
}

inline float GetFloat(const char* json, const char* path, float def = 0.0f) {
    try {
        const auto* j = detail::GetParsedJson(json);
        if (!j) return def;
        const auto* val = Traverse(*j, path);
        if (!val) return def;
        if (val->is_number()) return val->get<float>();
        if (val->is_string()) {
            std::string_view sv = StringUtils::TrimView(val->get_ref<const std::string&>());
            float parsed = def;
            auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsed);
            return (ec == std::errc{} && ptr == sv.data() + sv.size()) ? parsed : def;
        }
        return def;
    } catch (...) {
        return def;
    }
}

inline bool GetBool(const char* json, const char* path, bool def = false) {
    try {
        const auto* j = detail::GetParsedJson(json);
        if (!j) return def;
        const auto* val = Traverse(*j, path);
        if (!val) return def;
        if (val->is_boolean()) return val->get<bool>();
        if (val->is_string()) {
            std::string_view sv = StringUtils::TrimView(val->get_ref<const std::string&>());
            return StringUtils::CaseInsensitiveEquals(sv, "true") ||
                   sv == "1" ||
                   StringUtils::CaseInsensitiveEquals(sv, "yes");
        }
        if (val->is_number()) return val->get<int>() != 0;
        return def;
    } catch (...) {
        return def;
    }
}

inline int32_t GetArrayLength(const char* json, const char* path) {
    try {
        const auto* j = detail::GetParsedJson(json);
        if (!j) return -1;
        const auto* val = Traverse(*j, path);
        if (!val || !val->is_array()) return -1;
        return static_cast<int32_t>(val->size());
    } catch (...) {
        return -1;
    }
}

inline std::vector<RE::Actor*> GetActorArray(const char* json, const char* path) {
    std::vector<RE::Actor*> result;
    try {
        const auto* j = detail::GetParsedJson(json);
        if (!j) return result;

        const nlohmann::json* arr = Traverse(*j, path);
        if (!arr || !arr->is_array()) return result;

        for (const auto& item : *arr) {
            RE::FormID formID = detail::ExtractFormID(item);
            if (HasPhysicalActor(formID)) {
                auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(formID);
                if (actor) result.push_back(actor);
            }
        }
    } catch (...) {}
    return result;
}

// ---------------------------------------------------------------------------
// Sanitize a raw LLM response string into a bare JSON string.
// Extracts the substring between the first '{' / '[' and the last '}' / ']',
// handles markdown code fences, leading/trailing prose (including bracketed
// prose tags like "[Note]: {...}"), BOM, and any other wrapping.
// Returns an empty string if no JSON delimiters are found.
// ---------------------------------------------------------------------------
inline std::string SanitizeLLMJson(const char* raw) {
    if (!raw || raw[0] == '\0') return {};

    std::string_view s(raw);

    // 1. Strip UTF-8 BOM if present
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.remove_prefix(3);
    }

    // 2. If a markdown code fence (```...```) is present and contains '{' or '[',
    // narrow the view to the fenced block so prose outside the fence is ignored.
    if (auto fenceStart = s.find("```"); fenceStart != std::string_view::npos) {
        auto contentStart = fenceStart + 3;
        auto lineEnd = s.find('\n', contentStart);
        if (lineEnd != std::string_view::npos) {
            auto fenceEnd = s.rfind("```");
            if (fenceEnd != std::string_view::npos && fenceEnd > lineEnd) {
                std::string_view fenced = s.substr(lineEnd + 1, fenceEnd - (lineEnd + 1));
                if (fenced.find('{') != std::string_view::npos || fenced.find('[') != std::string_view::npos) {
                    s = fenced;
                }
            }
        }
    }

    // 3. Find opening delimiter: could be object { ... } or array [ ... ]
    auto firstBrace = s.find('{');
    auto firstBracket = s.find('[');

    // If '[' appears before '{', check whether that '[' closes before '{' (e.g. "[Note]: {\"allow\": true}").
    // If it closes before '{', the bracketed segment is prose/metadata rather than the outer JSON array.
    if (firstBracket != std::string_view::npos && firstBrace != std::string_view::npos && firstBracket < firstBrace) {
        int depth = 0;
        bool inStr = false;
        bool esc = false;
        size_t matchingClose = std::string_view::npos;
        for (size_t i = firstBracket; i < s.size(); ++i) {
            char c = s[i];
            if (esc) {
                esc = false;
                continue;
            }
            if (c == '\\' && inStr) {
                esc = true;
                continue;
            }
            if (c == '"') {
                inStr = !inStr;
                continue;
            }
            if (!inStr) {
                if (c == '[') {
                    ++depth;
                } else if (c == ']') {
                    if (--depth == 0) {
                        matchingClose = i;
                        break;
                    }
                }
            }
        }
        if (matchingClose != std::string_view::npos && matchingClose < firstBrace) {
            firstBracket = std::string_view::npos;
        }
    }

    size_t first = std::string_view::npos;
    size_t last = std::string_view::npos;

    if (firstBrace != std::string_view::npos && (firstBracket == std::string_view::npos || firstBrace < firstBracket)) {
        first = firstBrace;
        last = s.rfind('}');
    } else if (firstBracket != std::string_view::npos) {
        first = firstBracket;
        last = s.rfind(']');
    }

    if (first == std::string_view::npos) {
        return {};
    }

    if (last == std::string_view::npos || last < first) {
        // Truncated response: return substring from opening delimiter to end
        return std::string(s.substr(first));
    }

    return std::string(s.substr(first, last - first + 1));
}

// ---------------------------------------------------------------------------
// Repair malformed JSON produced by LLMs:
// 1. Strips trailing commas before } and ]
// 2. Escapes unescaped raw newlines/tabs inside string literals
// 3. Auto-closes unclosed strings, objects, and arrays (for truncated responses)
// ---------------------------------------------------------------------------
inline std::string RepairJsonString(const std::string& input) {
    if (input.empty()) return {};

    std::string s = input;

    // 1. Fix trailing commas before } or ]
    std::string noTrailingCommas;
    noTrailingCommas.reserve(s.size());
    bool inString = false;
    bool escaped = false;

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (escaped) {
            escaped = false;
            noTrailingCommas += c;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            noTrailingCommas += c;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            noTrailingCommas += c;
            continue;
        }

        if (!inString && c == ',') {
            size_t nextIdx = i + 1;
            while (nextIdx < s.size() && (s[nextIdx] == ' ' || s[nextIdx] == '\t' || s[nextIdx] == '\r' || s[nextIdx] == '\n')) {
                nextIdx++;
            }
            if (nextIdx < s.size() && (s[nextIdx] == '}' || s[nextIdx] == ']')) {
                continue; // Skip trailing comma
            }
        }
        noTrailingCommas += c;
    }
    s = std::move(noTrailingCommas);

    // 2. Fix unescaped control characters inside string literals
    std::string escapedControls;
    escapedControls.reserve(s.size() + 16);
    inString = false;
    escaped = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (escaped) {
            escaped = false;
            escapedControls += c;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            escapedControls += c;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            escapedControls += c;
            continue;
        }
        if (inString) {
            if (c == '\n') {
                escapedControls += "\\n";
                continue;
            } else if (c == '\r') {
                escapedControls += "\\r";
                continue;
            } else if (c == '\t') {
                escapedControls += "\\t";
                continue;
            }
        }
        escapedControls += c;
    }
    s = std::move(escapedControls);

    // 3. Balance unclosed quotes / braces / brackets
    std::vector<char> stack;
    inString = false;
    escaped = false;
    for (char c : s) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (!inString) {
            if (c == '{' || c == '[') {
                stack.push_back(c);
            } else if (c == '}' && !stack.empty() && stack.back() == '{') {
                stack.pop_back();
            } else if (c == ']' && !stack.empty() && stack.back() == '[') {
                stack.pop_back();
            }
        }
    }

    if (inString) {
        s += '"';
    }
    while (!stack.empty()) {
        char open = stack.back();
        stack.pop_back();
        if (open == '{') s += '}';
        else if (open == '[') s += ']';
    }

    return s;
}

// ---------------------------------------------------------------------------
// Parse raw LLM output into a nlohmann::json object with full sanitization & repair.
// 1. Direct parse (ignoring comments)
// 2. Sanitize (BOM removal, code fences, markdown, prose)
// 3. Repair (trailing commas, control characters, truncated brackets)
// Returns discarded json object if all parsing attempts fail.
// ---------------------------------------------------------------------------
inline nlohmann::json ParseLLMJson(const std::string& rawResponse) {
    if (rawResponse.empty()) return nlohmann::json(nlohmann::json::value_t::discarded);

    // Attempt 1: Direct parse with ignore_comments = true
    nlohmann::json resp = nlohmann::json::parse(rawResponse, nullptr, /*exceptions=*/false, /*ignore_comments=*/true);
    if (!resp.is_discarded()) {
        return resp;
    }

    // Attempt 2: Sanitize (BOM removal, code fences, markdown prose stripping)
    std::string sanitized = SanitizeLLMJson(rawResponse.c_str());
    if (!sanitized.empty()) {
        resp = nlohmann::json::parse(sanitized, nullptr, /*exceptions=*/false, /*ignore_comments=*/true);
        if (!resp.is_discarded()) {
            return resp;
        }

        // Attempt 3: Repair sanitized string
        std::string repaired = RepairJsonString(sanitized);
        if (!repaired.empty()) {
            resp = nlohmann::json::parse(repaired, nullptr, /*exceptions=*/false, /*ignore_comments=*/true);
            if (!resp.is_discarded()) {
                return resp;
            }
        }
    }

    // Attempt 4: Repair original string directly
    std::string repairedRaw = RepairJsonString(rawResponse);
    if (!repairedRaw.empty()) {
        std::string sanitizedRepaired = SanitizeLLMJson(repairedRaw.c_str());
        if (!sanitizedRepaired.empty()) {
            resp = nlohmann::json::parse(sanitizedRepaired, nullptr, /*exceptions=*/false, /*ignore_comments=*/true);
            if (!resp.is_discarded()) {
                return resp;
            }
        }
    }

    return nlohmann::json(nlohmann::json::value_t::discarded);
}

}  // namespace MARAS::JsonService
