#include <string.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <util/twi.h>
#include "ds18b20.h"
#include "usbdrv.h"
#include "romsearch.h"
#include "comm.h"

#define BIT_ON(r, b) (r |= (1 << b))
#define BIT_OFF(r, b) (r &= ~(1 << b))

#define barrier()  asm volatile ("" ::: "memory")

#define TIME_43ms   0x1
#define TIME_87ms   0x3
#define TIME_350ms  0xf
#define TIME_700ms  0x1f
#define TIME_1400ms 0x3f
#define TIME_2800ms 0x7f
#define TIME_5600ms 0xff

#define TIME_43ms_half   0x1
#define TIME_87ms_half   0x2
#define TIME_350ms_half  0x8
#define TIME_700ms_half  0x10
#define TIME_1400ms_half 0x20
#define TIME_2800ms_half 0x40
#define TIME_5600ms_half 0x80

#define VOLT

static uint16_t led_period;
static uint8_t led_alert;
static uint8_t led_on;

struct config config;
struct config config_eeprom EEMEM;
volatile static uint8_t config_updated;
static uint8_t *config_write_tgt;

static struct volt_response volt_response;

static struct temp_response temp_response;

static struct debug_data debug_data;

volatile static enum door_action door_action;
volatile static int16_t door_countdown;
#define DOOR_COUNTDOWN 700
#define DOOR_IGNORE_TIME 1
#define DOOR_DOUBLE_PUSH_TIME 10

static void read_config()
{
    eeprom_read_block(&config, &config_eeprom, sizeof(config));
    if(config.signature != CONFIG_SIGNATURE)
    {
        config.solar_relay_decivolt_lo = 126;
        config.solar_relay_decivolt_hi = 154;
    }
}

static void write_config()
{
    eeprom_write_block(&config, &config_eeprom, sizeof(config));
}

static void green_on()
{
    BIT_ON(PORTB, PORTB1);
    led_on = 1;
}

static void green_off()
{
    BIT_OFF(PORTB, PORTB1);
    led_on = 0;
}

static void relay_on()
{
    BIT_ON(PORTC, PORTC0);
    volt_response.relay = 1;
}

static void relay_off()
{
    BIT_OFF(PORTC, PORTC0);
    volt_response.relay = 0;
}

static void set_led_alert(uint16_t period, uint16_t count)
{
    led_period = period;
    led_alert = count;
}

static volatile struct 
{
    uint8_t address;
    uint8_t reg;
    union
    {
        uint16_t data;
        struct
        {
            uint8_t data0;
            uint8_t data1;
        };
    };
    uint16_t *p_data;
    enum __attribute__((__packed__))
    {
        TWI_OP_READ,
        TWI_OP_WRITE,
    } op;
    enum  __attribute__((__packed__))
    {
        TWI_READY,
        TWI_SETREG_SEND_ADDRESS,
        TWI_SETREG_SEND_REGPTR,
        TWI_SETREG_DONE,
        TWI_READ_SEND_ADDRESS,
        TWI_READ_SEND_ADDRESS_DONE,
        TWI_READ_RECV_BYTE0,
        TWI_READ_RECV_BYTE1,
        TWI_WRITE_SEND_BYTE0,
        TWI_WRITE_SEND_BYTE1,
    } status;
} twi;

static void twi_op(uint8_t address, uint8_t reg, uint16_t *val, uint8_t op)
{
    twi.address = address;
    twi.reg = reg;
    twi.op = op;
    twi.status = TWI_SETREG_SEND_ADDRESS;
    twi.p_data = val;
    twi.data = *val;

    barrier();

    uint8_t twcr = 0;
    BIT_ON(twcr, TWEN);  // enable TWI bus
    BIT_ON(twcr, TWINT); // reset TWI interrupt
    BIT_ON(twcr, TWIE);  // enable TWI interrupt
    BIT_ON(twcr, TWSTA); // start transmision;
    TWCR = twcr;
}

