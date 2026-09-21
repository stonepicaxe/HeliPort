//
//  Api.c
//  ClientKit
//
//  Created by 钟先耀 on 2020/4/7.
//  Copyright © 2020 OpenIntelWireless. All rights reserved.
//

/*
 * This program and the accompanying materials are licensed and made available
 * under the terms and conditions of the The 3-Clause BSD License
 * which accompanies this distribution. The full text of the license may be found at
 * https://opensource.org/licenses/BSD-3-Clause
 */

#include "Api.h"
#include "mach/mach_port.h"
#include "pthread.h"
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t api_mutex = PTHREAD_MUTEX_INITIALIZER;

bool get_platform_info(platform_info_t *info) {
    if (!info) {
        return false;
    }
    memset(info, 0, sizeof(platform_info_t));

    struct ioctl_driver_info driver_info;
    memset(&driver_info, 0, sizeof(struct ioctl_driver_info));
    if (ioctl_get(IOCTL_80211_DRIVER_INFO, &driver_info, sizeof(struct ioctl_driver_info)) != KERN_SUCCESS) {
        goto error;
    }

    snprintf(info->device_info_str, sizeof(info->device_info_str), "%s", driver_info.bsd_name);
    snprintf(info->driver_info_str, sizeof(info->driver_info_str), "%s %s", driver_info.driver_version, driver_info.fw_version);
    return true;

error:
    return false;
}

bool get_power_state(bool *enabled) {
    if (!enabled) {
        return false;
    }
    struct ioctl_power power;
    memset(&power, 0, sizeof(struct ioctl_power));
    if (ioctl_get(IOCTL_80211_POWER, &power, sizeof(struct ioctl_power)) != KERN_SUCCESS) {
        goto error;
    }

    *enabled = (power.enabled != 0);

    return true;

error:
    return false;
}

bool get_80211_state(uint32_t *state) {
    if (!state) {
        return false;
    }
    struct ioctl_state state_struct;
    memset(&state_struct, 0, sizeof(struct ioctl_state));
    if (ioctl_get(IOCTL_80211_STATE, &state_struct, sizeof(struct ioctl_state)) != KERN_SUCCESS) {
        goto error;
    }

    *state = state_struct.state;

    return true;

error:
    return false;
}

bool get_network_ssid(char *ssid)
{
    if (!ssid) {
        return false;
    }
    struct ioctl_nw_id nwid;
    memset(&nwid, 0, sizeof(struct ioctl_nw_id));
    if (ioctl_get(IOCTL_80211_NW_ID, &nwid, sizeof(struct ioctl_nw_id)) != KERN_SUCCESS) {
        goto error;
    }
    
    size_t copy_len = nwid.len < NWID_LEN ? nwid.len : NWID_LEN;
    memcpy(ssid, nwid.nwid, copy_len);
    if (copy_len < NWID_LEN) {
        ssid[copy_len] = '\0';
    }
    
    return true;
    
error:
    return false;
}

bool get_network_bssid(char *bssid)
{
    if (!bssid) {
        return false;
    }
    struct ioctl_nw_bssid nwbssid;
    memset(&nwbssid, 0, sizeof(struct ioctl_nw_bssid));
    if (ioctl_get(IOCTL_80211_NW_BSSID, &nwbssid, sizeof(struct ioctl_nw_bssid)) != KERN_SUCCESS) {
        goto error;
    }
    
    memcpy(bssid, nwbssid.bssid, ETHER_ADDR_LEN);
    
    return true;
    
error:
    return false;
}

bool get_network_list(network_info_list_t *list) {
    if (!list) {
        return false;
    }
    memset(list, 0, sizeof(network_info_list_t));

    struct ioctl_scan scan;
    memset(&scan, 0, sizeof(struct ioctl_scan));
    scan.version = IOCTL_VERSION;

    struct ioctl_network_info network_info_ret;
    io_connect_t con = 0;
    struct ioctl_sta_info sta_info;
    memset(&sta_info, 0, sizeof(struct ioctl_sta_info));

    get_station_info(&sta_info);

    if (!open_adapter(&con)) {
        goto error;
    }
    int oid = IOCTL_80211_SCAN_RESULT;
    while (_nake_ioctl(con, &oid, true, &network_info_ret, sizeof(struct ioctl_network_info)) == kIOReturnSuccess) {
        if (list->count >= MAX_NETWORK_LIST_LENGTH) {
            break;
        }
        if (strlen((const char *)sta_info.ssid) > 0 && memcmp(sta_info.bssid, network_info_ret.bssid, ETHER_ADDR_LEN) == 0) {
            continue;
        }
        struct ioctl_network_info *info = &list->networks[list->count++];
        memcpy(info, &network_info_ret, sizeof(struct ioctl_network_info));
    }
    close_adapter(con);

    if (ioctl_set(IOCTL_80211_SCAN, &scan, sizeof(struct ioctl_scan)) != KERN_SUCCESS) {
        goto error;
    }
    return true;

error:
    return false;
}

