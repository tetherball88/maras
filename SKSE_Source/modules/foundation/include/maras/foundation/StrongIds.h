#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <type_traits>

namespace MARAS::Foundation {

    // Canonical Skyrim / M.A.R.A.S raw FormID constants (engine-header-free)
    inline constexpr std::uint32_t kNullRawId = 0x00000000u;
    inline constexpr std::uint32_t kPlayerBaseRawId = 0x00000007u;
    inline constexpr std::uint32_t kPlayerRefRawId = 0x00000014u;
    inline constexpr std::uint32_t kUnknownFatherRawId = 0xFFFFFFFEu;

    // Synthetic child FormID pool (0xFF700000..0xFF7FFFFF) for unborn/infant/unspawned records
    inline constexpr std::uint32_t kSyntheticChildMinRawId = 0xFF700000u;
    inline constexpr std::uint32_t kSyntheticChildMaxRawId = 0xFF7FFFFFu;
    inline constexpr std::uint32_t kDynamicModIndexMask = 0xFF000000u;

    [[nodiscard]] constexpr bool IsSyntheticChildRawId(std::uint32_t raw) noexcept {
        return raw >= kSyntheticChildMinRawId && raw <= kSyntheticChildMaxRawId;
    }

    [[nodiscard]] constexpr bool IsDynamicPrefixRawId(std::uint32_t raw) noexcept {
        return (raw & kDynamicModIndexMask) == kDynamicModIndexMask;
    }

    // True for 0xFFxxxxxx spawned runtime actors (e.g. PlaceAtMe FMR children), excluding synthetic IDs & sentinels
    [[nodiscard]] constexpr bool IsDynamicRuntimeActorRawId(std::uint32_t raw) noexcept {
        return IsDynamicPrefixRawId(raw) && !IsSyntheticChildRawId(raw) && raw != kUnknownFatherRawId &&
               raw != 0xFFFFFFFFu;
    }

    [[nodiscard]] constexpr bool HasPhysicalActorRawId(std::uint32_t raw) noexcept {
        return raw != kNullRawId && !IsSyntheticChildRawId(raw) && raw != kUnknownFatherRawId;
    }

    // Zero-overhead strong 32-bit identifier template
    template <typename Tag>
    struct StrongId {
        std::uint32_t value{ kNullRawId };

        constexpr StrongId() noexcept = default;
        explicit constexpr StrongId(std::uint32_t raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value; }
        [[nodiscard]] constexpr bool IsNull() const noexcept { return value == kNullRawId; }
        explicit constexpr operator bool() const noexcept { return value != kNullRawId; }

        auto operator<=>(const StrongId&) const noexcept = default;
    };

    struct RefIdTag {};
    struct BaseIdTag {};
    struct HouseIdTag {};
    struct CellIdTag {};
    struct LocationIdTag {};
    struct FormRawIdTag {};

    using BaseId = StrongId<BaseIdTag>;
    using HouseId = StrongId<HouseIdTag>;
    using CellId = StrongId<CellIdTag>;
    using LocationId = StrongId<LocationIdTag>;
    using FormRawId = StrongId<FormRawIdTag>;

    // Placed actor / object reference FormID (0x14 Player, placed ACHR/REFR, or 0xFFxxxxxx runtime ref)
    struct RefId {
        std::uint32_t value{ kNullRawId };

        constexpr RefId() noexcept = default;
        explicit constexpr RefId(std::uint32_t raw) noexcept : value(raw) {}

        [[nodiscard]] static constexpr RefId Player() noexcept { return RefId{ kPlayerRefRawId }; }

        [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value; }
        [[nodiscard]] constexpr bool IsNull() const noexcept { return value == kNullRawId; }
        [[nodiscard]] constexpr bool IsPlayer() const noexcept { return value == kPlayerRefRawId; }
        [[nodiscard]] constexpr bool IsDynamicRuntimeRef() const noexcept {
            return IsDynamicRuntimeActorRawId(value);
        }
        explicit constexpr operator bool() const noexcept { return value != kNullRawId; }

