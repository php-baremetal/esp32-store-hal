/*
   +----------------------------------------------------------------------+
   | php-baremetal: esp32-store-hal                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2026 Gianfrancesco Aurecchia                           |
   +----------------------------------------------------------------------+
   | This source file is subject to the MIT license, that is bundled with |
   | this package in the file LICENSE.                                    |
   +----------------------------------------------------------------------+
   | Authors: Gianfrancesco Aurecchia <gianfri@php-baremetal.com>         |
   +----------------------------------------------------------------------+
*/

/* ESP32 store HAL: NVS in the dedicated `phpstore` partition. Every NVS call runs on the executor task
 * (internal-RAM stack): flash access from the PHP task, whose stack is in PSRAM, asserts. */
#include "bm_store_hal.h"
#include "executor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#define STORE_PARTITION "phpstore"
#define STORE_NAMESPACE "php"
#define STORE_TIMEOUT_MS 1000

static const char *TAG = "store";
static nvs_handle_t s_handle;
static bool s_ok;

typedef struct {
    const char *key;
    const char *value;
    char *buf;
    size_t *len;
    bool ok;
} store_op_t;

typedef struct {
    char (*keys)[NVS_KEY_NAME_MAX_SIZE];
    size_t count;
    size_t cap;
} store_keys_t;

static bool run(esp_err_t (*fn)(void *arg), store_op_t *op)
{
    return executor_run_sync(fn, op, STORE_TIMEOUT_MS) == ESP_OK && op->ok;
}

static esp_err_t store_init(void *arg)
{
    store_op_t *op = arg;
    esp_err_t err = nvs_flash_init_partition(STORE_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* full or newer-format partition: wipe it and start over */
        nvs_flash_erase_partition(STORE_PARTITION);
        err = nvs_flash_init_partition(STORE_PARTITION);
    }
    if (err == ESP_OK) {
        err = nvs_open_from_partition(STORE_PARTITION, STORE_NAMESPACE, NVS_READWRITE, &s_handle);
    }
    op->ok = err == ESP_OK;
    return ESP_OK;
}

void bm_store_hal_init(void)
{
    store_op_t op = { 0 };
    s_ok = run(store_init, &op);
    if (s_ok) {
        ESP_LOGI(TAG, "persistent store ready (partition '%s')", STORE_PARTITION);
    }
}

bool bm_store_hal_ready(void) { return s_ok; }

static esp_err_t store_set(void *arg)
{
    store_op_t *op = arg;
    esp_err_t err = nvs_set_str(s_handle, op->key, op->value);
    if (err == ESP_OK) {
        err = nvs_commit(s_handle);
    }
    op->ok = err == ESP_OK;
    return ESP_OK;
}

bool bm_store_hal_set(const char *key, const char *val)
{
    store_op_t op = { .key = key, .value = val };
    return run(store_set, &op);
}

static esp_err_t store_get(void *arg)
{
    store_op_t *op = arg;
    op->ok = nvs_get_str(s_handle, op->key, op->buf, op->len) == ESP_OK;
    return ESP_OK;
}

bool bm_store_hal_get(const char *key, char *buf, size_t *len)
{
    store_op_t op = { .key = key, .buf = buf, .len = len };
    return run(store_get, &op);
}

bool bm_store_hal_has(const char *key)
{
    size_t len = 0;
    return bm_store_hal_get(key, NULL, &len);
}

static esp_err_t store_delete(void *arg)
{
    store_op_t *op = arg;
    esp_err_t err = nvs_erase_key(s_handle, op->key);
    if (err == ESP_OK) {
        err = nvs_commit(s_handle);
    }
    op->ok = err == ESP_OK;
    return ESP_OK;
}

bool bm_store_hal_delete(const char *key)
{
    store_op_t op = { .key = key };
    return run(store_delete, &op);
}

static esp_err_t store_clear(void *arg)
{
    store_op_t *op = arg;
    esp_err_t err = nvs_erase_all(s_handle);
    if (err == ESP_OK) {
        err = nvs_commit(s_handle);
    }
    op->ok = err == ESP_OK;
    return ESP_OK;
}

bool bm_store_hal_clear(void)
{
    store_op_t op = { 0 };
    return run(store_clear, &op);
}

static esp_err_t store_collect_keys(void *arg)
{
    store_keys_t *out = arg;
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(STORE_PARTITION, STORE_NAMESPACE, NVS_TYPE_ANY, &it);
    while (err == ESP_OK && it != NULL) {
        if (out->count == out->cap) {
            size_t cap = out->cap ? out->cap * 2 : 16;
            void *grown = heap_caps_realloc(out->keys, cap * NVS_KEY_NAME_MAX_SIZE,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!grown) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            out->keys = grown;
            out->cap = cap;
        }
        nvs_entry_info_t info;
        err = nvs_entry_info(it, &info);
        if (err != ESP_OK) {
            break;
        }
        memcpy(out->keys[out->count++], info.key, NVS_KEY_NAME_MAX_SIZE);
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    /* The end of the entries: nvs_entry_find/next return ESP_ERR_NVS_NOT_FOUND with it == NULL. */
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

bool bm_store_hal_keys(void (*cb)(const char *key, void *ctx), void *ctx)
{
    store_keys_t keys = { 0 };
    bool ok = executor_run_sync(store_collect_keys, &keys, STORE_TIMEOUT_MS) == ESP_OK;
    if (ok) {
        for (size_t i = 0; i < keys.count; i++) {
            cb(keys.keys[i], ctx);
        }
    }
    heap_caps_free(keys.keys);
    return ok;
}
