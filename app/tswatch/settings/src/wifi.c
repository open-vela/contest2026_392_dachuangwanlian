#include <nuttx/config.h>
#include <unistd.h>
#include <errno.h>
#include <sys/boardctl.h>

#include <lvgl/lvgl.h>
#include <lvgl/demos/lv_demos.h>
#include <uv.h>
#include <nuttx/wireless/wireless.h>
#include "wireless/wapi.h"
#include "../include/wifi.h"
#include "../include/setting.h"
#include "../../utils/include/font_manager.h"
#include "../../utils/include/circle_screen.h"

#include <stdio.h>
#include <string.h>

#define DEFAULT_STA_NETCARD "wlan0"

#define PASSWORD_DIALOG_X 100
#define PASSWORD_DIALOG_Y_OLD 150
#define PASSWORD_DIALOG_Y_NEW 70

// 颜色定义
static lv_color_t bg_color = LV_COLOR_MAKE(0x0a, 0x0a, 0x1a);
static lv_color_t primary_color = LV_COLOR_MAKE(0xff, 0xff, 0xff);
static lv_color_t secondary_color = LV_COLOR_MAKE(0x05, 0x56, 0x8d);
static lv_color_t accent_color = LV_COLOR_MAKE(0xff, 0x6b, 0x9c);
static lv_color_t text_color = LV_COLOR_MAKE(0x00, 0x00, 0x10);
static lv_color_t weak_signal_color = LV_COLOR_MAKE(0xff, 0x55, 0x55);
static lv_color_t medium_signal_color = LV_COLOR_MAKE(0xff, 0xaa, 0x00);
static lv_color_t strong_signal_color = LV_COLOR_MAKE(0x00, 0xff, 0x9d);
static lv_color_t password_color = LV_COLOR_MAKE(0x1a, 0x1a, 0x2a);

// 函数声明
static void connect_btn_event_cb(lv_event_t *e);
static void cancel_dialog_event_cb(lv_event_t *e);
static void wifi_list_event_cb(lv_event_t *e);

/* WiFi 网络信息结构体 */
typedef struct {
    char ssid[WAPI_ESSID_MAX_SIZE + 1];
    struct ether_addr bssid;
    enum wapi_mode_e mode;
    double freq;
    int bitrate;
    int rssi;
    int signal_strength;  // 0-100 的信号强度
    bool has_password;
    bool is_connected;
    bool has_saved;       // 是否已保存密码
} wifi_network_t;

/* WiFi 管理上下文 */
typedef struct {
    int wireless_sock;
    char ifname[IF_NAMESIZE];
    lv_obj_t *screen;
    lv_obj_t *list;
    lv_obj_t *connect_btn;
    lv_obj_t *state_lable;
    lv_obj_t *password_ta;
    lv_obj_t *connect_dialog;
    wifi_network_t *networks;
    int network_count;
    int selected_index;
} wifi_manager_t;

static lv_obj_t *password_dialog = NULL;
static lv_obj_t *input_keyboard = NULL;

