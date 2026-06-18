#include "webserver.h"
#include "config.h"
#include "wifi.h"
#include "mqtt.h"
#include "relays.h"
#include "sensors.h"
#include "schedule.h"
#include "ota.h"
#include "time_sync.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "web";

#define BUF_SIZE 12288
#define SAVE_PARAM_MAX 4096
#define SCHEDULE_JSON_MAX 2048

static bool s_webserver_running = false;

// ---------------------------------------------------------------------------
// Timezone table — friendly name shown to user, POSIX string stored in NVS
// ---------------------------------------------------------------------------
static const struct { const char *label; const char *posix; } TZ_TABLE[] = {
    {"US/Eastern",          "EST5EDT,M3.2.0,M11.1.0"},
    {"US/Central",          "CST6CDT,M3.2.0,M11.1.0"},
    {"US/Mountain",         "MST7MDT,M3.2.0,M11.1.0"},
    {"US/Pacific",          "PST8PDT,M3.2.0,M11.1.0"},
    {"US/Alaska",           "AKST9AKDT,M3.2.0,M11.1.0"},
    {"US/Hawaii",           "HST10"},
    {"UK/London",           "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Amsterdam",    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Australia/Sydney",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth",     "AWST-8"},
    {"Asia/Tokyo",          "JST-9"},
    {"Asia/Singapore",      "SGT-8"},
    {"UTC",                 "UTC0"},
};
#define TZ_COUNT (sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]))

// ---------------------------------------------------------------------------
// Outlet device type options
// ---------------------------------------------------------------------------
static const char *DEVICE_TYPES[] = {
    "None", "Light", "Fan", "Humidifier",
    "Dehumidifier", "Water Pump", "Heater", "AC Controller",
};
#define DEVICE_TYPE_COUNT (sizeof(DEVICE_TYPES) / sizeof(DEVICE_TYPES[0]))

// ---------------------------------------------------------------------------
// Static page header — CSS + JS (stored in flash, never in RAM)
// ---------------------------------------------------------------------------
static const char PAGE_HEADER[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>GrowHub</title>"
"<style>"
"body{font-family:sans-serif;max-width:620px;margin:20px auto;padding:0 15px;"
"background:#1a1a2e;color:#e0e0e0}"
"h1{color:#4ecca3}h2{color:#4ecca3;border-bottom:1px solid #333;padding-bottom:5px}"
"input[type=text],input[type=password],input[type=number],input[type=time],select{"
"width:100%;padding:8px;margin:4px 0 12px;box-sizing:border-box;"
"background:#16213e;color:#e0e0e0;border:1px solid #444;border-radius:4px}"
"input[type=file]{width:100%;margin:4px 0 8px;color:#e0e0e0}"
"input[type=submit],button{background:#4ecca3;color:#1a1a2e;border:none;"
"padding:10px 20px;cursor:pointer;border-radius:4px;font-weight:bold;margin:4px 2px}"
"input[type=submit]:hover,button:hover{background:#3ba58a}"
"a.btn-sm{display:inline-block;background:#4ecca3;color:#1a1a2e;text-decoration:none;"
"border-radius:4px;font-weight:bold;margin:4px 2px}"
"a.btn-sm:hover{background:#3ba58a}"
".btn-danger{background:#e74c3c}.btn-danger:hover{background:#c0392b}"
".btn-warn{background:#e67e22}.btn-warn:hover{background:#ca6f1e}"
".btn-sm{padding:6px 12px;font-size:0.85em}"
".btn-disabled{display:inline-block;background:#555;color:#999;border-radius:4px;"
"font-weight:bold;margin:4px 2px;cursor:not-allowed}"
".section{background:#16213e;padding:15px;border-radius:8px;margin:15px 0}"
".status{display:inline-block;padding:3px 8px;border-radius:3px;font-size:0.85em}"
".online{background:#2ecc71;color:#000}.offline{background:#e74c3c;color:#fff}"
".disabled-cc{background:#888;color:#fff}.warn{background:#e67e22;color:#000}"
".badge{display:inline-block;padding:2px 8px;border-radius:3px;font-size:0.85em;"
"font-weight:bold;margin:0 4px}"
".badge-on{background:#2ecc71;color:#000}.badge-off{background:#555;color:#ccc}"
"label{font-weight:bold;display:block;margin-top:8px}"
"table{width:100%}td{padding:4px}td:first-child{width:120px}"
".row{display:flex;gap:8px;align-items:flex-end}"
".row>div{flex:1}"
".row>div>input{margin-bottom:0}"
".sched-card{background:#1e2a1e;padding:10px;border-radius:6px;margin:8px 0}"
".sched-card.disabled{opacity:.65}"
".sched-head{display:flex;align-items:center;justify-content:space-between;gap:8px}"
".cond-detail{margin:4px 0 10px 24px}"
".note{font-size:0.8em;color:#888;margin:4px 0}"
".fw-bar-wrap{background:#0d1526;border-radius:6px;height:10px;width:100%;"
"margin:8px 0;overflow:hidden}"
".fw-bar{height:100%;width:0;background:#4ecca3;border-radius:6px;"
"transition:width 0.4s;animation:fwpulse 1.5s ease-in-out infinite}"
"@keyframes fwpulse{0%,100%{opacity:1}50%{opacity:0.5}}"
"</style>"
"<script>"
"function scanWifi(){"
  "document.getElementById('scan-btn').disabled=true;"
  "document.getElementById('scan-btn').textContent='Scanning...';"
  "fetch('/scan').then(r=>r.json()).then(nets=>{"
    "var sel=document.getElementById('ssid-sel');"
    "sel.innerHTML='<option value=\"\">-- select network --</option>';"
    "nets.forEach(n=>{"
      "var o=document.createElement('option');"
      "o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+'dBm)';"
      "sel.appendChild(o);"
    "});"
    "document.getElementById('scan-btn').disabled=false;"
    "document.getElementById('scan-btn').textContent='Scan Again';"
  "}).catch(()=>{"
    "document.getElementById('scan-btn').disabled=false;"
    "document.getElementById('scan-btn').textContent='Scan';"
  "});}"
"function pickSsid(sel){"
  "if(sel.value)document.getElementById('ssid-inp').value=sel.value;}"
"function syncTime(){"
  "location.href='/savetime?epoch='+Math.floor(Date.now()/1000);}"
"function copyMac(){"
  "var el=document.getElementById('mac');"
  "if(!el)return;"
  "navigator.clipboard.writeText(el.textContent)"
  ".then(function(){"
    "var b=document.getElementById('mac-copy');"
    "if(b){b.textContent='Copied!';setTimeout(function(){b.textContent='Copy';},1500);}"
  "}).catch(function(){});}"
"function startFwUpload(){"
  "var f=document.getElementById('fw-file').files[0];"
  "if(!f){alert('Select a .bin file first');return;}"
  "if(!confirm('Flash '+f.name+'?\\nDevice will reboot after flashing.'))return;"
  "document.getElementById('fw-btn').disabled=true;"
  "document.getElementById('fw-prog').style.display='';"
  "var xhr=new XMLHttpRequest();"
  "xhr.open('POST','/ota_upload');"
  "xhr.setRequestHeader('Content-Type','application/octet-stream');"
  "xhr.upload.onprogress=function(e){"
    "if(e.lengthComputable){"
      "var pct=Math.floor(e.loaded/e.total*85);"
      "document.getElementById('fw-bar').style.width=pct+'%';"
      "document.getElementById('fw-kb').textContent="
        "Math.floor(e.loaded/1024)+' / '+Math.floor(e.total/1024)+' KB uploaded';"
    "}"
  "};"
  "xhr.onload=function(){"
    "if(xhr.status===200){"
      "document.getElementById('fw-bar').style.width='100%';"
      "document.getElementById('fw-bar').style.animation='none';"
      "document.getElementById('fw-kb').textContent='Flash complete! Rebooting...';"
      "setTimeout(fwWaitReboot,2000);"
    "}else{"
      "document.getElementById('fw-kb').textContent='Failed: '+xhr.responseText;"
      "document.getElementById('fw-btn').disabled=false;"
    "}"
  "};"
  "xhr.onerror=function(){"
    "document.getElementById('fw-kb').textContent='Rebooting — waiting for device...';"
    "fwWaitReboot();"
  "};"
  "xhr.send(f);"
"}"
"function fwWaitReboot(){"
  "var t=0;"
  "var iv=setInterval(function(){"
    "fetch('/').then(function(){clearInterval(iv);location.href='/';}).catch(function(){"
      "if(++t>30){clearInterval(iv);"
        "document.getElementById('fw-kb').textContent='Could not reconnect. Reload manually.';}"
    "});"
  "},2000);"
"}"
// Poll /status every 10s and update Status section DOM without page reload
"function fmt1(v){var n=Number(v);return Number.isFinite(n)?n.toFixed(1):'--';}"
"setInterval(function(){"
  "fetch('/status').then(function(r){return r.json();}).then(function(d){"
    "var e;"
    "e=document.getElementById('st-time');if(e)e.textContent=d.time||'';"
    "e=document.getElementById('st-sens');if(e&&d.temp!=null){"
      "e.textContent='Temp: '+fmt1(d.temp)+'°'+d.unit+' \\u00a0 Humidity: '+fmt1(d.rh)+'% \\u00a0 Light: '+d.light+'%';}"
    "['o1','o2','o3','o4'].forEach(function(k){"
      "e=document.getElementById('b-'+k);"
      "if(e){var on=d.relays&&d.relays[k];"
        "e.textContent=on?'ON':'off';"
        "e.className='badge '+(on?'badge-on':'badge-off');}});"
    "if(d.outlet_status){d.outlet_status.forEach(function(o){"
      "e=document.getElementById('sum-o'+o.id);"
      "if(e)e.textContent=o.summary?(' '+o.summary):'';});}"
  "}).catch(function(){});}"
",10000);"
// Toggle pump window time inputs when the window checkbox changes
"function pw(n,cb){"
  "var el=document.getElementById('s'+n+'_pw');"
  "if(el)el.style.display=cb.checked?'':'none';}"
"function condToggle(n,k,cb){"
  "var el=document.getElementById('s'+n+'_'+k+'_detail');"
  "if(el)el.style.display=cb.checked?'':'none';}"
"function schedToggle(n){"
  "var h=document.getElementById('s'+n+'_disabled');"
  "var b=document.getElementById('s'+n+'_body');"
  "var c=document.getElementById('s'+n+'_card');"
  "var btn=document.getElementById('s'+n+'_toggle');"
  "var note=document.getElementById('s'+n+'_disabled_note');"
  "if(!h||!b||!btn)return;"
  "var disabled=h.value!=='1';"
  "h.value=disabled?'1':'0';"
  "b.style.display=disabled?'none':'';"
  "if(note)note.style.display=disabled?'':'none';"
  "btn.textContent=disabled?'Enable':'Disable';"
  "if(c)c.className='sched-card'+(disabled?' disabled':'');"
  "return false;}"
"</script>"
"</head><body>"
"<h1>GrowHub Community Edition</h1>";

// ---------------------------------------------------------------------------
// URL-decode a query param value — returns true if key found (even if empty)
// ---------------------------------------------------------------------------
static bool get_param(const char *query, const char *key, char *val, size_t val_len)
{
    if (!query) return false;
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(query, search);
    if (!p) return false;
    p += strlen(search);
    size_t i = 0;
    while (*p && *p != '&' && i < val_len - 1) {
        if (*p == '+') {
            val[i++] = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            char hex[3] = {p[1], p[2], 0};
            val[i++] = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            val[i++] = *p;
        }
        p++;
    }
    val[i] = '\0';
    return true; // key was present (even if value is empty)
}

static bool appendf(char *buf, size_t len, int *n, const char *fmt, ...)
{
    if (!buf || !n || !fmt || *n < 0 || (size_t)*n >= len) return false;

    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(buf + *n, len - (size_t)*n, fmt, ap);
    va_end(ap);

    if (written < 0 || (size_t)written >= len - (size_t)*n) return false;
    *n += written;
    return true;
}

static esp_err_t read_save_params(httpd_req_t *req, char *buf, size_t len)
{
    if (!req || !buf || len == 0) return ESP_ERR_INVALID_ARG;

    if (req->method == HTTP_GET) {
        return httpd_req_get_url_query_str(req, buf, len);
    }

    if (req->method != HTTP_POST || req->content_len == 0 || req->content_len >= len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    size_t remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf + offset, remaining);
        if (r <= 0) return ESP_FAIL;
        offset += (size_t)r;
        remaining -= (size_t)r;
    }
    buf[offset] = '\0';
    return ESP_OK;
}

