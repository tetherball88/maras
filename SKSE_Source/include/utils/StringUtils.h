#pragma once

#include "utils/Common.h"

namespace MARAS::StringUtils {

    /**
     * Case-insensitive string equality comparison.
     */
    inline bool CaseInsensitiveEquals(std::string_view a, std::string_view b) {
        if (a.length() != b.length()) return false;
        return std::equal(a.begin(), a.end(), b.begin(), [](char c1, char c2) {
            return std::tolower(static_cast<unsigned char>(c1)) ==
                   std::tolower(static_cast<unsigned char>(c2));
        });
    }

    /**
     * Three-way case-insensitive comparison without heap allocation.
     * Returns < 0 if a < b, 0 if equal, > 0 if a > b.
     */
    inline int CaseInsensitiveCompare(std::string_view a, std::string_view b) noexcept {
        const size_t minLen = std::min(a.length(), b.length());
        for (size_t i = 0; i < minLen; ++i) {
            const int ca = std::tolower(static_cast<unsigned char>(a[i]));
            const int cb = std::tolower(static_cast<unsigned char>(b[i]));
            if (ca != cb) return ca - cb;
        }
        if (a.length() < b.length()) return -1;
        if (a.length() > b.length()) return 1;
        return 0;
    }

    /**
     * Convert string to lowercase.
     */
    inline std::string ToLowerStd(std::string_view text) {
        std::string result(text);
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    /**
     * Convert string to uppercase.
     */
    inline std::string ToUpperStd(std::string_view text) {
        std::string result(text);
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return result;
    }

    /**
     * Trim whitespace from both ends of string_view without heap allocation.
     */
    constexpr std::string_view TrimView(std::string_view text) noexcept {
        constexpr std::string_view whitespace = " \t\n\r\f\v";
        size_t start = text.find_first_not_of(whitespace);
        if (start == std::string_view::npos) return {};

        size_t end = text.find_last_not_of(whitespace);
        return text.substr(start, end - start + 1);
    }

    /**
     * Trim whitespace from both ends of string.
     */
    inline std::string TrimStd(std::string_view text) {
        return std::string(TrimView(text));
    }

    /**
     * Check if string contains substring (case-insensitive by default, zero heap allocations).
     */
    inline bool Contains(std::string_view haystack, std::string_view needle, bool caseInsensitive = true) {
        if (needle.empty()) return true;
        if (haystack.length() < needle.length()) return false;

        if (!caseInsensitive) {
            return haystack.find(needle) != std::string_view::npos;
        }

        auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                              [](char c1, char c2) {
                                  return std::tolower(static_cast<unsigned char>(c1)) ==
                                         std::tolower(static_cast<unsigned char>(c2));
                              });
        return it != haystack.end();
    }

    /**
     * Check if string starts with prefix (case-insensitive by default, zero heap allocations).
     */
    inline bool StartsWith(std::string_view text, std::string_view prefix, bool caseInsensitive = true) {
        if (prefix.empty()) return true;
        if (text.length() < prefix.length()) return false;

        std::string_view sub = text.substr(0, prefix.length());
        return caseInsensitive ? CaseInsensitiveEquals(sub, prefix) : (sub == prefix);
    }

    /**
     * Check if string ends with suffix (case-insensitive by default, zero heap allocations).
     */
    inline bool EndsWith(std::string_view text, std::string_view suffix, bool caseInsensitive = true) {
        if (suffix.empty()) return true;
        if (text.length() < suffix.length()) return false;

        std::string_view sub = text.substr(text.length() - suffix.length());
        return caseInsensitive ? CaseInsensitiveEquals(sub, suffix) : (sub == suffix);
    }

    /**
     * Check if text matches wildcard pattern (* matches zero or more chars, ? matches any single char).
     * By default case-insensitive.
     */
    inline bool WildcardMatch(std::string_view text, std::string_view pattern, bool caseInsensitive = true) {
        size_t t = 0, p = 0;
        size_t starIdx = std::string_view::npos, matchIdx = 0;
        size_t textLen = text.length(), patLen = pattern.length();

        auto charMatch = [caseInsensitive](char c1, char c2) {
            if (c2 == '?') return true;
            if (caseInsensitive) {
                return std::tolower(static_cast<unsigned char>(c1)) ==
                       std::tolower(static_cast<unsigned char>(c2));
            }
            return c1 == c2;
        };

        while (t < textLen) {
            if (p < patLen && (pattern[p] == '?' || charMatch(text[t], pattern[p]))) {
                ++t;
                ++p;
            } else if (p < patLen && pattern[p] == '*') {
                starIdx = p;
                matchIdx = t;
                ++p;
            } else if (starIdx != std::string_view::npos) {
                p = starIdx + 1;
                ++matchIdx;
                t = matchIdx;
            } else {
                return false;
            }
        }

        while (p < patLen && pattern[p] == '*') {
            ++p;
        }

        return p == patLen;
    }

