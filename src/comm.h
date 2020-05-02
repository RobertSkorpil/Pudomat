#define MAX_TEMP_COUNT 14

enum command {
    CMD_VOLT = 2,
    CMD_TEMP = 3,
    CMD_CFG_READ = 4,
    CMD_CFG_WRITE = 5,
};

struct config {
    union {
        uint8_t solar_relay_decivolt_low;
        uint8_t configured;
    };
    uint8_t solar_relay_decivolt_hi;
    uint8_t door_relay_temp_diff_low;
    uint8_t door_relay_temp_diff_high;
    uint64_t door_relay_temp_id_A;
    uint64_t door_relay_temp_id_B;
};

struct volt_response {
    uint16_t voltage;
    uint16_t current;
    uint8_t relay;
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
	
