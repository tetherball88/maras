#include "maras/engine/PlayerMiscStatsTracker.h"

#include "RE/G/GameSettingCollection.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/SessionEpoch.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace MARAS {

    namespace {
        constexpr std::array<const char*, 6> kStatGmstNames = {
            "sMostGoldCarried",
            "sHousesOwned",
            "sHorsesOwned",
            "sQuestsCompleted",
            "sDungeonsCleared",
            "sDragonSoulsCollected",
        };

        [[nodiscard]] int* SlotPtr(PlayerTrackedMiscStats& stats, std::size_t idx) noexcept {
            switch (idx) {
                case 0: return &stats.mostGold;
                case 1: return &stats.housesOwned;
                case 2: return &stats.horsesOwned;
                case 3: return &stats.questsCompleted;
                case 4: return &stats.dungeonsCleared;
                case 5: return &stats.dragonSoulsCollected;
                default: return nullptr;
            }
        }
    }  // namespace

    class PlayerMiscStatsTracker::QueryStatCallback final : public RE::BSScript::IStackCallbackFunctor {
    public:
        QueryStatCallback(SessionEpoch epoch, StatSlot slot) noexcept
            : epoch_(epoch), slot_(slot) {}

        ~QueryStatCallback() override = default;

        void operator()(RE::BSScript::Variable a_result) override {
            if (!IsSessionEpochCurrent(epoch_)) {
                return;
            }
            std::int32_t val = 0;
            if (a_result.IsInt()) {
                val = a_result.GetSInt();
            } else if (a_result.IsFloat()) {
                val = static_cast<std::int32_t>(a_result.GetFloat());
            } else {
                return;
            }
            PlayerMiscStatsTracker::GetSingleton()->ApplyVmSeededStat(epoch_, slot_, val);
        }

        void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>& /*a_object*/) override {}

    private:
        SessionEpoch epoch_{ 0 };
        StatSlot slot_{ StatSlot::MostGold };
    };

    PlayerMiscStatsTracker* PlayerMiscStatsTracker::GetSingleton() {
        static PlayerMiscStatsTracker instance;
        return &instance;
    }

    void PlayerMiscStatsTracker::Initialize() {
        auto* gsCol = RE::GameSettingCollection::GetSingleton();
        if (!gsCol) {
            MARAS_LOG_WARN("[PlayerMiscStatsTracker] GameSettingCollection unavailable; using default stat names");
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < kStatGmstNames.size(); ++i) {
            if (auto* setting = gsCol->GetSetting(kStatGmstNames[i]);
                setting && setting->GetType() == RE::Setting::Type::kString) {
                const char* val = setting->GetString();
                if (val && val[0] != '\0') {
                    statNames_[i] = val;
                }
            }
        }
        MARAS_LOG_DEBUG("[PlayerMiscStatsTracker] Resolved {} stat GMST names", statNames_.size());
    }

    RE::BSEventNotifyControl PlayerMiscStatsTracker::ProcessEvent(
        const RE::TESTrackedStatsEvent* a_event,
        RE::BSTEventSource<RE::TESTrackedStatsEvent>*) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const std::string_view statName = a_event->stat.c_str();
        if (statName.empty()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < statNames_.size(); ++i) {
            if (statName == statNames_[i]) {
                if (int* target = SlotPtr(stats_, i)) {
                    *target = std::max(0, static_cast<int>(a_event->value));
                }
                liveUpdated_[i] = true;
                break;
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    void PlayerMiscStatsTracker::Revert() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_ = PlayerTrackedMiscStats{};
        for (bool& flag : liveUpdated_) {
            flag = false;
        }
    }

    void PlayerMiscStatsTracker::ApplyVmSeededStat(
        std::uint64_t expectedEpoch,
        StatSlot slot,
        std::int32_t value) noexcept {
        const auto idx = static_cast<std::size_t>(slot);
        if (idx >= static_cast<std::size_t>(StatSlot::_Count)) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!IsSessionEpochCurrent(static_cast<SessionEpoch>(expectedEpoch))) {
            return;
        }
        if (!liveUpdated_[idx]) {
            if (int* target = SlotPtr(stats_, idx)) {
                *target = std::max(0, static_cast<int>(value));
            }
        }
    }

    void PlayerMiscStatsTracker::SeedFromVmAsync() {
        const SessionEpoch expectedEpoch = GetSessionEpoch();
        AddEpochTask(expectedEpoch, "PlayerMiscStatsTracker::SeedFromVmAsync", [this, expectedEpoch]() {
            if (!IsSessionEpochCurrent(expectedEpoch)) {
                return;
            }
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                MARAS_LOG_WARN("[PlayerMiscStatsTracker] VirtualMachine unavailable; skipping QueryStat seed");
                return;
            }

            std::array<std::string, static_cast<std::size_t>(StatSlot::_Count)> namesCopy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                namesCopy = statNames_;
            }

            for (std::size_t i = 0; i < namesCopy.size(); ++i) {
                RE::BSFixedString statName(namesCopy[i].c_str());
                auto* args = RE::MakeFunctionArguments(std::move(statName));
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                    new QueryStatCallback(expectedEpoch, static_cast<StatSlot>(i)));
                if (!vm->DispatchStaticCall(
                        RE::BSFixedString("Game"),
                        RE::BSFixedString("QueryStat"),
                        args,
                        callback)) {
                    MARAS_LOG_WARN("[PlayerMiscStatsTracker] DispatchStaticCall(Game.QueryStat, '{}') failed",
                                   namesCopy[i]);
                }
            }
        });
    }

    PlayerTrackedMiscStats PlayerMiscStatsTracker::GetStats() const {
        PlayerTrackedMiscStats copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            copy = stats_;
        }
        if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                copy.mostGold = std::max(copy.mostGold, static_cast<int>(player->GetGoldAmount()));
            }
        }
        return copy;
    }

}  // namespace MARAS