bool connect_network(const char *ssid, const char *pwd) {
    if (!ssid || !pwd) {
        return false;
    }

    if (associate_ssid(ssid, pwd) != KERN_SUCCESS) {
        goto error;
    }

    int timeout = 20;
    size_t ssid_len = strnlen(ssid, NWID_LEN);
    while (timeout-- > 0) {
        // Sleep first to wait for state to change
        sleep(1);
        uint32_t state;
        if (get_80211_state(&state) && state == ITL80211_S_RUN) {
            station_info_t sta_info;
            if (get_station_info(&sta_info) == KERN_SUCCESS) {
                if (strncmp(ssid, (const char *)sta_info.ssid, ssid_len) == 0) {
                    if (ssid_len == NWID_LEN || sta_info.ssid[ssid_len] == '\0') {
                        return true;
                    }
                }
            }
        }
    }

error:
    return false;
}

static bool isSupportService(const char *name)
{
    if (strcmp(name, "TestService")
        && strcmp(name, "itlwmx") && strcmp(name, "itlwm")
        ) {
        return false;
    }
    return true;
}

bool open_adapter(io_connect_t *connection_t)
{
    if (!connection_t) {
        return false;
    }

    pthread_mutex_lock(&api_mutex);

    kern_return_t kr;
    io_iterator_t iter;
    bool found = false;
    io_service_t service;
    mach_port_t port = MACH_PORT_NULL;
    bool need_dealloc_port = false;

    if (__builtin_available(macOS 12.0, *)) {
        port = kIOMainPortDefault;
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if (IOMasterPort(MACH_PORT_NULL, &port) != KERN_SUCCESS) {
            pthread_mutex_unlock(&api_mutex);
            return false;
        }
#pragma clang diagnostic pop
        need_dealloc_port = true;
    }

    CFMutableDictionaryRef matchingDict = IOServiceMatching("IOEthernetController");
    kr = IOServiceGetMatchingServices(port, matchingDict, &iter);
    if (need_dealloc_port) {
        mach_port_deallocate(mach_task_self(), port);
    }
    if (kr != KERN_SUCCESS) {
        pthread_mutex_unlock(&api_mutex);
        return false;
    }

    uint32_t type = 0;
    char nn[20];
    while ((service = IOIteratorNext(iter)) && !found) {
        CFTypeRef type_ref = IORegistryEntryCreateCFProperty(service, CFSTR("IOClass"), kCFAllocatorDefault, 0);
        if (type_ref) {
            const char *name = CFStringGetCStringPtr(type_ref, 0);
            if (!name) {
                name = nn;
                CFStringGetCString(type_ref, nn, sizeof(nn), 0);
            }
            if (isSupportService(name)) {
                if (IOServiceOpen(service, mach_task_self(), type, connection_t) == KERN_SUCCESS) {
                    found = true;
                }
            }
            // Fix leak issue if there is more than one Ethernet controller
            CFRelease(type_ref);
        }
        // Fix leak issue if there is more than one Ethernet controller
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);

    if (!found) {
        pthread_mutex_unlock(&api_mutex);
    }

    return found;
}

void close_adapter(io_connect_t connection)
{
    if (connection) {
        IOServiceClose(connection);
    }
    pthread_mutex_unlock(&api_mutex);
}

kern_return_t _nake_ioctl(io_connect_t con, int *ctl, bool is_get, void *data, size_t data_len)
{
    if (!is_get) {
        *ctl |= IOCTL_MASK;
    }
    kern_return_t ret;
    if (is_get) {
        ret = IOConnectCallStructMethod(con, *ctl, NULL, 0, data, &data_len);
    } else {
        ret = IOConnectCallStructMethod(con, *ctl, data, data_len, NULL, 0);
    }
    return ret;
}