/* 扫描 WiFi 网络 */
static int wifi_scan_networks(wifi_manager_t *wifi_mgr)
{
    struct wapi_list_s scan_list = {0};
    int ret;

    /* 开始扫描 */
    ret = wapi_scan_init(wifi_mgr->wireless_sock, wifi_mgr->ifname, NULL);
    if (ret < 0) {
        LV_LOG_USER("Scan init failed: %d\n", ret);
        return ret;
    }else{
        LV_LOG_ERROR("---sunbin---wifi_scan_networks: call wapi_scan_init success");
    }

    /* 等待扫描完成 */
    int status;
    do {
        usleep(100000); // 100ms
        status = wapi_scan_stat(wifi_mgr->wireless_sock, wifi_mgr->ifname);
    } while (status == 1); // 1 表示扫描未完成

    if (status < 0) {
        LV_LOG_USER("Scan failed: %d\n", status);
        return status;
    }

    /* 收集扫描结果 */
    ret = wapi_scan_coll(wifi_mgr->wireless_sock, wifi_mgr->ifname, &scan_list);
    if (ret < 0) {
        LV_LOG_USER("Scan collection failed: %d\n", ret);
        return ret;
    }

    /* 先统计数量 */
    struct wapi_scan_info_s *info = scan_list.head.scan;
    int count = 0;
    struct wapi_scan_info_s *iter = info;
    while (iter) {
        count++;
        iter = iter->next;
    }

    /* 释放旧的列表缓存并分配新数组 */
    if (wifi_mgr->networks) {
        free(wifi_mgr->networks);
        wifi_mgr->networks = NULL;
        wifi_mgr->network_count = 0;
    }

    /* 如果没有扫描到网络，直接返回 */
    if (count == 0) {
        LV_LOG_ERROR("---sunbin---wifi_scan_networks: no networks found");
        wapi_scan_coll_free(&scan_list);
        return 0;
    }

    wifi_mgr->networks = calloc(count, sizeof(wifi_network_t));
    if (!wifi_mgr->networks) {
        wapi_scan_coll_free(&scan_list);
        return -ENOMEM;
    }

    int idx = 0;
    while (info && idx < count) {
        char cur_ssid[WAPI_ESSID_MAX_SIZE + 1];
        if (info->essid) {
            strncpy(cur_ssid, info->essid, WAPI_ESSID_MAX_SIZE);
            cur_ssid[WAPI_ESSID_MAX_SIZE] = '\0';
        } else {
            cur_ssid[0] = '\0';
        }

        /* 过滤与已添加项相同的 SSID（按名字去重） */
        bool duplicate = false;
        for (int j = 0; j < idx; j++) {
            if (strcmp(wifi_mgr->networks[j].ssid, cur_ssid) == 0) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            LV_LOG_ERROR("---sunbin---wifi_scan_networks: skip duplicate SSID=%s", cur_ssid);
            info = info->next;
            continue;
        }

        /* 填充新的网络条目 */
        wifi_network_t *net = &wifi_mgr->networks[idx];
        strncpy(net->ssid, cur_ssid, WAPI_ESSID_MAX_SIZE + 1);

        /* BSSID
        info是wapi_scan_info_s指针
        char essid[WAPI_ESSID_MAX_SIZE + 1];
        struct ether_addr bssid;//sunbin add follow AI 
        net是wifi_network_t指针
        char ssid[WAPI_ESSID_MAX_SIZE + 1];
        struct ether_addr bssid;
        
        */
        memcpy(&net->bssid, &info->bssid, sizeof(net->bssid));

        /* 其他字段 */
        net->mode = info->mode;
        if (info->has_freq) net->freq = info->freq;
        if (info->has_bitrate) net->bitrate = info->bitrate;
        if (info->has_rssi) {
            net->rssi = info->rssi;
            /* 将 RSSI 转换为信号强度百分比 (假设 -100dBm 到 -30dBm) */
            int strength = (info->rssi + 100) * 100 / 70;
            if (strength < 0) strength = 0;
            if (strength > 100) strength = 100;
            net->signal_strength = strength;
        } else {
            net->rssi = 0;
            net->signal_strength = 0;
        }

        /* has_encode 表示 encode 字段是否有效，真正是否需要密码应检查 encode 值 */
        net->has_password = (info->has_encode && info->encode != 0) ? true : false;
        LV_LOG_ERROR("---sunbin--- wifi_scan_networks: SSID=%s, has_encode=%d, encode=%d, has_password=%d", 
            net->ssid, info->has_encode, info->encode, net->has_password);
        net->is_connected = false;

        idx++;
        info = info->next;
    }

    wifi_mgr->network_count = idx;

    /* 释放扫描结果 */
    wapi_scan_coll_free(&scan_list);
    return 0;
}