static void fmt_uptime(char *buf, size_t len)
{
    int64_t s = (int64_t)(esp_timer_get_time() / 1000000ULL);
    long d = (long)(s / 86400), h = (long)((s % 86400) / 3600), m = (long)((s % 3600) / 60);
    if (d > 0)      snprintf(buf, len, "%ldd %ldh %ldm", d, h, m);
    else if (h > 0) snprintf(buf, len, "%ldh %ldm", h, m);
    else            snprintf(buf, len, "%ldm", m);
}

static void fmt_time_12h(char *buf, size_t len, int time_src)
{
    time_t now; time(&now);
    if (now < 86400) {
        snprintf(buf, len, "--:--:-- (not synced)");
        return;
    }
    struct tm tm_info; localtime_r(&now, &tm_info);
    int h12 = tm_info.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    const char *ampm = tm_info.tm_hour < 12 ? "AM" : "PM";
    const char *src  = time_src == 1 ? "Manual" : "SNTP";
    snprintf(buf, len, "%d:%02d:%02d %s (%s)", h12, tm_info.tm_min, tm_info.tm_sec, ampm, src);
}

// Write a device-type <select> dropdown for a given outlet slot
static int write_outlet_select(char *buf, size_t len, int outlet_num, const char *current_name)
{
    int n = snprintf(buf, len, "<select name='outlet_%d'>", outlet_num);
    for (size_t i = 0; i < DEVICE_TYPE_COUNT; i++) {
        // DEVICE_TYPES[0] == "None" is the invariant; None is stored as "" in relay_names
        bool sel = (i == 0)
            ? (current_name == NULL || current_name[0] == '\0')
            : (current_name && strcmp(DEVICE_TYPES[i], current_name) == 0);
        n += snprintf(buf + n, len - (size_t)n,
            "<option value='%s'%s>%s</option>",
            i == 0 ? "" : DEVICE_TYPES[i],
            sel ? " selected" : "",
            DEVICE_TYPES[i]);
    }
    n += snprintf(buf + n, len - (size_t)n, "</select>");
    return n;
}

// Outlet display name: "Outlet N (Type)" or "Outlet N"
static void outlet_display_name(int outlet_num, const char *relay_name, char *out, size_t len)
{
    if (relay_name && relay_name[0]) {
        snprintf(out, len, "Outlet %d (%s)", outlet_num, relay_name);
    } else {
        snprintf(out, len, "Outlet %d", outlet_num);
    }
}

static const sched_condition_t *find_condition(const outlet_sched_t *sched,
                                               sched_condition_type_t type)
{
    if (!sched) return NULL;
    for (int i = 0; i < sched->condition_count; i++) {
        if (sched->conditions[i].type == type) return &sched->conditions[i];
    }
    return NULL;
}

static float ui_temp_from_c(float temp_c, const growhub_config_t *cfg)
{
    return cfg->temp_unit == 1 ? (temp_c * 9.0f / 5.0f + 32.0f) : temp_c;
}

static float ui_temp_to_c(float temp_ui, const growhub_config_t *cfg)
{
    return cfg->temp_unit == 1 ? ((temp_ui - 32.0f) * 5.0f / 9.0f) : temp_ui;
}

static bool add_condition_json(cJSON *arr, const sched_condition_t *cond)
{
    cJSON *item = cJSON_CreateObject();
    if (!item) return false;

    switch (cond->type) {
    case SCHED_COND_ALWAYS_ON:
        cJSON_AddStringToObject(item, "type", "always_on");
        break;
    case SCHED_COND_TIME_WINDOW:
        cJSON_AddStringToObject(item, "type", "time_window");
        cJSON_AddStringToObject(item, "start", cond->start);
        cJSON_AddStringToObject(item, "end", cond->end);
        break;
    case SCHED_COND_RH_LOW_BAND:
        cJSON_AddStringToObject(item, "type", "rh_low_band");
        cJSON_AddNumberToObject(item, "low", cond->low);
        cJSON_AddNumberToObject(item, "high", cond->high);
        break;
    case SCHED_COND_RH_HIGH_BAND:
        cJSON_AddStringToObject(item, "type", "rh_high_band");
        cJSON_AddNumberToObject(item, "low", cond->low);
        cJSON_AddNumberToObject(item, "high", cond->high);
        break;
    case SCHED_COND_TEMP_LOW_BAND_C:
        cJSON_AddStringToObject(item, "type", "temp_low_band_c");
        cJSON_AddNumberToObject(item, "low_c", cond->low);
        cJSON_AddNumberToObject(item, "high_c", cond->high);
        break;
    case SCHED_COND_TEMP_HIGH_BAND_C:
        cJSON_AddStringToObject(item, "type", "temp_high_band_c");
        cJSON_AddNumberToObject(item, "low_c", cond->low);
        cJSON_AddNumberToObject(item, "high_c", cond->high);
        break;
    case SCHED_COND_INTERVAL:
        cJSON_AddStringToObject(item, "type", "interval");
        cJSON_AddNumberToObject(item, "run_mins", cond->run_mins);
        cJSON_AddNumberToObject(item, "every_hrs", cond->every_hrs);
        if (cond->has_window) {
            cJSON *window = cJSON_CreateObject();
            if (!window) {
                cJSON_Delete(item);
                return false;
            }
            cJSON_AddStringToObject(window, "start", cond->window_start);
            cJSON_AddStringToObject(window, "end", cond->window_end);
            cJSON_AddItemToObject(item, "window", window);
        }
        break;
    default:
        cJSON_Delete(item);
        return false;
    }

    cJSON_AddItemToArray(arr, item);
    return true;
}

static bool remove_schedule_entries_for_outlets(uint8_t outlet_mask)
{
    outlet_sched_t outlets[MAX_OUTLET_SCHEDS] = {0};
    int count = schedule_get_outlets(outlets, MAX_OUTLET_SCHEDS);
    if (count <= 0 || outlet_mask == 0) return false;

    bool removed = false;
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (outlets[i].id >= 1 && outlets[i].id <= 4 &&
            (outlet_mask & (1 << (outlets[i].id - 1)))) {
            removed = true;
        } else {
            kept++;
        }
    }
    if (!removed) return false;

    if (kept == 0) {
        schedule_clear();
        config_clear_schedule();
        return true;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) {
        cJSON_Delete(root);
        cJSON_Delete(arr);
        schedule_clear();
        config_clear_schedule();
        return true;
    }
    cJSON_AddNumberToObject(root, "v", 3);
    cJSON_AddItemToObject(root, "outlets", arr);

    bool ok = true;
    for (int i = 0; i < count && ok; i++) {
        if (outlets[i].id >= 1 && outlets[i].id <= 4 &&
            (outlet_mask & (1 << (outlets[i].id - 1)))) {
            continue;
        }

        cJSON *item = cJSON_CreateObject();
        cJSON *conditions = cJSON_CreateArray();
        if (!item || !conditions) {
            cJSON_Delete(item);
            cJSON_Delete(conditions);
            ok = false;
            break;
        }
        cJSON_AddNumberToObject(item, "id", outlets[i].id);
        cJSON_AddItemToObject(item, "conditions", conditions);

        for (int j = 0; j < outlets[i].condition_count; j++) {
            if (!add_condition_json(conditions, &outlets[i].conditions[j])) {
                ok = false;
                break;
            }
        }
        cJSON_AddItemToArray(arr, item);
    }

    char *payload = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!payload) {
        schedule_clear();
        config_clear_schedule();
        return true;
    }

    if (schedule_load(payload, (int)strlen(payload))) {
        config_save_schedule(payload);
    } else {
        ESP_LOGW(TAG, "Schedule rebuild after assignment change failed: %s (outlet %d)",
                 schedule_last_error_reason(), schedule_last_error_outlet());
        schedule_clear();
        config_clear_schedule();
    }
    free(payload);
    return true;
}

