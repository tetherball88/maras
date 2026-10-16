#pragma once

#include "utils/Common.h"

namespace MARAS {

    // Enums for NPC attributes
    enum class SocialClass : uint8_t {
        Outcast = 0,
        Poverty = 1,
        Working = 2,
        Middle = 3,
        Wealthy = 4,
        Religious = 5,
        Nobles = 6,
        Rulers = 7,
        // Sentinel for range checking (not a valid value)
        _Count
    };

    enum class SkillType : uint8_t {
        Warrior = 0,
        Mage = 1,
        Rogue = 2,
        Craftsman = 3,
        Ranger = 4,
        Orator = 5,
        // Sentinel for range checking (not a valid value)
        _Count
    };

    enum class Temperament : uint8_t {
        Proud = 0,
        Humble = 1,
        Jealous = 2,
        Romantic = 3,
        Independent = 4,
        // Sentinel for range checking (not a valid value)
        _Count
    };

    enum class RelationshipStatus : uint8_t {
        Candidate = 0,
        Engaged = 1,
        Married = 2,
        Divorced = 3,
        Jilted = 4,
        Dating = 5,
        Lover = 6,
        ExDating = 7,
        ExLover = 8,
        // Unknown is used as a default/absent value when no stored data exists
        Unknown = 255
    };

    enum class AffairType : uint8_t {
        Casual = 0,  // Physical affair without formal relationship (e.g. sex event)
        Dating = 1,  // Cheater has Dating status with affair partner
        Lover = 2,   // Cheater has Lover status with affair partner
        _Count
    };

    enum class AffairStatus : uint8_t {
        Ongoing = 0,           // Active affair
        EndedBreakup = 1,      // Affair ended: cheater broke up with affair partner
        EndedVictimSplit = 2,  // Affair ended: victim relationship ended (divorced/jilted)
        Allowed = 3,           // No longer cheating: pair became open/polygamy
        EndedConfronted = 4,   // Affair ended: victim confronted cheater, demanded stop
        _Count
    };

    enum class AffairDiscovery : uint8_t {
        None = 0,       // Unspecified / silent discovery
        Confessed = 1,  // Cheater confessed to victim
        Exposed = 2,    // Affair partner or third party exposed the affair
        Caught = 3,     // Cheater and affair partner caught in the act
        _Count
    };

    enum class AffairResolution : uint8_t {
        None = 0,        // Unresolved / Ongoing (isSecret cleared)
        Confronted = 1,  // Affair ended, couple stays together
        Allowed = 2,     // Open relationship agreed
        Breakup = 3,     // Couple separated / divorced
        _Count
    };

    // Compile-time validation: ensure enums fit in uint8_t for serialization
    static_assert(static_cast<uint8_t>(SocialClass::_Count) <= 255,
                  "SocialClass enum exceeds uint8_t range");
    static_assert(static_cast<uint8_t>(SkillType::_Count) <= 255,
                  "SkillType enum exceeds uint8_t range");
    static_assert(static_cast<uint8_t>(Temperament::_Count) <= 255,
                  "Temperament enum exceeds uint8_t range");
    static_assert(static_cast<uint8_t>(RelationshipStatus::ExLover) < 255,
                  "RelationshipStatus valid values must be less than Unknown sentinel");
    static_assert(static_cast<uint8_t>(AffairType::_Count) <= 255,
                  "AffairType enum exceeds uint8_t range");
    static_assert(static_cast<uint8_t>(AffairStatus::_Count) <= 255,
                  "AffairStatus enum exceeds uint8_t range");
    static_assert(static_cast<uint8_t>(AffairDiscovery::_Count) <= 255,
                  "AffairDiscovery enum exceeds uint8_t range");
    static_assert(static_cast<uint8_t>(AffairResolution::_Count) <= 255,
                  "AffairResolution enum exceeds uint8_t range");

}  // namespace MARAS