    /**
     * Check if text matches pattern (supports * and ? wildcards, or full-string equality if no wildcard).
     */
    inline bool MatchesPattern(std::string_view text, std::string_view pattern, bool caseInsensitive = true) {
        if (pattern.empty()) return text.empty();
        if (pattern.find('*') != std::string_view::npos || pattern.find('?') != std::string_view::npos) {
            return WildcardMatch(text, pattern, caseInsensitive);
        }
        return caseInsensitive ? CaseInsensitiveEquals(text, pattern) : (text == pattern);
    }

    /**
     * Split string by delimiter.
     */
    inline std::vector<std::string> Split(std::string_view text, std::string_view delimiter) {
        std::vector<std::string> result;
        if (text.empty()) return result;
        if (delimiter.empty()) {
            result.emplace_back(text);
            return result;
        }

        size_t start = 0;
        size_t end = text.find(delimiter);

        while (end != std::string_view::npos) {
            result.emplace_back(text.substr(start, end - start));
            start = end + delimiter.length();
            end = text.find(delimiter, start);
        }

        result.emplace_back(text.substr(start));
        return result;
    }

    /**
     * Calculate Levenshtein edit distance between two strings (uses stack buffer for typical NPC name lengths).
     */
    inline int LevenshteinDistance(std::string_view a, std::string_view b) {
        if (a.length() < b.length()) {
            std::swap(a, b);
        }

        const size_t m = a.length();
        const size_t n = b.length();

        if (n == 0) return static_cast<int>(m);

        constexpr size_t kStackCap = 128;
        std::array<int, kStackCap> stackPrev{};
        std::array<int, kStackCap> stackCurr{};
        std::vector<int> heapPrev;
        std::vector<int> heapCurr;

        int* prev = nullptr;
        int* curr = nullptr;

        if (n + 1 <= kStackCap) {
            prev = stackPrev.data();
            curr = stackCurr.data();
        } else {
            heapPrev.resize(n + 1);
            heapCurr.resize(n + 1);
            prev = heapPrev.data();
            curr = heapCurr.data();
        }

        for (size_t j = 0; j <= n; ++j) {
            prev[j] = static_cast<int>(j);
        }

        for (size_t i = 1; i <= m; ++i) {
            curr[0] = static_cast<int>(i);
            const int ca = std::tolower(static_cast<unsigned char>(a[i - 1]));

            for (size_t j = 1; j <= n; ++j) {
                const int cost = (ca == std::tolower(static_cast<unsigned char>(b[j - 1]))) ? 0 : 1;
                curr[j] = (std::min)({
                    prev[j] + 1,       // deletion
                    curr[j - 1] + 1,   // insertion
                    prev[j - 1] + cost // substitution
                });
            }

            std::swap(prev, curr);
        }

        return prev[n];
    }

    /**
     * Result of a fuzzy search.
     */
    struct FuzzyResult {
        std::string match;
        int distance = INT_MAX;
        explicit operator bool() const { return distance < INT_MAX; }
    };

    /**
     * Find best fuzzy match for a search term in a list of candidate names.
     */
    inline FuzzyResult FuzzyFind(std::string_view searchTerm,
                                 const std::vector<std::string>& candidates,
                                 int maxDistance) {
        FuzzyResult result;
        auto searchLen = searchTerm.length();
        for (const auto& name : candidates) {
            int dist = LevenshteinDistance(searchTerm, name);
            if (dist == 0) {
                result.distance = 0;
                result.match = name;
                return result;
            }
            auto minLen = (std::min)(searchLen, name.length());
            if (dist >= static_cast<int>(minLen)) continue;

            if (dist < result.distance) {
                result.distance = dist;
                result.match = name;
            }
        }
        if (result.distance > maxDistance) {
            result.match.clear();
            result.distance = INT_MAX;
        }
        return result;
    }

}  // namespace MARAS::StringUtils