// ---------------------------------------------------------------------------
// GET / -- main config page
// ---------------------------------------------------------------------------
static esp_err_t root_handler(httpd_req_t *req)
{
    const growhub_config_t *cfg = config_get();
    sensor_reading_t s = sensors_read();
    uint8_t relay_mask = relays_get_bitmask();
    bool wifi_on = wifi_is_connected();
    bool mqtt_on = mqtt_is_connected();
    bool wifi_recovery = wifi_is_in_recovery_mode();
    bool cc_disabled = cfg->mqtt_disabled && cfg->mqtt_host[0];
    relay_mode_t mode = relays_get_mode();

    // outlet_slot[N-1] maps display Outlet N to relay_names[] index:
    // Outlet1->slot3, Outlet2->slot0, Outlet3->slot1, Outlet4->slot2
    const int outlet_slot[4] = {3, 0, 1, 2};

    // Relay bit -> outlet number mapping:
    // bit3=Outlet1, bit0=Outlet2, bit1=Outlet3, bit2=Outlet4
    int o1_on = (relay_mask >> 3) & 1;
    int o2_on = (relay_mask >> 0) & 1;
    int o3_on = (relay_mask >> 1) & 1;
    int o4_on = (relay_mask >> 2) & 1;

    // relay_names slots: [0]=bit0=Outlet2, [1]=bit1=Outlet3, [2]=bit2=Outlet4, [3]=bit3=Outlet1
    const char *name_o1 = cfg->relay_names[3];
    const char *name_o2 = cfg->relay_names[0];
    const char *name_o3 = cfg->relay_names[1];
    const char *name_o4 = cfg->relay_names[2];

    // Temperature display
    float temp_display = s.temp_valid ? s.temperature : 0.0f;
    char temp_unit_char = 'C';
    if (cfg->temp_unit == 1) {
        temp_display = temp_display * 9.0f / 5.0f + 32.0f;
        temp_unit_char = 'F';
    }

    // Current device time
    char time_str[32];
    fmt_time_12h(time_str, sizeof(time_str), cfg->time_src);

    // WiFi RSSI (only valid when connected)
    int rssi = 0;
    if (wifi_on) esp_wifi_sta_get_rssi(&rssi);

    char uptime_str[24];
    fmt_uptime(uptime_str, sizeof(uptime_str));

    bool time_blocked = schedule_time_sync_required();
    bool sntp_unhealthy = time_sync_sntp_unhealthy();
    bool sensor_blocked = schedule_sensor_data_unavailable();
    const char *time_warning = time_blocked
        ? "Time sync required; wall-clock automation is paused."
        : (sntp_unhealthy ? "SNTP sync is unavailable; device time may drift." : "");
    const char *sensor_warning = sensor_blocked
        ? "Sensor data unavailable; temp/rH automation is paused."
        : "";

    // Schedule data — fetched once and shared by the schedule section below.
    outlet_sched_t os_st[4];
    int os_st_count = schedule_get_outlets(os_st, 4);

    // Send static header first
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE_HEADER, HTTPD_RESP_USE_STRLEN);

    // Device name: inject title update + subtitle when set
    if (cfg->device_name[0]) {
        char inject[256];
        int ij = snprintf(inject, sizeof(inject),
            "<script>document.title='%s | GrowHub'</script>"
            "<p style='color:#888;margin:-12px 0 10px;font-size:0.95em'>%s</p>",
            cfg->device_name, cfg->device_name);
        httpd_resp_send_chunk(req, inject, ij);
    }

    // Dynamic body
    char *buf = malloc(BUF_SIZE);
    if (!buf) { httpd_resp_send_500(req); return ESP_FAIL; }
    int n = 0;

    // Firmware / MAC line with Copy button
    n += snprintf(buf + n, BUF_SIZE - n,
        "<p style='color:#888'>Firmware: " GROWHUB_VERSION
        " | MAC: <span id='mac'>%s</span>"
        " <button id='mac-copy' class='btn-sm' onclick='copyMac()'"
        " style='font-size:0.75em;padding:2px 7px'>Copy</button></p>",
        config_get_mac_str());

    // --- Status section ---
    char o1_disp[32], o2_disp[32], o3_disp[32], o4_disp[32];
    outlet_display_name(1, name_o1, o1_disp, sizeof(o1_disp));
    outlet_display_name(2, name_o2, o2_disp, sizeof(o2_disp));
    outlet_display_name(3, name_o3, o3_disp, sizeof(o3_disp));
    outlet_display_name(4, name_o4, o4_disp, sizeof(o4_disp));

    // CC status: disabled > connected > offline
    const char *cc_class = cc_disabled ? "disabled-cc" : (mqtt_on ? "online" : "offline");
    const char *cc_label = cc_disabled ? "Disabled" : (mqtt_on ? "Connected" : "Not connected");

    // WiFi line: show RSSI when connected
    char wifi_rssi_str[16] = "";
    if (wifi_on && rssi != 0)
        snprintf(wifi_rssi_str, sizeof(wifi_rssi_str), " (%d dBm)", rssi);

    n += snprintf(buf + n, BUF_SIZE - n,
        "<div class='section'><h2>Status</h2>"
        "<p>WiFi: <span class='status %s'>%s</span>%s%s &nbsp; "
        "Command Center: <span class='status %s'>%s</span>%s</p>"
        "<p><span id='st-sens'>Temp: %.1f&deg;%c &nbsp; Humidity: %.1f%% &nbsp; Light: %d%%</span></p>"
        "<p>Time: <strong id='st-time'>%s</strong> &nbsp; Up: %s</p>"
        "<p>Schedule: <strong>%s</strong> &nbsp; Mode: <strong>%s</strong></p>",
        wifi_on ? "online" : "offline", wifi_on ? "Connected" : "Not connected",
        wifi_rssi_str,
        wifi_recovery ? " <span class='status warn'>WiFi Recovery</span>" : "",
        cc_class, cc_label,
        mqtt_on ? " <span class='status online'>Mirroring</span>" : "",
        temp_display, temp_unit_char,
        s.temp_valid ? s.humidity : 0.0f, s.light,
        time_str, uptime_str,
        schedule_is_active() ? "Active" : "None",
        mode == RELAY_MODE_MANUAL ? "MANUAL" : "AUTO");

    if (time_warning[0]) {
        if (time_blocked) {
            n += snprintf(buf + n, BUF_SIZE - n,
                "<p class='note' style='color:#e67e22'>%s "
                "<button type='button' class='btn-sm' onclick='syncTime()'>Sync Time</button> "
                "<a href='#device-time'>Time settings</a></p>",
                time_warning);
        } else {
            n += snprintf(buf + n, BUF_SIZE - n,
                "<p class='note' style='color:#e67e22'>%s "
                "<a href='#device-time'>Time settings</a></p>",
                time_warning);
        }
    }
    if (sensor_warning[0]) {
        n += snprintf(buf + n, BUF_SIZE - n,
            "<p class='note' style='color:#e67e22'>%s</p>",
            sensor_warning);
    }

    // Per-outlet status rows with firmware-owned schedule summary text.
    const struct { int num; int on; const char *disp; const char *bid; } ost[4] = {
        {1, o1_on, o1_disp, "b-o1"},
        {2, o2_on, o2_disp, "b-o2"},
        {3, o3_on, o3_disp, "b-o3"},
        {4, o4_on, o4_disp, "b-o4"},
    };
    for (int i = 0; i < 4; i++) {
        n += snprintf(buf + n, BUF_SIZE - n,
            "<p>%s: <span id='%s' class='badge %s'>%s</span>",
            ost[i].disp, ost[i].bid,
            ost[i].on ? "badge-on" : "badge-off",
            ost[i].on ? "ON" : "off");
        const char *summary = schedule_get_outlet_summary(ost[i].num);
        n += snprintf(buf + n, BUF_SIZE - n,
            " <small id='sum-o%d' style='color:#888'>%s</small>",
            ost[i].num, summary);
        n += snprintf(buf + n, BUF_SIZE - n, "</p>");
    }
    n += snprintf(buf + n, BUF_SIZE - n, "</div>");

    // --- Mode control ---
    n += snprintf(buf + n, BUF_SIZE - n, "<div class='section'><h2>Control Mode</h2>");
    if (mode == RELAY_MODE_AUTO) {
        n += snprintf(buf + n, BUF_SIZE - n,
            "<p>Currently in <strong>AUTO</strong> mode -- schedule controls outlets.</p>"
            "<form method='GET' action='/save'>"
            "<input type='hidden' name='mode' value='manual'>"
            "<button type='submit' class='btn-warn'>Switch to Manual</button>"
            "</form>");
    } else {
        n += snprintf(buf + n, BUF_SIZE - n,
            "<p>Currently in <strong>MANUAL</strong> mode -- you control each outlet.</p>"
            "<form method='GET' action='/save'>"
            "<input type='hidden' name='mode' value='auto'>"
            "<button type='submit'>Switch to Auto</button>"
            "</form><br>");
        // Per-outlet ON/OFF buttons
        // Outlet 1 = bit3, Outlet 2 = bit0, Outlet 3 = bit1, Outlet 4 = bit2
        struct { int outlet_num; int bit; int on; const char *name; } outlets[4] = {
            {1, 3, o1_on, name_o1},
            {2, 0, o2_on, name_o2},
            {3, 1, o3_on, name_o3},
            {4, 2, o4_on, name_o4},
        };
        for (int i = 0; i < 4; i++) {
            char disp[32];
            outlet_display_name(outlets[i].outlet_num, outlets[i].name, disp, sizeof(disp));
            uint8_t mask_on  = relay_mask | (1 << outlets[i].bit);
            uint8_t mask_off = relay_mask & ~(1 << outlets[i].bit);
            n += snprintf(buf + n, BUF_SIZE - n,
                "<strong>%s:</strong> "
                "<form style='display:inline' method='GET' action='/save'>"
                "<input type='hidden' name='relay_mask' value='%d'>"
                "<button type='submit' class='btn-sm %s'>ON</button></form> "
                "<form style='display:inline' method='GET' action='/save'>"
                "<input type='hidden' name='relay_mask' value='%d'>"
                "<button type='submit' class='btn-sm btn-danger'>OFF</button></form><br>",
                disp,
                mask_on, outlets[i].on ? "btn-warn" : "",
                mask_off);
        }
    }
    n += snprintf(buf + n, BUF_SIZE - n, "</div>");

    // --- Schedule section (V3 outlet-condition model) ---
    {
        n += snprintf(buf + n, BUF_SIZE - n,
            "<div class='section'><h2>Schedule</h2>"
            "<p class='note'>Set schedule per outlet. "
            "Changes here and in Command Center mirror the active device schedule.</p>");

        n += snprintf(buf + n, BUF_SIZE - n,
            "<form method='POST' action='/save'>"
            "<input type='hidden' name='sched_v3' value='1'>");

        int sched_outlets = 0;
        for (int on = 1; on <= 4; on++) {
            int slot = outlet_slot[on - 1];
            const char *dev = cfg->relay_names[slot];
            if (!dev || !dev[0]) continue;  // skip unassigned
            sched_outlets++;

            outlet_sched_t *cur = NULL;
            for (int j = 0; j < os_st_count; j++) {
                if (os_st[j].id == on) { cur = &os_st[j]; break; }
            }

            bool is_pump     = strcmp(dev, "Water Pump") == 0;
            bool is_fan      = strcmp(dev, "Fan") == 0;
            bool is_humid    = strcmp(dev, "Humidifier") == 0;
            bool is_dehumid  = strcmp(dev, "Dehumidifier") == 0;
            bool is_heater   = strcmp(dev, "Heater") == 0;
            bool is_ac       = strcmp(dev, "AC Controller") == 0;
            const sched_condition_t *always = find_condition(cur, SCHED_COND_ALWAYS_ON);
            const sched_condition_t *timew = find_condition(cur, SCHED_COND_TIME_WINDOW);
            const sched_condition_t *rh_low = find_condition(cur, SCHED_COND_RH_LOW_BAND);
            const sched_condition_t *rh_high = find_condition(cur, SCHED_COND_RH_HIGH_BAND);
            const sched_condition_t *temp_low = find_condition(cur, SCHED_COND_TEMP_LOW_BAND_C);
            const sched_condition_t *temp_high = find_condition(cur, SCHED_COND_TEMP_HIGH_BAND_C);
            const sched_condition_t *interval = find_condition(cur, SCHED_COND_INTERVAL);
            bool has_saved_rules = cur && cur->condition_count > 0;
            bool disabled = has_saved_rules &&
                            ((cfg->schedule_disabled_mask & (1 << (on - 1))) != 0);
            bool time_checked = timew != NULL;
            bool rh_checked = rh_low || rh_high;
            bool temp_checked = temp_low || temp_high;
            bool interval_checked = interval != NULL;

            n += snprintf(buf + n, BUF_SIZE - n,
                "<div id='s%d_card' class='sched-card%s'>"
                "<div class='sched-head'><strong>Outlet %d (%s)</strong>",
                on, disabled ? " disabled" : "",
                on, dev);
            if (has_saved_rules) {
                n += snprintf(buf + n, BUF_SIZE - n,
                    "<button type='button' id='s%d_toggle' class='btn-sm' onclick='return schedToggle(%d)'>"
                    "%s</button>",
                    on, on,
                    disabled ? "Enable" : "Disable");
            }
            n += snprintf(buf + n, BUF_SIZE - n,
                "</div>"
                "<input type='hidden' id='s%d_disabled' name='s%d_disabled' value='%d'>",
                on, on, disabled ? 1 : 0);

            n += snprintf(buf + n, BUF_SIZE - n,
                "<div id='s%d_body' style='display:%s'>",
                on, disabled ? "none" : "");

            if (is_fan || strcmp(dev, "Light") == 0) {
                n += snprintf(buf + n, BUF_SIZE - n,
                    "<label style='font-weight:normal'>"
                    "<input type='checkbox' name='s%d_always' value='1' %s> Always on</label>",
                    on, always ? "checked" : "");
            }

            if (strcmp(dev, "Light") == 0 || is_fan) {
                const char *start = timew ? timew->start : "06:00";
                const char *end = timew ? timew->end : "22:00";
                n += snprintf(buf + n, BUF_SIZE - n,
                    "<div style='margin-top:8px'>"
                    "<label style='font-weight:normal'>"
                    "<input type='checkbox' name='s%d_time_en' value='1' onchange='condToggle(%d,\"time\",this)' %s> "
                    "Time control</label>"
                    "<div id='s%d_time_detail' class='cond-detail' style='display:%s'>"
                    "<small>On at <input type='time' name='s%d_start' value='%s' style='width:auto'>"
                    " &nbsp; off at <input type='time' name='s%d_end' value='%s' style='width:auto'></small>"
                    "</div></div>",
                    on, on, time_checked ? "checked" : "",
                    on, time_checked ? "" : "none",
                    on, start,
                    on, end);
            }

            if (is_fan || is_humid || is_dehumid) {
                const sched_condition_t *rh = is_humid ? rh_low : rh_high;
                float def_low = is_humid ? 50.0f : 55.0f;
                float def_high = is_humid ? 60.0f : 65.0f;
                float low = rh ? rh->low : def_low;
                float high = rh ? rh->high : def_high;
                n += snprintf(buf + n, BUF_SIZE - n,
                    "<div style='margin-top:8px'>"
                    "<label style='font-weight:normal'>"
                    "<input type='checkbox' name='s%d_rh_en' value='1' onchange='condToggle(%d,\"rh\",this)' %s> "
                    "Humidity control</label><br>"
                    "<div id='s%d_rh_detail' class='cond-detail' style='display:%s'>",
                    on, on, rh_checked ? "checked" : "",
                    on, rh_checked ? "" : "none");
                if (is_humid) {
                    n += snprintf(buf + n, BUF_SIZE - n,
                        "<small>On below <input type='number' name='s%d_rh_low' min='10' max='95' "
                        "value='%.0f' style='width:70px'> %% &nbsp; "
                        "off at <input type='number' name='s%d_rh_high' min='10' max='95' "
                        "value='%.0f' style='width:70px'> %%</small>",
                        on, low,
                        on, high);
                } else {
                    n += snprintf(buf + n, BUF_SIZE - n,
                        "<small>On above <input type='number' name='s%d_rh_high' min='10' max='95' "
                        "value='%.0f' style='width:70px'> %% &nbsp; "
                        "off at <input type='number' name='s%d_rh_low' min='10' max='95' "
                        "value='%.0f' style='width:70px'> %%</small>",
                        on, high,
                        on, low);
                }
                n += snprintf(buf + n, BUF_SIZE - n, "</div></div>");
            }

            if (is_fan || is_heater || is_ac) {
                const sched_condition_t *tc = is_heater ? temp_low : temp_high;
                float def_low_c = is_heater ? 18.0f : 24.0f;
                float def_high_c = is_heater ? 21.0f : 27.0f;
                float low = ui_temp_from_c(tc ? tc->low : def_low_c, cfg);
                float high = ui_temp_from_c(tc ? tc->high : def_high_c, cfg);
                const char *unit_label = cfg->temp_unit == 1 ? "F" : "C";
                n += snprintf(buf + n, BUF_SIZE - n,
                    "<div style='margin-top:8px'>"
                    "<label style='font-weight:normal'>"
                    "<input type='checkbox' name='s%d_temp_en' value='1' onchange='condToggle(%d,\"temp\",this)' %s> "
                    "Temperature control</label><br>"
                    "<div id='s%d_temp_detail' class='cond-detail' style='display:%s'>",
                    on, on, temp_checked ? "checked" : "",
                    on, temp_checked ? "" : "none");
                if (is_heater) {
                    n += snprintf(buf + n, BUF_SIZE - n,
                        "<small>On below <input type='number' step='0.1' name='s%d_temp_low' "
                        "value='%.1f' style='width:80px'> %s &nbsp; "
                        "off at <input type='number' step='0.1' name='s%d_temp_high' "
                        "value='%.1f' style='width:80px'> %s</small>",
                        on, low, unit_label,
                        on, high, unit_label);
                } else {
                    n += snprintf(buf + n, BUF_SIZE - n,
                        "<small>On above <input type='number' step='0.1' name='s%d_temp_high' "
                        "value='%.1f' style='width:80px'> %s &nbsp; "
                        "off at <input type='number' step='0.1' name='s%d_temp_low' "
                        "value='%.1f' style='width:80px'> %s</small>",
                        on, high, unit_label,
                        on, low, unit_label);
                }
                n += snprintf(buf + n, BUF_SIZE - n, "</div></div>");
            }

            if (is_pump) {
                int run = interval ? interval->run_mins : 15;
                int every = interval ? interval->every_hrs : 4;
                bool has_win = interval && interval->has_window;
                const char *wstart = has_win ? interval->window_start : "06:00";
                const char *wend = has_win ? interval->window_end : "22:00";

                int next_secs = schedule_pump_next_run_secs(on);
                bool pump_running = schedule_pump_is_running(on);
                bool run_now_allowed = schedule_pump_run_now_allowed(on);
                char next_str[80] = "";
                if (pump_running) {
                    snprintf(next_str, sizeof(next_str), " &nbsp;<small style='color:#2ecc71'>Running now</small>");
                } else if (next_secs > 0) {
                    int nh = next_secs / 3600, nm = (next_secs % 3600) / 60;
                    if (nh > 0)
                        snprintf(next_str, sizeof(next_str),
                            " &nbsp;<small style='color:#888'>Next run in %dh %dm</small>", nh, nm);
                    else
                        snprintf(next_str, sizeof(next_str),
                            " &nbsp;<small style='color:#888'>Next run in %dm</small>", nm);
                }

                n += snprintf(buf + n, BUF_SIZE - n,
                    "<div style='margin-top:8px'>"
                    "<label style='font-weight:normal'>"
                    "<input type='checkbox' name='s%d_interval_en' value='1' onchange='condToggle(%d,\"interval\",this)' %s> "
                    "Watering control</label>"
                    "<div id='s%d_interval_detail' class='cond-detail' style='display:%s'>"
                    "<small>Every <input type='number' name='s%d_every' min='1' max='24' value='%d' "
                    "style='width:60px'> hrs, run "
                    "<input type='number' name='s%d_run' min='1' max='60' value='%d' "
                    "style='width:60px'> min</small>%s<br>"
                    "<label style='font-weight:normal;margin-top:6px'>"
                    "<input type='checkbox' name='s%d_win_en' value='1' onchange='pw(%d,this)' %s>"
                    " Allowed hours</label>"
                    "<div id='s%d_pw' style='display:%s;margin-top:4px'>"
                    "<small>From <input type='time' name='s%d_wstart' value='%s' style='width:auto'>"
                    " to <input type='time' name='s%d_wend' value='%s' style='width:auto'></small>"
                    "</div>"
                    "</div>"
                    "</div>",
                    on, on, interval_checked ? "checked" : "",
                    on, interval_checked ? "" : "none",
                    on, every, on, run, next_str,
                    on, on, has_win ? "checked" : "",
                    on, has_win ? "" : "none",
                    on, wstart, on, wend);

                if (interval) {
                    if (pump_running) {
                        n += snprintf(buf + n, BUF_SIZE - n,
                            "<span class='btn-sm btn-disabled'>Running</span>");
                    } else if (mode == RELAY_MODE_AUTO && !disabled && run_now_allowed) {
                        n += snprintf(buf + n, BUF_SIZE - n,
                            "<a href='/save?pump_run_now=%d' class='btn-sm'>Run Now</a>",
                            on);
                    } else {
                        n += snprintf(buf + n, BUF_SIZE - n,
                            "<span class='btn-sm btn-disabled'>Run Now</span>");
                    }
                }
            }

            n += snprintf(buf + n, BUF_SIZE - n,
                "</div>"
                "<div id='s%d_disabled_note' class='note' style='display:%s'>"
                "Automation disabled</div>"
                "</div>",
                on, disabled ? "" : "none");
        }

        if (sched_outlets == 0) {
            n += snprintf(buf + n, BUF_SIZE - n,
                "<p class='note'>No outlets assigned. Assign devices in Power Outlets below first.</p>");
        }

        n += snprintf(buf + n, BUF_SIZE - n,
            "<br><input type='submit' value='Save Schedule'>"
            "</form>"
            "<form method='GET' action='/save' style='display:inline'>"
            "<input type='hidden' name='sched_clear' value='1'>"
            "<button type='submit' class='btn-danger btn-sm' style='margin-top:8px'>"
            "Clear Schedule</button>"
            "</form></div>");
    }

    // --- WiFi section ---
    n += snprintf(buf + n, BUF_SIZE - n,
        "<div class='section'><h2>WiFi</h2>"
        "<button onclick='scanWifi()' id='scan-btn'>Scan for Networks</button>"
        "<select id='ssid-sel' onchange='pickSsid(this)' style='margin-top:8px'>"
        "<option value=''>-- scan first --</option></select>"
        "<form method='GET' action='/save'>"
        "<label>Network name (SSID)</label>"
        "<input type='text' id='ssid-inp' name='ssid' value='%s'>"
        "<label>Password</label>"
        "<input type='password' name='password' value=''>"
        "<input type='submit' value='Save &amp; Connect'>"
        "</form>",
        cfg->sta_ssid);

    if (cfg->sta_ssid[0]) {
        n += snprintf(buf + n, BUF_SIZE - n,
            "<form method='GET' action='/save' style='margin-top:8px'>"
            "<input type='hidden' name='wifi_forget' value='1'>"
            "<button type='submit' class='btn-danger btn-sm' "
            "onclick=\"return confirm('Forget WiFi credentials and stay in AP mode?')\">"
            "Forget WiFi</button>"
            "</form>");
    }
    n += snprintf(buf + n, BUF_SIZE - n, "</div>");

    // --- Command Center section ---
    n += snprintf(buf + n, BUF_SIZE - n,
        "<div class='section'><h2>Command Center</h2>"
        "<form method='GET' action='/save'>"
        "<label>Server address</label><input type='text' name='mqtt_host' value='%s'>"
        "<label>Port</label><input type='number' name='mqtt_port' value='%d'>"
        "<input type='submit' value='Save'>"
        "</form>",
        cfg->mqtt_host, cfg->mqtt_port);

    if (cc_disabled) {
        n += snprintf(buf + n, BUF_SIZE - n,
            "<p class='note' style='color:#e67e22'>Command Center is disabled. "
            "Save the server address above or click Reconnect to resume.</p>"
            "<form method='GET' action='/save' style='margin-top:4px'>"
            "<input type='hidden' name='cc_reconnect' value='1'>"
            "<button type='submit'>Reconnect CC</button>"
            "</form>");
    } else if (cfg->mqtt_host[0]) {
        n += snprintf(buf + n, BUF_SIZE - n,
            "<form method='GET' action='/save' style='margin-top:8px'>"
            "<input type='hidden' name='cc_disconnect' value='1'>"
            "<button type='submit' class='btn-warn btn-sm'>Disconnect CC</button>"
            "</form>");
    }
    n += snprintf(buf + n, BUF_SIZE - n, "</div>");

    // --- Device section ---
    n += snprintf(buf + n, BUF_SIZE - n,
        "<div class='section' id='device-time'><h2>Device</h2>"
        "<form method='GET' action='/save'>"
        "<label>Name</label><input type='text' name='dev_name' value='%s'>"
        "<label>Timezone</label><select name='timezone'>",
        cfg->device_name);
    for (size_t i = 0; i < TZ_COUNT; i++) {
        bool sel = strcmp(TZ_TABLE[i].posix, cfg->timezone) == 0;
        n += snprintf(buf + n, BUF_SIZE - n,
            "<option value='%s'%s>%s</option>",
            TZ_TABLE[i].posix, sel ? " selected" : "", TZ_TABLE[i].label);
    }
    n += snprintf(buf + n, BUF_SIZE - n,
        "</select>"
        "<label>Temperature unit</label>"
        "<label style='font-weight:normal'>"
        "<input type='radio' name='temp_unit' value='0' %s> Celsius (&deg;C)</label>&nbsp;"
        "<label style='font-weight:normal'>"
        "<input type='radio' name='temp_unit' value='1' %s> Fahrenheit (&deg;F)</label>"
        "<label>Time source</label>"
        "<label style='font-weight:normal'>"
        "<input type='radio' name='time_src' value='0' %s> SNTP (syncs when WiFi connected)</label>&nbsp;"
        "<label style='font-weight:normal'>"
        "<input type='radio' name='time_src' value='1' %s> Manual (set from browser)</label>"
        "<label>SNTP primary server</label>"
        "<input type='text' name='sntp_primary' value='%s' placeholder='" DEFAULT_SNTP_PRIMARY "'>"
        "<label>SNTP secondary server</label>"
        "<input type='text' name='sntp_secondary' value='%s' placeholder='" DEFAULT_SNTP_SECONDARY "'>"
        "<br><input type='submit' value='Save'>"
        "</form>",
        cfg->temp_unit == 0 ? "checked" : "",
        cfg->temp_unit == 1 ? "checked" : "",
        cfg->time_src == 0 ? "checked" : "",
        cfg->time_src == 1 ? "checked" : "",
        cfg->sntp_primary,
        cfg->sntp_secondary);

    if (cfg->time_src == 1) {
        n += snprintf(buf + n, BUF_SIZE - n,
            "<br><button onclick='syncTime()' style='margin-top:8px'>"
            "Sync Time from Browser</button>");
    }
    n += snprintf(buf + n, BUF_SIZE - n, "</div>");

    // --- Power Outlets section ---
    n += snprintf(buf + n, BUF_SIZE - n,
        "<div class='section'><h2>Power Outlets</h2>"
        "<p class='note'>Assign what's plugged into each outlet so the app can label them correctly.</p>");
    n += snprintf(buf + n, BUF_SIZE - n,
        "<form method='GET' action='/save'><table>");
    for (int on = 1; on <= 4; on++) {
        int slot = outlet_slot[on - 1];
        n += snprintf(buf + n, BUF_SIZE - n, "<tr><td><label>Outlet %d</label></td><td>", on);
        n += write_outlet_select(buf + n, BUF_SIZE - n, on, cfg->relay_names[slot]);
        n += snprintf(buf + n, BUF_SIZE - n, "</td></tr>");
    }
    n += snprintf(buf + n, BUF_SIZE - n,
        "</table><input type='submit' value='Save'></form></div>");

    // Flush accumulated body before tail sections so we don't truncate
    // when total dynamic content (schedule, outlet names, etc.) grows past BUF_SIZE
    if (n > BUF_SIZE) n = BUF_SIZE;
    httpd_resp_send_chunk(req, buf, n);
    n = 0;

    // --- Firmware Update section ---
    n += snprintf(buf + n, BUF_SIZE - n,
        "<div class='section'><h2>Firmware Update</h2>"
        "<label>Upload .bin file</label>"
        "<input type='file' id='fw-file' accept='.bin'>"
        "<div id='fw-prog' style='display:none'>"
        "<div class='fw-bar-wrap'><div class='fw-bar' id='fw-bar'></div></div>"
        "<p class='note' id='fw-kb'></p>"
        "</div>"
        "<button id='fw-btn' onclick='startFwUpload()'>Flash from file</button>"
        "<br><br>"
        "<label>From URL</label>"
        "<form method='GET' action='/save'>"
        "<input type='text' name='ota_url' placeholder='https://example.com/firmware.bin'>"
        "<input type='submit' value='Flash from URL'>"
        "</form></div>");


    // --- Actions ---
    n += snprintf(buf + n, BUF_SIZE - n,
        "<div class='section'><h2>Actions</h2>"
        "<form method='GET' action='/save' style='display:inline'>"
        "<input type='hidden' name='reboot' value='1'>"
        "<button type='submit'>Reboot</button></form> "
        "<form method='GET' action='/save' style='display:inline'>"
        "<input type='hidden' name='reset' value='1'>"
        "<button type='submit' class='btn-danger'"
        " onclick=\"return confirm('Erase ALL settings and reboot into setup mode?')\">Factory Reset</button>"
        "</form></div>"
        "</body></html>");

    if (n > BUF_SIZE) n = BUF_SIZE;
    httpd_resp_send_chunk(req, buf, n);
    httpd_resp_send_chunk(req, NULL, 0); // end chunked response
    free(buf);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /save -- handle all form submissions
