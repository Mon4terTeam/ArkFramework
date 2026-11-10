cmake_minimum_required(VERSION 3.22)
project(ArkFramework VERSION 1.0.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "ArkFramework targets Windows x64")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ARK_DIST_DIRECTORY "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Runtime output directory")
set(ARK_TEST_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-1.91.9b")
set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook-1.3.4")

# Places a target's DLL/EXE in `directory` for every build configuration.
function(ark_output_directory directory)
  foreach(target IN LISTS ARGN)
    set_target_properties(${target} PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${directory}")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
      set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${directory}")
    endforeach()
  endforeach()
endfunction()

# Executables use wide entry points (wmain / wWinMain).
function(ark_wide_entry_point)
  if(MINGW)
    foreach(target IN LISTS ARGN)
      target_link_options(${target} PRIVATE -municode)
    endforeach()
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Sources. The globs mirror build.ps1 so both builds always agree.
# ---------------------------------------------------------------------------
file(GLOB ARK_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB ARK_NATIVE_HOOK_SOURCES CONFIGURE_DEPENDS src/core/native_hook*.cpp)
set(ARK_CORE_ENTRY_SOURCES ${ARK_CORE_SOURCES})
list(FILTER ARK_CORE_ENTRY_SOURCES INCLUDE REGEX "/(framework|overlay[^/]*)\\.cpp$")
set(ARK_PLUGIN_RUNTIME_SOURCES ${ARK_CORE_SOURCES})
list(REMOVE_ITEM ARK_PLUGIN_RUNTIME_SOURCES ${ARK_CORE_ENTRY_SOURCES} ${ARK_NATIVE_HOOK_SOURCES})
file(GLOB ARK_LOADER_SOURCES CONFIGURE_DEPENDS src/loader/*.cpp)
file(GLOB ARK_BOOTSTRAP_SOURCES CONFIGURE_DEPENDS src/bootstrap/*.c)
file(GLOB ARK_DAMAGE_METER_SOURCES CONFIGURE_DEPENDS plugins/damage_meter/*.cpp plugins/common/*.cpp)
file(GLOB ARK_BETTER_BATTLE_SOURCES CONFIGURE_DEPENDS plugins/BetterBattle/*.cpp plugins/common/*.cpp)
file(GLOB ARK_WEB_BRIDGE_SOURCES CONFIGURE_DEPENDS plugins/web_bridge/*.cpp)
file(GLOB ARK_TEST_HOST_SOURCES CONFIGURE_DEPENDS tests/host/*.cpp)

# ---------------------------------------------------------------------------
# Third-party libraries
# ---------------------------------------------------------------------------
add_library(ark_headers INTERFACE)
target_include_directories(ark_headers INTERFACE include third_party "${IMGUI_DIR}")
target_compile_definitions(ark_headers INTERFACE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN IMGUI_USER_CONFIG="ark/imgui_config.h")

add_library(ArkImGui SHARED
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_win32.cpp")
target_compile_definitions(ArkImGui PRIVATE ARK_IMGUI_BUILD)
target_link_libraries(ArkImGui PUBLIC ark_headers PRIVATE d3d11 dxgi dwmapi gdi32 user32)

add_library(ark_minhook STATIC
  "${MINHOOK_DIR}/src/buffer.c"
  "${MINHOOK_DIR}/src/hook.c"
  "${MINHOOK_DIR}/src/trampoline.c"
  "${MINHOOK_DIR}/src/hde/hde64.c")
target_include_directories(ark_minhook PUBLIC "${MINHOOK_DIR}/include")
target_compile_definitions(ark_minhook PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

# ---------------------------------------------------------------------------
# Framework
# ---------------------------------------------------------------------------
add_library(ark_native_hooks OBJECT ${ARK_NATIVE_HOOK_SOURCES})
target_link_libraries(ark_native_hooks PRIVATE ark_headers ark_minhook)

add_library(ark_plugin_runtime OBJECT ${ARK_PLUGIN_RUNTIME_SOURCES})
target_link_libraries(ark_plugin_runtime PRIVATE ark_headers ark_minhook)

add_library(ArkFramework SHARED
  ${ARK_CORE_ENTRY_SOURCES}
  $<TARGET_OBJECTS:ark_plugin_runtime>
  $<TARGET_OBJECTS:ark_native_hooks>)
target_link_libraries(ArkFramework PRIVATE ArkImGui ark_headers ark_minhook d3d11 dxgi user32 gdi32)

add_executable(ArkFrameworkLoader ${ARK_LOADER_SOURCES})
target_link_libraries(ArkFrameworkLoader PRIVATE ark_headers user32 advapi32 shell32)

add_library(ArkBootstrap SHARED ${ARK_BOOTSTRAP_SOURCES})
target_link_libraries(ArkBootstrap PRIVATE user32)

add_executable(ArkFrameworkHost WIN32 ${ARK_TEST_HOST_SOURCES})
target_link_libraries(ArkFrameworkHost PRIVATE ark_headers d3d11 dxgi user32 gdi32)

ark_output_directory("${ARK_DIST_DIRECTORY}" ArkImGui ArkFramework ArkFrameworkLoader ArkBootstrap
  ArkFrameworkHost)
ark_wide_entry_point(ArkFrameworkLoader ArkFrameworkHost)

# ---------------------------------------------------------------------------
# Plugins
# ---------------------------------------------------------------------------
add_library(damage_meter SHARED ${ARK_DAMAGE_METER_SOURCES})
target_link_libraries(damage_meter PRIVATE ark_headers ArkImGui user32)
ark_output_directory("${ARK_DIST_DIRECTORY}/plugins" damage_meter)
# The graphical self-test uses damage_meter's synthetic battle event provider.
add_dependencies(ArkFrameworkHost damage_meter)

add_library(BetterBattle SHARED ${ARK_BETTER_BATTLE_SOURCES})
target_link_libraries(BetterBattle PRIVATE ark_headers ArkImGui user32)
ark_output_directory("${ARK_DIST_DIRECTORY}/plugins" BetterBattle)

add_library(web_bridge SHARED ${ARK_WEB_BRIDGE_SOURCES})
target_link_libraries(web_bridge PRIVATE ark_headers ArkImGui user32)
ark_output_directory("${ARK_DIST_DIRECTORY}/plugins" web_bridge)

find_program(DOTNET_EXECUTABLE dotnet)
if(DOTNET_EXECUTABLE)
  set(ARK_BETTER_BATTLE_WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/webview")
  set(ARK_BETTER_BATTLE_WEB_OUT "${ARK_DIST_DIRECTORY}/BetterBattleWeb")
  add_custom_target(BetterBattleWeb ALL
    COMMAND ${DOTNET_EXECUTABLE} publish "${ARK_BETTER_BATTLE_WEB_DIR}/BetterBattleWeb.csproj"
            -c Release -r win-x64 --self-contained false -o "${ARK_BETTER_BATTLE_WEB_OUT}"
    BYPRODUCTS "${ARK_BETTER_BATTLE_WEB_OUT}/BetterBattleWeb.exe"
    WORKING_DIRECTORY "${ARK_BETTER_BATTLE_WEB_DIR}"
    COMMENT "Publishing BetterBattle WebView2 host"
    VERBATIM)
else()
  message(WARNING "dotnet was not found; skipping the BetterBattleWeb WebView2 host")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  add_library(ark_test_headers INTERFACE)
  target_include_directories(ark_test_headers INTERFACE src/core tests/fixtures)
  target_link_libraries(ark_test_headers INTERFACE ark_headers)

  add_executable(damage_meter_tests tests/damage_meter_tests.cpp)
  target_link_libraries(damage_meter_tests PRIVATE ark_headers)

  add_executable(lifecycle_tests tests/lifecycle_tests.cpp
    $<TARGET_OBJECTS:ark_plugin_runtime> $<TARGET_OBJECTS:ark_native_hooks>)
  target_link_libraries(lifecycle_tests PRIVATE ark_test_headers ark_minhook)

  add_executable(native_hook_tests tests/native_hook_tests.cpp $<TARGET_OBJECTS:ark_native_hooks>)
  target_link_libraries(native_hook_tests PRIVATE ark_test_headers ark_minhook)

  add_library(fixture SHARED tests/fixtures/fixture_plugin.cpp)
  add_library(bad_abi SHARED tests/fixtures/fixture_plugin.cpp)
  target_compile_definitions(bad_abi PRIVATE TEST_BAD_ABI)
  set(ark_fixtures fixture bad_abi)

  # One real DLL per FIXTURE_KIND (1..12), in this order.
  set(service_fixtures provider consumer intermediate indirect undeclared cycle_a cycle_b
    missing_dependency unsafe_dependency failed_init lifetime_provider failed_init_refuse)
  set(fixture_kind 0)
  foreach(fixture_name IN LISTS service_fixtures)
    math(EXPR fixture_kind "${fixture_kind} + 1")
    add_library(${fixture_name} SHARED tests/fixtures/lifecycle_service_fixture.cpp)
    target_compile_definitions(${fixture_name} PRIVATE FIXTURE_KIND=${fixture_kind})
    list(APPEND ark_fixtures ${fixture_name})
  endforeach()
  add_library(consumer_updated SHARED tests/fixtures/lifecycle_service_fixture.cpp)
  target_compile_definitions(consumer_updated PRIVATE FIXTURE_KIND=2 FIXTURE_ADDED_DEPENDENCY)
  add_library(native_hook_fixture SHARED tests/fixtures/native_hook_fixture.cpp)
  list(APPEND ark_fixtures consumer_updated native_hook_fixture)

  foreach(fixture_target IN LISTS ark_fixtures)
    target_link_libraries(${fixture_target} PRIVATE ark_test_headers)
  endforeach()
  ark_output_directory("${ARK_TEST_DIRECTORY}" damage_meter_tests lifecycle_tests native_hook_tests
    ${ark_fixtures})
  ark_wide_entry_point(damage_meter_tests lifecycle_tests native_hook_tests)

  add_test(NAME damage_meter_model COMMAND damage_meter_tests)
  add_test(NAME plugin_lifecycle COMMAND lifecycle_tests "${ARK_TEST_DIRECTORY}")
  add_test(NAME native_hook_lifetime COMMAND native_hook_tests "${ARK_TEST_DIRECTORY}")
  add_test(NAME graphical_plugin_lifecycle COMMAND ArkFrameworkHost --self-test)
  set_tests_properties(graphical_plugin_lifecycle PROPERTIES TIMEOUT 75 RUN_SERIAL TRUE)
endif()
