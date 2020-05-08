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

void prepare_request(struct libusb_transfer *transfer, char *buffer,
                      size_t buf_size, libusb_device_handle *dev_handle,
                      int cmd) {
}

void prepare_response(struct libusb_transfer *transfer, char *buffer,
                               size_t buf_size,
                               libusb_device_handle *dev_handle) {
    libusb_fill_control_transfer(transfer, dev_handle, buffer, transfer_cb,
                                 NULL, 50);
    struct libusb_control_setup *ctrl =
        libusb_control_transfer_get_setup(transfer);
    ctrl->bmRequestType = LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR |
                          LIBUSB_ENDPOINT_IN;
    ctrl->wLength = 256;
    transfer->length = buf_size;
}

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
const char *argp_program_bug_address = "<robert@skorpil.net>";

static struct argp argp = 
{ .doc = "Pudomat - ovladaci program" };

int main(int argc, char *argv[]) {
//    argp_parse(&argp, argc, argv, 0, 0, 0);

    time_t current_time;
    time(&current_time);

    struct tm *tm = localtime(&current_time);
    if (!tm)
        abort();

    char ts[256];
    sprintf(ts, "%d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1,
            tm->tm_mday, tm->tm_hour, tm->tm_min);

    libusb_context *ctx;
    libusb_init(&ctx);

    libusb_device_handle *dev_handle =
        libusb_open_device_with_vid_pid(ctx, 0x16c0, 0x0939);

    if (!dev_handle) {
        fprintf(stderr,
                "Pudomat nenalezen (mozna nebezis jako root?)\n");
        return 1;
    }

    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    if (!transfer)
        goto err;

    uint16_t transfer_buffer[256] = { 0 };
    int cmd = CMD_TEMP;

    int test = 0;

    if (argc > 1)
    { 
        if(strcmp(argv[1], "volt") == 0)
            cmd = CMD_VOLT;
        else if(strcmp(argv[1], "cr") == 0)
            cmd = CMD_CFG_READ;
        else if(strcmp(argv[1], "wdc") == 0)
            cmd = CMD_CFG_WRITE;
        else if(strcmp(argv[1], "test") == 0)
            test = 1;
        else if(strcmp(argv[1], "test2") == 0)
            test = 2;
    }

    void *response_data = NULL;
    transfer_fail = 0;
    int retry;
    for (retry = 0; retry < 5; retry++) {
        libusb_fill_control_setup((void *)transfer_buffer, 
                                   LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
                                   cmd,
                                   0,
                                   0,
                                   (cmd == CMD_CFG_WRITE) ? sizeof(struct config) : 0);
        if(cmd == CMD_CFG_WRITE)
        {
            struct config *config = (void *)(transfer_buffer + 4);
            config->solar_relay_decivolt_lo = 130;
            config->solar_relay_decivolt_hi = 145;
            config->signature = CONFIG_SIGNATURE;
        }
        libusb_fill_control_transfer(transfer, dev_handle, (void *)transfer_buffer, transfer_cb,
                                     NULL, 500);
        
        libusb_submit_transfer(transfer);
        libusb_handle_events(ctx);

        usleep(20000 * retry);
        if (!transfer_fail) {
            if(cmd == CMD_CFG_WRITE)
                break;
            prepare_response(transfer, (void *)transfer_buffer,
                                      sizeof(transfer_buffer), dev_handle);
            libusb_submit_transfer(transfer);
            libusb_handle_events(ctx);
            if (!transfer_fail) {
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
        goto err;
    }

    if (!response_data) {
        fprintf(stderr, "data == NULL\n");
        goto err;
    }

    char line[1024] = {0};
    char t[1024];

    if(test == 1)
    {
        printf("Retry count: %d\n", retry);
        return 0;
    }

    switch (cmd) {
    case CMD_VOLT:
    {
        if(transfer->actual_length != sizeof(struct volt_response))
            goto bad_length; 

        struct volt_response *r = response_data;
        strcat(line, ts);
        sprintf(t, " %.2lf %d", convert_voltage(r->voltage),
                r->relay ? 14 : 12);
        strcat(line, t);
        printf("%s\n", line);
        // printf("U = %.2lfV, I = %.1lfmA\n", convert_voltage(r->voltage),
        // convert_current(r->current));
        break;
    }
    case CMD_TEMP:
    {
        if(transfer->actual_length != sizeof(struct temp_response))
            goto bad_length; 

        struct temp_response *r = response_data;
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

            if(!test)
                sprintf(t, " %d", (int)convert_temperature(r->data[i].temperature));
            else if(test == 2)
                sprintf(t, " %0.2lf°C[age=%d,id=x'%016lX']", convert_temperature(r->data[i].temperature), r->data[i].age, r->data[i].id);

            strcat(line, t);
        }
        printf("%s\n", line);

        break;
    }
    case CMD_CFG_READ:
    {
        if(transfer->actual_length != sizeof(struct config))
            goto bad_length; 

        struct config *r = response_data;
        int configured = r->signature == CONFIG_SIGNATURE;
        printf("Konfigurovano:                  %s\n", configured ? "ANO" : "NE");
        printf("Napeti vypnuti rele solaru:     %.1lfV\n", (double)r->solar_relay_decivolt_lo / 10.0 );
        printf("Napeti zapnuti rele solaru:     %.1lfV\n", (double)r->solar_relay_decivolt_hi / 10.0 );
        printf("Teplotni rozdil zavreni dveri:  %d°C\n", r->door_temp_diff_close);
        printf("Teplotni rozdil otevreni dveri: %d°C\n", r->door_temp_diff_open);
        printf("ID teplomeru u schodu:          x'%016lX'\n", r->door_temp_id_A);
        printf("ID teplomeru v pracovne:        x'%016lX'\n", r->door_temp_id_B);
    }
    break;
    }

    return 0;

bad_length:
    fprintf(stderr, "Chybna delka odpovedi\n");
err:
    return 1;
}
