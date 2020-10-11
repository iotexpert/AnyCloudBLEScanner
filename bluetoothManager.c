
#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>

#include "bluetoothManager.h"
#include "wiced_bt_stack.h"
#include "app_bt_cfg.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_trace.h"

#include "btutil.h"

static QueueHandle_t btm_cmdQueue;


typedef enum {
	BTM_PRINT_TABLE,
} btm_cmd_t;

typedef struct {
	btm_cmd_t cmd;
	void *data;

} btm_cmdMsg_t;


typedef struct {
	uint8_t mac[6];
	TickType_t time;
	int8_t rssi;
	int len;
	uint8_t *data;
} collect_t;

#define MAX_DATA 40
collect_t myData[MAX_DATA];
static int count=0;


static void btm_addDevice(wiced_bt_ble_scan_results_t *p_scan_result, uint8_t *packet)
{

	int len = btutil_adv_len(packet);
	int element;

	element = count;

	for(int i=0;i<count;i++)
	{
		if(memcmp(myData[i].mac,p_scan_result->remote_bd_addr,6) == 0)
		{
			if(myData[i].len != len+1)
			{
				free(myData[i].data);
				myData[i].data = 0;
			}
			element = i;

			break;
		}
	}

	if(myData[element].data == 0)
	{
		myData[element].data = malloc(len+1);
		if(myData[element].data == 0)
		{
			printf("Malloc failed\n");
			CY_ASSERT(0);
		}
	}

	memcpy(myData[element].data,packet,len+1);

	myData[element].time = xTaskGetTickCount();
	myData[element].len = len+1;

	myData[element].rssi = p_scan_result->rssi;

	if(element == count)
	{
		memcpy(myData[element].mac,p_scan_result->remote_bd_addr,6);
		printf("Added %02i Mac ",count);
		btutil_printBDaddress(myData[element].mac);
		printf("\n");

		if(count <MAX_DATA)
			count = count + 1;
		else
			printf("max count\n");
	}

}

static void btm_dumpTable()
{
	printf("\n------------------------------------------------------------------------------------------------------------------------------\n");
	printf("##   Time RSSI       MAC         Data\n");
	printf("------------------------------------------------------------------------------------------------------------------------------\n");
	for(int i=0;i<count;i++)
	{
		float time=0;
		time = (float)(xTaskGetTickCount() - myData[i].time) / 1000;

		printf("%02d %6.1f %04d ",i,time,myData[i].rssi);
		btutil_printBDaddress(myData[i].mac);
		printf(" ");
		btutil_adv_printPacketBytes(myData[i].data);
		printf("\n");
	}
}


// static void adv_scan_result_cback(wiced_bt_ble_scan_results_t *p_scan_result, uint8_t *p_adv_data )
// {
//     if (p_scan_result)
//     {
//     	btm_addDevice(p_scan_result,p_adv_data);
//     }
// 	else
// 	{
// 		printf("Scan result error\n");
// 	}
// }



static void btm_processBluetoothAppQueue(TimerHandle_t xTimer)
{
	btm_cmdMsg_t msg;

	 BaseType_t rval;

	 rval = xQueueReceive( btm_cmdQueue,&msg,0);
	 if(rval == pdTRUE)
	 {
		 switch(msg.cmd)
		 {
		 case BTM_PRINT_TABLE:
			 btm_dumpTable();
			 break;
		 }
	 }
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
            /* Bluetooth Controller and Host Stack Enabled */

        	memset(myData,0,sizeof(myData));

            if (WICED_BT_SUCCESS == p_event_data->enabled.status)
            {
				
				wiced_bt_ble_observe (WICED_TRUE, 0,btm_addDevice);
                btm_cmdQueue = xQueueCreate( 5, sizeof(btm_cmdMsg_t));

                TimerHandle_t timerHandle = xTimerCreate("Process Queue",100,true,0,btm_processBluetoothAppQueue);
                xTimerStart(timerHandle,0);
            }
            else
            {
            	printf("Error enabling BTM_ENABLED_EVENT\n");
            }

            break;

        case BTM_BLE_SCAN_STATE_CHANGED_EVT:

            if(p_event_data->ble_scan_state_changed == BTM_BLE_SCAN_TYPE_HIGH_DUTY)
            {
                printf("Scan State Change: BTM_BLE_SCAN_TYPE_HIGH_DUTY\n");
            }
            else if(p_event_data->ble_scan_state_changed == BTM_BLE_SCAN_TYPE_LOW_DUTY)
            {
                printf("Scan State Change: BTM_BLE_SCAN_TYPE_LOW_DUTY\n");
            }
            else if(p_event_data->ble_scan_state_changed == BTM_BLE_SCAN_TYPE_NONE)
            {
                printf("Scan stopped\n");
            }
            else
            {
                printf("Invalid scan state\n");
            }
            break;

        default:
            printf("Unhandled Bluetooth Management Event: 0x%x %s\n", event, btutil_getBTEventName(event));
            break;
    }

    return result;
}


void btm_printTable()
{
	btm_cmdMsg_t msg;
	msg.cmd = BTM_PRINT_TABLE;
	xQueueSend(btm_cmdQueue, &msg,0);
}
