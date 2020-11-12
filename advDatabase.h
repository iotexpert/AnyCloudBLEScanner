#pragma once
#include "wiced_bt_ble.h"

void adb_task(void *arg);

void adb_addAdv(wiced_bt_ble_scan_results_t *scan_result,void *data);
void adb_print(int entry);
void adb_decode(int entry);

#define ADB_WATCH_ALL -1
#define ADB_WATCH_CLEAR -2
void adb_watch(int entry);
void adb_record(int packets);
#define ADB_ERASE_ALL -1
void adb_erase(int entry);

void adb_filter(int entry,bool filter);