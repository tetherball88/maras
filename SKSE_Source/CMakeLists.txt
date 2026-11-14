# It's recommended to set a minimum CMake version.
# If you use CMake features from higher versions, update this to match.
cmake_minimum_required(VERSION 3.21)

# Set your project name. This will be the name of your SKSE .dll file.
project(MARAS VERSION 0.0.1 LANGUAGES CXX)

# #
# YOU DO NOT NEED TO EDIT ANYTHING BELOW HERE
# #

# If you're not using a mod manager, you probably want the SKSE plugin to go
# inside of your Skyrim "Data" folder.
#
# To do this automatically, set the `SKYRIM_FOLDER` environment variable
# to the path of your Skyrim Special Edition folder
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

# Force static linking for all vcpkg dependencies
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

add_subdirectory("${CommonLibPath}" ${CMAKE_CURRENT_BINARY_DIR}/_deps/clib-build EXCLUDE_FROM_ALL)
get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)

find_path(RAPIDCSV_INCLUDE_DIRS
    NAMES rapidcsv.h
    PATH_SUFFIXES rapidcsv
    REQUIRED
)

include("${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake")

# Otherwise, you can set OUTPUT_FOLDER to any place you'd like :)
# set(OUTPUT_FOLDER "C:/path/to/any/folder")

# Setup your SKSE plugin as an SKSE plugin!
# find_package(CommonLibSSE CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
if(TARGET yaml-cpp::yaml-cpp)
    set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
else()
    set(YAML_CPP_TARGET yaml-cpp)
endif()

# ---------------------------------------------------- #
#     Engine-Free Layered Modules (Phase 1+ Core)      #
# ---------------------------------------------------- #

add_library(maras_foundation STATIC
    modules/foundation/src/EnumUtils.cpp
    modules/foundation/src/Logging.cpp
)
target_compile_features(maras_foundation PUBLIC cxx_std_23)
target_include_directories(maras_foundation
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/foundation/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/foundation/src
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

add_library(maras_projection STATIC
    modules/projection/src/PerspectiveLens.cpp
)
target_compile_features(maras_projection PUBLIC cxx_std_23)
target_include_directories(maras_projection
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/projection/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/projection/src
)
target_link_libraries(maras_projection PUBLIC maras_state)

add_library(maras_domain_relationships STATIC
    modules/domain_relationships/src/RelationshipRules.cpp
    modules/domain_relationships/src/InfidelityRules.cpp
    modules/domain_relationships/src/AffectionRules.cpp
    modules/domain_relationships/src/SpouseHierarchyRules.cpp
    modules/domain_relationships/src/MarriageDifficultyRules.cpp
    modules/domain_relationships/src/NpcTypeRules.cpp
    modules/domain_relationships/src/BonusesDomain.cpp
)
target_compile_features(maras_domain_relationships PUBLIC cxx_std_23)
target_include_directories(maras_domain_relationships
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_relationships/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_relationships/src
)
target_link_libraries(maras_domain_relationships PUBLIC maras_state)

add_library(maras_domain_lineage STATIC
    modules/domain_lineage/src/LineageRules.cpp
    modules/domain_lineage/src/FertilityRules.cpp
    modules/domain_lineage/src/ChildGrowthRules.cpp
    modules/domain_lineage/src/TrainingCatalog.cpp
    modules/domain_lineage/src/NamePool.cpp
)
target_compile_features(maras_domain_lineage PUBLIC cxx_std_23)
target_include_directories(maras_domain_lineage
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_lineage/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/domain_lineage/src
)
target_link_libraries(maras_domain_lineage PUBLIC maras_projection maras_state)

add_library(maras_domain_household STATIC
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
target_link_libraries(maras_domain_household PUBLIC maras_projection maras_state maras_domain_relationships)

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

if(MSVC)
    set_property(TARGET maras_foundation PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET maras_state PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET maras_projection PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET maras_domain_relationships PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET maras_domain_lineage PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET maras_domain_household PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET maras_domain_agency PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Collect all source files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "plugin/plugin.cpp"
    "modules/engine/src/*.cpp"
    "modules/app/src/*.cpp"
    "modules/bridge_c_api/src/*.cpp"
    "modules/bridge_papyrus/src/*.cpp"
    "modules/bridge_ui/src/*.cpp"
    "modules/bridge_skyrimnet/src/*.cpp"
    "version.rc"
)

# add_library(${PROJECT_NAME} SHARED ${SOURCES})
add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${SOURCES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23) # <--- use C++23 standard
target_precompile_headers(${PROJECT_NAME} PRIVATE PCH.h) # <--- PCH.h is required!
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    maras_domain_agency
    maras_domain_household
    maras_domain_lineage
    maras_domain_relationships
    maras_projection
    maras_state
    nlohmann_json::nlohmann_json
    ${YAML_CPP_TARGET}
    ${CommonLibName}::${CommonLibName}
)

# Add include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/app/include
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_c_api/include
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_papyrus/include
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_skyrimnet/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Silence Microsoft STL deprecation warnings
target_compile_definitions(${PROJECT_NAME} PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)

# Force static linking of runtime library to reduce dependencies
if(MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Also ensure CommonLibSSE uses static runtime
    if(TARGET ${CommonLibName})
        set_property(TARGET ${CommonLibName} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    # Additional linker flags to embed dependencies
    target_link_options(${PROJECT_NAME} PRIVATE
        /INCREMENTAL:NO
        /OPT:REF
        /OPT:ICF
    )
endif()

# When your SKSE .dll is compiled, this will automatically copy the .dll into your mods folder.
# Only works if you configure DEPLOY_ROOT above (or set the SKYRIM_MODS_FOLDER environment variable)
if(DEFINED OUTPUT_FOLDER)
    # If you specify an <OUTPUT_FOLDER> (including via environment variables)
    # then we'll copy your mod files into Skyrim or a mod manager for you!

    # Copy the SKSE plugin .dll files into the SKSE/Plugins/ folder
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    # If you perform a "Debug" build, also copy .pdb file (for debug symbols)
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
    )
    target_compile_features(maras_tests PRIVATE cxx_std_23)
    target_include_directories(maras_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
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