        auto operator<=>(const RefId&) const noexcept = default;
    };

    // Synthetic child ID strictly bounded within [0xFF700000, 0xFF7FFFFF] when non-null
    struct SyntheticChildId {
        std::uint32_t value{ kNullRawId };

        constexpr SyntheticChildId() noexcept = default;
        explicit constexpr SyntheticChildId(std::uint32_t raw) noexcept
            : value(IsSyntheticChildRawId(raw) ? raw : kNullRawId) {}

        [[nodiscard]] static constexpr bool InRange(std::uint32_t raw) noexcept {
            return IsSyntheticChildRawId(raw);
        }

        [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value; }
        [[nodiscard]] constexpr bool IsNull() const noexcept { return value == kNullRawId; }
        [[nodiscard]] constexpr bool IsValid() const noexcept { return IsSyntheticChildRawId(value); }
        explicit constexpr operator bool() const noexcept { return IsValid(); }

        auto operator<=>(const SyntheticChildId&) const noexcept = default;
    };

    // Actor identifier used across relationship edges, guardianship, affection, and hierarchy
    struct ActorId {
        std::uint32_t value{ kNullRawId };

        constexpr ActorId() noexcept = default;
        explicit constexpr ActorId(std::uint32_t raw) noexcept : value(raw) {}
        constexpr ActorId(RefId ref) noexcept : value(ref.raw()) {}

        [[nodiscard]] static constexpr ActorId Player() noexcept { return ActorId{ kPlayerRefRawId }; }
        [[nodiscard]] static constexpr ActorId UnknownFather() noexcept { return ActorId{ kUnknownFatherRawId }; }

        [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value; }
        [[nodiscard]] constexpr bool IsNull() const noexcept { return value == kNullRawId; }
        [[nodiscard]] constexpr bool IsPlayer() const noexcept { return value == kPlayerRefRawId; }
        [[nodiscard]] constexpr bool IsUnknownFather() const noexcept { return value == kUnknownFatherRawId; }
        [[nodiscard]] constexpr bool IsSyntheticChild() const noexcept { return IsSyntheticChildRawId(value); }
        [[nodiscard]] constexpr bool IsDynamicRuntimeActor() const noexcept {
            return IsDynamicRuntimeActorRawId(value);
        }
        [[nodiscard]] constexpr bool HasPhysicalActor() const noexcept { return HasPhysicalActorRawId(value); }
        [[nodiscard]] constexpr RefId AsRefId() const noexcept {
            return HasPhysicalActor() ? RefId{ value } : RefId{};
        }
        explicit constexpr operator bool() const noexcept { return value != kNullRawId; }

        auto operator<=>(const ActorId&) const noexcept = default;
    };

    // Child identifier representing either a SyntheticChildId (0xFF700000..0xFF7FFFFF) or a physical child/adult RefId
    struct ChildId {
        std::uint32_t value{ kNullRawId };

        constexpr ChildId() noexcept = default;
        explicit constexpr ChildId(std::uint32_t raw) noexcept : value(raw) {}
        constexpr ChildId(RefId ref) noexcept : value(ref.raw()) {}
        constexpr ChildId(SyntheticChildId syn) noexcept : value(syn.raw()) {}

        [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value; }
        [[nodiscard]] constexpr bool IsNull() const noexcept { return value == kNullRawId; }
        [[nodiscard]] constexpr bool IsSynthetic() const noexcept { return IsSyntheticChildRawId(value); }
        [[nodiscard]] constexpr bool IsDynamicRuntimeActor() const noexcept {
            return IsDynamicRuntimeActorRawId(value);
        }
        [[nodiscard]] constexpr bool HasPhysicalActor() const noexcept { return HasPhysicalActorRawId(value); }

        [[nodiscard]] constexpr std::optional<SyntheticChildId> AsSynthetic() const noexcept {
            if (IsSynthetic()) {
                return SyntheticChildId{ value };
            }
            return std::nullopt;
        }

        [[nodiscard]] constexpr std::optional<RefId> AsRef() const noexcept {
            if (HasPhysicalActor()) {
                return RefId{ value };
            }
            return std::nullopt;
        }

