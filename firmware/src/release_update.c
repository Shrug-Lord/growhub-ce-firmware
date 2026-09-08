#include "release_update.h"
#include "release_version.h"
#include "ota.h"
#include "wifi.h"
#include "time_sync.h"
#include "webserver.h"
#include "sensors.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define REPO "https://github.com/Shrug-Lord/growhub-ce-firmware"
#define API "https://api.github.com/repos/Shrug-Lord/growhub-ce-firmware/releases/latest"
#define SIX_HOURS (6LL * 3600 * 1000000)
#define DAY 86400

typedef struct {
    unsigned version;
    bool enabled;
    char skipped[32], later_tag[32], pending[32];
    int64_t later_until;
} prefs_t;
typedef struct { char op[16], tag[32], id[64]; bool enabled; } action_t;
static prefs_t prefs = {.version = 1};
static SemaphoreHandle_t lock;
static QueueHandle_t queue;
static char tag[32], url[256], digest[65], phase[24] = "idle", error[160], action_id[64];
static int image_size, bytes;
static int64_t checked, next_check, later_uptime;
static bool available, action_pending;
static char seen_ids[8][64], last_install_id[64];
static unsigned seen_index;
static const char *str(cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static bool save(void) {
    nvs_handle_t h;
    if (nvs_open("ce_updates", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, "prefs", &prefs, sizeof(prefs));
    if (e == ESP_OK) e = nvs_set_str(h, "install_id", last_install_id);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}
static void state(const char *p, const char *e) {
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(phase, sizeof(phase), "%s", p);
    snprintf(error, sizeof(error), "%s", e ? e : "");
    xSemaphoreGive(lock);
}
static bool trusted_url(const char *u) {
    const char *hosts[] = {"https://github.com/", "https://api.github.com/", "https://release-assets.githubusercontent.com/", "https://objects.githubusercontent.com/"};
    for (int i = 0; i < 4; i++) if (!strncmp(u, hosts[i], strlen(hosts[i]))) return true;
    return false;
}
static esp_err_t headers(esp_http_client_event_t *e) {
    if (e->user_data && e->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key, "Location")) {
        char *location = e->user_data;
        if (strlen(e->header_value) >= 2048) return ESP_FAIL;
        strcpy(location, e->header_value);
    }
    return ESP_OK;
}
// Explicit redirects prevent HTTPS downgrades and requests to unexpected hosts.
static esp_http_client_handle_t open_url(const char *initial) {
    char *target = strdup(initial), *location = calloc(1, 2048);
    if (!target || !location) { free(target); free(location); return NULL; }
    esp_http_client_handle_t client = NULL;
    for (int hop = 0; hop < 5 && trusted_url(target); hop++) {
        location[0] = 0;
        esp_http_client_config_t cfg = {.url=target, .timeout_ms=15000,
            .crt_bundle_attach=esp_crt_bundle_attach, .disable_auto_redirect=true,
            .event_handler=headers, .user_data=location, .buffer_size=2048,
            // GitHub signed asset redirect request lines exceed the 512-byte default.
            .buffer_size_tx=2048};
        client = esp_http_client_init(&cfg);
        if (!client) break;
        esp_http_client_set_header(client, "User-Agent", "Growhub-CE/" GROWHUB_VERSION);
        esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
        bool ok = esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0;
        int status = esp_http_client_get_status_code(client);
        if (ok && status == 200) break;
        esp_http_client_cleanup(client); client = NULL;
        if (!ok || status < 300 || status >= 400 || !trusted_url(location)) break;
        free(target); target = strdup(location);
        if (!target) break;
    }
    // Event handler only references this buffer during header processing.
    if (client) esp_http_client_set_user_data(client, NULL);
    free(target); free(location);
    return client;
}
static void check_release(void) {
    state("checking", NULL);
    esp_http_client_handle_t c = open_url(API);
    char *body = calloc(1, 32769);
    int count = 0, n = -1;
    if (c && body) {
        while (count < 32768 && (n=esp_http_client_read(c, body+count, 32768-count)) > 0) count += n;
    }
    bool complete = c && n == 0 && count < 32768 && esp_http_client_is_complete_data_received(c);
    if (c) esp_http_client_cleanup(c);
    cJSON *root = complete ? cJSON_Parse(body) : NULL;
    free(body);
    const char *t = str(root, "tag_name");
    unsigned parts[3];
    bool valid = root && cJSON_IsFalse(cJSON_GetObjectItem(root,"draft")) &&
        cJSON_IsFalse(cJSON_GetObjectItem(root,"prerelease")) && t[0]=='v' && strlen(t)<sizeof(tag) && release_version_parse(t, parts);
    cJSON *asset = NULL, *candidate = NULL;
    cJSON_ArrayForEach(asset, cJSON_GetObjectItem(root,"assets")) {
        if (!strcmp(str(asset,"name"), "firmware.bin")) candidate=asset;
    }
    char expected[256]; snprintf(expected,sizeof(expected),REPO "/releases/download/%s/firmware.bin",t);
    const char *d = str(candidate,"digest");
    cJSON *size = cJSON_GetObjectItem(candidate,"size");
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    valid = valid && candidate && !strcmp(str(candidate,"browser_download_url"),expected) &&
        strlen(d)==71 && !strncmp(d,"sha256:",7) && cJSON_IsNumber(size) && size->valuedouble>0 &&
        partition && size->valuedouble<=partition->size;
    for (int i=7; valid && i<71; i++) valid=isxdigit((unsigned char)d[i]);
    xSemaphoreTake(lock,portMAX_DELAY);
    checked = time_sync_wall_time_valid() ? time(NULL) : 0;
    if (valid) {
        snprintf(tag,sizeof(tag),"%s",t); snprintf(url,sizeof(url),"%s",expected);
        for (int i=0;i<64;i++) digest[i]=tolower((unsigned char)d[i+7]);
        digest[64]=0; image_size=size->valueint; available=release_version_newer(tag);
    }
    xSemaphoreGive(lock);
    cJSON_Delete(root);
    state(valid ? "idle" : "check_failed",valid ? NULL : "Release check failed: network, release metadata, or checksum unavailable. Use Check now to retry.");
}
static void install_release(void) {
    if (!ota_begin_operation()) { state("failed","Another firmware update is in progress."); return; }
    state("downloading",NULL);
    esp_http_client_handle_t c=open_url(url);
    const esp_partition_t *p=esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t ota=0;
    bool begun=false, ok=c && p && esp_ota_begin(p,image_size,&ota)==ESP_OK;
    begun=ok;
    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha);
    ok=ok && mbedtls_sha256_starts(&sha,0)==0;
    unsigned char *buf=malloc(4096), hash[32] = {0};
    int total=0,n=0;
    ok=ok && buf;
    while (ok && (n=esp_http_client_read(c,(char *)buf,4096))>0) {
        total+=n;
        ok=total<=image_size && mbedtls_sha256_update(&sha,buf,n)==0 && esp_ota_write(ota,buf,n)==ESP_OK;
        xSemaphoreTake(lock,portMAX_DELAY); bytes=total; xSemaphoreGive(lock);
        ota_set_progress(OTA_FLASHING,total);
    }
    ok=ok && n==0 && total==image_size && esp_http_client_is_complete_data_received(c) && mbedtls_sha256_finish(&sha,hash)==0;
    char actual[65]; for(int i=0;i<32;i++) snprintf(actual+i*2,3,"%02x",hash[i]);
    ok=ok && !strcmp(actual,digest);
    free(buf); mbedtls_sha256_free(&sha); if(c) esp_http_client_cleanup(c);
    if (ok) { ok=esp_ota_end(ota)==ESP_OK; begun=false; }
    esp_app_desc_t desc;
    ok=ok && esp_ota_get_partition_description(p,&desc)==ESP_OK && !strcmp(desc.version,tag+1);
    if(ok) {
        xSemaphoreTake(lock,portMAX_DELAY); snprintf(prefs.pending,sizeof(prefs.pending),"%s",tag); ok=save(); xSemaphoreGive(lock);
        ok=ok && esp_ota_set_boot_partition(p)==ESP_OK;
    }
    if(ok) { state("restarting",NULL); ota_set_progress(OTA_DONE,total); vTaskDelay(pdMS_TO_TICKS(2500)); esp_restart(); }
    if(begun) esp_ota_abort(ota);
    xSemaphoreTake(lock,portMAX_DELAY); prefs.pending[0]=0; save(); xSemaphoreGive(lock);
    ota_set_progress(OTA_FAILED,total); ota_end_operation();
    state("failed","Installation failed. Current boot selection was preserved. Check connectivity and release integrity, then retry manually.");
}
char *release_update_json(void) {
    if(!lock) return strdup("{\"v\":1,\"stage\":\"unavailable\"}");
    xSemaphoreTake(lock,portMAX_DELAY);
    bool deferred=!strcmp(prefs.later_tag,tag) &&
        ((time_sync_wall_time_valid() && prefs.later_until>0) ? time(NULL)<prefs.later_until : esp_timer_get_time()<later_uptime);
    cJSON *r=cJSON_CreateObject();
    cJSON_AddNumberToObject(r,"v",1); cJSON_AddBoolToObject(r,"checks_enabled",prefs.enabled);
    cJSON_AddStringToObject(r,"current_version",GROWHUB_VERSION);
    cJSON_AddStringToObject(r,"tag",tag); cJSON_AddStringToObject(r,"stage",phase);
    cJSON_AddStringToObject(r,"error",error); cJSON_AddStringToObject(r,"action_id",action_id);
    cJSON_AddStringToObject(r,"target_tag",prefs.pending);
    cJSON_AddNumberToObject(r,"checked_at",checked); cJSON_AddNumberToObject(r,"bytes",bytes);
    cJSON_AddNumberToObject(r,"size",image_size); cJSON_AddBoolToObject(r,"available",available);
    cJSON_AddBoolToObject(r,"prompt",available && strcmp(prefs.skipped,tag) && !deferred && !strcmp(phase,"idle"));
    char notes[192]; snprintf(notes,sizeof(notes),REPO "/releases/tag/%s",tag);
    cJSON_AddStringToObject(r,"release_url",tag[0]?notes:"");
    char *json=cJSON_PrintUnformatted(r); cJSON_Delete(r); xSemaphoreGive(lock); return json;
}
bool release_update_action(const char *json,size_t length) {
    if(!queue || !json || !length || length>512) return false;
    cJSON *r=cJSON_ParseWithLength(json,length);
    const char *op=str(r,"op"), *t=str(r,"tag"), *id=str(r,"id");
    bool ok=cJSON_IsObject(r) && cJSON_IsNumber(cJSON_GetObjectItem(r,"v")) && cJSON_GetObjectItem(r,"v")->valuedouble==1 && id[0] && strlen(id)<64 && strlen(t)<32;
    ok=ok && (!strcmp(op,"check") || !strcmp(op,"settings") || !strcmp(op,"later") || !strcmp(op,"skip") || !strcmp(op,"install"));
    if(!strcmp(op,"settings")) ok=ok && cJSON_IsBool(cJSON_GetObjectItem(r,"enabled"));
    if(!strcmp(op,"install")) ok=ok && cJSON_IsTrue(cJSON_GetObjectItem(r,"confirmed"));
    action_t a={0}; snprintf(a.op,sizeof(a.op),"%s",op); snprintf(a.tag,sizeof(a.tag),"%s",t); snprintf(a.id,sizeof(a.id),"%s",id);
    a.enabled=cJSON_IsTrue(cJSON_GetObjectItem(r,"enabled")); cJSON_Delete(r);
    xSemaphoreTake(lock,portMAX_DELAY);
    bool busy=action_pending || !strcmp(phase,"checking") || !strcmp(phase,"downloading") || !strcmp(phase,"restarting");
    bool sent=ok && !busy && xQueueSend(queue,&a,0)==pdTRUE;
    if(sent) action_pending=true;
    xSemaphoreGive(lock);
    return sent;
}
static void worker(void *unused) {
    action_t a;
    int64_t last_manual=0;
    for(;;) {
        bool handled=xQueueReceive(queue,&a,pdMS_TO_TICKS(1000))==pdTRUE;
        if(handled) {
            xSemaphoreTake(lock,portMAX_DELAY);
            bool duplicate=!strcmp(a.op,"install") && !strcmp(last_install_id,a.id);
            for (unsigned i=0;i<8;i++) if(!strcmp(seen_ids[i],a.id)) duplicate=true;
            if(!duplicate) {
                snprintf(action_id,sizeof(action_id),"%s",a.id);
                snprintf(seen_ids[seen_index++ % 8],sizeof(seen_ids[0]),"%s",a.id);
            }
            bool match=available && !strcmp(a.tag,tag);
            xSemaphoreGive(lock);
            if(duplicate) { xSemaphoreTake(lock,portMAX_DELAY); action_pending=false; xSemaphoreGive(lock); continue; }
            if(!strcmp(a.op,"check")) {
                if(esp_timer_get_time()-last_manual<30000000 && last_manual) { state("check_failed","Please wait 30 seconds between checks."); xSemaphoreTake(lock,portMAX_DELAY); action_pending=false; xSemaphoreGive(lock); continue; }
                last_manual=esp_timer_get_time(); check_release();
            } else if(!strcmp(a.op,"install")) {
                if(match) {
                    xSemaphoreTake(lock,portMAX_DELAY);
                    snprintf(last_install_id,sizeof(last_install_id),"%s",a.id);
                    bool persisted=save();
                    xSemaphoreGive(lock);
                    if(persisted) install_release(); else state("failed","Could not persist installation request. No update started.");
                } else state("failed","Release changed or is unavailable. Check again before installing.");
            } else {
                xSemaphoreTake(lock,portMAX_DELAY);
                prefs_t old=prefs;
                if(!strcmp(a.op,"settings")) { prefs.enabled=a.enabled; next_check=esp_timer_get_time()+(esp_random()%60000000); }
                else if(match && !strcmp(a.op,"skip")) snprintf(prefs.skipped,sizeof(prefs.skipped),"%s",tag);
                else if(match && !strcmp(a.op,"later")) {
                    snprintf(prefs.later_tag,sizeof(prefs.later_tag),"%s",tag);
                    prefs.later_until=time_sync_wall_time_valid()?time(NULL)+DAY:0; later_uptime=esp_timer_get_time()+DAY*1000000LL;
                }
                bool saved=save(); if(!saved) prefs=old;
                xSemaphoreGive(lock);
                if(!saved) state("failed","Could not save update preference.");
            }
        }
        xSemaphoreTake(lock,portMAX_DELAY);
        if(handled) action_pending=false;
        bool run=!action_pending && prefs.enabled && esp_timer_get_time()>=next_check && wifi_is_connected();
        if(run) next_check=esp_timer_get_time()+SIX_HOURS+(esp_random()%60000000);
        bool pending=prefs.pending[0]!=0;
        xSemaphoreGive(lock);
        if(pending) {
            esp_ota_img_states_t status;
            if(strcmp(prefs.pending+1,GROWHUB_VERSION)) state("failed","Requested version did not boot; rollback or interrupted update. Retry manually.");
            else if(esp_ota_get_state_partition(esp_ota_get_running_partition(),&status)==ESP_OK && status==ESP_OTA_IMG_PENDING_VERIFY) continue;
            else if (!webserver_is_running() || !wifi_is_connected() || !sensors_first_read_attempt_done()) continue;
            else state("installed",NULL);
            xSemaphoreTake(lock,portMAX_DELAY); prefs.pending[0]=0; save(); xSemaphoreGive(lock);
        }
        if(run) check_release();
    }
}
void release_update_init(void) {
    lock=xSemaphoreCreateMutex(); queue=xQueueCreate(2,sizeof(action_t));
    if(!lock || !queue) return;
    nvs_handle_t h;
    if(nvs_open("ce_updates",NVS_READONLY,&h)==ESP_OK) {
        prefs_t loaded; size_t size=sizeof(loaded);
        if(nvs_get_blob(h,"prefs",&loaded,&size)==ESP_OK && size==sizeof(loaded) && loaded.version==1) prefs=loaded;
        size_t id_size=sizeof(last_install_id);
        if(nvs_get_str(h,"install_id",last_install_id,&id_size)!=ESP_OK) last_install_id[0]=0;
        nvs_close(h);
    }
    if(prefs.later_tag[0]) later_uptime=DAY*1000000LL; // conservatively defer across boot without wall time
    next_check=esp_timer_get_time()+(esp_random()%60000000);
    if(xTaskCreate(worker,"release_update",10240,NULL,2,NULL)!=pdPASS) { vQueueDelete(queue); queue=NULL; state("unavailable","Update worker could not start."); }
}
