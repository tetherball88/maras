cmake_minimum_required(VERSION 3.21)

project(MARAS VERSION 0.0.1 LANGUAGES CXX)

set(MOD_FOLDER_NAME "M.A.R.A.S Dev")
set(SKYRIM_MODS_FOLDER "E:\\Skyrim\\development\\mods")
set(OUTPUT_FOLDER "${SKYRIM_MODS_FOLDER}/${MOD_FOLDER_NAME}")

# ---------------------------------------------------- #
#                     CommonLib                        #
# ---------------------------------------------------- #

if(NOT DEFINED ENV{COMMONLIB_SSE_FOLDER})
    message(FATAL_ERROR "Missing COMMONLIB_SSE_FOLDER environment variable")
endif()

set(CommonLibPath "$ENV{COMMONLIB_SSE_FOLDER}")
set(CommonLibName "CommonLibSSE")

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

find_path(RAPIDCSV_INCLUDE_DIRS
    NAMES rapidcsv.h
    PATH_SUFFIXES rapidcsv
    REQUIRED
)

add_subdirectory("${CommonLibPath}" ${CMAKE_CURRENT_BINARY_DIR}/_deps/clib-build EXCLUDE_FROM_ALL)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
if(TARGET yaml-cpp::yaml-cpp)
    set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
else()
    set(YAML_CPP_TARGET yaml-cpp)
endif()

# ---------------------------------------------------- #
#   Layers 0-2: Pure Engine-Free STATIC Libraries      #
#   Precompiled Header: pch_core.h                     #
# ---------------------------------------------------- #

