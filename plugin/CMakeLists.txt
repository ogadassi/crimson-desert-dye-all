cmake_minimum_required(VERSION 3.20)
project(DyeAll LANGUAGES CXX RC)

# Dye All: an ASI plugin for Crimson Desert. Lists every dyeable item the player owns
# beside the camp dyer's screen. Overlay, input and memory code come from Master Looter
# (MIT), which took its overlay from Trinity (MIT); see THIRD_PARTY_NOTICES.md.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5-docking)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS=0)

FetchContent_Declare(minhook GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git GIT_TAG master)
FetchContent_MakeAvailable(minhook)

add_library(DyeAll SHARED
    src/dllmain.cpp
    src/core/mod.cpp
    src/core/paths.cpp
    src/core/log.cpp
    src/hooks/dx12_hook.cpp
    src/hooks/input.cpp
    src/hooks/xinput_hook.cpp
    src/gui/panel.cpp
    src/game/mem.cpp
    src/game/farhook.cpp
    src/game/game.cpp
    src/dye/dyedb.cpp
    src/dye/hooks.cpp
    src/dye/stock.cpp
    src/dye/actions.cpp
    src/resources.rc
)
set_source_files_properties(src/resources.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/data/dyeables.tsv")
target_include_directories(DyeAll PRIVATE src ${minhook_SOURCE_DIR}/src/hde)
target_link_libraries(DyeAll PRIVATE imgui minhook d3d12 dxgi dwmapi imm32 xinput9_1_0)
target_compile_definitions(DyeAll PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(DyeAll PRIVATE /utf-8 /W3)
set_target_properties(DyeAll PROPERTIES PREFIX "" SUFFIX ".asi" OUTPUT_NAME "DyeAll")

add_custom_command(TARGET DyeAll POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/../dist"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:DyeAll>" "${CMAKE_SOURCE_DIR}/../dist/"
    COMMENT "Staging dist/"
    VERBATIM
)