// ---------------------------------------------------------------------------
static esp_err_t save_handler(httpd_req_t *req)
{
    char *query = calloc(1, SAVE_PARAM_MAX);
    char val[128] = {0};
    bool need_reboot = false;

    if (!query) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_err_t param_err = read_save_params(req, query, SAVE_PARAM_MAX);
    if (param_err != ESP_OK) {
        free(query);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No parameters");
        return ESP_FAIL;
    }

    // --- Mode toggle ---
    if (get_param(query, "mode", val, sizeof(val))) {
        if (strcmp(val, "manual") == 0) {
            relays_set_mode(RELAY_MODE_MANUAL);
            mqtt_publish_schedule_state("local");
        } else if (strcmp(val, "auto") == 0) {
            relays_set_mode(RELAY_MODE_AUTO);
            schedule_evaluate_now();
            mqtt_publish_schedule_state("local");
        }
    }

    // --- Manual relay bitmask ---
    if (get_param(query, "relay_mask", val, sizeof(val))) {
        int mask = atoi(val);
        if (relays_get_mode() == RELAY_MODE_MANUAL && mask >= 0 && mask <= 15) {
            relays_set_bitmask((uint8_t)mask);
            mqtt_publish_schedule_state("local");
        }
    }

    // --- Schedule (V3 outlet-condition model) ---
    if (get_param(query, "sched_v3", val, sizeof(val))) {
        char *sched = calloc(1, SCHEDULE_JSON_MAX);
        int sn = 0;
        bool first_outlet = true;
        bool any_sched = false;
        bool sched_overflow = false;
        uint8_t disabled_mask = 0;
        const int outlet_slot[4] = {3, 0, 1, 2};
        const growhub_config_t *cfg_now = config_get();

        if (!sched) {
            free(query);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        #define SCHED_APPEND(...) do { \
            if (!sched_overflow && !appendf(sched, SCHEDULE_JSON_MAX, &sn, __VA_ARGS__)) { \
                sched_overflow = true; \
            } \
        } while (0)

        SCHED_APPEND("{\"v\":3,\"outlets\":[");

        for (int on = 1; on <= 4; on++) {
            char key[24], tmp[24];
            int slot = outlet_slot[on - 1];
            const char *dev = cfg_now->relay_names[slot];
            if (!dev || !dev[0]) continue;

            snprintf(key, sizeof(key), "s%d_disabled", on);
            bool requested_disabled = get_param(query, key, tmp, sizeof(tmp)) && strcmp(tmp, "1") == 0;

            bool is_light = strcmp(dev, "Light") == 0;
            bool is_fan = strcmp(dev, "Fan") == 0;
            bool is_humid = strcmp(dev, "Humidifier") == 0;
            bool is_dehumid = strcmp(dev, "Dehumidifier") == 0;
            bool is_heater = strcmp(dev, "Heater") == 0;
            bool is_ac = strcmp(dev, "AC Controller") == 0;
            bool is_pump = strcmp(dev, "Water Pump") == 0;
            bool first_condition = true;
            int outlet_start = sn;

            if (!first_outlet) SCHED_APPEND(",");
            SCHED_APPEND("{\"id\":%d,\"conditions\":[", on);

            snprintf(key, sizeof(key), "s%d_always", on);
            bool always = (is_light || is_fan) && get_param(query, key, tmp, sizeof(tmp));
            if (always) {
                SCHED_APPEND("{\"type\":\"always_on\"}");
                first_condition = false;
            }

            snprintf(key, sizeof(key), "s%d_time_en", on);
            if (!always && (is_light || is_fan) && get_param(query, key, tmp, sizeof(tmp))) {
                char start[6] = "06:00", end[6] = "22:00";
                snprintf(key, sizeof(key), "s%d_start", on);
                get_param(query, key, start, sizeof(start));
                snprintf(key, sizeof(key), "s%d_end", on);
                get_param(query, key, end, sizeof(end));
                if (!first_condition) SCHED_APPEND(",");
                SCHED_APPEND(
                    "{\"type\":\"time_window\",\"start\":\"%s\",\"end\":\"%s\"}",
                    start, end);
                first_condition = false;
            }

            snprintf(key, sizeof(key), "s%d_rh_en", on);
            if (!always && (is_fan || is_humid || is_dehumid) &&
                get_param(query, key, tmp, sizeof(tmp))) {
                char low[8] = "55", high[8] = "65";
                if (is_humid) {
                    strncpy(low, "50", sizeof(low));
                    strncpy(high, "60", sizeof(high));
                }
                snprintf(key, sizeof(key), "s%d_rh_low", on);
                get_param(query, key, low, sizeof(low));
                snprintf(key, sizeof(key), "s%d_rh_high", on);
                get_param(query, key, high, sizeof(high));
                if (!first_condition) SCHED_APPEND(",");
                SCHED_APPEND(
                    "{\"type\":\"%s\",\"low\":%s,\"high\":%s}",
                    is_humid ? "rh_low_band" : "rh_high_band",
                    low, high);
                first_condition = false;
            }

            snprintf(key, sizeof(key), "s%d_temp_en", on);
            if (!always && (is_fan || is_heater || is_ac) &&
                get_param(query, key, tmp, sizeof(tmp))) {
                char low_s[12] = "24.0", high_s[12] = "27.0";
                if (is_heater) {
                    strncpy(low_s, "18.0", sizeof(low_s));
                    strncpy(high_s, "21.0", sizeof(high_s));
                }
                snprintf(key, sizeof(key), "s%d_temp_low", on);
                get_param(query, key, low_s, sizeof(low_s));
                snprintf(key, sizeof(key), "s%d_temp_high", on);
                get_param(query, key, high_s, sizeof(high_s));
                float low_c = ui_temp_to_c((float)atof(low_s), cfg_now);
                float high_c = ui_temp_to_c((float)atof(high_s), cfg_now);
                if (!first_condition) SCHED_APPEND(",");
                SCHED_APPEND(
                    "{\"type\":\"%s\",\"low_c\":%.2f,\"high_c\":%.2f}",
                    is_heater ? "temp_low_band_c" : "temp_high_band_c",
                    low_c, high_c);
                first_condition = false;
            }

            snprintf(key, sizeof(key), "s%d_interval_en", on);
            if (is_pump && get_param(query, key, tmp, sizeof(tmp))) {
                char run[8] = "15", every[8] = "4", wstart[6] = "06:00", wend[6] = "22:00";
                snprintf(key, sizeof(key), "s%d_run", on);
                get_param(query, key, run, sizeof(run));
                snprintf(key, sizeof(key), "s%d_every", on);
                get_param(query, key, every, sizeof(every));
                if (!first_condition) SCHED_APPEND(",");
                SCHED_APPEND(
                    "{\"type\":\"interval\",\"run_mins\":%s,\"every_hrs\":%s",
                    run, every);
                snprintf(key, sizeof(key), "s%d_win_en", on);
                if (get_param(query, key, tmp, sizeof(tmp))) {
                    snprintf(key, sizeof(key), "s%d_wstart", on);
                    get_param(query, key, wstart, sizeof(wstart));
                    snprintf(key, sizeof(key), "s%d_wend", on);
                    get_param(query, key, wend, sizeof(wend));
                    SCHED_APPEND(
                        ",\"window\":{\"start\":\"%s\",\"end\":\"%s\"}", wstart, wend);
                }
                SCHED_APPEND("}");
                first_condition = false;
            }

            if (first_condition) {
                sn = outlet_start;
                continue;
            }

            if (requested_disabled) {
                disabled_mask |= (1 << (on - 1));
            }

            SCHED_APPEND("]}");
            first_outlet = false;
            any_sched = true;
        }

        SCHED_APPEND("]}");

        if (sched_overflow) {
            ESP_LOGW(TAG, "Local schedule rejected: generated payload too large");
            mqtt_publish_schedule_error("invalid_payload", 0, "local schedule too large");
        } else if (!any_sched) {
            config_save_schedule_disabled_mask(disabled_mask);
            schedule_clear();
            config_clear_schedule();
            if (relays_get_mode() == RELAY_MODE_AUTO) {
                schedule_evaluate_now();
            }
            mqtt_publish_schedule_state("local");
        } else if (sn > 0 && sn < SCHEDULE_JSON_MAX) {
            if (schedule_load(sched, sn)) {
                config_save_schedule_disabled_mask(disabled_mask);
                config_save_schedule(sched);
                if (relays_get_mode() == RELAY_MODE_AUTO) {
                    schedule_evaluate_now();
                }
                mqtt_publish_schedule_state("local");
            } else {
                ESP_LOGW(TAG, "Local schedule rejected: %s (outlet %d)",
                         schedule_last_error_reason(), schedule_last_error_outlet());
                mqtt_publish_schedule_error(schedule_last_error_reason(),
                                            schedule_last_error_outlet(), NULL);
            }
        }

        #undef SCHED_APPEND
        free(sched);
    }

    if (get_param(query, "sched_clear", val, sizeof(val))) {
        schedule_clear();
        config_clear_schedule();
        config_save_schedule_disabled_mask(0);
        if (relays_get_mode() == RELAY_MODE_AUTO) {
            schedule_evaluate_now();
        }
        mqtt_publish_schedule_state("local");
    }

    // --- Pump run-now trigger ---
    if (get_param(query, "pump_run_now", val, sizeof(val))) {
        int outlet_id = atoi(val);
        if (outlet_id >= 1 && outlet_id <= 4) {
            schedule_pump_run_now(outlet_id);
            mqtt_publish_schedule_state("local");
        }
    }

    // --- Forget WiFi ---
    if (get_param(query, "wifi_forget", val, sizeof(val))) {
        wifi_exit_recovery_mode(true);  // clears credentials, cancels recovery
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        free(query);
        return ESP_OK;
    }

    // --- CC disconnect (soft disable) ---
    if (get_param(query, "cc_disconnect", val, sizeof(val))) {
        config_save_mqtt_disabled(true);
        mqtt_stop();
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        free(query);
        return ESP_OK;
    }

    // --- CC reconnect ---
    if (get_param(query, "cc_reconnect", val, sizeof(val))) {
        const growhub_config_t *cur = config_get();
        config_save_mqtt(cur->mqtt_host, cur->mqtt_port);  // clears disabled flag
        need_reboot = true;
    }

    // --- WiFi credentials ---
    char ssid[MAX_SSID_LEN + 1] = {0}, pass[MAX_PASSWORD_LEN + 1] = {0};
    if (get_param(query, "ssid", ssid, sizeof(ssid)) && ssid[0]) {
        get_param(query, "password", pass, sizeof(pass));
        config_save_wifi(ssid, pass);
        wifi_reconnect();
    }

    // --- Command Center (MQTT) ---
    char host[MAX_HOSTNAME_LEN + 1] = {0}, port_str[6] = {0};
    if (get_param(query, "mqtt_host", host, sizeof(host)) && host[0]) {
        uint16_t port = DEFAULT_MQTT_PORT;
        if (get_param(query, "mqtt_port", port_str, sizeof(port_str)))
            port = (uint16_t)atoi(port_str);
        config_save_mqtt(host, port);
        need_reboot = true;
    }

    // --- Device (name, timezone, temp_unit, time_src, SNTP servers) ---
    char name[MAX_DEVICE_NAME_LEN + 1] = {0}, tz[MAX_TIMEZONE_LEN + 1] = {0};
    char sntp_primary[MAX_SNTP_HOST_LEN + 1] = {0}, sntp_secondary[MAX_SNTP_HOST_LEN + 1] = {0};
    char tu_str[4] = {0}, ts_str[4] = {0};
    bool has_name = get_param(query, "dev_name",   name,   sizeof(name))   && name[0];
    bool has_tz   = get_param(query, "timezone",   tz,     sizeof(tz))     && tz[0];
    bool has_tu   = get_param(query, "temp_unit",  tu_str, sizeof(tu_str)) && tu_str[0];
    bool has_ts   = get_param(query, "time_src",   ts_str, sizeof(ts_str)) && ts_str[0];
    bool has_sntp_primary = get_param(query, "sntp_primary", sntp_primary, sizeof(sntp_primary)) &&
                            sntp_primary[0];
    bool has_sntp_secondary = get_param(query, "sntp_secondary", sntp_secondary, sizeof(sntp_secondary)) &&
                              sntp_secondary[0];
    bool time_changed = has_tz || has_ts || has_sntp_primary || has_sntp_secondary;
    if (has_name || has_tz || has_tu || has_ts) {
        int tu = has_tu ? atoi(tu_str) : -1;
        int ts = has_ts ? atoi(ts_str) : -1;
        config_save_device(has_name ? name : NULL,
                           has_tz   ? tz   : NULL,
                           tu, ts);
    }
    if (has_sntp_primary || has_sntp_secondary) {
        config_save_sntp_servers(has_sntp_primary ? sntp_primary : NULL,
                                 has_sntp_secondary ? sntp_secondary : NULL);
    }
    if (time_changed) {
        time_sync_apply_config();
    }

    // --- Outlet assignments ---
    // Display outlet N maps to relay_names[slot]: O1->slot3, O2->slot0, O3->slot1, O4->slot2
    int outlet_slot[4] = {3, 0, 1, 2};
    bool any_outlet = false;
    uint8_t changed_outlet_mask = 0;
    uint8_t disabled_mask = config_get()->schedule_disabled_mask;
    growhub_config_t pin_cfg = *config_get();
    for (int on = 1; on <= 4; on++) {
        char pname[12];
        snprintf(pname, sizeof(pname), "outlet_%d", on);
        if (get_param(query, pname, val, sizeof(val))) {
            int slot = outlet_slot[on - 1];
            if (strncmp(pin_cfg.relay_names[slot], val, MAX_RELAY_NAME_LEN) != 0) {
                changed_outlet_mask |= (1 << (on - 1));
                disabled_mask &= ~(1 << (on - 1));
            }
            // Empty value means "None" — clear the name
            strncpy(pin_cfg.relay_names[slot], val, MAX_RELAY_NAME_LEN);
            pin_cfg.relay_names[slot][MAX_RELAY_NAME_LEN] = '\0';
            any_outlet = true;
        }
    }
    if (any_outlet) {
        config_save_pins(&pin_cfg);
        if (changed_outlet_mask) {
            config_save_schedule_disabled_mask(disabled_mask);
            if (remove_schedule_entries_for_outlets(changed_outlet_mask) &&
                relays_get_mode() == RELAY_MODE_AUTO) {
                schedule_evaluate_now();
            }
            mqtt_publish_schedule_state("local");
        }
    }

    // --- OTA ---
    char ota_url[256] = {0};
    if (get_param(query, "ota_url", ota_url, sizeof(ota_url)) && ota_url[0]) {
        if (!ota_start_from_url(ota_url)) {
            free(query);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "OTA already in progress or URL is invalid");
            return ESP_FAIL;
        }
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req,
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Firmware Update</title>"
            "<style>"
            "body{font-family:sans-serif;background:#1a1a2e;color:#e0e0e0;"
            "text-align:center;padding:50px;margin:0}"
            "h1{color:#4ecca3}"
            ".bar-wrap{background:#16213e;border-radius:8px;height:12px;"
            "width:280px;margin:20px auto;overflow:hidden}"
            ".bar{height:100%;width:0;background:#4ecca3;border-radius:8px;"
            "transition:width 0.4s;animation:pulse 1.5s ease-in-out infinite}"
            "@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.5}}"
            ".kb{font-size:0.85em;color:#888;margin:8px 0}"
            ".err{color:#e74c3c}"
            "</style></head><body>"
            "<h1>Firmware Update</h1>"
            "<p id='msg'>Connecting to server...</p>"
            "<div class='bar-wrap'><div class='bar' id='bar'></div></div>"
            "<p class='kb' id='kb'></p>"
            "<script>"
            "var lastStage='';"
            "var failCount=0;"
            "var timer=setInterval(poll,1500);"
            "poll();"
            "function poll(){"
              "fetch('/ota_status')"
              ".then(function(r){return r.json();})"
              ".then(function(d){"
                "failCount=0;"
                "var msg=document.getElementById('msg');"
                "var kb=document.getElementById('kb');"
                "var bar=document.getElementById('bar');"
                "if(d.stage==='connecting'){"
                  "msg.textContent='Connecting to server...';"
                  "bar.style.width='15%';"
                "}else if(d.stage==='flashing'){"
                  "var kbv=Math.floor(d.bytes/1024);"
                  "msg.textContent='Flashing firmware...';"
                  "kb.textContent=kbv+' KB written';"
                  "var pct=Math.min(15+kbv/15,90);"
                  "bar.style.width=pct+'%';"
                "}else if(d.stage==='done'){"
                  "clearInterval(timer);"
                  "msg.textContent='Flash complete! Rebooting...';"
                  "bar.style.width='100%';"
                  "bar.style.animation='none';"
                  "setTimeout(waitForReboot,3000);"
                "}else if(d.stage==='failed'){"
                  "clearInterval(timer);"
                  "msg.className='err';"
                  "msg.textContent='Update failed. Check URL and try again.';"
                  "bar.style.background='#e74c3c';"
                  "bar.style.width='100%';"
                  "bar.style.animation='none';"
                "}"
                "lastStage=d.stage;"
              "})"
              ".catch(function(){"
                "failCount++;"
                "if(failCount>=2){"
                  "clearInterval(timer);"
                  "if(lastStage==='done'||lastStage==='flashing'||lastStage==='connecting'){"
                    "document.getElementById('msg').textContent="
                      "'Rebooting — waiting for device...';"
                    "waitForReboot();"
                  "}"
                "}"
              "});}"
            "function waitForReboot(){"
              "var tries=0;"
              "var check=setInterval(function(){"
                "fetch('/').then(function(){"
                  "clearInterval(check);"
                  "location.href='/';"
                "}).catch(function(){"
                  "tries++;"
                  "if(tries>30){"
                    "clearInterval(check);"
                    "document.getElementById('msg').textContent="
                      "'Could not reconnect. Try reloading the page manually.';"
                  "}"
                "});"
              "},2000);}"
            "</script></body></html>");
        free(query);
        return ESP_OK;
    }

    // --- Reboot ---
    if (get_param(query, "reboot", val, sizeof(val))) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req,
            "<html><body style='background:#1a1a2e;color:#e0e0e0;"
            "font-family:sans-serif;text-align:center;padding:50px'>"
            "<h1 style='color:#4ecca3'>Rebooting...</h1>"
            "<script>setTimeout(()=>location='/',6000)</script>"
            "</body></html>");
        free(query);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return ESP_OK;
    }

    // --- Factory reset ---
    if (get_param(query, "reset", val, sizeof(val))) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req,
            "<html><body style='background:#1a1a2e;color:#e0e0e0;"
            "font-family:sans-serif;text-align:center;padding:50px'>"
            "<h1 style='color:#e74c3c'>Factory Reset</h1>"
            "<p>All settings erased. Device rebooting into setup mode...</p>"
            "</body></html>");
        free(query);
        vTaskDelay(pdMS_TO_TICKS(500));
        config_factory_reset();
        return ESP_OK;
    }

    if (need_reboot) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req,
            "<html><body style='background:#1a1a2e;color:#e0e0e0;"
            "font-family:sans-serif;text-align:center;padding:50px'>"
            "<h1 style='color:#4ecca3'>Saved</h1>"
            "<p>Settings saved. Rebooting to apply...</p>"
            "<script>setTimeout(()=>location='/',8000)</script>"
            "</body></html>");
        free(query);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        free(query);
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /scan -- WiFi network scan, returns JSON array
// ---------------------------------------------------------------------------
static esp_err_t scan_handler(httpd_req_t *req)
{
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan: get mode failed: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (mode == WIFI_MODE_AP) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WiFi scan: APSTA switch failed: %s", esp_err_to_name(err));
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "WiFi scan: enabled station interface for setup scan");
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 100, .max = 300},
    };
    err = esp_wifi_scan_start(&scan_cfg, true); // blocking
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    uint16_t count = 20;
    wifi_ap_record_t *aps = malloc(count * sizeof(wifi_ap_record_t));
    if (!aps) { httpd_resp_send_500(req); return ESP_FAIL; }

    err = esp_wifi_scan_get_ap_records(&count, aps);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan results failed: %s", esp_err_to_name(err));
        free(aps);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan results failed");
        return ESP_FAIL;
    }

    // Sort by RSSI descending (insertion sort -- small N)
    for (int i = 1; i < count; i++) {
        wifi_ap_record_t tmp = aps[i];
        int j = i - 1;
        while (j >= 0 && aps[j].rssi < tmp.rssi) { aps[j+1] = aps[j]; j--; }
        aps[j+1] = tmp;
    }

    char *rbuf = malloc(count * 80 + 4);
    if (!rbuf) { free(aps); httpd_resp_send_500(req); return ESP_FAIL; }

    int rn = 0;
    rbuf[rn++] = '[';
    for (int i = 0; i < count; i++) {
        if (i > 0) rbuf[rn++] = ',';
        rn += snprintf(rbuf + rn, count * 80 - rn,
            "{\"ssid\":\"%s\",\"rssi\":%d}",
            (char *)aps[i].ssid, aps[i].rssi);
    }
    rbuf[rn++] = ']';
    rbuf[rn] = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, rbuf, rn);
    free(rbuf);
    free(aps);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /savetime?epoch=N -- set device time from browser
