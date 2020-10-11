
#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "usrcmd.h"
#include "bluetoothManager.h"

#include "wiced_bt_stack.h"
#include "app_bt_cfg.h"
#include "wiced_bt_dev.h"


volatile int uxTopUsedPriority ;
TaskHandle_t blinkTaskHandle;

void blinkTask(void *arg)
{
    cyhal_gpio_init(CYBSP_USER_LED,CYHAL_GPIO_DIR_OUTPUT,CYHAL_GPIO_DRIVE_STRONG,0);

    for(;;)
    {
    	cyhal_gpio_toggle(CYBSP_USER_LED);
    	vTaskDelay(500);
    }
}


int main(void)
{

    uxTopUsedPriority = configMAX_PRIORITIES - 1 ; // enable OpenOCD Thread Debugging

    /* Initialize the device and board peripherals */
    cybsp_init() ;

    __enable_irq();

    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);
    setvbuf(stdin, NULL, _IONBF, 0);

    printf("Started Application\n");

    cybt_platform_config_init(&bt_platform_cfg_settings);
    wiced_bt_stack_init (app_bt_management_callback, &wiced_bt_cfg_settings);

    // Stack size in WORDs
    // Idle task = priority 0
    xTaskCreate(blinkTask, "blink", configMINIMAL_STACK_SIZE,0 /* args */ ,0 /* priority */, &blinkTaskHandle);
    xTaskCreate(usrcmd_task, "nt shell", configMINIMAL_STACK_SIZE*4,0 /* args */ ,0 /* priority */, 0);
    vTaskStartScheduler();
}