/* 连接 WiFi 网络 */
static int wifi_connect_network(wifi_manager_t *wifi_mgr, int network_idx, 
                               const char *password)
{
    LV_LOG_ERROR("---sunbin---start,selected_index:%d network_idx=%d", wifi_mgr->selected_index, network_idx);
    if(password){
        LV_LOG_ERROR("---sunbin---password:%s", password);
    }else{
        LV_LOG_ERROR("---sunbin---open ap no password");
    }
    if (network_idx < 0 || network_idx >= wifi_mgr->network_count) {
        LV_LOG_ERROR("Invalid network index: %d\n", network_idx);
        return -EINVAL;
    }
    
    wifi_network_t *net = &wifi_mgr->networks[network_idx];
    int ret;
    
    /* 设置操作模式为 Managed */
    //WAPI_MODE_MANAGED = IW_MODE_INFRA = 2 wireless.h and wapi.h
    ret = wapi_set_mode(wifi_mgr->wireless_sock, wifi_mgr->ifname, WAPI_MODE_MANAGED);
    if (ret < 0) {
        LV_LOG_USER("Set mode failed: %d\n", ret);
        return ret;
    }
    
    /* 如果有密码，使用 wpa_driver_wext_associate 进行关联（正确设置 WPA2-PSK 等）
     * wapi_set_pmksa 使用的是 PMK ioctl（SIOCSIWPMKSA），多数驱动不支持，
     * 并且 PMK 与 passphrase 不同（PMK 是派生的二进制密钥），
     * 所以直接调用 wpa_driver_wext_associate 可以正确设置密钥与参数。 */
    if (net->has_password && password && strlen(password) > 0) {
        struct wpa_wconfig_s conf;

        memset(&conf, 0, sizeof(conf));
        conf.sta_mode = WAPI_MODE_MANAGED;
        /* 要连接 WPA2-PSK，设置 WPA2 与 CCMP（AES）为首选 */
        conf.auth_wpa = IW_AUTH_WPA_VERSION_WPA2;
        conf.cipher_mode = IW_AUTH_CIPHER_CCMP;
        conf.alg = WPA_ALG_CCMP;
        conf.freq = net->freq;
        conf.flag = WAPI_FREQ_AUTO;
        conf.ssidlen = (uint8_t)strnlen(net->ssid, WAPI_ESSID_MAX_SIZE);
    /* WPA passphrase max length is 64 */
    conf.phraselen = (uint8_t)strnlen(password, 64);
        conf.ifname = wifi_mgr->ifname;
        conf.ssid = net->ssid;
        conf.bssid = NULL;
        conf.passphrase = password;

        ret = wpa_driver_wext_associate(&conf);
        if (ret < 0) {
            LV_LOG_USER("wpa_driver_wext_associate() failed: %d\n", ret);
            return ret;
        }
    }
    else
    {
        /* 开放网络：直接设置 ESSID 与 AP，然后获取 IP */
        ret = wapi_set_essid(wifi_mgr->wireless_sock, wifi_mgr->ifname,
                            net->ssid, WAPI_ESSID_ON);
        if (ret < 0) {
            LV_LOG_USER("Set ESSID failed: %d\n", ret);
            return ret;
        }

        /* 设置 AP (BSSID) - 可选，系统通常会自动选择 */
        ret = wapi_set_ap(wifi_mgr->wireless_sock, wifi_mgr->ifname, &net->bssid);
        if (ret < 0) {
            LV_LOG_USER("Set AP failed: %d\n", ret);
            /* 继续执行，这不是致命错误 */
        }

        ret = netlib_obtain_ipv4addr(wifi_mgr->ifname);
        if (ret < 0){
            printf("ERROR: netlib_obtain_ipv4addr() failed\n");
            return ret;
        }
    }
    
    net->is_connected = true;
    return 0;
}

