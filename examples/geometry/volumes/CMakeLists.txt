add_subdirectory(distance)

set(DGTAL_EXAMPLES_SRC
        dvcm-2d
)

foreach(FILE ${DGTAL_EXAMPLES_SRC})
  add_executable(${FILE} ${FILE})
  target_link_libraries (${FILE} DGtal  )
endforeach()