        [[nodiscard]] constexpr ActorId AsActorId() const noexcept { return ActorId{ value }; }
        explicit constexpr operator bool() const noexcept { return value != kNullRawId; }

        auto operator<=>(const ChildId&) const noexcept = default;
    };

    // Monotonic 64-bit Affair identifier in AffairLedger
    struct AffairId {
        std::uint64_t value{ 0 };

        constexpr AffairId() noexcept = default;
        explicit constexpr AffairId(std::uint64_t raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value; }
        [[nodiscard]] constexpr bool IsNull() const noexcept { return value == 0; }
        explicit constexpr operator bool() const noexcept { return value != 0; }

        auto operator<=>(const AffairId&) const noexcept = default;
    };

    // Canonical symmetric pair key where first <= second
    struct CanonicalPairKey {
        ActorId first{};
        ActorId second{};

        constexpr CanonicalPairKey() noexcept = default;
        constexpr CanonicalPairKey(ActorId a, ActorId b) noexcept
            : first(a.raw() < b.raw() ? a : b),
              second(a.raw() < b.raw() ? b : a) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return !first.IsNull() && !second.IsNull() && first != second;
        }

        [[nodiscard]] constexpr bool Contains(ActorId actor) const noexcept {
            return !actor.IsNull() && (first == actor || second == actor);
        }

        [[nodiscard]] constexpr ActorId Other(ActorId actor) const noexcept {
            if (first == actor) {
                return second;
            }
            if (second == actor) {
                return first;
            }
            return ActorId{};
        }

        auto operator<=>(const CanonicalPairKey&) const noexcept = default;
    };

    struct CanonicalPairKeyHash {
        [[nodiscard]] std::size_t operator()(const CanonicalPairKey& k) const noexcept {
            const std::size_t h1 = std::hash<std::uint32_t>{}(k.first.raw());
            const std::size_t h2 = std::hash<std::uint32_t>{}(k.second.raw());
            return h1 ^ (h2 + 0x9e3779b9u + (h1 << 6) + (h1 >> 2));
        }
    };

    static_assert(sizeof(RefId) == sizeof(std::uint32_t));
    static_assert(sizeof(BaseId) == sizeof(std::uint32_t));
    static_assert(sizeof(ActorId) == sizeof(std::uint32_t));
    static_assert(sizeof(ChildId) == sizeof(std::uint32_t));
    static_assert(sizeof(SyntheticChildId) == sizeof(std::uint32_t));
    static_assert(std::is_trivially_copyable_v<RefId>);
    static_assert(std::is_trivially_copyable_v<ActorId>);
    static_assert(std::is_trivially_copyable_v<ChildId>);
    static_assert(std::is_trivially_copyable_v<CanonicalPairKey>);

}  // namespace MARAS::Foundation

namespace maras {
    namespace foundation = ::MARAS::Foundation;
}

template <typename Tag>
struct std::hash<MARAS::Foundation::StrongId<Tag>> {
    [[nodiscard]] std::size_t operator()(const MARAS::Foundation::StrongId<Tag>& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.raw());
    }
};

template <>
struct std::hash<MARAS::Foundation::RefId> {
    [[nodiscard]] std::size_t operator()(const MARAS::Foundation::RefId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.raw());
    }
};

template <>
struct std::hash<MARAS::Foundation::SyntheticChildId> {
    [[nodiscard]] std::size_t operator()(const MARAS::Foundation::SyntheticChildId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.raw());
    }
};

template <>
struct std::hash<MARAS::Foundation::ActorId> {
    [[nodiscard]] std::size_t operator()(const MARAS::Foundation::ActorId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.raw());
    }
};

template <>
struct std::hash<MARAS::Foundation::ChildId> {
    [[nodiscard]] std::size_t operator()(const MARAS::Foundation::ChildId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.raw());
    }
};

template <>
struct std::hash<MARAS::Foundation::AffairId> {
    [[nodiscard]] std::size_t operator()(const MARAS::Foundation::AffairId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.raw());
    }
};
