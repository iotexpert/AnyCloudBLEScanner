
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
    ADB_FILTER,
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
    bool record;
    bool filter;
    int numSeen;
    int listCount;
    adb_adv_data_t *list;
} adb_adv_t ;

#define ADB_MAX_SIZE (40)
static adb_adv_t adb_database[ADB_MAX_SIZE];
static int adb_db_count=0;

#define ADB_RECORD_MAX (100)
static int adb_recording_count = 0;
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


static void adb_db_printEntry(adb_print_method_t method, int entry, adb_adv_data_t *adv_data)
{
    printf("%c%c%02d %05d %03d MAC: ",adb_database[entry].record?'W':' ',
    adb_database[entry].filter?'F':' ',
    entry,adb_database[entry].numSeen,adb_database[entry].listCount);
    btutil_printBDaddress(adb_database[entry].result->remote_bd_addr);

    switch(method)
    {
    
    case ADB_PRINT_METHOD_BYTES:
        printf(" Data: ");
        btutil_adv_printPacketBytes(adv_data->data);
    break;

    case ADB_PRINT_METHOD_DECODE:
        printf("\n");
        btutil_adv_printPacketDecode(adv_data->data);
    break;
    } 
    printf("\n");

}

static void adb_db_print(adb_print_method_t method,bool history,int entry)
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
        if(history) // Then iterate through the linked list print all of the packets
        {
            for(adb_adv_data_t *list = adb_database[i].list;list;list = (adb_adv_data_t *)list->next)
            {
                adb_db_printEntry(method,i,list);    
            }
        }
        else  // Just print the first packet in the list
            adb_db_printEntry(method,i,adb_database[i].list);
    }
}

static void adb_db_add(wiced_bt_ble_scan_results_t *scan_result,uint8_t *data)
{
 
    int entry = adb_db_find(&scan_result->remote_bd_addr);
    
    // If it is NOT found && you have room
    if(entry == -1 && adb_db_count<ADB_MAX_SIZE)
    {
        adb_database[adb_db_count].result = scan_result;
        adb_database[adb_db_count].listCount = 1;
        adb_database[adb_db_count].record = false;
        adb_database[adb_db_count].filter = true;
        adb_database[adb_db_count].numSeen = 1;

        adb_adv_data_t *current = malloc(sizeof(adb_adv_data_t));
        current->next = 0;
        current->data = data;
        current->count = 1;

        adb_database[adb_db_count].list = current;

        adb_db_count = adb_db_count + 1;    
        adb_db_print(ADB_PRINT_METHOD_BYTES,false,adb_db_count-1);
        
    }
    else if(adb_database[entry].record && adb_recording_count<ADB_RECORD_MAX && adb_recording)
    {
        adb_database[entry].numSeen += 1;

        if(adb_database[entry].filter) // if filtering is on.
        {
            int len = btutil_adv_len(data);
            
            for(adb_adv_data_t *list = adb_database[entry].list;list;list = (adb_adv_data_t *)list->next)
            {
                if(memcmp(list->data,data,len) == 0) // Found the data
                {
                    list->count += 1;
                    free(data);
                    free(scan_result);
                    return;
                }
            }
        }

        adb_adv_data_t *current = malloc(sizeof(adb_adv_data_t));
        current->next = (struct adb_adv_data_t *)adb_database[entry].list;
        current->data = data;
        current->count = 1;


        adb_database[entry].listCount += 1;
        adb_database[entry].list = current;

        adb_db_print(ADB_PRINT_METHOD_BYTES,false,entry);

        adb_recording_count += 1;
        if(adb_recording_count == ADB_RECORD_MAX)
        {
            adb_recording = false;
            printf("Recording buffer full\n");
        }
    }
    else
    {
        adb_database[entry].numSeen += 1;
        adb_database[entry].list->count += 1;        
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

    if(entry > adb_db_count-1 || entry < ADB_WATCH_CLEAR)
    {
        printf("Record doesnt exist: %d\n",entry);
        return;      
    }
    adb_database[entry].record = !adb_database[entry].record; 

}


static void adb_db_filter(int entry)
{
    if(entry == ADB_FILTER_ALL)
    {
        for(int i=0;i<adb_db_count;i++)
        {
            adb_database[i].filter = true;
        }
        return;
    }

    if(entry == ADB_FILTER_CLEAR)
    {
        for(int i=0;i<adb_db_count;i++)
        {
            adb_database[i].filter = false;
        }
        return;
    }

    if(entry > adb_db_count-1 || entry < ADB_WATCH_CLEAR)
    {
        printf("Record doesnt exist: %d\n",entry);
        return;      
    }
    adb_database[entry].filter = !adb_database[entry].filter; 

}

static void adb_eraseEntry(int entry)
{
    if(entry > adb_db_count-1 || entry<0)
    {
        printf("Erase Entry Not Found %d\n",entry);
        return;
    }

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
        adb_recording_count -= 1;
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
                    adb_db_print(ADB_PRINT_METHOD_BYTES,true,(int)msg.data0);
                break;
                case ADB_PRINT_DECODE:
                    adb_db_print(ADB_PRINT_METHOD_DECODE,true,(int)msg.data0);
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

                    printf("Record Buffer Free %d\n",ADB_RECORD_MAX-adb_recording_count);
                break;
                case ADB_RECORD:
                    adb_recording = !adb_recording;
                    printf("Record %s Buffer Entries Free=%d\n",adb_recording?"ON":"OFF",
                        ADB_RECORD_MAX-adb_recording_count);
                break;

                case ADB_FILTER:
                    adb_db_filter((int)msg.data0);
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
inline void adb_filter(int entry) { adb_queueCmd(ADB_FILTER,(void*)entry,(void *)0); }
