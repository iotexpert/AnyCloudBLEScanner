#pragma once
#include "wiced_bt_ble.h"

void adb_task(void *arg);

void adb_addAdv(wiced_bt_ble_scan_results_t *scan_result,void *data);
void adb_print(int entry);