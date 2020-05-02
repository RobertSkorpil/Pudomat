#include <string.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/eeprom.h>
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
static uint8_t *config_write_tgt = (uint8_t *)&config;

#ifdef VOLT
static struct volt_response volt_response;
#endif

static struct temp_response temp_response;

static void read_config()
{
    eeprom_read_block(&config, &config_eeprom, sizeof(config));
    if(!config.signature != CONFIG_SIGNATURE)
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
    BIT_ON(PORTB, PINB1);
    led_on = 1;
}

static void green_off()
{
    BIT_OFF(PORTB, PINB1);
    led_on = 0;
}

static void relay_on()
{
    BIT_ON(PORTC, PINC0);
    volt_response.relay = 1;
}

static void relay_off()
{
    BIT_OFF(PORTC, PINC0);
    volt_response.relay = 0;
}

static void set_led_alert(uint16_t period, uint16_t count)
{
    led_period = period;
    led_alert = count;
}

#ifdef VOLT
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
#endif

static void twi_interrupt();
static void timer_interrupt();
ISR(TWI_vect)
{
    if(TWCR & (1 << TWINT))
#ifdef VOLT    
        twi_interrupt();
#else
    ;
#endif  
    else
        timer_interrupt();
}

ISR(TIMER0_OVF_vect, ISR_ALIASOF(TWI_vect));

#ifdef VOLT
static void twi_interrupt()
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
#endif

static uint8_t temp_rom_count;
static uint8_t temp_rom[MAX_TEMP_COUNT * 8] __attribute__((section(".bss")));

volatile uint8_t * const onewire_port = &PORTB;
volatile uint8_t * const onewire_direction = &DDRB;
volatile uint8_t * const onewire_portin = &PINB;
const uint8_t onewire_mask = (1 << PINB0);


static void scan_temp()
{
    green_on();
    ds18b20search(&temp_rom_count, temp_rom, (uint16_t)sizeof(temp_rom));
    green_off();
}

static void start_temp_read()
{
    for(uint8_t i = 0; i < temp_rom_count; i++)
        ds18b20convert(temp_rom + i * 8);
}

static void finish_temp_read()
{
    memset(&temp_response, 0, sizeof(temp_response));
    for(uint8_t i = 0; i < MAX_TEMP_COUNT; i++)
    {
        uint16_t t;

        if(i >= temp_rom_count || ds18b20read(temp_rom + i * 8, &t) != 0)
        {
            if(temp_response.data[i].age != 255)
                temp_response.data[i].age += 1;
            temp_response.data[i].valid = 0;
            return;
        }
        temp_response.data[i].id = *(uint64_t *) (temp_rom + i * 8);
        temp_response.data[i].temperature = t;
        temp_response.data[i].age = 0;
        temp_response.data[i].valid = 1;
    }
}

static usbMsgLen_t handle_temperature_request()
{
	usbMsgPtr = (usbMsgPtr_t)&temp_response;
	return sizeof(temp_response);
}

#ifdef VOLT
static usbMsgLen_t handle_voltmeter_request()
{
    usbMsgPtr = (usbMsgPtr_t)&volt_response;
    return sizeof(volt_response);
}
#endif

static usbMsgLen_t handle_cfg_read_request()
{
    usbMsgPtr = (usbMsgPtr_t)&config;
    return sizeof(config);
}

static usbMsgLen_t handle_cfg_write_request()
{
    config_write_tgt = (void *)&config;
    return USB_NO_MSG;
}

uint8_t usbFunctionWrite(uint8_t *data, uint8_t len)
{
    if(config_write_tgt + len > (uint8_t *)(&config + 1) ||
       config_write_tgt < (uint8_t *)&config)
        return -1;

    memcpy(config_write_tgt, data, len);
    config_write_tgt += len;

    if(config_write_tgt == (uint8_t *)(&config + 1))
    {
        write_config();
        set_led_alert(TIME_350ms, 4);
        return 0;
    }
    else
        return 1;
}

usbMsgLen_t usbFunctionSetup(unsigned char data[8])
{
	usbRequest_t *req = (void *)data;
	switch(req->bRequest)
	{
#ifdef VOLT    
    case CMD_VOLT:
        return handle_voltmeter_request();
#endif    
	case CMD_TEMP:
		return handle_temperature_request();
    case CMD_CFG_READ:
        return handle_cfg_read_request();
    case CMD_CFG_WRITE:
        return handle_cfg_write_request();
	}
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

static void handle_thermo()
{
    static enum { TS_SCAN, TS_START, TS_FINISH } th_state = TS_SCAN;

    if(is_time(TIME_1400ms, 0))
    {
        switch(th_state)
        {
        case TS_SCAN:
            scan_temp();
            th_state = TS_START;
            break;
        case TS_START:
            start_temp_read();
            th_state = TS_FINISH;
            break;
        case TS_FINISH:
            finish_temp_read();
            th_state = TS_SCAN;
            break;
        }
    }
}

static void handle_usb()
{
    if(is_time(TIME_87ms, 0))
        usbPoll();
}

#ifdef VOLT
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
#endif

//ISR(TIMER0_OVF_vect)
void timer_interrupt()
{
  handle_leds();

  handle_thermo();
#ifdef VOLT
  handle_volt();
#endif
  handle_usb();

  t0ov_counter++;
}

static void init()
{
    BIT_OFF(MCUCR, PUD);              // enable pull-ups
    BIT_ON(DDRB, PINB1);              // enable LED
    BIT_ON(DDRC, PINC0);              // enable relay
    TCCR0B |= ((1<<CS02)|(1<<CS00));  // timer0 clk/1024 prescaler
    BIT_ON(TIMSK0, TOIE0);            // timer0 overflow interrupt
    BIT_ON(TWSR, TWPS1);              // 1/16 TWI prescaler
    TWBR = 4;                         // TWI Bit Rate
    //TWI Bit Rate = 12MHz / (16 + 2*TWBR*TWIPrescale) =
    //               12MHz / (16 + 2*4*16) = 83kHz
}

void __attribute__((noreturn)) main(void)
{
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

    for(;;)
        usbPoll();
}