add_library(maras_foundation STATIC
    modules/foundation/src/EnumUtils.cpp
    modules/foundation/src/LocalizationCatalog.cpp
    modules/foundation/src/Logging.cpp
)
target_compile_features(maras_foundation PUBLIC cxx_std_23)
target_compile_definitions(maras_foundation PUBLIC _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
if(MSVC)
    target_compile_options(maras_foundation PUBLIC /FS)
endif()
target_include_directories(maras_foundation
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/foundation/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/foundation/src
)
target_precompile_headers(maras_foundation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch_core.h
)

add_library(maras_state STATIC
    modules/state/src/WorldTables.cpp
    modules/state/src/WorldState.cpp
    modules/state/src/WorldStore.cpp
    modules/state/src/CoSaveCodec.cpp
    modules/state/src/SpatialActorSnapshot.cpp
    modules/state/src/EngineSyncPlan.cpp
    modules/state/src/SessionEpoch.cpp
)
target_compile_features(maras_state PUBLIC cxx_std_23)
target_include_directories(maras_state
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/state/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/state/src
)
target_link_libraries(maras_state PUBLIC maras_foundation)
target_precompile_headers(maras_state REUSE_FROM maras_foundation)

add_library(maras_projection STATIC
    modules/projection/src/EventDispatchPlan.cpp
    modules/projection/src/PerspectiveLens.cpp
)
target_compile_features(maras_projection PUBLIC cxx_std_23)
target_include_directories(maras_projection
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/projection/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/projection/src
)
target_link_libraries(maras_projection PUBLIC maras_state)
target_precompile_headers(maras_projection REUSE_FROM maras_foundation)

add_library(maras_domain_relationships STATIC
    modules/domain_relationships/src/RelationshipRules.cpp
    modules/domain_relationships/src/InfidelityRules.cpp
    modules/domain_relationships/src/AffectionRules.cpp
    modules/domain_relationships/src/AffectionEventRules.cpp
    modules/domain_relationships/src/SpouseHierarchyRules.cpp
    modules/domain_relationships/src/MarriageDifficultyRules.cpp
    modules/domain_relationships/src/NpcTypeRules.cpp
    modules/domain_relationships/src/RelationshipSettlementRules.cpp
    modules/domain_relationships/src/GiftAndIntimacyRules.cpp
    modules/domain_relationships/src/DialogueAndActionRules.cpp
    modules/domain_relationships/src/BonusesDomain.cpp
)
target_compile_features(maras_domain_relationships PUBLIC cxx_std_23)
target_include_directories(maras_domain_relationships
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_relationships/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_relationships/src
)
target_link_libraries(maras_domain_relationships PUBLIC maras_state)
target_precompile_headers(maras_domain_relationships REUSE_FROM maras_foundation)

add_library(maras_domain_lineage STATIC
    modules/domain_lineage/src/LineageRules.cpp
    modules/domain_lineage/src/FertilityRules.cpp
    modules/domain_lineage/src/ChildGrowthRules.cpp
    modules/domain_lineage/src/TrainingCatalog.cpp
    modules/domain_lineage/src/NamePool.cpp
    modules/domain_lineage/src/PaternityActionRules.cpp
    modules/domain_lineage/src/FertilityLifecycleRules.cpp
    modules/domain_lineage/src/ChildProfileGuidance.cpp
)
target_compile_features(maras_domain_lineage PUBLIC cxx_std_23)
target_include_directories(maras_domain_lineage
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_lineage/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_lineage/src
)
target_link_libraries(maras_domain_lineage PUBLIC maras_projection maras_state)
target_precompile_headers(maras_domain_lineage REUSE_FROM maras_foundation)

add_library(maras_domain_household STATIC
    modules/domain_household/src/ChildForcegreetRules.cpp
    modules/domain_household/src/HouseholdRules.cpp
    modules/domain_household/src/SpouseAssetRules.cpp
    modules/domain_household/src/DeceasedQueries.cpp
    modules/domain_household/src/TravelRules.cpp
    modules/domain_household/src/SpouseBuffRules.cpp
)
target_compile_features(maras_domain_household PUBLIC cxx_std_23)
target_include_directories(maras_domain_household
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_household/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_household/src
)
target_link_libraries(maras_domain_household PUBLIC maras_projection maras_state maras_domain_relationships maras_domain_lineage)
target_precompile_headers(maras_domain_household REUSE_FROM maras_foundation)

add_library(maras_domain_agency STATIC
    modules/domain_agency/src/MatchmakingRules.cpp
    modules/domain_agency/src/NpcMatchmaker.cpp
    modules/domain_agency/src/PartnerAgencyEvaluator.cpp
    modules/domain_agency/src/EncounterTemplate.cpp
    modules/domain_agency/src/BanterPlanner.cpp
)
target_compile_features(maras_domain_agency PUBLIC cxx_std_23)
target_include_directories(maras_domain_agency
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_agency/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_agency/src
)
target_link_libraries(maras_domain_agency PUBLIC maras_projection maras_state maras_domain_relationships maras_domain_lineage)
target_precompile_headers(maras_domain_agency REUSE_FROM maras_foundation)

# ---------------------------------------------------- #
#   Layers 3-5: Engine, App & Bridge OBJECT Libraries  #
#   Precompiled Header: pch_engine.h                   #
# ---------------------------------------------------- #

# Dedicated STATIC PCH owner in the build tree prevents CMake's VS generator from duplicating
# cmake_pch.obj across OBJECT targets and the final SHARED link step (LNK4042).
set(_maras_pch_anchor "${CMAKE_CURRENT_BINARY_DIR}/maras_pch_engine_anchor.cpp")
if(NOT EXISTS "${_maras_pch_anchor}")
    file(WRITE "${_maras_pch_anchor}" "// Anchor translation unit for CMake pch_engine.pch generation.\n")
endif()
add_library(maras_pch_engine STATIC
    "${_maras_pch_anchor}"
)
target_compile_features(maras_pch_engine PUBLIC cxx_std_23)
target_link_libraries(maras_pch_engine PUBLIC maras_foundation ${CommonLibName}::${CommonLibName})
target_precompile_headers(maras_pch_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch_engine.h
)

add_library(maras_engine OBJECT
    modules/engine/src/ActorEngineUtils.cpp
    modules/engine/src/ActorService.cpp
    modules/engine/src/BanterSceneStarter.cpp
    modules/engine/src/BedOwnershipReconciler.cpp
    modules/engine/src/BonusesConfigLoader.cpp
    modules/engine/src/BuffReconciler.cpp
    modules/engine/src/ChildrenFleeService.cpp
    modules/engine/src/ChildSpawnService.cpp
    modules/engine/src/ChildVisualReconciler.cpp
    modules/engine/src/DeathEventHandler.cpp
    modules/engine/src/DeferredActorResolver.cpp
    modules/engine/src/DialogueEventSink.cpp
    modules/engine/src/EncounterConfigLoader.cpp
    modules/engine/src/EngineLogging.cpp
    modules/engine/src/FactionReconciler.cpp
    modules/engine/src/FormCache.cpp
    modules/engine/src/FormUtils.cpp
    modules/engine/src/GiftMenuEventSink.cpp
    modules/engine/src/GlobalVarReconciler.cpp
    modules/engine/src/HouseholdEngineOps.cpp
    modules/engine/src/IntimateThreadService.cpp
    modules/engine/src/JsonOverrideLoader.cpp
    modules/engine/src/LocalizationService.cpp
    modules/engine/src/MarriageDifficultyLoader.cpp
    modules/engine/src/NamePoolLoader.cpp
    modules/engine/src/NPCIndexReportWriter.cpp
    modules/engine/src/NPCTypeDeterminer.cpp
    modules/engine/src/PackageReconciler.cpp
    modules/engine/src/PlayerChildrenEngineOps.cpp
    modules/engine/src/PlayerMiscStatsTracker.cpp
    modules/engine/src/PollingService.cpp
    modules/engine/src/QuestEventConfigLoader.cpp
    modules/engine/src/QuestEventHandler.cpp
    modules/engine/src/RelationshipEngineReconciler.cpp
    modules/engine/src/SkseSerializationCallbacks.cpp
    modules/engine/src/SleepEventSink.cpp
    modules/engine/src/SpatialWorldIndex.cpp
    modules/engine/src/TaskDispatcher.cpp
    modules/engine/src/TrainingCatalogLoader.cpp
    modules/engine/src/PromptStringsLoader.cpp
)
target_compile_features(maras_engine PUBLIC cxx_std_23)
target_include_directories(maras_engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/engine/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/engine/src
)
target_link_libraries(maras_engine
    PUBLIC
    maras_domain_agency
    maras_domain_household
    maras_domain_lineage
    maras_domain_relationships
    maras_projection
    maras_state
    maras_foundation
    ${CommonLibName}::${CommonLibName}
    nlohmann_json::nlohmann_json
    PRIVATE
    ${YAML_CPP_TARGET}
)
target_precompile_headers(maras_engine REUSE_FROM maras_pch_engine)

add_library(maras_app OBJECT
    modules/app/src/AffectionEventCoordinator.cpp
    modules/app/src/AgencyWorkflowCoordinator.cpp
    modules/app/src/AppContext.cpp
    modules/app/src/AppLifecycleCoordinator.cpp
    modules/app/src/AsyncLlmBatchRunner.cpp
    modules/app/src/BanterSceneCoordinator.cpp
    modules/app/src/ChildInteractionCoordinator.cpp
    modules/app/src/ChildSkyrimNetSuccession.cpp
    modules/app/src/DialogueOutcomeCoordinator.cpp
    modules/app/src/FertilityLifecycleCoordinator.cpp
    modules/app/src/HouseholdUseCases.cpp
    modules/app/src/LineageUseCases.cpp
    modules/app/src/ModalTypes.cpp
    modules/app/src/PaternityWorkflowCoordinator.cpp
    modules/app/src/RelationshipInteractionCoordinator.cpp
    modules/app/src/RelationshipUseCases.cpp
    modules/app/src/SettingsCoordinator.cpp
    modules/app/src/SkyrimNetActionCoordinator.cpp
)
target_compile_features(maras_app PUBLIC cxx_std_23)
target_include_directories(maras_app
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/app/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/app/src
)
target_link_libraries(maras_app PRIVATE maras_engine)
target_precompile_headers(maras_app REUSE_FROM maras_pch_engine)

add_library(maras_bridge_papyrus OBJECT
    modules/bridge_papyrus/src/PapyrusChildInteractions.cpp
    modules/bridge_papyrus/src/PapyrusChildren.cpp
    modules/bridge_papyrus/src/PapyrusHousehold.cpp
    modules/bridge_papyrus/src/PapyrusInterface.cpp
    modules/bridge_papyrus/src/PapyrusRelationship.cpp
    modules/bridge_papyrus/src/PapyrusSettings.cpp
    modules/bridge_papyrus/src/PapyrusSkyrimNet.cpp
)
target_compile_features(maras_bridge_papyrus PUBLIC cxx_std_23)
target_include_directories(maras_bridge_papyrus
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_papyrus/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_papyrus/src
)
target_link_libraries(maras_bridge_papyrus PRIVATE maras_app maras_engine)
target_precompile_headers(maras_bridge_papyrus REUSE_FROM maras_pch_engine)

add_library(maras_bridge_ui OBJECT
    modules/bridge_ui/src/ConfirmationModal.cpp
    modules/bridge_ui/src/HouseholdBuffsJsonBuilder.cpp
    modules/bridge_ui/src/LocaleJsonBuilder.cpp
    modules/bridge_ui/src/ModalManager.cpp
    modules/bridge_ui/src/UIDataBridge.cpp
    modules/bridge_ui/src/UIManager.cpp
)
target_compile_features(maras_bridge_ui PUBLIC cxx_std_23)
target_include_directories(maras_bridge_ui
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_ui/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_ui/src
)
target_link_libraries(maras_bridge_ui PRIVATE maras_app maras_engine)
target_precompile_headers(maras_bridge_ui REUSE_FROM maras_pch_engine)

add_library(maras_bridge_skyrimnet OBJECT
    modules/bridge_skyrimnet/src/SkyrimNetApiLoader.cpp
    modules/bridge_skyrimnet/src/SkyrimNetDecisions.cpp
    modules/bridge_skyrimnet/src/SkyrimNetDecorators.cpp
    modules/bridge_skyrimnet/src/SkyrimNetEventPayloads.cpp
    modules/bridge_skyrimnet/src/SkyrimNetEventService.cpp
    modules/bridge_skyrimnet/src/SkyrimNetMemoryService.cpp
    modules/bridge_skyrimnet/src/SkyrimNetProfileService.cpp
)
target_compile_features(maras_bridge_skyrimnet PUBLIC cxx_std_23)
target_include_directories(maras_bridge_skyrimnet
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_skyrimnet/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_skyrimnet/src
            ${CMAKE_CURRENT_SOURCE_DIR}/external/skyrimnet
)
target_link_libraries(maras_bridge_skyrimnet PRIVATE maras_app maras_engine)
target_precompile_headers(maras_bridge_skyrimnet REUSE_FROM maras_pch_engine)

add_library(maras_bridge_c_api OBJECT
    modules/bridge_c_api/src/MARAS_PublicAPI.cpp
)
target_compile_features(maras_bridge_c_api PUBLIC cxx_std_23)
target_include_directories(maras_bridge_c_api
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_c_api/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_c_api/src
)
target_link_libraries(maras_bridge_c_api PRIVATE maras_engine)
target_precompile_headers(maras_bridge_c_api REUSE_FROM maras_pch_engine)

# ---------------------------------------------------- #
#   Composition Root: MARAS.dll                        #
# ---------------------------------------------------- #

add_library(${PROJECT_NAME} SHARED
    plugin/plugin.cpp
    version.rc
)
set_target_properties(${PROJECT_NAME} PROPERTIES DEFINE_SYMBOL "")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} REUSE_FROM maras_pch_engine)
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    maras_bridge_papyrus
    maras_bridge_ui
    maras_bridge_skyrimnet
    maras_bridge_c_api
    maras_app
    maras_engine
    ${YAML_CPP_TARGET}
)

