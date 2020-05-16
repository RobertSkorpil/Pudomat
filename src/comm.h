#pragma pack(push, 1)

#define MAX_TEMP_COUNT 14

enum command {
    CMD_DBG_READ = 1,
    CMD_VOLT = 2,
    CMD_TEMP = 3,
    CMD_CFG_READ = 4,
    CMD_CFG_WRITE = 5,
};

enum door_action {
    DA_NO_ACTION,
    DA_OPEN,
    DA_CLOSE,
    DA_FORCE_OPEN,
    DA_FORCE_CLOSE,
};

#define CONFIG_SIGNATURE 0xCC
struct config {
    uint64_t door_temp_id_A;
    uint64_t door_temp_id_B;
    uint8_t solar_relay_decivolt_lo;
    uint8_t solar_relay_decivolt_hi;
    uint8_t door_temp_diff_close;
    uint8_t door_temp_diff_open;
    uint8_t signature;
};

struct volt_response {
    uint16_t voltage;
    uint16_t current;
    uint8_t relay;
    uint8_t padding;
};

struct temp_data {
    uint64_t id;
    uint16_t temperature;
    uint8_t  age;
    uint8_t  valid;
    uint32_t padding;
};

struct temp_response {
    struct temp_data data[MAX_TEMP_COUNT];
};

struct debug_data {
    uint32_t usb_polls;
    uint32_t usb_reqs;
    uint32_t usb_req_errors;
    uint32_t temp_scans;
    uint32_t temp_scan_errors;
    uint32_t temp_scan_warns;
    uint32_t temp_read_errors;
    uint32_t temp_reads;
    int8_t door_countdown;
    int8_t door_action;
};
    
#pragma pack(pop)
