
#include "FreeRTOS.h"
#include "queue.h"
#include "wiced_bt_ble.h"
#include <stdio.h>
#include <stdlib.h>

#include "btutil.h"
#include "advDatabase.h"

static QueueHandle_t adb_cmdQueue;
typedef enum {
    ADB_ADD,
    ADB_PRINT_RAW,
    ADB_PRINT_DECODE,
    ADB_WATCH,
    ADB_ERASE,
    ADB_RECORD,
} adb_cmd_t;

typedef struct
{
    adb_cmd_t cmd;
    void *data0;
    void *data1;
} adb_cmdMsg_t;

typedef struct {
    uint8_t *data;
    int count;
    struct adb_adv_data_t *next;
} adb_adv_data_t;

typedef struct {
    wiced_bt_ble_scan_results_t *result;
    int listCount;
    bool record;
    int numSeen;
    adb_adv_data_t *list;
} adb_adv_t ;

#define ADB_MAX_SIZE (40)
static adb_adv_t adb_database[ADB_MAX_SIZE];
static int adb_db_count=0;

#define ADB_RECORD_MAX (100)
static int adb_db_record_count = 0;
static bool adb_recording = false;

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
        end = entry+1;
    }

    if(end>adb_db_count)
        end = adb_db_count; 

    for(int i=start;i<end;i++)
    {
    
        printf("%s%02d %05d %03d MAC: ",adb_database[i].record?"*":" ",i,adb_database[i].numSeen,adb_database[i].listCount);
        btutil_printBDaddress(adb_database[i].result->remote_bd_addr);
        switch(method)
        {
        
        case ADB_PRINT_METHOD_BYTES:
            printf(" Data: ");
            btutil_adv_printPacketBytes(adb_database[i].list->data);
        break;

        case ADB_PRINT_METHOD_DECODE:
            printf("\n");
            btutil_adv_printPacketDecode(adb_database[i].list->data);
        break;
        } 
        printf("\n");
    }
}

static void adb_db_add(wiced_bt_ble_scan_results_t *scan_result,uint8_t *data)
{
 
    int entry = adb_db_find(&scan_result->remote_bd_addr);
    
    // If it is NOT found
    if(entry == -1)
    {
        adb_database[adb_db_count].result = scan_result;
        adb_database[adb_db_count].listCount = 1;
        adb_database[adb_db_count].record = false;
        adb_database[adb_db_count].numSeen = 1;
        
 
        adb_adv_data_t *current = malloc(sizeof(adb_adv_data_t));
        current->next = 0;
        current->data = data;

        adb_database[adb_db_count].list = current;

        adb_db_count = adb_db_count + 1;
        if(adb_db_count == ADB_MAX_SIZE)
        {
            printf("ADV Table Max Size\n");
            adb_db_count = adb_db_count - 1;
        }
        else
        {    
            adb_db_print(ADB_PRINT_METHOD_BYTES,adb_db_count-1);
        }
    }
    else if(adb_database[entry].record && adb_db_record_count<ADB_RECORD_MAX && adb_recording)
    {

        adb_adv_data_t *current = malloc(sizeof(adb_adv_data_t));

        current->next = (struct adb_adv_data_t *)adb_database[entry].list;
        current->data = data;
        adb_database[entry].listCount += 1;
        adb_database[entry].list = current;
        adb_db_print(ADB_PRINT_METHOD_BYTES,entry);

        adb_db_record_count += 1;
        if(adb_db_record_count == ADB_RECORD_MAX)
        {
            adb_recording = false;
            printf("Recording buffer full\n");
        }

        adb_database[entry].numSeen += 1;

    }
    else
    {
        adb_database[entry].numSeen += 1;
        free(adb_database[entry].list->data);
        adb_database[entry].list->data = data;
        free(scan_result);
    }
}

static void adb_db_watch(int entry)
{
    if(entry == ADB_WATCH_ALL)
    {
        for(int i=0;i<adb_db_count;i++)
        {
            adb_database[i].record = true;
        }
        return;
    }

    if(entry == ADB_WATCH_CLEAR)
    {
        for(int i=0;i<adb_db_count;i++)
        {
            adb_database[i].record = false;
        }
        return;
    }

    if(entry > adb_db_count) // ARH could be a bug right here
    {
        printf("Record doesnt exist: %d\n",entry);
        return;      
    }
    adb_database[entry].record = !adb_database[entry].record; 

}

void adb_eraseEntry(int entry)
{
    adb_adv_data_t *ptr;
    ptr = (adb_adv_data_t *)adb_database[entry].list->next;
    adb_database[entry].list->next = 0;
    while(ptr)
    {
        adb_adv_data_t *next;
        next = (adb_adv_data_t *)ptr->next;
        free(ptr->data);
        free(ptr);
        adb_database[entry].listCount -= 1;
        adb_db_record_count -= 1;
        ptr = next;
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
                    scan_result = (wiced_bt_ble_scan_results_t *)msg.data0;
                    data = (uint8_t *)msg.data1;
                    adb_db_add(scan_result,data);
                break;
                case ADB_PRINT_RAW:
                    adb_db_print(ADB_PRINT_METHOD_BYTES,(int)msg.data0);
                break;
                case ADB_PRINT_DECODE:
                    adb_db_print(ADB_PRINT_METHOD_DECODE,(int)msg.data0);
                break;
                case ADB_WATCH:
                    adb_db_watch((int)msg.data0);
                break;
                case ADB_ERASE:
                    if((int)msg.data0 == ADB_ERASE_ALL)
                    {
                        for(int i=0;i<adb_db_count;i++)
                        {
                            adb_eraseEntry(i);
                        }
                    }
                    else
                        adb_eraseEntry((int)msg.data0);

                    printf("Record Buffer Free %d\n",ADB_RECORD_MAX-adb_db_record_count);
                break;
                case ADB_RECORD:
                    adb_recording = !adb_recording;
                    printf("Record %s Buffer Entries Free=%d\n",adb_recording?"ON":"OFF",
                        ADB_RECORD_MAX-adb_db_record_count);
                break;

            }

        }
    }
}

static void adb_queueCmd(adb_cmd_t cmd,void *data0, void *data1)
{
    adb_cmdMsg_t msg;
    msg.cmd = cmd;
    msg.data0 = data0;
    msg.data1 = data1;
    xQueueSend(adb_cmdQueue,&msg,0); // If you loose an adv packet it is OK...
}

inline void adb_addAdv(wiced_bt_ble_scan_results_t *scan_result,void *data) { adb_queueCmd(ADB_ADD,(void *)scan_result,(void *)data);}
inline void adb_print(int entry) { adb_queueCmd(ADB_PRINT_RAW,(void *)entry,(void *)0); }
inline void adb_decode(int entry) { adb_queueCmd(ADB_PRINT_DECODE,(void*)entry,(void *)0); }

inline void adb_watch(int entry) { adb_queueCmd(ADB_WATCH,(void*)entry,(void *)0); }
inline void adb_record(int packets) { adb_queueCmd(ADB_RECORD,(void*)packets,(void *)0); }
inline void adb_erase(int entry) { adb_queueCmd(ADB_ERASE,(void*)entry,(void *)0); }
