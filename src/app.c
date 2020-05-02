#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
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

void prepare_temp_cmd(struct libusb_transfer *transfer, char *buffer,
                      size_t buf_size, libusb_device_handle *dev_handle,
                      int cmd) {
    libusb_fill_control_transfer(transfer, dev_handle, buffer, transfer_cb,
                                 NULL, 50);
    struct libusb_control_setup *ctrl =
        libusb_control_transfer_get_setup(transfer);
    ctrl->bmRequestType = LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR |
                          LIBUSB_ENDPOINT_OUT;
    ctrl->bRequest = cmd;
    ctrl->wValue = 0;
    ctrl->wIndex = 0;
    ctrl->wLength = 0;
}

void prepare_temp_cmd_response(struct libusb_transfer *transfer, char *buffer,
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

int main(int argc, char *argv[]) {
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

    char transfer_buffer[4096];
    int cmd = CMD_TEMP;

    if (argc > 1)
    { 
        if(strcmp(argv[1], "volt") == 0)
            cmd = CMD_VOLT;
        else if(strcmp(argv[1], "cr") == 0)
            cmd = CMD_CFG_READ;
        else if(strcmp(argv[1], "wdc") == 0)
            cmd = CMD_CFG_WRITE;
    }

    void *response_data = NULL;
    transfer_fail = 0;
    for (int retry = 0; retry < 3; retry++) {
        prepare_temp_cmd(transfer, transfer_buffer, sizeof(transfer_buffer),
                         dev_handle, cmd);
        libusb_submit_transfer(transfer);
        libusb_handle_events(ctx);
        usleep(50000);

        if (!transfer_fail) {
            prepare_temp_cmd_response(transfer, transfer_buffer,
                                      sizeof(transfer_buffer), dev_handle);
            libusb_submit_transfer(transfer);
            libusb_handle_events(ctx);
            if (!transfer_fail) {
                response_data = libusb_control_transfer_get_data(transfer);
                break;
            }
        }

        usleep(100000);
        if (retry == 1) {
            libusb_reset_device(dev_handle);
            usleep(100000);
        }
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

    char line[256] = {0};
    char t[64];
    if (transfer->actual_length) {
        switch (cmd) {
        case CMD_VOLT:
        {
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
            struct temp_response *r = response_data;
            qsort(r->data, sizeof(r->data) / sizeof(r->data[0]),
                  sizeof(r->data[0]), comp_temp);

            strcat(line, ts);

            for (int i = 0; i < sizeof(r->data) / sizeof(r->data[0]); i++) {
                if (!r->data[i].valid)
                    break;

                sprintf(t, " %d", (int)convert_temperature(r->data[i].temperature));
                strcat(line, t);
            }
            printf("%s\n", line);

            break;
        }
        case CMD_CFG_READ:
        {
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
    } else {
        fprintf(stderr, "Chybna delka odpovedi\n");
        goto err;
    }

    return 0;

err:
    return 1;
}
