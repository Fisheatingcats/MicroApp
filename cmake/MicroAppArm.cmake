include_guard(GLOBAL)
set(MICROAPP_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
function(microapp_add_arm_app name)
    get_filename_component(MICROAPP_SDK_ROOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    cmake_parse_arguments(APP "" "PROFILE;MANIFEST;OUTPUT_DIRECTORY" "SOURCES;INCLUDE_DIRECTORIES;HOST_HEADERS;COMPILE_DEFINITIONS;RESOURCES" ${ARGN})
    if(NOT APP_PROFILE OR NOT APP_MANIFEST OR NOT APP_SOURCES)
        message(FATAL_ERROR "microapp_add_arm_app requires PROFILE, MANIFEST and SOURCES")
    endif()
    foreach(source IN LISTS APP_SOURCES)
        get_filename_component(extension "${source}" LAST_EXT)
        if(NOT extension STREQUAL ".c")
            message(FATAL_ERROR "MicroApp V0.0.2 supports freestanding .c sources only: ${source}")
        endif()
    endforeach()
    if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi")
        message(FATAL_ERROR "MicroApp ARM Apps require arm-none-eabi-gcc")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import elftools" RESULT_VARIABLE dep_rc)
    if(NOT dep_rc EQUAL 0)
        message(FATAL_ERROR "Install ${MICROAPP_SDK_ROOT}/tools/requirements.txt into ${Python3_EXECUTABLE}")
    endif()
    file(READ "${APP_PROFILE}" profile)
    string(JSON float_abi GET "${profile}" float_abi)
    if(NOT float_abi MATCHES "^(soft|softfp|hard)$")
        message(FATAL_ERROR "Unsupported float ABI: ${float_abi}")
    endif()
    if(NOT APP_OUTPUT_DIRECTORY)
        set(APP_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    file(MAKE_DIRECTORY "${APP_OUTPUT_DIRECTORY}")
    add_executable(${name}.elf ${APP_SOURCES} ${APP_HOST_HEADERS})
    set_target_properties(${name}.elf PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${APP_OUTPUT_DIRECTORY}"
        LINK_DEPENDS "${MICROAPP_SDK_ROOT}/cmake/microapp_pic.ld")
    target_link_libraries(${name}.elf PRIVATE MicroApp::sdk)
    target_include_directories(${name}.elf PRIVATE ${APP_INCLUDE_DIRECTORIES})
    target_compile_definitions(${name}.elf PRIVATE ${APP_COMPILE_DEFINITIONS})
    target_compile_options(${name}.elf PRIVATE -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16
        -mfloat-abi=${float_abi} -Os -ffreestanding -fno-builtin -fno-common
        -fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative
        -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
        -ffunction-sections -fdata-sections -Wall -Wextra -Werror)
    target_link_options(${name}.elf PRIVATE -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16
        -mfloat-abi=${float_abi} -nostdlib -nostartfiles
        -T${MICROAPP_SDK_ROOT}/cmake/microapp_pic.ld
        -Wl,-shared,-Bsymbolic,--no-undefined,--emit-relocs,--gc-sections,--build-id=none
        -Wl,-Map=${APP_OUTPUT_DIRECTORY}/${name}.map)
    set(resource_args)
    foreach(resource IN LISTS APP_RESOURCES)
        list(APPEND resource_args --resource "${resource}")
    endforeach()
    set(package "${APP_OUTPUT_DIRECTORY}/${name}.mapp")
    file(GLOB packer_deps "${MICROAPP_SDK_ROOT}/tools/microapp/*.py")
    set(resource_deps)
    foreach(resource IN LISTS APP_RESOURCES)
        string(REGEX REPLACE "^[^=]*=" "" path "${resource}")
        list(APPEND resource_deps "${path}")
    endforeach()
    add_custom_command(OUTPUT "${package}" "${package}.json"
        COMMAND ${Python3_EXECUTABLE} ${MICROAPP_SDK_ROOT}/tools/microapp/pack_microapp.py
            --elf $<TARGET_FILE:${name}.elf> --profile "${APP_PROFILE}" --manifest "${APP_MANIFEST}"
            --out "${package}" ${resource_args}
        COMMAND ${Python3_EXECUTABLE} ${MICROAPP_SDK_ROOT}/tools/microapp/verify_microapp.py
            "${package}" --profile "${APP_PROFILE}"
        DEPENDS ${name}.elf ${packer_deps} "${APP_PROFILE}" "${APP_MANIFEST}" ${resource_deps}
        VERBATIM)
    add_custom_target(${name}_package ALL DEPENDS "${package}" "${package}.json")
endfunction()
