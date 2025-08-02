file(GLOB WEB_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.html" "${CMAKE_CURRENT_SOURCE_DIR}/*.css" "${CMAKE_CURRENT_SOURCE_DIR}/*.js")

foreach(file ${WEB_FILES})
    get_filename_component(fname ${file} NAME_WE)
    set(header_path ${CMAKE_CURRENT_BINARY_DIR}/${fname}.h)

    add_custom_command(
        OUTPUT ${header_path}
        COMMAND ${CMAKE_COMMAND} -E echo "Converting ${file} to ${header_path}"
        COMMAND xxd -i ${file} > ${header_path}
        DEPENDS ${file}
        VERBATIM
    )

    list(APPEND GENERATED_HEADERS ${header_path})
endforeach()

# Khai báo các header là file sinh ra
set_source_files_properties(${GENERATED_HEADERS} PROPERTIES GENERATED TRUE)

# Gắn mấy file này vào thư viện component
idf_component_register(SRCS "dummy.c"  # dummy nếu không có file .c
                       INCLUDE_DIRS "."
                       )

# Gắn các file header vào target compile (bắt buộc để được build)
target_sources(${COMPONENT_LIB} PRIVATE ${GENERATED_HEADERS})