kern_return_t _ioctl(int ctl, bool is_get, void *data, size_t data_len)
{
    kern_return_t ret;
    io_connect_t con = 0;
    if (!open_adapter(&con)) {
        return KERN_FAILURE;
    }
    ret = _nake_ioctl(con, &ctl, is_get, data, data_len);
    close_adapter(con);
    return ret;
}
    
kern_return_t ioctl_set(int ctl, void *data, size_t data_len) {
    return _ioctl(ctl, false, data, data_len);
}

kern_return_t ioctl_get(int ctl, void *data, size_t data_len) {
    return _ioctl(ctl, true, data, data_len);
}

bool is_power_on(void) {
    struct ioctl_power power;
    memset(&power, 0, sizeof(struct ioctl_power));
    if (ioctl_get(IOCTL_80211_POWER, &power, sizeof(struct ioctl_power)) != KERN_SUCCESS) {
        return false;
    }
    return power.enabled != 0;
}

kern_return_t power_on(void) {
    struct ioctl_power power;
    memset(&power, 0, sizeof(struct ioctl_power));
    power.enabled = 1;
    power.version = IOCTL_VERSION;
    return ioctl_set(IOCTL_80211_POWER, &power, sizeof(struct ioctl_power));
}

kern_return_t power_off(void) {
    struct ioctl_power power;
    memset(&power, 0, sizeof(struct ioctl_power));
    power.enabled = 0;
    power.version = IOCTL_VERSION;
    return ioctl_set(IOCTL_80211_POWER, &power, sizeof(struct ioctl_power));
}

kern_return_t get_station_info(station_info_t *info)
{
    if (!info) {
        return KERN_INVALID_ARGUMENT;
    }
    memset(info, 0, sizeof(station_info_t));
    return ioctl_get(IOCTL_80211_STA_INFO, info, sizeof(struct ioctl_sta_info));
}

kern_return_t join_ssid(const char *ssid, const char *pwd)
{
    if (!ssid || !pwd) {
        return KERN_INVALID_ARGUMENT;
    }
    struct ioctl_join join;
    memset(&join, 0, sizeof(struct ioctl_join));
    join.version = IOCTL_VERSION;

    join.nwid.version = IOCTL_VERSION;
    size_t ssid_len = strnlen(ssid, NWID_LEN);
    join.nwid.len = (unsigned int)ssid_len;
    memcpy(join.nwid.nwid, ssid, ssid_len);

    join.wpa_key.version = IOCTL_VERSION;
    size_t pwd_len = strnlen(pwd, WPA_KEY_LEN);
    join.wpa_key.len = (unsigned int)pwd_len;
    memcpy(join.wpa_key.key, pwd, pwd_len);

    return ioctl_set(IOCTL_80211_JOIN, &join, sizeof(struct ioctl_join));
}

kern_return_t associate_ssid(const char *ssid, const char *pwd)
{
    if (!ssid || !pwd) {
        return KERN_INVALID_ARGUMENT;
    }
    struct ioctl_associate ass;
    memset(&ass, 0, sizeof(struct ioctl_associate));
    ass.version = IOCTL_VERSION;

    ass.nwid.version = IOCTL_VERSION;
    size_t ssid_len = strnlen(ssid, NWID_LEN);
    ass.nwid.len = (unsigned int)ssid_len;
    memcpy(ass.nwid.nwid, ssid, ssid_len);

    ass.wpa_key.version = IOCTL_VERSION;
    size_t pwd_len = strnlen(pwd, WPA_KEY_LEN);
    ass.wpa_key.len = (unsigned int)pwd_len;
    memcpy(ass.wpa_key.key, pwd, pwd_len);

    return ioctl_set(IOCTL_80211_ASSOCIATE, &ass, sizeof(struct ioctl_associate));
}

kern_return_t dis_associate_ssid(const char *ssid)
{
    if (!ssid) {
        return KERN_INVALID_ARGUMENT;
    }
    struct ioctl_disassociate dis;
    memset(&dis, 0, sizeof(struct ioctl_disassociate));
    dis.version = IOCTL_VERSION;
    size_t ssid_len = strnlen(ssid, NWID_LEN);
    memcpy(dis.ssid, ssid, ssid_len);
    return ioctl_set(IOCTL_80211_DISASSOCIATE, &dis, sizeof(struct ioctl_disassociate));
}

void api_terminate(void) {
    pthread_mutex_lock(&api_mutex);
    pthread_mutex_unlock(&api_mutex);
    pthread_mutex_destroy(&api_mutex);
}