ISR(TWI_vect)
{
    uint8_t twcr = 0;
  
    BIT_ON(twcr, TWIE);  // enable TWI interrupt
    BIT_ON(twcr, TWINT); // reset TWI interrupt
    BIT_ON(twcr, TWEN);  // enable TWI bus
    switch(TW_STATUS)
    {
    case TW_START:
    case TW_REP_START:
        switch(twi.status)
        {
        case TWI_SETREG_SEND_ADDRESS:
            TWDR = twi.address;
            twi.status = TWI_SETREG_SEND_REGPTR;
            break;
        case TWI_READ_SEND_ADDRESS:
            TWDR = twi.address | 0x01;
            twi.status = TWI_READ_SEND_ADDRESS_DONE;
            break;
        }
    break;
    case TW_MT_SLA_ACK:
        switch(twi.status)
        {
        case TWI_SETREG_SEND_REGPTR:
            TWDR = twi.reg;
            twi.status = TWI_SETREG_DONE;
            break;
        default:
            goto unexpected;
        }
        break;
    case TW_MT_DATA_ACK:
    switch(twi.status)
    {
    case TWI_SETREG_DONE:
        if(twi.op == TWI_OP_READ)
        {
        BIT_ON(twcr, TWSTA);
        twi.status = TWI_READ_SEND_ADDRESS;
        }
        else //TWI_OP_WRITE
        {
        TWDR = twi.data1;
        twi.status = TWI_WRITE_SEND_BYTE0;
        }
        break;
    case TWI_WRITE_SEND_BYTE0:
        TWDR = twi.data0;
        twi.status = TWI_WRITE_SEND_BYTE1;
        break;
    case TWI_WRITE_SEND_BYTE1:
        //set_led_alert(TIME_87ms, 5);
        BIT_ON(twcr, TWSTO);
        break;
    default:
        goto unexpected;
    }
    break;
    case TW_MR_SLA_ACK:
        switch(twi.status)
        {
        case TWI_READ_SEND_ADDRESS_DONE:
            BIT_ON(twcr, TWEA);
            twi.status = TWI_READ_RECV_BYTE0;
            break;
        default:
            goto unexpected;
        }
    break;
    case TW_MR_DATA_ACK:
        switch(twi.status)
        {
        case TWI_READ_RECV_BYTE0:
            twi.data1 = TWDR;
            BIT_ON(twcr, TWEA);   //ACK (optional)
            twi.status = TWI_READ_RECV_BYTE1;
            break;
        case TWI_READ_RECV_BYTE1:
            twi.data0 = TWDR;
            BIT_ON(twcr, TWSTO);
            *twi.p_data = twi.data;
            twi.status = TWI_READY;
            break;
        default:
            goto unexpected;
        }
        break;
    default:
        goto unexpected;
    }

    TWCR = twcr;
    return;
unexpected:
    BIT_OFF(TWCR, TWEN); // disable TWI bus
}

static uint8_t temp_rom_count;
static uint8_t temp_rom[MAX_TEMP_COUNT * 8] __attribute__((section(".bss")));

volatile uint8_t * const onewire_port = &PORTB;
volatile uint8_t * const onewire_direction = &DDRB;
volatile uint8_t * const onewire_portin = &PINB;
const uint8_t onewire_mask = (1 << PORTB0);

void preempt_wait_us(uint16_t us)     //assumption: interrupts disabled
{
    uint16_t cycles = us + (us / 2);  
    TCNT1 = 0;                        //reset timer1 counter
    OCR1A = cycles;                   //set timer1 TOP value
    BIT_ON(TIFR1, OCF1A);             //reset OCF1A flag (sic)
    TCCR1B |= ((1<<WGM12)|(1<<CS11)); //timer1 clk/8 prescaler, CTC mode

    sei();
    while(!(TIFR1 & (1 << OCF1A)));    //wait for the counter to hit TOP with interrupts enabled
    cli();

    TCCR1B = 0;                       //timer1 disable
}

static void scan_temp()
{
    green_on();
    static uint8_t rom[MAX_TEMP_COUNT * 8];
    ++debug_data.temp_scans;
#define SEARCH_TRYS 3    
    for(uint8_t retry = 0; retry < SEARCH_TRYS; retry++)
    {
        cli();
        uint8_t count;
        if(ds18b20search(&count, rom, (uint16_t)sizeof(temp_rom)) == DS18B20_ERROR_OK)
        {
            if(count >= temp_rom_count || retry == (SEARCH_TRYS - 1))
            {
                sei();
                temp_rom_count = count;
                memcpy(temp_rom,  rom, sizeof(temp_rom));
                break;
            }
            else
                ++debug_data.temp_scan_warns;
        }
        else
            ++debug_data.temp_scan_errors;

        preempt_wait_us(5000);
        sei();
    }
    green_off();
}

static void start_temp_read()
{
    for(uint8_t i = 0; i < temp_rom_count; i++)
    {
        cli();
        ds18b20convert(temp_rom + i * 8);
        sei();
    }
}