if(MSVC)
    foreach(_tgt IN ITEMS
        maras_foundation
        maras_state
        maras_projection
        maras_domain_relationships
        maras_domain_lineage
        maras_domain_household
        maras_domain_agency
        maras_pch_engine
        maras_engine
        maras_app
        maras_bridge_papyrus
        maras_bridge_ui
        maras_bridge_skyrimnet
        maras_bridge_c_api
        ${PROJECT_NAME}
    )
        set_property(TARGET ${_tgt} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()

    if(TARGET ${CommonLibName})
        set_property(TARGET ${CommonLibName} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    target_link_options(${PROJECT_NAME} PRIVATE
        /INCREMENTAL:NO
        /OPT:REF
        /OPT:ICF
    )
endif()

if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endif()

# ---------------------------------------------------- #
#   Headless Unit Tests & Engine-Free Link Gate        #
# ---------------------------------------------------- #

option(MARAS_BUILD_TESTS "Build MARAS headless unit tests" ON)
if(MARAS_BUILD_TESTS)
    enable_testing()
    add_executable(maras_tests
        tests/main.cpp
        tests/test_cosave_migrations.cpp
        tests/test_custody_and_death.cpp
        tests/test_affair_ledger.cpp
        tests/test_world_store.cpp
        tests/test_perspective_lens.cpp
        tests/test_reconcilers.cpp
        tests/test_domain_relationships.cpp
        tests/test_domain_lineage.cpp
        tests/test_domain_household.cpp
        tests/test_domain_agency.cpp
        tests/test_localization.cpp
    )
    target_compile_features(maras_tests PRIVATE cxx_std_23)
    target_include_directories(maras_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_precompile_headers(maras_tests REUSE_FROM maras_foundation)
    # Physical LNK2019 link gate: maras_tests links ONLY pure STATIC domain/projection/state/foundation targets
    target_link_libraries(maras_tests PRIVATE
        maras_domain_agency
        maras_domain_household
        maras_domain_lineage
        maras_domain_relationships
        maras_projection
        maras_state
        maras_foundation
    )
    if(MSVC)
        set_property(TARGET maras_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME maras_headless_tests COMMAND maras_tests)
endif()