// ---------------------------------------------------------------------------
static esp_err_t savetime_handler(httpd_req_t *req)
{
    char query[64] = {0};
    char val[20] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        get_param(query, "epoch", val, sizeof(val)) && val[0]) {
        time_t epoch = (time_t)atol(val);
        if (epoch > 86400) {
            struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "Time set from browser: %ld", (long)epoch);
            if (relays_get_mode() == RELAY_MODE_AUTO) {
                schedule_evaluate_now();
            }
            mqtt_publish_schedule_state("time");
        }
    }

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /ota_upload -- receive raw firmware binary and flash to OTA partition
// ---------------------------------------------------------------------------
static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 0x12c000) { // OTA partition is 1,228,800 bytes
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    if (!ota_begin_operation()) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "OTA already in progress");
        return ESP_FAIL;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ota_end_operation();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin failed: %s", esp_err_to_name(err));
        ota_end_operation();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    ota_set_progress(OTA_FLASHING, 0);

    char *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(ota_handle);
        ota_set_progress(OTA_FAILED, 0);
        ota_end_operation();
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int written = 0;
    bool ok = true;

    while (remaining > 0 && ok) {
        int to_recv = remaining < 4096 ? remaining : 4096;
        int recvd = httpd_req_recv(req, buf, to_recv);
        if (recvd <= 0) { ok = false; break; }
        err = esp_ota_write(ota_handle, buf, recvd);
        if (err != ESP_OK) { ok = false; break; }
        written += recvd;
        remaining -= recvd;
        ota_set_progress(OTA_FLASHING, written);
    }

    free(buf);

    if (ok) {
        err = esp_ota_end(ota_handle);
        if (err == ESP_OK) err = esp_ota_set_boot_partition(update_partition);
    }

    if (ok && err == ESP_OK) {
        ESP_LOGI(TAG, "Upload OTA succeeded (%d bytes) — rebooting", written);
        ota_set_progress(OTA_DONE, written);
        httpd_resp_sendstr(req, "OK");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Upload OTA failed after %d bytes", written);
        if (!ok) esp_ota_abort(ota_handle);
        ota_set_progress(OTA_FAILED, written);
        ota_end_operation();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash failed");
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /ota_status -- JSON OTA progress for update page polling
// ---------------------------------------------------------------------------
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    ota_progress_t p = ota_get_progress();
    const char *stage_str;
    switch (p.stage) {
        case OTA_CONNECTING: stage_str = "connecting"; break;
        case OTA_FLASHING:   stage_str = "flashing";   break;
        case OTA_DONE:       stage_str = "done";       break;
        case OTA_FAILED:     stage_str = "failed";     break;
        default:             stage_str = "idle";       break;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"stage\":\"%s\",\"bytes\":%d}", stage_str, p.bytes_written);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /status -- JSON status for Command Center
// ---------------------------------------------------------------------------
static esp_err_t status_handler(httpd_req_t *req)
{
    const growhub_config_t *cfg = config_get();
    sensor_reading_t s = sensors_read();
    uint8_t relay_mask = relays_get_bitmask();
    bool wifi_connected = wifi_is_connected();

    char time_str[32];
    fmt_time_12h(time_str, sizeof(time_str), cfg->time_src);

    float temp_display = s.temp_valid ? s.temperature : 0.0f;
    if (cfg->temp_unit == 1) temp_display = temp_display * 9.0f / 5.0f + 32.0f;

    int rssi_val = 0;
    if (wifi_connected) esp_wifi_sta_get_rssi(&rssi_val);

    int64_t uptime_s = (int64_t)(esp_timer_get_time() / 1000000ULL);

    const char *sntp_status = time_sync_sntp_status_str();
    bool time_valid = time_sync_wall_time_valid();
    bool time_blocked = schedule_time_sync_required();
    bool sntp_unhealthy = time_sync_sntp_unhealthy();
    bool sensor_blocked = schedule_sensor_data_unavailable();
    const char *time_warning = time_blocked
        ? "Time sync required; wall-clock automation is paused"
        : (sntp_unhealthy ? "SNTP sync is unavailable; device time may drift" : "");
    const char *sensor_warning = sensor_blocked
        ? "Sensor data unavailable; temp/rH automation is paused"
        : "";

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(root, "mac", config_get_mac_str());
    cJSON_AddStringToObject(root, "fw", GROWHUB_VERSION);
    cJSON_AddBoolToObject(root, "wifi", wifi_connected);
    cJSON_AddBoolToObject(root, "mqtt", mqtt_is_connected());
    cJSON_AddBoolToObject(root, "recovery_mode", wifi_is_in_recovery_mode());
    cJSON_AddNumberToObject(root, "rssi", rssi_val);
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s);
    cJSON_AddNumberToObject(root, "temp", temp_display);
    cJSON_AddNumberToObject(root, "rh", s.temp_valid ? s.humidity : 0.0f);
    cJSON_AddNumberToObject(root, "co2", s.co2_valid ? s.co2 : 0);
    cJSON_AddNumberToObject(root, "light", s.light);
    char unit[2] = { cfg->temp_unit == 1 ? 'F' : 'C', '\0' };
    cJSON_AddStringToObject(root, "unit", unit);
    cJSON_AddStringToObject(root, "time", time_str);
    cJSON_AddBoolToObject(root, "time_valid", time_valid);
    cJSON_AddStringToObject(root, "time_source", time_sync_source_str());
    cJSON_AddStringToObject(root, "sntp_status", sntp_status);
    cJSON_AddStringToObject(root, "time_warning", time_warning);
    cJSON_AddStringToObject(root, "sensor_warning", sensor_warning);

    cJSON *warnings = cJSON_CreateArray();
    if (warnings) {
        cJSON_AddItemToObject(root, "warnings", warnings);
        if (time_blocked) {
            cJSON *w = cJSON_CreateObject();
            if (w) {
                cJSON_AddStringToObject(w, "code", "time_sync_required");
                cJSON_AddStringToObject(w, "message", time_warning);
                cJSON_AddStringToObject(w, "severity", "blocking");
                cJSON_AddItemToArray(warnings, w);
            }
        }
        if (sensor_blocked) {
            cJSON *w = cJSON_CreateObject();
            if (w) {
                cJSON_AddStringToObject(w, "code", "sensor_data_unavailable");
                cJSON_AddStringToObject(w, "message", sensor_warning);
                cJSON_AddStringToObject(w, "severity", "warning");
                cJSON_AddItemToArray(warnings, w);
            }
        }
        if (sntp_unhealthy) {
            cJSON *w = cJSON_CreateObject();
            if (w) {
                cJSON_AddStringToObject(w, "code", "time_sntp_unhealthy");
                cJSON_AddStringToObject(w, "message", time_warning);
                cJSON_AddStringToObject(w, "severity", "warning");
                cJSON_AddItemToArray(warnings, w);
            }
        }
    }

    cJSON *relays = cJSON_CreateObject();
    if (relays) {
        cJSON_AddBoolToObject(relays, "o1", (relay_mask >> 3) & 1);
        cJSON_AddBoolToObject(relays, "o2", (relay_mask >> 0) & 1);
        cJSON_AddBoolToObject(relays, "o3", (relay_mask >> 1) & 1);
        cJSON_AddBoolToObject(relays, "o4", (relay_mask >> 2) & 1);
        cJSON_AddItemToObject(root, "relays", relays);
    }
    cJSON_AddStringToObject(root, "mode",
                            relays_get_mode() == RELAY_MODE_MANUAL ? "manual" : "auto");
    cJSON_AddStringToObject(root, "name", cfg->device_name);

    cJSON *outlet_status = cJSON_CreateArray();
    if (outlet_status) {
        cJSON_AddItemToObject(root, "outlet_status", outlet_status);
        for (int id = 1; id <= 4; id++) {
            cJSON *item = cJSON_CreateObject();
            if (!item) continue;
            int bit = id == 1 ? 3 : id - 2;
            cJSON_AddNumberToObject(item, "id", id);
            cJSON_AddStringToObject(item, "state", ((relay_mask >> bit) & 1) ? "on" : "off");
            cJSON_AddStringToObject(item, "summary", schedule_get_outlet_summary(id));
            cJSON_AddItemToArray(outlet_status, item);
        }
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, payload, strlen(payload));
    free(payload);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Server init
// ---------------------------------------------------------------------------
void webserver_init(void)
{
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.uri_match_fn = httpd_uri_match_wildcard;
    http_cfg.max_uri_handlers = 8;
    http_cfg.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &http_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        s_webserver_running = false;
        return;
    }

    httpd_uri_t uris[] = {
        {.uri="/",           .method=HTTP_GET,  .handler=root_handler},
        {.uri="/save",       .method=HTTP_GET,  .handler=save_handler},
        {.uri="/save",       .method=HTTP_POST, .handler=save_handler},
        {.uri="/scan",       .method=HTTP_GET,  .handler=scan_handler},
        {.uri="/savetime",   .method=HTTP_GET,  .handler=savetime_handler},
        {.uri="/status",     .method=HTTP_GET,  .handler=status_handler},
        {.uri="/ota_status", .method=HTTP_GET,  .handler=ota_status_handler},
        {.uri="/ota_upload", .method=HTTP_POST, .handler=ota_upload_handler},
    };
    bool handlers_ok = true;
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register handler %s: %s",
                     uris[i].uri, esp_err_to_name(err));
            handlers_ok = false;
        }
    }

    s_webserver_running = handlers_ok;
    ESP_LOGI(TAG, "HTTP server started on port 80%s",
             handlers_ok ? "" : " with handler registration errors");
}

bool webserver_is_running(void)
{
    return s_webserver_running;
}
