set(CMAKE_CXX_STANDARD 20)

project(DXPG)

set(SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Source")
set(SHADER_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders")
set(INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Include;${EXTERNAL_DIR}/stb;${CMAKE_CURRENT_SOURCE_DIR}/Source;${SHADER_DIRECTORY}")

file(GLOB_RECURSE SHADER_FILES "${SHADER_DIRECTORY}/*")
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "${SOURCE_DIRECTORY}/*")
file(GLOB_RECURSE INCLUDE_FILES CONFIGURE_DEPENDS "${INCLUDE_DIRECTORY}/*")

add_executable(${PROJECT_NAME} WIN32 ${INCLUDE_FILES} ${SRC_FILES} ${SHADER_FILES})

foreach(SHADER ${SHADER_FILES})
	set_source_files_properties(${SHADER} PROPERTIES VS_TOOL_OVERRIDE "Text")
endforeach()

foreach(source IN LISTS SRC_FILES)
	get_filename_component(source_path "${source}" PATH)
	string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" source_path_compact "${source_path}")
	string(REPLACE "/" "\\" source_path_msvc "${source_path_compact}")
	source_group("${source_path_msvc}" FILES "${source}")
endforeach()

foreach(header IN LISTS INCLUDE_FILES)
	get_filename_component(header_path "${header}" PATH)
	string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" header_path_compact "${header_path}")
	string(REPLACE "/" "\\" header_path_msvc "${header_path_compact}")
	source_group("${header_path_msvc}" FILES "${header}")
endforeach()

foreach(shader IN LISTS SHADER_FILES)
	get_filename_component(shader_path "${shader}" PATH)
	string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "" shader_path_compact "${shader_path}")
	string(REPLACE "/" "\\" shader_path_msvc "${shader_path_compact}")
	source_group("${shader_path_msvc}" FILES "${shader}")
endforeach()

target_compile_definitions(${PROJECT_NAME} PRIVATE DXPG_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Assets/")
target_compile_definitions(${PROJECT_NAME} PRIVATE DXPG_SHADERS_DIR="${SHADER_DIRECTORY}/")
target_compile_definitions(${PROJECT_NAME} PRIVATE DXPG_SPONZA_DIR="${EXTERNAL_DIR}/Sponza/")

target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${INCLUDE_DIRECTORY}>)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    d3d12 dxgi dxcompiler SDL2-static SDL2::SDL2main imgui DirectX-Headers Shlwapi.lib tinyobjloader FidelityFX-SPD
)


function(copy_redist_files target_name redist_src redist_dst)
	add_custom_command(TARGET ${target_name} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${redist_src}"
		$<TARGET_FILE_DIR:${target_name}>/${redist_dst}
	)
endfunction()

copy_redist_files(${PROJECT_NAME} "${EXTERNAL_DIR}/DirectXShaderCompiler/bin/x64/dxcompiler.dll" "dxcompiler.dll")
copy_redist_files(${PROJECT_NAME} "${EXTERNAL_DIR}/DirectXShaderCompiler/bin/x64/dxil.dll" "dxil.dll")


set(AGILITY_SDK_NUGET_ID "Microsoft.Direct3D.D3D12")
set(AGILITY_SDK_NUGET_VERSION "1.711.3-preview")

if(NOT AGILITY_SDK_NUGET_VERSION MATCHES "[0-9]+\\.([0-9]+)\\.[0-9]+.*")
message(FATAL_ERROR "Could not parse D3D12 package version")
endif()

set(agility_sdk_version ${CMAKE_MATCH_1})

set(content ${PROJECT_NAME}_content)
string(TOLOWER content content)

include(FetchContent)
FetchContent_Declare(
	${content}
	URL "https://www.nuget.org/api/v2/package/${AGILITY_SDK_NUGET_ID}/${AGILITY_SDK_NUGET_VERSION}"
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(${content})

set(bin_path "${${content}_SOURCE_DIR}/build/native/bin/x64")

# The agility SDK does not redistribute import libraries since the inbox version of
# d3d12.dll provides all necessary exports.
target_link_libraries(${PROJECT_NAME} PRIVATE
	Microsoft::DirectX-Guids 
)

target_compile_definitions(${PROJECT_NAME} PRIVATE DIRECT3D_AGILITY_SDK_VERSION=${agility_sdk_version})

copy_redist_files(${PROJECT_NAME} "${bin_path}/D3D12Core.dll" "D3D12/D3D12Core.dll")
copy_redist_files(${PROJECT_NAME} "${bin_path}/d3d12SDKLayers.dll" "D3D12/d3d12SDKLayers.dll")
copy_redist_files(${PROJECT_NAME} "${bin_path}/d3dconfig.exe" "D3D12/d3dconfig.exe")

set_property(TARGET ${PROJECT_NAME} PROPERTY DX_COMPONENT_CONFIG "NuGet (${pkg_id}.${pkg_version})")

