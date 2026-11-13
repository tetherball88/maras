#pragma once

#include "maras/state/CoSaveCodec.h"
#include "maras/state/WorldState.h"

#include <concepts>
#include <cstdint>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MARAS::Tests {

    struct TestAbortException final {};

    struct FailureDiagnostic {
        std::string expression{};
        std::string detail{};
        std::string file{};
        std::uint_least32_t line{ 0 };
    };

    struct TestCaseEntry {
        std::string_view suite{};
        std::string_view name{};
        void (*fn)(){ nullptr };
    };

    std::vector<TestCaseEntry>& Registry();
    void RecordAssertion(bool passed, std::string_view expr, std::string detail, std::source_location loc);

    struct AutoRegisterTest {
        AutoRegisterTest(std::string_view suite, std::string_view name, void (*fn)()) {
            Registry().push_back(TestCaseEntry{ .suite = suite, .name = name, .fn = fn });
        }
    };

    template <typename T>
    concept HasRawMethod = requires(const T& v) {
        { v.raw() } -> std::convertible_to<std::uint64_t>;
    };

    template <typename T>
    [[nodiscard]] std::string FormatTestValue(const T& value) {
        using CleanT = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<CleanT, bool>) {
            return value ? "true" : "false";
        } else if constexpr (std::is_convertible_v<CleanT, std::string_view>) {
            return std::format("\"{}\"", std::string_view(value));
        } else if constexpr (HasRawMethod<CleanT>) {
            return std::format("0x{:X}", static_cast<std::uint64_t>(value.raw()));
        } else if constexpr (std::is_enum_v<CleanT>) {
            return std::format("enum({})", static_cast<long long>(std::to_underlying(value)));
        } else if constexpr (std::is_integral_v<CleanT>) {
            if constexpr (std::is_signed_v<CleanT>) {
                return std::format("{}", static_cast<long long>(value));
            } else {
                return std::format("{} (0x{:X})", static_cast<unsigned long long>(value),
                                   static_cast<unsigned long long>(value));
            }
        } else if constexpr (std::is_floating_point_v<CleanT>) {
            return std::format("{:.6g}", static_cast<double>(value));
        } else {
            return "<object>";
        }
    }

    inline void CheckImpl(
        bool passed,
        std::string_view expr,
        std::string detail,
        std::source_location loc) {
        RecordAssertion(passed, expr, std::move(detail), loc);
    }

    inline void RequireImpl(
        bool passed,
        std::string_view expr,
        std::string detail,
        std::source_location loc) {
        RecordAssertion(passed, expr, std::move(detail), loc);
        if (!passed) {
            throw TestAbortException{};
        }
    }

    template <typename Lhs, typename Rhs>
    void CheckEqImpl(
        const Lhs& lhs,
        const Rhs& rhs,
        std::string_view lhsExpr,
        std::string_view rhsExpr,
        std::source_location loc) {
        const bool ok = (lhs == rhs);
        std::string detail{};
        if (!ok) {
            detail = std::format("lhs ({}) = {}, rhs ({}) = {}",
                                 lhsExpr, FormatTestValue(lhs),
                                 rhsExpr, FormatTestValue(rhs));
        }
        RecordAssertion(ok, std::format("{} == {}", lhsExpr, rhsExpr), std::move(detail), loc);
    }

    template <typename Lhs, typename Rhs>
    void RequireEqImpl(
        const Lhs& lhs,
        const Rhs& rhs,
        std::string_view lhsExpr,
        std::string_view rhsExpr,
        std::source_location loc) {
        const bool ok = (lhs == rhs);
        std::string detail{};
        if (!ok) {
            detail = std::format("lhs ({}) = {}, rhs ({}) = {}",
                                 lhsExpr, FormatTestValue(lhs),
                                 rhsExpr, FormatTestValue(rhs));
        }
        RecordAssertion(ok, std::format("{} == {}", lhsExpr, rhsExpr), std::move(detail), loc);
        if (!ok) {
            throw TestAbortException{};
        }
    }

    /**
     * Configurable mock FormID resolver & actor metadata provider for headless tests.
     * Supports load-order FormID remapping, uninstalled form rejection, unloaded placed NPC preservation,
     * dead-on-load actor reconciliation, and orphaned 0xFFxxxxxx dynamic reference pruning.
     */
    class MockFormIdResolver final : public State::IFormIdResolver {
    public:
        std::unordered_map<std::uint32_t, std::uint32_t> remapTable{};
        std::unordered_set<std::uint32_t> unresolvedRawIds{};
        std::unordered_set<Foundation::ActorId> deadOrInvalidActors{};
        std::unordered_set<Foundation::RefId> missingDynamicRefs{};
        std::unordered_map<Foundation::ActorId, State::ActorSex> sexes{};
        std::unordered_map<Foundation::ActorId, std::string> names{};
        std::unordered_map<Foundation::ActorId, Foundation::FormRawId> races{};
        std::unordered_map<Foundation::ActorId, Foundation::BaseId> baseIds{};

        [[nodiscard]] bool ResolveFormId(std::uint32_t oldRawId, std::uint32_t& outNewRawId) const noexcept override {
            if (oldRawId == 0 || unresolvedRawIds.contains(oldRawId)) {
                outNewRawId = 0;
                return false;
            }
            if (const auto it = remapTable.find(oldRawId); it != remapTable.end()) {
                outNewRawId = it->second;
                return outNewRawId != 0;
            }
            outNewRawId = oldRawId;
            return true;
        }

        [[nodiscard]] State::ActorSex GetSex(Foundation::ActorId actor) const noexcept override {
            if (const auto it = sexes.find(actor); it != sexes.end()) {
                return it->second;
            }
            return State::ActorSex::Unknown;
        }

        [[nodiscard]] bool IsAliveAndValid(Foundation::ActorId actor) const noexcept override {
            if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
                return false;
            }
            return !deadOrInvalidActors.contains(actor);
        }

        [[nodiscard]] bool IsDynamicActorPresent(Foundation::RefId refId) const noexcept override {
            if (refId.IsNull()) {
                return false;
            }
            return !missingDynamicRefs.contains(refId);
        }

        [[nodiscard]] std::string GetActorName(Foundation::ActorId actor) const override {
            if (const auto it = names.find(actor); it != names.end()) {
                return it->second;
            }
            if (actor.IsPlayer()) {
                return "Player";
            }
            return {};
        }

        [[nodiscard]] Foundation::FormRawId GetActorRaceId(Foundation::ActorId actor) const noexcept override {
            if (const auto it = races.find(actor); it != races.end()) {
                return it->second;
            }
            return Foundation::FormRawId{};
        }

        [[nodiscard]] Foundation::BaseId GetActorBaseId(Foundation::ActorId actor) const noexcept override {
            if (const auto it = baseIds.find(actor); it != baseIds.end()) {
                return it->second;
            }
            if (actor.IsPlayer()) {
                return Foundation::BaseId{ Foundation::kPlayerBaseRawId };
            }
            if (actor.IsDynamicRuntimeActor()) {
                return Foundation::BaseId{ actor.raw() };
            }
            return Foundation::BaseId{};
        }
    };

}  // namespace MARAS::Tests

#define MARAS_TEST(Suite, Name)                                                             \
    static void MarasTest_##Suite##_##Name();                                               \
    [[maybe_unused]] static const ::MARAS::Tests::AutoRegisterTest kMarasReg_##Suite##_##Name{ \
        #Suite, #Name, &MarasTest_##Suite##_##Name                                          \
    };                                                                                      \
    static void MarasTest_##Suite##_##Name()

#define MARAS_CHECK(Expr)                                                            \
    ::MARAS::Tests::CheckImpl(static_cast<bool>(Expr), #Expr, {}, std::source_location::current())

#define MARAS_REQUIRE(Expr)                                                          \
    ::MARAS::Tests::RequireImpl(static_cast<bool>(Expr), #Expr, {}, std::source_location::current())

#define MARAS_CHECK_EQ(Lhs, Rhs)                                                     \
    ::MARAS::Tests::CheckEqImpl((Lhs), (Rhs), #Lhs, #Rhs, std::source_location::current())

#define MARAS_REQUIRE_EQ(Lhs, Rhs)                                                   \
    ::MARAS::Tests::RequireEqImpl((Lhs), (Rhs), #Lhs, #Rhs, std::source_location::current())
