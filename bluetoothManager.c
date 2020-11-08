#include <stdio.h>
#include <stdlib.h>

#include "cybsp.h"

#include "FreeRTOS.h"

#include "bluetoothManager.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_trace.h"
#include "wiced_timer.h"
#include "btutil.h"

#include "advDatabase.h"

#include "queue.h"
static QueueHandle_t btm_cmdQueue;
static wiced_timer_ext_t btm_mgmtQueueTimer;

typedef enum {
	BTM_SCAN,
} btm_cmd_t;

typedef struct {
	btm_cmd_t cmd;
	void *data;
} btm_cmdMsg_t;

void btm_advCallback(wiced_bt_ble_scan_results_t *p_scan_result, uint8_t *p_adv_data);

static void btm_processBluetoothAppQueue()
{
	btm_cmdMsg_t msg;

	 BaseType_t rval;

	 rval = xQueueReceive( btm_cmdQueue,&msg,0);
	 if(rval == pdTRUE)
	 {
		 switch(msg.cmd)
		 {
		 case BTM_SCAN:
            wiced_bt_ble_observe((wiced_bool_t)msg.data,0,btm_advCallback);
			 break;
		 }
	 }
}

void btm_advCallback(wiced_bt_ble_scan_results_t *p_scan_result, uint8_t *p_adv_data)
{
    wiced_bt_ble_scan_results_t *scan_result = malloc(sizeof(wiced_bt_ble_scan_results_t));
    uint8_t *data = malloc(32);
   
    memcpy(data,p_adv_data,32);
    memcpy(scan_result,p_scan_result->remote_bd_addr,BD_ADDR_LEN);
    adb_addAdv(scan_result,data);
}

/**************************************************************************************************
* Function Name: app_bt_management_callback()
***************************************************************************************************
* Summary:
*   This is a Bluetooth stack event handler function to receive management events from
*   the BLE stack and process as per the application.
*
* Parameters:
*   wiced_bt_management_evt_t event             : BLE event code of one byte length
*   wiced_bt_management_evt_data_t *p_event_data: Pointer to BLE management event structures
*
* Return:
*  wiced_result_t: Error code from WICED_RESULT_LIST or BT_RESULT_LIST
*
*************************************************************************************************/
wiced_result_t app_bt_management_callback(wiced_bt_management_evt_t event, wiced_bt_management_evt_data_t *p_event_data)
{
    wiced_result_t result = WICED_BT_SUCCESS;

    switch (event)
    {
        case BTM_ENABLED_EVT:
            printf("Started BT Stack Succesfully\n");
            btm_cmdQueue = xQueueCreate(10,sizeof(btm_cmdMsg_t));
            wiced_init_timer_ext (&btm_mgmtQueueTimer, btm_processBluetoothAppQueue,0, WICED_TRUE);
            wiced_start_timer_ext (&btm_mgmtQueueTimer, 50);
            wiced_bt_ble_observe(WICED_TRUE,0,btm_advCallback);

        break;

        default:
            printf("Unhandled Bluetooth Management Event: %s\n", btutil_getBTEventName(event));
            break;
    }

    return result;
}

void btm_cmdScan(bool enable)
{
    btm_cmdMsg_t msg;
    msg.cmd = BTM_SCAN;
    msg.data = (void *)enable;
  	xQueueSend(btm_cmdQueue, &msg,0);
}