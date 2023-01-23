/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>


#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

// General note: DT == DeviceTree (.dts is device tree settings)

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* Data of ADC io-channels specified in devicetree. */
// For each element in io_channels (an autogenerated node from the .dts file), get a handle to it's adc_dt_spec
static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA)
};

#define LED_ON          0
#define LED_OFF         1

// Get the device tree handles by their friendly name from the macro-snap.dts file
// static const struct pwm_dt_spec cyber_led = PWM_DT_SPEC_GET(DT_ALIAS(pwm0));
// static const struct gpio_dt_spec blue_led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
// static const struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
// static const struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);
static const struct gpio_dt_spec ui_btn = GPIO_DT_SPEC_GET_OR(DT_ALIAS(uibtn), gpios,{0});
static const struct gpio_dt_spec fsr_det = GPIO_DT_SPEC_GET_OR(DT_ALIAS(fsrdet), gpios,{0});
static struct gpio_callback button_cb_data;

//PWM period
#define PWM_PERIOD 1000000



//for bluetooth test
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* The ble message must be an array of uint*/
/* [0] = start message pt 1, [1] = start message pt 2, [2] = vbat; [3] = fsr_1; [4] = fsr_2; [5] = fsr_3, [6] = calibration_button, [7] = status, [8] = band_is_connected (1 = connected)*/
uint8_t ble_msg []= {0xED, 0xFE, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xaa, 0xfe),
	BT_DATA(BT_DATA_SVC_DATA16, ble_msg, ARRAY_SIZE(ble_msg)) 
};

/* Set Scan Response data */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};


static void bt_ready(int err)
{
	char addr_s[BT_ADDR_LE_STR_LEN];
	bt_addr_le_t addr = {0};
	size_t count = 1;

	if (err) {
		return;
	}

	/* Start advertising */
	err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		return;
	}

	/* For connectable advertising you would use
	 * bt_le_oob_get_local().  For non-connectable non-identity
	 * advertising an non-resolvable private address is used;
	 * there is no API to retrieve that.
	 */

	bt_id_get(&addr, &count);
	bt_addr_le_to_str(&addr, addr_s, sizeof(addr_s));
}

void set_ble_msg (uint8_t vbat, uint8_t fsr1, uint8_t fsr2, uint8_t fsr3)
{
    int err;
    err = bt_le_adv_stop();
    k_msleep(5);

    ble_msg[2] = vbat;
    ble_msg[3] = fsr1;
    ble_msg[4] = fsr2;
    ble_msg[5] = fsr3;
    err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad),
            sd, ARRAY_SIZE(sd));
}

void get_adc_readings (uint8_t* array)
{
    int16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(buf),
	};
    for(uint8_t i = 0; i < ARRAY_SIZE(adc_channels); i++)
    {
        // Iniitlize the adc read sequence
        (void)adc_sequence_init_dt(&adc_channels[i], &sequence);
        // Read the ADC channel (internall updates sequence.buf)
        (void)adc_read(adc_channels[i].dev, &sequence);
        // Upcast the buffer to a int32_t from int16_t
        array[i] = (uint8_t)buf; // 3.5V = 8192, 4V = <optimized out>
        // Do the conversion to Mv. based on the reference voltage
    }
}

// Before main executes, zephyr-the RTOS, automatically initializes the pin in/out and set's up the clock
void main(void)
{
    volatile int err;
    volatile uint8_t adc_readings[4] = {0, 0, 0, 0};

    k_msleep(1000);

  	/* Initialize the Bluetooth Subsystem */
    err = bt_enable(bt_ready); //causes reset if used in gdb

    /* Initialize button and gpio inputs*/
    err = gpio_pin_configure_dt(&ui_btn, GPIO_INPUT);
	err = gpio_pin_configure_dt(&fsr_det, GPIO_INPUT);
	err = gpio_pin_interrupt_configure_dt(&ui_btn, GPIO_INT_EDGE_TO_ACTIVE);
	err = gpio_pin_interrupt_configure_dt(&fsr_det, GPIO_INT_EDGE_TO_ACTIVE);

    // Initialization
    // gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT_ACTIVE);
    // gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_ACTIVE);
    // gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_ACTIVE);

    for(uint8_t i = 0; i < ARRAY_SIZE(adc_channels); i++)
    {
        adc_channel_setup_dt(&adc_channels[i]);
    }
    volatile int val;
    // Begin main logic
    // gpio_pin_set_dt(&blue_led, LED_OFF);
    // gpio_pin_set_dt(&red_led, LED_OFF);
    // gpio_pin_set_dt(&green_led, LED_OFF);
    while(1){
        k_msleep(100);
        /*poll the ui button and send that the val 1 or not upon press in the ble msg*/
        val = gpio_pin_get_dt(&ui_btn);
        ble_msg[6] = val;

        val = gpio_pin_get_dt(&fsr_det);
        ble_msg[8] = val;

        get_adc_readings(adc_readings);
        set_ble_msg (adc_readings[0], adc_readings[1], adc_readings[2], adc_readings[3]);

        
        // gpio_pin_toggle_dt(&blue_led);
        // gpio_pin_toggle_dt(&green_led);
        // gpio_pin_toggle_dt(&red_led);
    };
}