/* 创建 WiFi 列表项 */
static lv_obj_t *create_wifi_list_item(lv_obj_t *parent, wifi_network_t *network)
{
    lv_obj_t *item = lv_list_add_btn(parent, NULL, network->ssid);
    
    /* 创建信号强度指示器 */
    lv_obj_t *signal_cont = lv_obj_create(item);
    lv_obj_set_size(signal_cont, 60, 20);
    lv_obj_set_style_bg_opa(signal_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(signal_cont, LV_OPA_0, 0);
    lv_obj_align(signal_cont, LV_ALIGN_RIGHT_MID, -10, 0);
    
    /* 信号强度条 */
    lv_obj_t *signal_bar = lv_bar_create(signal_cont);
    lv_obj_set_size(signal_bar, 40, 8);
    lv_bar_set_value(signal_bar, network->signal_strength, LV_ANIM_OFF);
    lv_obj_align(signal_bar, LV_ALIGN_TOP_MID, 0, 0);
    
    /* 锁图标（如果需要密码） */
    // if (network->has_password) {
    //     lv_obj_t *lock_label = lv_label_create(signal_cont);
    //     lv_label_set_text(lock_label, LV_SYMBOL_LOCK);
    //     lv_obj_align(lock_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    // }
    
    /* 连接状态指示 */
    if (network->is_connected) {
        lv_obj_t *conn_label = lv_label_create(item);
        lv_label_set_text(conn_label, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(conn_label, lv_color_hex(0x00FF00), 0);
        lv_obj_align(conn_label, LV_ALIGN_LEFT_MID, 5, 0);
    }
    
    return item;
}

/* 刷新 WiFi 列表显示 */
static void refresh_wifi_list(wifi_manager_t *wifi_mgr)
{
    if (!wifi_mgr->list) return;
    
    /* 清空现有列表 */
    LV_LOG_ERROR("---sunbin--- clearing existing list");
    lv_obj_clean(wifi_mgr->list);
    
    /* 添加网络到列表 */
    for (int i = 0; i < wifi_mgr->network_count; i++) {
        lv_obj_t *item = create_wifi_list_item(wifi_mgr->list, &wifi_mgr->networks[i]);
        
        /* 存储网络索引到用户数据 */
        lv_obj_set_user_data(item, (void *)(intptr_t)i);
        /* 为每个列表项注册点击回调，确保点击事件能被处理 */
        lv_obj_add_event_cb(item, wifi_list_event_cb, LV_EVENT_CLICKED, wifi_mgr);
    }
}

/* 密码输入回调 */
static void password_textarea_event_cb(lv_event_t *e)
{
    lv_obj_t *textarea = lv_event_get_target(e);

    lv_keyboard_set_textarea(input_keyboard, textarea);
    lv_obj_set_pos(password_dialog, PASSWORD_DIALOG_X, PASSWORD_DIALOG_Y_NEW);
    lv_obj_remove_flag(input_keyboard, LV_OBJ_FLAG_HIDDEN);
}

/* 显示密码输入对话框 */
static void show_password_dialog(wifi_manager_t *wifi_mgr, const char *ssid)
{
    LV_LOG_ERROR("---sunbin---show_password_dialog start,ssid=%s", ssid);
    // 创建对话框背景
    // lv_obj_t *password_dialog = lv_obj_create(lv_scr_act());
    // lv_obj_set_size(password_dialog, 220, 200);
    // lv_obj_center(password_dialog);
    password_dialog = lv_obj_create(wifi_mgr->screen);
    lv_obj_set_size(password_dialog, 220, 160);
    lv_obj_set_pos(password_dialog, PASSWORD_DIALOG_X, PASSWORD_DIALOG_Y_OLD);

    lv_obj_set_style_bg_color(password_dialog, password_color, 0);
    lv_obj_set_style_bg_opa(password_dialog,LV_OPA_90, 0);
    lv_obj_set_style_radius(password_dialog, 20, 0);
    lv_obj_set_style_border_width(password_dialog, 2, 0);
    lv_obj_set_style_border_color(password_dialog, primary_color, 0);
    lv_obj_set_scrollbar_mode(password_dialog, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(password_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(password_dialog, LV_OBJ_FLAG_EVENT_BUBBLE); // 允许事件冒泡
    
    /* 标题 */
    lv_obj_t *title = lv_label_create(password_dialog);
    lv_label_set_text(title, "输入密码");
    lv_obj_set_style_text_color(title, primary_color, 0);
    lv_obj_set_style_text_font(title, font_manager_get_font(20), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    
    /* 网络名称 */
    lv_obj_t *ssid_label = lv_label_create(password_dialog);
    lv_label_set_text(ssid_label, ssid);
    lv_obj_set_style_text_color(ssid_label, primary_color, 0);
    lv_obj_set_style_text_font(ssid_label, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_align_to(ssid_label, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /* 密码输入框 */
    wifi_mgr->password_ta = lv_textarea_create(password_dialog);
    lv_obj_set_size(wifi_mgr->password_ta, 180, 40);
    lv_obj_set_style_text_font(wifi_mgr->password_ta, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(wifi_mgr->password_ta, primary_color, 0);
    lv_obj_align_to(wifi_mgr->password_ta, ssid_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_textarea_set_placeholder_text(wifi_mgr->password_ta, "输入WiFi密码");
    //lv_textarea_set_password_mode(wifi_mgr->password_ta, true);
    lv_textarea_set_one_line(wifi_mgr->password_ta, true);
    lv_obj_set_style_bg_color(wifi_mgr->password_ta, secondary_color, 0);
    lv_obj_set_style_bg_opa(wifi_mgr->password_ta, LV_OPA_20, 0);
    lv_obj_set_style_radius(wifi_mgr->password_ta, 10, 0);

    /* 按钮容器 */
    lv_obj_t *btn_cont = lv_obj_create(password_dialog);
    lv_obj_set_size(btn_cont, 180, 50);
    lv_obj_align_to(btn_cont, wifi_mgr->password_ta, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    /* 连接按钮 */
    lv_obj_t *connect_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(connect_btn, 80, 35);
    lv_obj_set_style_bg_color(connect_btn, primary_color, 0);
    lv_obj_set_style_radius(connect_btn, 10, 0);
    
    lv_obj_t *connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, "连接");
    lv_obj_set_style_text_font(connect_label, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(connect_label, text_color, 0);
    lv_obj_center(connect_label);
    
    /* 取消按钮 */
    lv_obj_t *cancel_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(cancel_btn, 80, 35);
    lv_obj_set_style_bg_color(cancel_btn, secondary_color, 0);
    lv_obj_set_style_radius(cancel_btn, 10, 0);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_font(cancel_label, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(cancel_label, text_color, 0);
    lv_obj_center(cancel_label);
    
    /* 输入键盘 */
    input_keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_set_size(input_keyboard, 300, 150);
    lv_obj_align_to(input_keyboard, wifi_mgr->screen, LV_ALIGN_CENTER, 0, 100);
    lv_obj_set_style_text_font(input_keyboard, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_add_flag(input_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(input_keyboard, wifi_mgr->password_ta);
    
    /* 保存对话框引用 */
    wifi_mgr->connect_dialog = password_dialog;
    
    /* 按钮事件处理 */
    LV_LOG_ERROR("---sunbin---setting up button event callbacks:connect_btn_event_cb and cancel_dialog_event_cb");
    lv_obj_add_event_cb(connect_btn, connect_btn_event_cb, LV_EVENT_CLICKED, wifi_mgr);
    lv_obj_add_event_cb(cancel_btn, cancel_dialog_event_cb, LV_EVENT_CLICKED, wifi_mgr);
    lv_obj_add_event_cb(wifi_mgr->password_ta, password_textarea_event_cb, LV_EVENT_CLICKED, NULL);
}

/* 连接按钮事件回调 */
static void connect_btn_event_cb(lv_event_t *e)
{
    LV_LOG_ERROR("---sunbin---connect_btn_event_cb start");
    wifi_manager_t *wifi_mgr = lv_event_get_user_data(e);

    lv_obj_add_flag(input_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    if (wifi_mgr->selected_index >= 0) {
        const char *password = lv_textarea_get_text(wifi_mgr->password_ta);
        
        /* 尝试连接 */
        int ret = wifi_connect_network(wifi_mgr, wifi_mgr->selected_index, password);
        
        if (ret == 0) {
            LV_LOG_ERROR("---sunbin---Connected successfully to network index %d", wifi_mgr->selected_index);
            /* 连接成功，刷新列表显示连接状态 */
            refresh_wifi_list(wifi_mgr);
        } else {
            LV_LOG_ERROR("---sunbin---Failed to connect to network index %d, error %d", 
                wifi_mgr->selected_index, ret);
            // /* 连接失败，显示错误信息 */
            // lv_obj_t *msgbox = lv_msgbox_create(NULL, "连接失败", 
            //     "无法连接到该网络，请检查密码或重试", NULL, false);
            // lv_obj_center(msgbox);
        }
    }
    
    /* 关闭对话框 */
    // if (wifi_mgr->connect_dialog) {
    //     lv_obj_del(wifi_mgr->connect_dialog);
    //     wifi_mgr->connect_dialog = NULL;
    // }
    if (password_dialog) {
        lv_obj_del(password_dialog);
        password_dialog = NULL;
    }
}

/* 取消对话框事件回调 */
static void cancel_dialog_event_cb(lv_event_t *e)
{
    wifi_manager_t *wifi_mgr = lv_event_get_user_data(e);
    
    // if (wifi_mgr->connect_dialog) {
    //     lv_obj_del(wifi_mgr->connect_dialog);
    //     wifi_mgr->connect_dialog = NULL;
    lv_obj_add_flag(input_keyboard, LV_OBJ_FLAG_HIDDEN);
    if (password_dialog) {
        lv_obj_del(password_dialog);
        password_dialog = NULL;
    }
}

/* WiFi 列表项点击事件 */
static void wifi_list_event_cb(lv_event_t *e)
{
    LV_LOG_ERROR("---sunbin---wifi_list_event_cb start");
    lv_obj_t *obj = lv_event_get_target(e);
    wifi_manager_t *wifi_mgr = lv_event_get_user_data(e);
    
    /* 获取选中的网络索引 */
    int network_idx = (int)(intptr_t)lv_obj_get_user_data(obj);
    wifi_mgr->selected_index = network_idx;
    
    if (network_idx >= 0 && network_idx < wifi_mgr->network_count) {
        wifi_network_t *net = &wifi_mgr->networks[network_idx];
        LV_LOG_ERROR("---sunbin---Selected network: %s, requires password: %s, is_connected: %s", 
            net->ssid, net->has_password ? "yes" : "no", net->is_connected ? "yes" : "no");
        if (net->has_password && !net->is_connected) {
            /* 需要密码，显示密码输入对话框 */
            show_password_dialog(wifi_mgr, net->ssid);
        } else if (!net->is_connected) {
            LV_LOG_ERROR("---sunbin---Connecting to open network: %s", net->ssid);
            /* 开放网络，直接连接 */
            wifi_connect_network(wifi_mgr, network_idx, NULL);
            refresh_wifi_list(wifi_mgr);
        }
    }
}

/* wifi扫描事件 */
static int wifi_scan_event(wifi_manager_t *wifi_mgr)
{
    int ret = 0;
    LV_LOG_ERROR("=== wifi_scan_event start ===");
    /* 执行扫描 */
    ret = wifi_scan_networks(wifi_mgr);

    if (ret == 0) {
        /* 刷新列表显示 */
        refresh_wifi_list(wifi_mgr);
    } else {
        /* 显示扫描错误 */
        LV_LOG_ERROR("---sunbin---wifi_scan_event: scan failed with error %d", ret);
        // lv_obj_t *msgbox = lv_msgbox_create(NULL, "扫描失败","无法扫描WiFi网络，请重试", NULL, false);
        // lv_obj_center(msgbox);
    }

    return ret;
}

// 显示连接状态
static void show_connection_status(wifi_manager_t *wifi_mgr, const char *message, lv_color_t color) {
    if (wifi_mgr->state_lable) {
        lv_label_set_text(wifi_mgr->state_lable, message);
        lv_obj_set_style_text_font(wifi_mgr->state_lable, font_manager_get_font(FONT_SMALL), 0);
        lv_obj_set_style_text_color(wifi_mgr->state_lable, color, 0);
    }
}

// WiFi开关事件处理
static void wifi_switch_event_handler(lv_event_t *e) {
    LV_LOG_ERROR("---sunbin---wifi_switch_event_handler start");
    int ret = 0;
    lv_obj_t *switch_obj = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(switch_obj, LV_STATE_CHECKED);
    LV_LOG_ERROR("WiFi开关状态: %s  sunbin", enabled ? "开启" : "关闭");
    wifi_manager_t *wifi_mgr = lv_event_get_user_data(e);
    
    if (enabled) {
        /* 启动网络接口 */
        ret = netlib_ifup(DEFAULT_STA_NETCARD);
        if (ret < 0) {
            LV_LOG_ERROR("Failed to bring up the network interface: %d", ret);
        }

        // WiFi开启
        LV_LOG_ERROR("正在扫描网络  sunbin");
        lv_obj_clear_flag(wifi_mgr->list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_mgr->state_lable, LV_OBJ_FLAG_HIDDEN);
        show_connection_status(wifi_mgr, "正在扫描网络...", primary_color);
        wifi_scan_event(wifi_mgr);
    } else {
        // WiFi关闭
        /* 关闭网络接口 */
        ret = netlib_ifdown(DEFAULT_STA_NETCARD);
        if (ret < 0) {
            LV_LOG_ERROR("Failed to bring down the network interface: %d", ret);
        }
         LV_LOG_ERROR("WiFi已关闭  sunbin");
        lv_obj_add_flag(wifi_mgr->list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_mgr->state_lable, LV_OBJ_FLAG_HIDDEN);
        show_connection_status(wifi_mgr, "WiFi已关闭", secondary_color);
        /* 清空现有列表 */
        lv_obj_clean(wifi_mgr->list);
    }
}

/* 创建 WiFi 管理界面 */
wifi_manager_t *create_wifi_interface(const char *ifname)
{
    LV_LOG_USER("---sunbin---create_wifi_interface start\n");
    wifi_manager_t *wifi_mgr = malloc(sizeof(wifi_manager_t));
    if (!wifi_mgr) return NULL;

    memset(wifi_mgr, 0, sizeof(wifi_manager_t));
    strlcpy(wifi_mgr->ifname, ifname, IF_NAMESIZE);
    wifi_mgr->selected_index = -1;

    /* 创建无线套接字 */
    //wifi_mgr->wireless_sock = socket(AF_INET, SOCK_DGRAM, 0);
    wifi_mgr->wireless_sock = wapi_make_socket();
    if (wifi_mgr->wireless_sock < 0) {
        LV_LOG_USER("Failed to create socket\n");
        free(wifi_mgr);
        return NULL;
    }

    /* 创建主容器 */
    lv_obj_t *wifi_screen = lv_obj_create(get_watch_scr());
    /* 保存屏幕到管理结构，便于后续销毁 */
    wifi_mgr->screen = wifi_screen;
    lv_obj_set_size(wifi_screen, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(wifi_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(wifi_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(wifi_screen, 0, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(wifi_screen);
    lv_label_set_text(title, "网络连接");
    lv_obj_set_style_text_color(title, primary_color, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_t *wifi_title = lv_label_create(wifi_screen);
    lv_obj_t *wifi_icon = lv_label_create(wifi_screen);
    lv_label_set_text(wifi_title, "无线局域网");
    lv_obj_set_size(wifi_title, 180, 50);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_icon, primary_color, 0);
    lv_obj_set_style_text_font(wifi_icon, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(wifi_title, primary_color, 0);
    lv_obj_set_style_text_font(wifi_title, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_align(wifi_title, LV_ALIGN_TOP_MID, 20, 100);
    lv_obj_align(wifi_icon, LV_ALIGN_TOP_MID, -100, 100);
    // 状态提示
    wifi_mgr->state_lable = lv_label_create(wifi_screen);
    lv_label_set_text(wifi_mgr->state_lable, "WiFi已关闭");
    lv_obj_align(wifi_mgr->state_lable, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_text_font(wifi_mgr->state_lable, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(wifi_mgr->state_lable, primary_color, 0);
    lv_obj_set_style_text_align(wifi_mgr->state_lable, LV_TEXT_ALIGN_CENTER, 0);
    // WiFi开关
    lv_obj_t *wifi_switch = lv_switch_create(wifi_screen);
    //lv_obj_set_size(wifi_switch, 50, 25);
    lv_obj_set_size(wifi_switch, 75, 50);
    lv_obj_align(wifi_switch, LV_ALIGN_TOP_MID, 70, 100);
    lv_obj_set_style_bg_color(wifi_switch, secondary_color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(wifi_switch, primary_color, LV_PART_INDICATOR);
    // 开关事件
    lv_obj_add_event_cb(wifi_switch, wifi_switch_event_handler, LV_EVENT_VALUE_CHANGED, wifi_mgr);
    // wifi列表
    wifi_mgr->list = lv_obj_create(wifi_screen);
    lv_obj_set_size(wifi_mgr->list, 300, 280);
    lv_obj_align(wifi_mgr->list, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_opa(wifi_mgr->list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(wifi_mgr->list, 0, 0);
    lv_obj_set_flex_flow(wifi_mgr->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_mgr->list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(wifi_mgr->list, LV_SCROLLBAR_MODE_OFF);
    // 初始状态下禁用列表
    lv_obj_add_flag(wifi_mgr->list, LV_OBJ_FLAG_HIDDEN);
    
    //LV_LOG_ERROR("---sunbin---setup wifi list event callback need add event for each item");
    //lv_obj_add_event_cb(wifi_mgr->list, wifi_list_event_cb, LV_EVENT_CLICKED, wifi_mgr);
    
    // 添加手势事件处理
    lv_obj_add_event_cb(wifi_screen, sub_page_gesture_cb, LV_EVENT_GESTURE, wifi_screen);
    lv_obj_remove_flag(wifi_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    
    // 返回按钮
    create_back_button(wifi_screen);

    if (wifi_screen == NULL) {
        LV_LOG_USER("---sunbin---destroy_wifi_interface start\n");
        destroy_wifi_interface(wifi_mgr);
    }
    LV_LOG_USER("---sunbin---create_wifi_interface end\n");
    return wifi_mgr;
}

/* 销毁 WiFi 管理界面 */
void destroy_wifi_interface(wifi_manager_t *wifi_mgr)
{
    if (!wifi_mgr) return;
    
    if (wifi_mgr->wireless_sock >= 0) {
        close(wifi_mgr->wireless_sock);
    }
    
    if (wifi_mgr->screen) {
        lv_obj_del(wifi_mgr->screen);
        wifi_mgr->screen = NULL;
    }
    
    if (wifi_mgr->networks) {
        free(wifi_mgr->networks);
    }
    
    free(wifi_mgr);
}

/* wifi主函数 */
void wifi_connect_app(void)
{
    LV_LOG_USER(" wifi_connect_app start\n");
    /* 创建 WiFi 界面 */
    wifi_manager_t *wifi_mgr = create_wifi_interface("wlan0");
    
    if (wifi_mgr) {
        LV_LOG_USER("---sunbin---wifi_mgr exist\n");
                /* 确保网络接口已经 up（避免 SIOCSIWSCAN 返回 ENETDOWN 导致 -100 错误） */
                int ret = netlib_ifup(DEFAULT_STA_NETCARD);
                if (ret < 0)
                    {
                        LV_LOG_ERROR("netlib_ifup failed: %d", ret);
                        /* 继续尝试扫描，wapi_scan_networks 会返回具体错误码 */
                    }

                /* 立即执行一次扫描 */
                wifi_scan_networks(wifi_mgr);
        LV_LOG_USER("---sunbin---refresh_wifi_list start\n");
        refresh_wifi_list(wifi_mgr);
        LV_LOG_USER("---sunbin---refresh_wifi_list end\n");
    }
}
