#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <argp.h>
#include "comm.h"

static int transfer_fail = 0;

const char *translate_error(int status) {
    const char *message = "Neznamy status";

    switch (status) {
    case LIBUSB_TRANSFER_COMPLETED:
        message = NULL;
        break;
    case LIBUSB_TRANSFER_ERROR:
        message = "Chyba";
        break;
    case LIBUSB_TRANSFER_TIMED_OUT:
        message = "Vyprsel cas";
        break;
    case LIBUSB_TRANSFER_CANCELLED:
        message = "Zruseno";
        break;
    case LIBUSB_TRANSFER_STALL:
        message = "Chyba prenosu dat";
        break;
    case LIBUSB_TRANSFER_NO_DEVICE:
        message = "Odpojeno";
        break;
    case LIBUSB_TRANSFER_OVERFLOW:
        message = "Preteceni datoveho bufferu";
        break;
    }

    return message;
}
void transfer_cb(struct libusb_transfer *transfer) {
    int *completed = transfer->user_data;
    if(completed)
        *completed = 1;

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
        transfer_fail = 1;
    else
        transfer_fail = 0;
}

double convert_temperature(uint16_t t) {
    return (double)((((int16_t)t) << 4) >> 4) / 16;
}

double convert_voltage(int t) { return (double)(t >> 3) * 0.004; }

double convert_current(int t) { return (double)(t >> 3) * 0.004 * 100; }

int comp_temp(const void *a, const void *b) {
    const struct temp_data *t1 = a, *t2 = b;
    if (!t1->valid) {
        if (!t2->valid)
            return 0;
        else
            return 1;
    } else {
        if (!t2->valid)
            return -1;
        else {
            if (t1->id > t2->id)
                return 1;
            else
                return -1;
        }
    }
}

const char *argp_program_version = "Pudomat (kompilace " __TIMESTAMP__ ")";
const char *argp_program_bug_address = "Robert Skorpil <robert@skorpil.net>";

static struct argp_option options[] = {
    { "verbose", 'v', 0, 0, "Detailni vystup" },
    { "temperature", 't', 0, 0, "Vypsani teplot (vychozi)" },
    { "voltage", 'u', 0, 0, "Vypsani voltmetru" },
    { "config-read", 'r', 0, 0, "Vypsani konfigurace" },
    { "config-write", 'w', "klic=hodnota[,klic=hodnota,...]", 0, "Zmen konfiguracni parametr <klic> na <hodnota>. Seznam klicu je dostupny ve vystupu config-read." },
    { "debug", 'd', 0, 0, "Vypsani ladicich dat" },
    { 0 }
};