static void finish_temp_read()
{
    int8_t temp_a = -128;
    int8_t temp_b = -128;

    for(uint8_t i = 0; i < MAX_TEMP_COUNT; i++)
    {
        if(i >= temp_rom_count)
            temp_response.data[i].valid = 0;
        else
        {
            cli();
            uint16_t t;
            ++debug_data.temp_reads;

            temp_response.data[i].id = *(uint64_t *) (temp_rom + i * 8);
            temp_response.data[i].valid = 1;

            if(ds18b20read(temp_rom + i * 8, &t) != 0)
            {
                ++debug_data.temp_read_errors;
                if(temp_response.data[i].age != 255)
                    temp_response.data[i].age += 1;
            }
            else
            {
                temp_response.data[i].age = 0;
                temp_response.data[i].temperature = t;
                if(temp_response.data[i].id == config.door_temp_id_A)
                    temp_a = (int16_t)t / 16;
                else if(temp_response.data[i].id == config.door_temp_id_B)
                    temp_b = (int16_t)t / 16;
            }
            sei();
        }
    }

    if(door_action != DA_FORCE_CLOSE && door_action != DA_FORCE_OPEN
       && temp_a != -128 && temp_b != -128)
    {
        int8_t diff = temp_b - temp_a;
        if(diff > config.door_temp_diff_open)
            door_action = DA_OPEN;
        else if(diff < config.door_temp_diff_close)
            door_action = DA_CLOSE;
    }
}

static usbMsgLen_t handle_dbg_read_request()
{
    debug_data.door_action = door_action;
    debug_data.door_countdown = door_countdown;
    usbMsgPtr = (usbMsgPtr_t)&debug_data;
    return sizeof(debug_data);
}

static usbMsgLen_t handle_temperature_request()
{
    usbMsgPtr = (usbMsgPtr_t)&temp_response;
    return sizeof(temp_response);
}

static usbMsgLen_t handle_voltmeter_request()
{
    usbMsgPtr = (usbMsgPtr_t)&volt_response;
    return sizeof(volt_response);
}

static usbMsgLen_t handle_cfg_read_request()
{
    usbMsgPtr = (usbMsgPtr_t)&config;
    return sizeof(config);
}

static usbMsgLen_t handle_cfg_write_request(usbRequest_t *req)
{
    if(req->wLength.word != sizeof(struct config))
        return 0;

    config_write_tgt = (void *)&config;
    return USB_NO_MSG;
}

uint8_t usbFunctionWrite(uint8_t *data, uint8_t len)
{
    if(!config_write_tgt || config_write_tgt + len > (uint8_t *)(&config + 1))
        return -1;

    memcpy(config_write_tgt, data, len);
    config_write_tgt += len;

    if(config_write_tgt >= (uint8_t *)(&config + 1))
    {    
        config_write_tgt = 0;
        config_updated = 1;
        return 1;
    }
    else
        return 0;
}

usbMsgLen_t usbFunctionSetup(unsigned char data[8])
{
    ++debug_data.usb_reqs;
    usbRequest_t *req = (void *)data;
    switch(req->bRequest)
    {
    case CMD_DBG_READ:
        return handle_dbg_read_request();
    case CMD_VOLT:
        return handle_voltmeter_request();
    case CMD_TEMP:
        return handle_temperature_request();
    case CMD_CFG_READ:
        return handle_cfg_read_request();
    case CMD_CFG_WRITE:
        return handle_cfg_write_request(req);
    }
    ++debug_data.usb_req_errors;
    return 0;
}

uint8_t t0ov_counter = 0;
static int is_time(uint8_t mask, uint8_t val)
{
    if((t0ov_counter & mask) == val)
        return 1;
    else
        return 0;
}

static void handle_leds()
{
    if(led_alert)
    {
        if(is_time(led_period, 0))
            green_on();

        if(led_on && is_time(led_period, (led_period + 1) / 2))
        {
            green_off();
            led_alert--;
        }
    }
    else
        green_off();
}

volatile static enum { TS_SCAN, TS_SCAN_DONE, TS_START, TS_START_DONE, TS_FINISH, TS_FINISH_DONE} th_state = TS_SCAN;
static void handle_thermo()
{
    static uint8_t cycle = 0;
    if(is_time(TIME_1400ms, 0))
    {
        switch(th_state)
        {
        case TS_SCAN_DONE:
            th_state = TS_START;
            break;
        case TS_START_DONE:
            th_state = TS_FINISH;
            break;
        case TS_FINISH_DONE:
            if(cycle == 10)
            {
                th_state = TS_SCAN;
                cycle = 0;
            }
            else
            {
                th_state = TS_START;
                ++cycle;
            }
            break;
        }
    }
}

static void handle_volt()
{
    static enum { VS_CONFIG, VS_CURRENT, VS_VOLT, VS_ACTION } volt_state = VS_CONFIG;
    static uint16_t config_val = 0x1eef;
    if(is_time(TIME_2800ms, 0))
    {
        switch(volt_state)
        {
        case VS_CONFIG:
            twi_op(0x80, 0x00, &config_val, TWI_OP_WRITE);
            volt_state = VS_CURRENT;
            break;
        case VS_CURRENT:
            twi_op(0x80, 0x01, &volt_response.current, TWI_OP_READ);
            volt_state = VS_VOLT;
            break;
        case VS_VOLT:
            twi_op(0x80, 0x02, &volt_response.voltage, TWI_OP_READ);
            volt_state = VS_ACTION;
            break;
        case VS_ACTION:
            if(volt_response.voltage < config.solar_relay_decivolt_lo * 200)
                relay_off();
            if(volt_response.voltage > config.solar_relay_decivolt_hi * 200)
                relay_on();
            volt_state = VS_CONFIG;
            break;
        }
    }
}

