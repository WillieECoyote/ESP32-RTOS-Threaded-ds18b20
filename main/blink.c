//Created by: Elmo AKA WillieECoyote
//Description: This is a simple ESP32 program written using eclipse and the ESP32 IDF, it's purpose is to blink a light faster or slower depending
//			   on the temperature the ds18 sensor is measuring. The blink ratio is arbitrary.
//
//
//
//
//

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "sdkconfig.h"

#include "owb.h"
#include "owb_rmt.h"
#include "ds18b20.h"

//RTOS Defines
#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

//Task handles, This helps with contolling states
static TaskHandle_t task_1 = NULL;
static TaskHandle_t task_2 = NULL;

//GPIO Defines
#define BLINK_GPIO 2

//one wire defines
#define GPIO_DS18B20_0       (CONFIG_ONE_WIRE_GPIO)
#define MAX_DEVICES          (8)
#define DS18B20_RESOLUTION   (DS18B20_RESOLUTION_12_BIT)
#define SAMPLE_PERIOD        (500)   // milliseconds

//Temperature constants
static const int multipleT = 10;
static int blink_rate = 200;

//Threads
void taskOne(void *parameter) //thread 1
{
    gpio_pad_select_gpio(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    while(1) {
        /* Blink off (output low) */
		gpio_set_level(BLINK_GPIO, 0);
		vTaskDelay(blink_rate / portTICK_PERIOD_MS);
		/* Blink on (output high) */
		gpio_set_level(BLINK_GPIO, 1);
		vTaskDelay(blink_rate / portTICK_PERIOD_MS);
		//printf("%d", blink_rate);

    }
}

void taskTwo(void *parameter) //thread2
{
	// Uses slightly modifided example code from DavidAntliff's example ds18 sesnor code (https://github.com/DavidAntliff/esp32-ds18b20-example)
	// Stable readings require a brief period before communication
	vTaskDelay(2000.0 / portTICK_PERIOD_MS);

	// Create a 1-Wire bus, using the RMT timeslot driver
	OneWireBus * owb;
	owb_rmt_driver_info rmt_driver_info;
	owb = owb_rmt_initialize(&rmt_driver_info, GPIO_DS18B20_0, RMT_CHANNEL_1, RMT_CHANNEL_0);
	owb_use_crc(owb, true);  // enable CRC check for ROM code

	// Find all connected devices
	printf("Find devices:\n");
	OneWireBus_ROMCode device_rom_codes[MAX_DEVICES] = {0};
	int num_devices = 0;
	OneWireBus_SearchState search_state = {0};
	bool found = false;
	owb_search_first(owb, &search_state, &found);
	while (found)
	{
		char rom_code_s[17];
		owb_string_from_rom_code(search_state.rom_code, rom_code_s, sizeof(rom_code_s));
		printf("  %d : %s\n", num_devices, rom_code_s);
		device_rom_codes[num_devices] = search_state.rom_code;
		++num_devices;
		owb_search_next(owb, &search_state, &found);
	}
	printf("Found %d device%s\n", num_devices, num_devices == 1 ? "" : "s");

	if (num_devices == 1)
	{
		// For a single device only:
		OneWireBus_ROMCode rom_code;
		owb_status status = owb_read_rom(owb, &rom_code);
		if (status == OWB_STATUS_OK)
		{
			char rom_code_s[OWB_ROM_CODE_STRING_LENGTH];
			owb_string_from_rom_code(rom_code, rom_code_s, sizeof(rom_code_s));
			printf("Single device %s present\n", rom_code_s);
		}
		else
		{
			printf("An error occurred reading ROM code: %d", status);
		}
	}

	 // Create DS18B20 devices on the 1-Wire bus
	DS18B20_Info * devices[MAX_DEVICES] = {0};
	for (int i = 0; i < num_devices; ++i)
	{
		DS18B20_Info * ds18b20_info = ds18b20_malloc();  // heap allocation
		devices[i] = ds18b20_info;

		if (num_devices == 1)
		{
			printf("Single device optimisations enabled\n");
			ds18b20_init_solo(ds18b20_info, owb);          // only one device on bus
		}
		else
		{
			ds18b20_init(ds18b20_info, owb, device_rom_codes[i]); // associate with bus and device
		}
		ds18b20_use_crc(ds18b20_info, true);           // enable CRC check on all reads
		ds18b20_set_resolution(ds18b20_info, DS18B20_RESOLUTION);
	}

    // Read temperatures more efficiently by starting conversions on all devices at the same time
    int errors_count[MAX_DEVICES] = {0};
    if (num_devices > 0)
    {
        TickType_t last_wake_time = xTaskGetTickCount();

        while (1)
        {
            ds18b20_convert_all(owb);

            // In this application all devices use the same resolution,
            // so use the first device to determine the delay
            ds18b20_wait_for_conversion(devices[0]);

            // Read the results immediately after conversion otherwise it may fail
            // (using printf before reading may take too long)
            float readings[MAX_DEVICES] = { 0 };
            DS18B20_ERROR errors[MAX_DEVICES] = { 0 };

            for (int i = 0; i < num_devices; ++i)
            {
                errors[i] = ds18b20_read_temp(devices[i], &readings[i]);
            }

            for (int i = 0; i < num_devices; ++i)
            {
                if (errors[i] != DS18B20_OK)
                {
                    ++errors_count[i];
                }

                printf("  %d: %.1f    %d errors\n", i, readings[i], errors_count[i]);
                blink_rate = (int)readings[i]*multipleT;
            }

            vTaskDelayUntil(&last_wake_time, SAMPLE_PERIOD / portTICK_PERIOD_MS);
        }
    }
    else
    {
        printf("\nNo DS18B20 devices detected!\n");
    }

    // clean up dynamically allocated data
    for (int i = 0; i < num_devices; ++i)
    {
        ds18b20_free(&devices[i]);
    }
    owb_uninitialize(owb);
}


void app_main(void)
{
    xTaskCreatePinnedToCore( //Use xTaskCreate() in vanilla RTOS
      taskOne,    //task that must be called
      "task 1", //task description
      2048,         // Stack size allocated to task in bytes (would be words in vanilla) min size 768 bytes
      NULL,         // Parameter that can be passed to function
      1,            //Priority number the higher the number the more priority the task has. (0 lowest and 24 highest)
      &task_1,         //task handle
      app_cpu);     // Run on one core for demo purposes (ESP 32 only) (not in vanilla xTaskCreate())

    xTaskCreatePinnedToCore( //Use xTaskCreate() in vanilla RTOS
       taskTwo,    //task that must be called
       "task 2", //task description
	   8192,         // Stack size allocated to task in bytes (would be words in vanilla) min size 768 bytes
       NULL,         // Parameter that can be passed to function
       1,            //Priority number the higher the number the more priority the task has. (0 lowest and 24 highest)
       &task_2,         //task handle
       app_cpu);     // Run on one core for demo purposes (ESP 32 only) (not in vanilla xTaskCreate())

}