struct arguments
{
    enum command command;
    uint8_t verbose;
    char *config_write_arg;
};

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
    struct arguments *arguments = state->input;
    switch(key)
    {
    case 'v':
        arguments->verbose = 1;
        break;
    case 't':
        arguments->command = CMD_TEMP;
        break;
    case 'u':
        arguments->command = CMD_VOLT;
        break;
    case 'r':
        arguments->command = CMD_CFG_READ;
        break;
    case 'w':
        arguments->command = CMD_CFG_WRITE;
        arguments->config_write_arg = arg;
        break;
    case 'd':
        arguments->command = CMD_DBG_READ;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = 
{ .options = options, .parser = parse_opt, .doc = "Pudomat - ovladaci program\n\n Priklad nastaveni napeti vypnuti rele na 12.0V a sepnuti na 13.5V:\n pudomat -w svlo=120,svhi=135 " };

static inline uint16_t get_response_size(enum command command)
{
    switch(command)
    {
    case CMD_DBG_READ:
        return sizeof(struct debug_data);
    case CMD_VOLT:
        return sizeof(struct volt_response);
    case CMD_TEMP:
        return sizeof(struct temp_response);
    case CMD_CFG_READ:
    case CMD_CFG_WRITE:
        return sizeof(struct config);
    default:
        abort();
    }        
}

static error_t
parse_uint8_t(const char *arg, uint8_t *v)
{
    uint64_t t = 0;

    if(!*arg)
        return 1;

    for(; *arg; arg++)
    {
        if(*arg < '0' || *arg > '9')
            return 1;
        t = t * 10 + (*arg - '0');
    }

    if(t > 255)
        return 1;

    *v = t;
    return 0;
}

static error_t
parse_uint64_t(const char *arg, uint64_t *v)
{
    uint64_t t = 0;

    const char *a;
    for(a = arg; *a; a++)
    {
        if(*a >= '0' && *a <= '9')
            t = t * 16 + (*a - '0');
        else if(*a >= 'a' && *a <= 'f')
            t = t * 16 + (*a - 'a' + 10) ;
        else if(*a >= 'A' && *a <= 'F') 
            t = t * 16 + (*a - 'A' + 10);
        else
            return 1;
    }
    if(a != arg + 16)
        return 1;

    *v = t;
    return 0;
}

static error_t
update_config(const char *arg, struct config *config)
{
    enum { ST_KEY, ST_VALUE } state = ST_KEY;

    char key_buf[32] = { 0 };
    char *key = key_buf;
    char *key_end = key_buf + sizeof(key_buf);

    char val_buf[32] = { 0 };
    char *val = val_buf;
    char *val_end = val_buf + sizeof(val_buf);

    for(;; ++arg)
    {
        //fprintf(stderr, "%s:%s:%c:%d\n", key_buf, val_buf, *arg, state);
        switch(state)
        {
        case ST_KEY:
            if(!*arg)
                goto break_loop;
            if(key == key_end - 1)
                return 1;
            if(*arg == '=')
            {
                val = val_buf;
                *val = 0;
                state = ST_VALUE;
            }
            else
            {
                *key++ = *arg;
                *key = 0;
            }
            break;
        case ST_VALUE:
            if(!*arg || *arg == ',')
            {
                if(strcmp(key_buf, "svlo") == 0)
                {
                    uint8_t v;
                    if(parse_uint8_t(val_buf, &v) != 0)
                        return 1;
                    if(config)
                        config->solar_relay_decivolt_lo = v;
                }
                else if(strcmp(key_buf, "svhi") == 0)
                {
                    uint8_t v;
                    if(parse_uint8_t(val_buf, &v) != 0)
                        return 1;
                    if(config)
                        config->solar_relay_decivolt_hi = v;
                }
                else if(strcmp(key_buf, "dtdc") == 0)
                {
                    uint8_t v;
                    if(parse_uint8_t(val_buf, &v) != 0)
                        return 1;
                    if(config)
                        config->door_temp_diff_close = v;
                }
                else if(strcmp(key_buf, "dtdo") == 0)
                {
                    uint8_t v;
                    if(parse_uint8_t(val_buf, &v) != 0)
                        return 1;
                    if(config)
                        config->door_temp_diff_open = v;
                }
                else if(strcmp(key_buf, "dtia") == 0)
                {
                    uint64_t v;
                    if(parse_uint64_t(val_buf, &v) != 0)
                        return 1;
                    if(config)
                        config->door_temp_id_A = v;
                }
                else if(strcmp(key_buf, "dtib") == 0)
                {
                    uint64_t v;
                    if(parse_uint64_t(val_buf, &v) != 0)
                        return 1;
                    if(config)
                        config->door_temp_id_B = v;
                }
                else
                    return 1;                

                if(!*arg)
                    goto break_loop;

                key = key_buf;
                *key = 0;
                state = ST_KEY;
            }
            if(val == val_end - 1)
                return 1;
            *val++ = *arg;
            *val = 0;

            break;
        }
    }

break_loop:    
    if(state == ST_KEY && *key)
        return 1;

    return 0;
}    

static error_t
process_usb_command(enum command command, void *data)
{
    error_t rc = 0;
    uint16_t transfer_buffer[256] = { 0 };
    libusb_context *ctx;
    libusb_init(&ctx);

    libusb_device_handle *dev_handle =
        libusb_open_device_with_vid_pid(ctx, 0x16c0, 0x0939);

    if (!dev_handle) {
        fprintf(stderr,
                "Pudomat nenalezen (mozna nebezis jako root?)\n");
        rc = 1;
        goto err;
    }

    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    if (!transfer)
    {
        rc = 1;
        goto err;
    }

    transfer_fail = 0;
    int retry;
    void *response_data = NULL;
    for (retry = 0; retry < 5; retry++) {
        libusb_fill_control_setup((void *)transfer_buffer, 
                                   LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR | (command == CMD_CFG_WRITE ? LIBUSB_ENDPOINT_OUT : LIBUSB_ENDPOINT_IN),
                                   command,
                                   0,
                                   0,
                                   get_response_size(command));
        if(command == CMD_CFG_WRITE)
        {
            struct config *config = (void *)(transfer_buffer + 4);
            memcpy(config, data, sizeof(struct config));
            config->signature = CONFIG_SIGNATURE;
        }
        libusb_fill_control_transfer(transfer, dev_handle, (void *)transfer_buffer, transfer_cb,
                                     NULL, 500);
        
        libusb_submit_transfer(transfer);
        int completed = 0;
        libusb_handle_events_completed(ctx, &completed);

        if (!transfer_fail) {
            if(transfer->actual_length == get_response_size(command))
            {
                response_data = libusb_control_transfer_get_data(transfer);
                break;
            }
        }

        if (retry == 3) {
            libusb_reset_device(dev_handle);
            usleep(100000);
        }
        else
            usleep(80000 * (retry + 1));
    }
    if (transfer_fail) {
        const char *msg = translate_error(transfer->status);
        fprintf(stderr, "%s\n", msg);
        rc = 1;
        goto err;
    }

    if (!response_data) {
        fprintf(stderr, "data == NULL\n");
        rc = 1;
        goto err;
    }

    if(command != CMD_CFG_WRITE)
        memcpy(data, response_data, get_response_size(command));
err:
    if(transfer)
        libusb_free_transfer(transfer);

    if(dev_handle)
        libusb_close(dev_handle);

    libusb_exit(ctx);
    return rc;
}

int main(int argc, char *argv[]) {
    struct arguments arguments = { 0 };
    arguments.command = CMD_TEMP;

    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    time_t current_time;
    time(&current_time);

    struct tm *tm = localtime(&current_time);
    if (!tm)
        abort();

    char ts[256];
    sprintf(ts, "%d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1,
            tm->tm_mday, tm->tm_hour, tm->tm_min);

    char line[1024] = {0};
    char t[1024];
    uint64_t response_data[64];

    if(arguments.command == CMD_CFG_WRITE)
    {
        if(update_config(arguments.config_write_arg, 0) != 0)
        {
            fprintf(stderr, "Neplatny argument\n");
            goto err;
        }

        if(process_usb_command(CMD_CFG_READ, response_data) != 0)
            goto err;

        if(update_config(arguments.config_write_arg, (struct config *)response_data) != 0)
            abort();
    }

    if(process_usb_command(arguments.command, response_data) != 0)
        goto err;

    switch (arguments.command) {
    case CMD_VOLT:
    {
        struct volt_response *r = (void *)response_data;
        strcat(line, ts);
        sprintf(t, " %.2lf %d", convert_voltage(r->voltage),
                r->relay ? 14 : 12);
        strcat(line, t);
        printf("%s\n", line);
    }
    break;
    case CMD_TEMP:
    {
        struct temp_response *r = (void *)response_data;
        qsort(r->data, sizeof(r->data) / sizeof(r->data[0]),
              sizeof(r->data[0]), comp_temp);

        strcat(line, ts);

        uint64_t last_id = -1;
        for (int i = 0; i < sizeof(r->data) / sizeof(r->data[0]); i++) {
            if (!r->data[i].valid)
                continue;
            if(r->data[i].id == last_id)
                continue;
            last_id = r->data[i].id;

            if(!arguments.verbose)
                sprintf(t, " %d", (int)convert_temperature(r->data[i].temperature));
            else
                sprintf(t, " %0.2lf°C[age=%d,id=x'%016lX']", convert_temperature(r->data[i].temperature), r->data[i].age, r->data[i].id);

            strcat(line, t);
        }
        printf("%s\n", line);
    }
    break;
    case CMD_CFG_READ:
    {
        struct config *r = (void *)response_data;
        int configured = r->signature == CONFIG_SIGNATURE;
        printf("Konfigurovano:                  %s\n", configured ? "ANO" : "NE");
        printf("Napeti vypnuti rele solaru:     %.1lfV (svlo=%d)\n", (double)r->solar_relay_decivolt_lo / 10.0, r->solar_relay_decivolt_lo);
        printf("Napeti zapnuti rele solaru:     %.1lfV (svhi=%d)\n", (double)r->solar_relay_decivolt_hi / 10.0, r->solar_relay_decivolt_hi);
        printf("Teplotni rozdil zavreni dveri:  %d°C (dtdc=%d)\n", r->door_temp_diff_close, r->door_temp_diff_close);
        printf("Teplotni rozdil otevreni dveri: %d°C (dtdo=%d)\n", r->door_temp_diff_open, r->door_temp_diff_open);
        printf("ID teplomeru u schodu:          x'%016lX' (dtia=%016lX)\n", r->door_temp_id_A, r->door_temp_id_A);
        printf("ID teplomeru v pracovne:        x'%016lX' (dtib=%016lX)\n", r->door_temp_id_B, r->door_temp_id_B);
    }
    break;
    case CMD_DBG_READ:
    {
        struct debug_data *r = (void *)response_data;
        printf("Pocet volani usbPoll():                         %d\n", r->usb_polls);
        printf("Pocet USB prikazu:                              %d\n", r->usb_reqs);
        printf("Pocet chybnych USB prikazu:                     %d\n", r->usb_req_errors);
        printf("Pocet hledani teplomeru:                        %d\n", r->temp_scans);
        printf("Pocet chyb pri hledani teplomeru:               %d\n", r->temp_scan_errors);
        printf("Pocet opakovanych pokusu pri hledani teplomeru: %d\n", r->temp_scan_warns);
        printf("Pocet cteni teploty:                            %d\n", r->temp_reads);
        printf("Pocet chyb pri cteni teploty:                   %d\n", r->temp_read_errors);
    }
    break;
    }

    return 0;

bad_length:
    fprintf(stderr, "Chybna delka odpovedi\n");
err:
    return 1;
}
