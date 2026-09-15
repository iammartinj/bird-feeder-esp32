file(GLOB APP_SRCS "*.c" "*.cpp" "scene/*.cpp" "engine/*.cpp")

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS "." "scene" "engine")
