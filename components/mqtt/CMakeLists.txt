idf_component_register(
SRCS
    "mqtt_client_wrapper.cpp"

INCLUDE_DIRS
    "include"

REQUIRES
    "freertos"
    "mqtt"
    "json"
    "esp_event"
)

target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/tasks")