static void handle_door()
{
    static uint8_t peak = 0;
    uint8_t pressed = 0;
    if(is_time(TIME_87ms, 0))
    {        
        if(EIFR & (1<<INTF1)) //button pushed?
        {
            BIT_ON(EIFR, INTF1); //clear interrupt request flag (sic)
            peak = 1;
        }
        else if(peak == 1)
        {
            peak = 0;
            if(PIND & (1<<PIND3))
            {
                pressed = 1;
            }
        }
        if(pressed)
        {
            if(door_countdown <= DOOR_COUNTDOWN - DOOR_IGNORE_TIME)
            {
                switch(door_action)
                {
                case DA_FORCE_OPEN:
                    if(door_countdown >= (DOOR_COUNTDOWN - DOOR_DOUBLE_PUSH_TIME))
                        door_action = DA_FORCE_CLOSE;

                    door_countdown = DOOR_COUNTDOWN;
                    break;
                default:
                    door_action = DA_FORCE_OPEN;
                    door_countdown = DOOR_COUNTDOWN;
                    break;       
                }
            }
        }    
        if(door_countdown)
        {
            --door_countdown;
            if(!door_countdown)
                door_action = DA_NO_ACTION;
        }
    }
}

ISR(TIMER0_OVF_vect)
{
  handle_leds();

  handle_thermo();

  handle_volt();

  handle_door();

  t0ov_counter++;
}

ISR(TIMER2_OVF_vect)
{
    usbPoll();
    ++debug_data.usb_polls;
}


static void door_open()
{
    BIT_OFF(PORTC, PORTC1);
    BIT_ON(PORTC, PORTC2);
}

static void door_close()
{
    BIT_OFF(PORTC, PORTC2);
    BIT_ON(PORTC, PORTC1);
}

static void door_off()
{
    PORTC &= ~((1<<PORTC1)|(1<<PORTC2));
}

static void init()
{
    BIT_OFF(MCUCR, PUD);              // enable pull-ups
    BIT_ON(DDRB, PORTB1);             // enable LED
    TCCR0B |= ((1<<CS02)|(1<<CS00));  // timer0 clk/1024 prescaler
    BIT_ON(TIMSK0, TOIE0);            // timer0 overflow interrupt
    BIT_ON(TWSR, TWPS1);              // 1/16 TWI prescaler
    TWBR = 4;                         // TWI Bit Rate
    //TWI Bit Rate = 12MHz / (16 + 2*TWBR*TWIPrescale) =
    //               12MHz / (16 + 2*4*16) = 83kHz
    BIT_ON(TCCR2B, CS22);             // timer2 clk/64 prescaler
    BIT_ON(TIMSK2, TOIE2);            // timer2 overflow interrupt

    EICRA |= ((1<<ISC11)|(1<<ISC10)); // interrupt on rising edge INT1 (door button)

    DDRC |= ((1<<PORTC0)|(1<<PORTC1)|(1<<PORTC2)); // PC0, PC1, PC2 for output (door & solar control)
}

void init_wdt_disable(void) \
  __attribute__((naked)) \
  __attribute__((section(".init3")));
void init_wdt_disable(void)
{
  MCUSR = 0;
  wdt_disable();
}

void __attribute__((noreturn)) main(void)
{
    wdt_disable();

    cli();
    init();
    relay_off(); 
    green_on();

    read_config();

    usbInit();
    usbDeviceDisconnect();
    _delay_ms(250);
    usbDeviceConnect();

    sei();
    green_off();

    set_led_alert(TIME_87ms, 3);

    wdt_enable(WDTO_8S);

    for(;;){
        if(config_updated)
        {
            set_led_alert(TIME_350ms, 4);
            cli();
            preempt_wait_us(10000);
            write_config();
            config_updated = 0;
            sei();
            cli();
            read_config();
            sei();
        }

        switch(th_state)
        {
        case TS_SCAN:
            scan_temp();
            th_state = TS_SCAN_DONE;
            break;
        case TS_START:
            start_temp_read();
            th_state = TS_START_DONE;
            break;
        case TS_FINISH:
            finish_temp_read();
            wdt_reset();
            th_state = TS_FINISH_DONE;
            break;
        }

        
        switch(door_action)
        {
        case DA_NO_ACTION:
            door_off();
            break;
        case DA_OPEN:
        case DA_FORCE_OPEN:
            door_open();
            break;
        case DA_CLOSE:
        case DA_FORCE_CLOSE:
            door_close();
            break;
        }
    }
}

