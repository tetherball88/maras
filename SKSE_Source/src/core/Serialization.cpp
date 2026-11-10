#include "core/Serialization.h"

#include "core/AdultChildProgression.h"
#include "core/AdultChildTravelService.h"
#include "core/AffectionService.h"
#include "core/BanterCoordinatorService.h"
#include "core/ChildrenFleeService.h"
#include "core/DeceasedRegistry.h"
#include "core/DialogueEventSink.h"
#include "core/IntimateThreadService.h"
#include "core/MatchmakingService.h"
#include "core/NPCRelationshipManager.h"
#include "core/PackageOverrideService.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/SkyrimNetProfileService.h"
#include "core/SpouseAssetsService.h"
#include "core/SpouseHierarchyManager.h"
#include "ui/ConfirmationModal.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "ui/PrismaUIManager.h"
#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS::Serialization {

    void SaveCallback(SKSE::SerializationInterface* serialization) {
        if (!serialization) {
            MARAS_LOG_ERROR("SaveCallback: serialization interface is null");
            return;
        }

        auto saveRecord = [&](std::uint32_t type, std::uint32_t version, std::string_view label, auto&& saveFn) {
            if (!serialization->OpenRecord(type, version)) {
                MARAS_LOG_ERROR("Failed to open record for saving {}", label);
                return;
            }
            if (!saveFn()) {
                MARAS_LOG_ERROR("Failed to save {}", label);
            } else {
                MARAS_LOG_INFO("Successfully saved {}", label);
            }
        };

        saveRecord(kNPCRelationshipData, kDataVersion, "NPC relationship data", [&]() {
            return NPCRelationshipManager::GetSingleton().Save(serialization);
        });

        saveRecord(kSpouseHierarchyData, kDataVersion, "spouse hierarchy data", [&]() {
            return SpouseHierarchyManager::GetSingleton().Save(serialization);
        });

        saveRecord(kAffectionData, kDataVersion, "affection data", [&]() {
            return AffectionService::GetSingleton().Save(serialization);
        });

        saveRecord(kPlayerHouseData, kDataVersion, "player house data", [&]() {
            return PlayerHouseService::GetSingleton().Save(serialization);
        });

        saveRecord(kSpouseAssetsData, kDataVersion, "spouse assets data", [&]() {
            return SpouseAssetsService::GetSingleton().Save(serialization);
        });

        saveRecord(kPlayerChildrenData, kDataVersion, "player children data", [&]() {
            return PlayerChildrenService::GetSingleton().Save(serialization);
        });

        saveRecord(kChildFactionSnapshotData, kDataVersion, "child faction snapshot data", [&]() {
            return PlayerChildrenService::GetSingleton().SaveFactionSnapshots(serialization);
        });

        saveRecord(kMatchmakingData, kDataVersion, "matchmaking data", [&]() {
            return MatchmakingService::GetSingleton().Save(serialization);
        });

        saveRecord(kDeceasedRegistryData, kDataVersion, "deceased registry data", [&]() {
            return DeceasedRegistry::GetSingleton().Save(serialization);
        });

        saveRecord(kBanterData, kDataVersion, "banter coordinator data", [&]() {
            return BanterCoordinatorService::GetSingleton().Save(serialization);
        });

        saveRecord(kAdultChildTravelData, kDataVersion, "adult child travel data", [&]() {
            return AdultChildTravelService::GetSingleton().Save(serialization);
        });

        saveRecord(kPluginSettingsData, kDataVersion, "plugin settings", [&]() {
            return Settings::GetSingleton().SaveGameSettings(serialization);
        });
    }

    void LoadCallback(SKSE::SerializationInterface* serialization) {
        if (!serialization) {
            MARAS_LOG_ERROR("LoadCallback: serialization interface is null");
            return;
        }

        SkyrimNetMemoryService::SuppressScope suppressMemories;
        SkyrimNetProfileService::SuppressScope suppressProfiles;
        auto& manager = NPCRelationshipManager::GetSingleton();

        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;

        bool npcrLoaded = false;
        bool sphrLoaded = false;
        bool phouLoaded = false;

        // Phase 1: Deserialize each service's independent co-save record
        while (serialization->GetNextRecordInfo(type, version, length)) {
            if (version < 1 || version > kDataVersion) {
                MARAS_LOG_ERROR("Unsupported co-save record version {} for record type 0x{:08X} (expected 1-{})",
                                version, type, kDataVersion);
                continue;
            }

            if (type == kNPCRelationshipData) {
                if (!manager.Load(serialization, version, length)) {
                    MARAS_LOG_ERROR("Failed to load NPC relationship data");
                } else {
                    npcrLoaded = true;
                    MARAS_LOG_INFO("Successfully loaded NPC relationship data (version {})", version);
                }
            } else if (type == kSpouseHierarchyData) {
                if (!SpouseHierarchyManager::GetSingleton().Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load spouse hierarchy data");
                } else {
                    sphrLoaded = true;
                    MARAS_LOG_INFO("Successfully loaded spouse hierarchy data (version {})", version);
                }
            } else if (type == kAffectionData) {
                if (!AffectionService::GetSingleton().Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load affection data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded affection data (version {})", version);
                }
            } else if (type == kPlayerHouseData) {
                if (!PlayerHouseService::GetSingleton().Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load player house data");
                } else {
                    phouLoaded = true;
                    MARAS_LOG_INFO("Successfully loaded player house data (version {})", version);
                }
            } else if (type == kSpouseAssetsData) {
                if (!SpouseAssetsService::GetSingleton().Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load spouse assets data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded spouse assets data (version {})", version);
                }
            } else if (type == kPlayerChildrenData) {
                if (!PlayerChildrenService::GetSingleton().Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load player children data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded player children data (version {})", version);
                }
            } else if (type == kChildFactionSnapshotData) {
                if (!PlayerChildrenService::GetSingleton().LoadFactionSnapshots(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load child faction snapshot data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded child faction snapshot data (version {})", version);
                }
            } else if (type == kMatchmakingData) {
                if (!MatchmakingService::GetSingleton().Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load matchmaking data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded matchmaking data (version {})", version);
                }
            } else if (type == kDeceasedRegistryData) {
                if (!DeceasedRegistry::GetSingleton().Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load deceased registry data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded deceased registry data (version {})", version);
                }
            } else if (type == kBanterData) {
                if (!BanterCoordinatorService::GetSingleton().Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load banter coordinator data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded banter coordinator data (version {})", version);
                }
            } else if (type == kAdultChildTravelData) {
                if (!AdultChildTravelService::GetSingleton().Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load adult child travel data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded adult child travel data (version {})", version);
                }
            } else if (type == kPluginSettingsData) {
                if (!Settings::GetSingleton().LoadGameSettings(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load plugin settings");
                } else {
                    MARAS_LOG_INFO("Successfully loaded plugin settings (version {})", version);
                }
            } else {
                MARAS_LOG_WARN("Skipping unrecognized co-save record type 0x{:08X} (version {}, length {})",
                               type, version, length);
            }
        }

        // Phase 2: Post-chunk cross-service finalization (runs only after all co-save chunks are populated)
        if (npcrLoaded) {
            manager.FinalizeLoad();
        }
        if (sphrLoaded || npcrLoaded) {
            SpouseHierarchyManager::GetSingleton().FillGaps(0, false);
        }
        if (phouLoaded || npcrLoaded) {
            PackageOverrideService::GetSingleton().RebuildFromTenants();
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        AdultChildProgression::GetSingleton().Revert();
        NPCRelationshipManager::GetSingleton().Revert();
        SpouseHierarchyManager::GetSingleton().Revert();
        AffectionService::GetSingleton().Revert();
        PlayerHouseService::GetSingleton().Revert();
        SpouseAssetsService::GetSingleton().Revert();
        PlayerChildrenService::GetSingleton().Revert();
        AdultChildTravelService::GetSingleton().Revert();
        MatchmakingService::GetSingleton().Revert();
        DeceasedRegistry::GetSingleton().Revert();
        BanterCoordinatorService::GetSingleton().Revert();
        Settings::GetSingleton().RevertGameSettings();
        PackageOverrideService::GetSingleton().Revert();
        IntimateThreadService::GetSingleton().ClearAll();
        ChildrenFleeService::GetSingleton().Reset();
        SkyrimNetMemoryService::GetSingleton().Revert();
        SkyrimNetProfileService::GetSingleton().Revert();
        if (auto* dialogueSink = DialogueEventSink::GetSingleton()) {
            dialogueSink->Revert();
        }
        ConfirmationModal::GetSingleton().Revert();
        PrismaModalManager::GetSingleton().Revert();
        PrismaUIManager::GetSingleton().Revert();
        PrismaUIDataBridge::GetSingleton().Revert();

        MARAS_LOG_INFO("Reverted MARAS co-save runtime state");
    }

}  // namespace MARAS::Serialization

