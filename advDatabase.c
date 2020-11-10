
#include "FreeRTOS.h"
#include "queue.h"
#include "wiced_bt_ble.h"
#include <stdio.h>
#include <stdlib.h>

#include "btutil.h"

static QueueHandle_t adb_cmdQueue;
typedef enum {
    ADB_ADD,
    ADB_PRINT_RAW,
    ADB_PRINT_DECODE,
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

typedef enum {
    ADB_PRINT_METHOD_BYTES,
    ADB_PRINT_METHOD_DECODE,
} adb_print_method_t;

static void adb_db_print(adb_print_method_t method,int entry)
{
    int start,end;
 
    if(entry < 0)
    {
        start = 0;
        end = adb_db_count;
    }
    else
    {
        start = entry;
        end = entry;
    }

    if(end>adb_db_count)
        end = adb_db_count; 

    for(int i=start;i<=end;i++)
    {
    
        printf("%02d MAC: ",i);
        btutil_printBDaddress(adb_database[i].result->remote_bd_addr);
        switch(method)
        {
        
        case ADB_PRINT_METHOD_BYTES:
            printf(" Data: ");
            btutil_adv_printPacketBytes(adb_database[i].data);
        break;

        case ADB_PRINT_METHOD_DECODE:
            printf("\n");
            btutil_adv_printPacketDecode(adb_database[i].data);
        break;
        } 
        printf("\n");
    }
}

static void adb_db_add(wiced_bt_ble_scan_results_t *scan_result,uint8_t *data)
{
 
    int entry = adb_db_find(&scan_result->remote_bd_addr);
    if(entry == -1)
    {
        
        adb_database[adb_db_count].result = scan_result;
        adb_database[adb_db_count].data = data;
        adb_db_print(ADB_PRINT_METHOD_BYTES,adb_db_count);
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

#if 0
static void adb_printDecodePacket(int entry)
{
    int start,end;
 
    if(entry == -1)
    {
        start = 0;
        end = adb_db_count;
    }
    else
    {
        start = entry;
        end = entry;
    }

    if(end>adb_db_count)
        end = adb_db_count; 

    for(int i=start;i<=end;i++)
    {

        printf("%02d MAC: ",i);
        btutil_printBDaddress(adb_database[i].result->remote_bd_addr);
        printf("\n");
        btutil_adv_printPacketDecode(adb_database[i].data);
        printf("\n");
    }
}
#endif

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
                case ADB_PRINT_RAW:
//                    adb_db_printRawPacket((int)msg.data0);
                    adb_db_print(ADB_PRINT_METHOD_BYTES,(int)msg.data0);
                break;
                case ADB_PRINT_DECODE:
                    adb_db_print(ADB_PRINT_METHOD_DECODE,(int)msg.data0);
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


void adb_printRaw(int entry)
{
    adb_cmdMsg_t msg;
    msg.cmd = ADB_PRINT_RAW;
    msg.data0 = (void *)entry;
    xQueueSend(adb_cmdQueue,&msg,0); // If the queue is full... oh well
}

void adb_printDecode(int entry)
{
    adb_cmdMsg_t msg;
    msg.cmd = ADB_PRINT_DECODE;
    msg.data0 = (void *)entry;
    xQueueSend(adb_cmdQueue,&msg,0); // If the queue is full... oh well
}