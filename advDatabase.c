
#include "FreeRTOS.h"
#include "queue.h"
#include "wiced_bt_ble.h"
#include <stdio.h>
#include <stdlib.h>

static QueueHandle_t adb_cmdQueue;
typedef enum {
    ADB_ADD,
    ADB_PRINT,
} adb_cmd_t;

typedef struct
{
    adb_cmd_t cmd;
    void *data0;
    void *data1;
} adb_cmdMsg_t;

typedef struct {
    wiced_bt_ble_scan_results_t *result;
    uint8_t *data;
} adb_adv_t ;

#define ADB_MAX_SIZE (40)
adb_adv_t adb_database[ADB_MAX_SIZE];
int adb_db_count=0;

static int adb_db_find(wiced_bt_device_address_t *add)
{
    int rval=-1;
    for(int i=0;i<adb_db_count;i++)
    {
        if(memcmp(add,&adb_database[i].result->remote_bd_addr,BD_ADDR_LEN)==0)
        {
            rval = i;
            break;
        }
    }
    return rval;
}

static void adb_db_printEntry(int entry)
{
    if(!(entry>= 0 && entry <= adb_db_count))
    {
        printf("Illegal entry\n");
        return;
    }
    printf("%02d MAC: ",entry);

    for(int i=0;i<BD_ADDR_LEN;i++)
    {
        printf("%02X:",adb_database[entry].result->remote_bd_addr[i]);
    }

    // Print the RAW Data of the ADV Packet
    printf(" Data: ");
    int i=0;
    while(adb_database[entry].data[i])
    {
        for(int j=0;j<adb_database[entry].data[i];j++)
        {
            printf("%02X ",adb_database[entry].data[i+1+j]);
        }
        i = i + adb_database[entry].data[i]+1;
    }
    printf("\n");
}

static void adb_db_add(wiced_bt_ble_scan_results_t *scan_result,uint8_t *data)
{
 
    int entry = adb_db_find(&scan_result->remote_bd_addr);
    if(entry == -1)
    {
        
        adb_database[adb_db_count].result = scan_result;
        adb_database[adb_db_count].data = data;
        adb_db_printEntry(adb_db_count);
        adb_db_count = adb_db_count + 1;
        if(adb_db_count == ADB_MAX_SIZE)
        {
            printf("ADV Table Max Size\n");
            adb_db_count = adb_db_count - 1;
        }
    }
    else
    {
        free(scan_result);
        free(data);
    }
}

static void adb_printTable(int entry)
{
    if(entry == -1)
    {
        for(int i=0;i<adb_db_count;i++)
        {
            adb_db_printEntry(i);
        }
    }
    else
    {
        adb_db_printEntry(entry);
    }
}

void adb_task(void *arg)
{
    // setup the queue
    adb_cmdMsg_t msg;
    wiced_bt_ble_scan_results_t *scan_result;
    uint8_t *data;

    adb_cmdQueue = xQueueCreate(10,sizeof(adb_cmdMsg_t));
    
    while(1)
    {
        BaseType_t status = xQueueReceive(adb_cmdQueue,&msg,portMAX_DELAY);
        if(status == pdTRUE) 
        {
            switch(msg.cmd)
            {
                case ADB_ADD:
                    // Print the MAC Address
                    scan_result = (wiced_bt_ble_scan_results_t *)msg.data0;
                    data = (uint8_t *)msg.data1;
                    adb_db_add(scan_result,data);
                break;
                case ADB_PRINT:
                    adb_printTable((int)msg.data0);
                break;
            }

        }
    }
}

void adb_addAdv(wiced_bt_ble_scan_results_t *scan_result,void *data)
{
    adb_cmdMsg_t msg;
    msg.cmd = ADB_ADD;
    msg.data0 = (void *)scan_result;
    msg.data1 = (void *)data;
    xQueueSend(adb_cmdQueue,&msg,0); // If you loose an adv packet it is OK...
}


void adb_print(int entry)
{
    adb_cmdMsg_t msg;
    msg.cmd = ADB_PRINT;
    msg.data0 = (void *)entry;
    xQueueSend(adb_cmdQueue,&msg,0); // If the queue is full... oh well
}