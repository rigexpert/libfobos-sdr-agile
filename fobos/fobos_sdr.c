//==============================================================================
//       _____     __           _______
//      /  __  \  /_/          /  ____/                                __
//     /  /_ / / _   ____     / /__  __  __   ____    ____    ____   _/ /_
//    /    __ / / / /  _  \  / ___/  \ \/ /  / __ \  / __ \  / ___\ /  _/
//   /  /\ \   / / /  /_/ / / /___   /   /  / /_/ / /  ___/ / /     / /_
//  /_ /  \_\ /_/  \__   / /______/ /_/\_\ / ____/  \____/ /_/      \___/
//               /______/                 /_/             
//  Fobos SDR (agile) special API library
//  To be used with special firmware only
//  2024.12.07
//  2025.01.29 - v.3.0.1 - fobos_sdr_reset(), fobos_sdr_read_firmware(), fobos_sdr_write_firmware
//  2025.08.25 - v.3.1.0 - VGA gain fixed
//  2025.10.23 - v.3.1.0 - new software DC filter
//  2026.03.10 - v.3.3.0 - HW rev.4 support
//==============================================================================
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif//_CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fobos_sdr.h"
#include <libusb.h>
#ifdef _WIN32
#include <conio.h>
#include <Windows.h>
#pragma comment(lib, "libusb-1.0.lib")                                             
#define printf_internal _cprintf
#else
#include <unistd.h>
#endif
#ifndef printf_internal
#define printf_internal printf
#endif // !printf_internal
//==============================================================================
#define FOBOS_SDR_PRINT_DEBUG
//==============================================================================
#define LIB_VERSION "3.3.0 (agile)"
#define DRV_VERSION "libusb"
//==============================================================================
#define FOBOS_VENDOR_ID         0x16d0
#define FOBOS_PRODUCT_ID        0x132e
#define FOBOS_DEV_ID            0x0101
//==============================================================================
#define FOBOS_SDR_CMD           0xE1
#define CMD_OPEN                0x00
#define CMD_CLOSE               0x01
#define CMD_START               0x02
#define CMD_STOP                0x03
#define CMD_SET_FREQ            0x10
#define CMD_SET_SR              0x11
#define CMD_SET_BW              0x12
#define CMD_SET_AUTOBW          0x13
#define CMD_SET_DIRECT          0x20
#define CMD_SET_CLK_SOURCE      0x21
#define CMD_SET_LNA             0x22
#define CMD_SET_VGA             0x23
#define CMD_SET_GPO             0x30
#define CMD_START_SCAN          0x40
#define CMD_STOP_SCAN           0x41
#define FOBOS_DEF_BUF_COUNT     16
#define FOBOS_MAX_BUF_COUNT     64
#define FOBOS_MIN_BUF_LENGTH    8192
#define FOBOS_DEF_BUF_LENGTH    65536
#define LIBUSB_BULK_TIMEOUT     0
#define LIBUSB_BULK_IN_ENDPOINT 0x81
#ifndef LIBUSB_CALL
#define LIBUSB_CALL
#endif
//==============================================================================
enum fobos_async_status
{
    FOBOS_IDDLE = 0,
    FOBOS_STARTING,
    FOBOS_RUNNING,
    FOBOS_CANCELING
};
//==============================================================================
struct fobos_sdr_dev_t
{
    //=== libusb ===============================================================
    libusb_context *libusb_ctx;
    struct libusb_device_handle *libusb_devh;
    uint32_t transfers_count;
    uint32_t transfer_buf_size;
    uint32_t packs_per_transfer;
    struct libusb_transfer **transfer;
    unsigned char **transfer_buf;
    int transfer_errors;
    int dev_lost;
    int use_zerocopy;
    //=== common ===============================================================
    uint16_t user_gpo;
    char hw_revision[FOBOS_INFO_LEN];
    char fw_version[FOBOS_INFO_LEN];
    char fw_build[FOBOS_INFO_LEN];
    char manufacturer[FOBOS_INFO_LEN];
    char product[FOBOS_INFO_LEN];
    char serial[FOBOS_INFO_LEN];
    //=== rx stuff =============================================================
    double rx_frequency;
    double rx_samplerate;
    double rx_bandwidth;
    double rx_auto_bandwidth;
    uint32_t rx_lna_gain;
    uint32_t rx_vga_gain;
    uint32_t rx_direct_sampling;
    int rx_scan_active;
    int rx_scan_freq_index;
    double rx_scan_freqs[FOBOS_MAX_FREQS_CNT];
    fobos_sdr_cb_t rx_cb;
    void *rx_cb_usrer;
    enum fobos_async_status rx_async_status;
    int rx_async_cancel;
    uint32_t rx_failures;
    uint32_t rx_buff_counter;
    int rx_swap_iq;
    float rx_dc_re;
    float rx_dc_im;
    float rx_avg_re;
    float rx_avg_im;
    float rx_scale_re;
    float rx_scale_im;
    float * rx_buff;
    int rx_sync_started;
    unsigned char * rx_sync_buf;
    int do_reset;
};
//==============================================================================
char * to_bin(uint16_t s16, char * str)
{
    for (uint16_t i = 0; i < 16; i++)
    {
        *str = ((s16 & 0x8000) >> 15) + '0';
        str++;
        s16 <<= 1;
    }
    *str = 0;
    return str;
}
//==============================================================================
void print_buff(void *buff, int size)
{
    int16_t * b16 = (int16_t *)buff;
    int count = size / 4;
    char bin_re[17];
    char bin_im[17];
    for (int i = 0; i < count; i++)
    {
        int16_t re16 = b16[i * 2 + 0] & 0xFFFF;
        int16_t im16 = b16[i * 2 + 1] & 0xFFFF;
        to_bin(re16, bin_re);
        to_bin(im16, bin_im);
        printf_internal("%s % 6d  %s % 6d \r\n", bin_re, re16, bin_im, im16);
    }
}
//==============================================================================
int fobos_sdr_get_api_info(char * lib_version, char * drv_version)
{
    if (lib_version)
    {
        strcpy(lib_version, LIB_VERSION" "__DATE__" "__TIME__);
    }
    if (drv_version)
    {
        strcpy(drv_version, DRV_VERSION);
    }
    return 0;
}
//==============================================================================
int fobos_sdr_get_device_count(void)
{
    int i;
    int result;
    libusb_context *ctx;
    libusb_device **list;
    uint32_t device_count = 0;
    struct libusb_device_descriptor dd;
    ssize_t cnt;
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s();\n", __FUNCTION__);
#endif // FOBOS_SDR_PRINT_DEBUG
    result = libusb_init(&ctx);
    if (result < 0)
    {
        return 0;
    }
    cnt = libusb_get_device_list(ctx, &list);
    for (i = 0; i < cnt; i++)
    {
        libusb_get_device_descriptor(list[i], &dd);
#ifdef FOBOS_SDR_PRINT_DEBUG
        printf_internal("%04x:%04x\n", dd.idVendor, dd.idProduct);
#endif // FOBOS_SDR_PRINT_DEBUG        
        if ((dd.idVendor == FOBOS_VENDOR_ID) && 
            (dd.idProduct == FOBOS_PRODUCT_ID) &&
            (dd.bcdDevice == FOBOS_DEV_ID))
        {
            device_count++;
        }
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return device_count;
}
//==============================================================================
int fobos_sdr_list_devices(char * serials)
{
    int i;
    int result;
    libusb_context *ctx;
    libusb_device **list;
    uint32_t device_count = 0;
    struct libusb_device_descriptor dd;
    ssize_t cnt;
    libusb_device_handle *handle;
    char string[256];
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s();\n", __FUNCTION__);
#endif // FOBOS_SDR_PRINT_DEBUG
    memset(string, 0, sizeof(string));
    result = libusb_init(&ctx);
    if (result < 0)
    {
        return 0;
    }
    cnt = libusb_get_device_list(ctx, &list);
    for (i = 0; i < cnt; i++)
    {
        libusb_get_device_descriptor(list[i], &dd);

        if ((dd.idVendor == FOBOS_VENDOR_ID) && 
            (dd.idProduct == FOBOS_PRODUCT_ID) &&
            (dd.bcdDevice == FOBOS_DEV_ID))
        {
            if (serials)
            {
                handle = 0;
                result = libusb_open(list[i], &handle);
                if ((result == 0) && (handle))
                {
                    result = libusb_get_string_descriptor_ascii(handle, dd.iSerialNumber, (unsigned char*)string, sizeof(string));
                    if (result > 0)
                    {
                        serials = strcat(serials, string);
                    }
                    libusb_close(handle);
                }
                else
                {
                    serials = strcat(serials, "XXXXXXXXXXXX");
                }
                serials = strcat(serials, " ");
            }
            device_count++;
        }
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return device_count;
}
//==============================================================================
int fobos_sdr_check(struct fobos_sdr_dev_t * dev)
{
    if (dev != NULL)
    {
        if ((dev->libusb_ctx != NULL) && (dev->libusb_devh != NULL))
        {
            return FOBOS_ERR_OK;
        }
        return FOBOS_ERR_NOT_OPEN;
    }
    return FOBOS_ERR_NO_DEV;
}
//==============================================================================
#define CTRLI       (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRLO       (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)
#define CTRL_TIMEOUT    300
int fobos_sdr_fx3_cmd(struct fobos_sdr_dev_t * dev, uint8_t code, uint16_t value, uint16_t index)
{
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    result = libusb_control_transfer(dev->libusb_devh, CTRLO, code, value, index, 0, 0, CTRL_TIMEOUT);
    return result;
}
//==============================================================================
int fobos_sdr_ctrl_in(struct fobos_sdr_dev_t * dev, uint8_t code, uint16_t value, uint16_t index, uint8_t *data, uint16_t len)
{
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    result = libusb_control_transfer(dev->libusb_devh, CTRLI, code, value, index, data, len, CTRL_TIMEOUT);
    return result;
}
//==============================================================================
int fobos_sdr_ctrl_out(struct fobos_sdr_dev_t * dev, uint8_t code, uint16_t value, uint16_t index, uint8_t *data, uint16_t len)
{
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    result = libusb_control_transfer(dev->libusb_devh, CTRLO, code, value, index, data, len, CTRL_TIMEOUT);
    return result;
}
//==============================================================================
int fobos_sdr_set_user_gpo(struct fobos_sdr_dev_t * dev, uint8_t value)
{
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(0x%04x);\n", __FUNCTION__, value);
#endif // FOBOS_SDR_PRINT_DEBUG
    return fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_SET_GPO, value);
}
//==============================================================================
int fobos_sdr_open(struct fobos_sdr_dev_t ** out_dev, uint32_t index)
{
    int result = 0;
    int i = 0;
    struct fobos_sdr_dev_t * dev = NULL;
    libusb_device **dev_list;
    libusb_device *device = NULL;
    ssize_t cnt;
    uint32_t device_count = 0;
    struct libusb_device_descriptor dd;
    dev = (struct fobos_sdr_dev_t*)malloc(sizeof(struct fobos_sdr_dev_t));
    if (NULL == dev)
    {
        return FOBOS_ERR_NO_MEM;
    }
    memset(dev, 0, sizeof(struct fobos_sdr_dev_t));
    result = libusb_init(&dev->libusb_ctx);
    if (result < 0)
    {
        free(dev);
        return result;
    }
    cnt = libusb_get_device_list(dev->libusb_ctx, &dev_list);
    for (i = 0; i < cnt; i++)
    {
        libusb_get_device_descriptor(dev_list[i], &dd);

        if ((dd.idVendor == FOBOS_VENDOR_ID) && 
            (dd.idProduct == FOBOS_PRODUCT_ID) &&
            (dd.bcdDevice == FOBOS_DEV_ID))
        {
            if (index == device_count)
            {
                device = dev_list[i];
                break;
            }
            device_count++;
        }
    }
    if (device)
    {
        result = libusb_open(device, &dev->libusb_devh);
        if (result == 0)
        {
            libusb_get_string_descriptor_ascii(dev->libusb_devh, dd.iSerialNumber, (unsigned char*)dev->serial, sizeof(dev->serial));
            libusb_get_string_descriptor_ascii(dev->libusb_devh, dd.iManufacturer, (unsigned char*)dev->manufacturer, sizeof(dev->manufacturer));
            libusb_get_string_descriptor_ascii(dev->libusb_devh, dd.iProduct, (unsigned char*)dev->product, sizeof(dev->product));
            result = libusb_claim_interface(dev->libusb_devh, 0);
            if (result == 0)
            {
                *out_dev = dev;
                //======================================================================
                result  = libusb_control_transfer(dev->libusb_devh, CTRLI, 0xE8, 0, 0, (unsigned char*)dev->hw_revision, sizeof(dev->hw_revision), CTRL_TIMEOUT);
                if (result <= 0)
                {
                    strcpy(dev->hw_revision, "2.0.0");
                }
                result = libusb_control_transfer(dev->libusb_devh, CTRLI, 0xE8, 1, 0, (unsigned char*)dev->fw_version, sizeof(dev->fw_version), CTRL_TIMEOUT);
                if (result <= 0)
                {
                    strcpy(dev->hw_revision, "2.0.0");
                }
                result = libusb_control_transfer(dev->libusb_devh, CTRLI, 0xE8, 2, 0, (unsigned char*)dev->fw_build, sizeof(dev->fw_build), CTRL_TIMEOUT);
                if (result <= 0)
                {
                    strcpy(dev->fw_build, "unknown");
                }
                //======================================================================
                dev->rx_scale_re = 1.0f / 32768.0f;
                dev->rx_scale_im = 1.0f / 32768.0f;
                dev->rx_dc_re = 8192.0f;
                dev->rx_dc_im = 8192.0f;
                dev->rx_avg_re = 0.0f;
                dev->rx_avg_im = 0.0f;
                if (fobos_sdr_check(dev) == 0)
                {
                    fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_OPEN, 0);
                    fobos_sdr_set_frequency(dev, 100E6);
                    fobos_sdr_set_samplerate(dev, 25000000.0);
                    fobos_sdr_set_auto_bandwidth(dev, 0.9);
                    libusb_free_device_list(dev_list, 1);
                    return FOBOS_ERR_OK;
                }
            }
            else
            {
                printf_internal("usb_claim_interface error %d\n", result);
            }
        }
        else
        {
            printf_internal("usb_open error %d\n", result);
#ifndef _WIN32
            if (result == LIBUSB_ERROR_ACCESS)
            {
                printf_internal("Please fix the device permissions by installing fobos-sdr.rules\n");
            }
#endif
        }
    }
    libusb_free_device_list(dev_list, 1);
    if (dev->libusb_devh)
    {
        libusb_close(dev->libusb_devh);
    }
    if (dev->libusb_ctx)
    {
        libusb_exit(dev->libusb_ctx);
    }
    free(dev);
    return FOBOS_ERR_NO_DEV;
}
//==============================================================================
int fobos_sdr_close(struct fobos_sdr_dev_t * dev)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s();\n", __FUNCTION__);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    fobos_sdr_cancel_async(dev);
    if (dev->rx_sync_started)
    {
        fobos_sdr_stop_sync(dev);
    }
    while (FOBOS_IDDLE != dev->rx_async_status)
    {
#ifdef FOBOS_SDR_PRINT_DEBUG
        printf_internal("s");
#endif
#ifdef _WIN32
        Sleep(1);
#else
        sleep(1);
#endif
    }
    fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_CLOSE, 0);
    if (dev->do_reset)
    {
        libusb_control_transfer(dev->libusb_devh, CTRLO, 0xE0, 0, 0, 0, 0, CTRL_TIMEOUT);
    }
    libusb_close(dev->libusb_devh);
    libusb_exit(dev->libusb_ctx);
    free(dev);
    return result;
}
//==============================================================================
int fobos_sdr_reset(struct fobos_sdr_dev_t * dev)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s();\n", __FUNCTION__);
#endif // FOBOS_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    dev->do_reset = 1;
    return fobos_sdr_close(dev);
}
//==============================================================================
int fobos_sdr_get_board_info(struct fobos_sdr_dev_t * dev, char * hw_revision, char * fw_version, char * manufacturer, char * product, char * serial)
{
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s();\n", __FUNCTION__);
#endif // FOBOS_SDR_PRINT_DEBUG
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (hw_revision)
    {
        strcpy(hw_revision, dev->hw_revision);
    }
    if (fw_version)
    {
        strcpy(fw_version, dev->fw_version);
        strcat(fw_version, " ");
        strcat(fw_version, dev->fw_build);
    }
    if (manufacturer)
    {
        strcpy(manufacturer, dev->manufacturer);
    }
    if (product)
    {
        strcpy(product, dev->product);
    }
    if (serial)
    {
        strcpy(serial, dev->serial);
    }
    return result;
}
//==============================================================================
int fobos_sdr_set_frequency(struct fobos_sdr_dev_t * dev, double value)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(%f);\n", __FUNCTION__, value);
#endif // FOBOS_SDR_PRINT_DEBUG    
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (dev->rx_frequency != value)
    {
        uint64_t freq = (uint64_t)value;
        fobos_sdr_ctrl_out(dev, FOBOS_SDR_CMD, CMD_SET_FREQ, 0, (uint8_t*)&freq, 8);
    }
    return result;
}
//==============================================================================
int fobos_sdr_start_scan(struct fobos_sdr_dev_t * dev, double *frequencies, unsigned int count)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s( .. , %d, %d);\n", __FUNCTION__, count, count);
#endif // FOBOS_SDR_PRINT_DEBUG    
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if ((!frequencies) || (count < FOBOS_MIN_FREQS_CNT) || (count > FOBOS_MAX_FREQS_CNT))
    {
        return FOBOS_ERR_UNSUPPORTED;
    }
    result = fobos_sdr_ctrl_out(dev, FOBOS_SDR_CMD, CMD_START_SCAN, 0, (uint8_t*)frequencies, 8 * count);
    if (result == 8 * (int)count)
    {
        dev->rx_scan_active = 1;
        result = FOBOS_ERR_OK;
    }
    return result;
}
//==============================================================================
int fobos_sdr_stop_scan(struct fobos_sdr_dev_t * dev)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s();\n", __FUNCTION__);
#endif // FOBOS_SDR_PRINT_DEBUG    
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    result = fobos_sdr_ctrl_out(dev, FOBOS_SDR_CMD, CMD_STOP_SCAN, 0, 0, 0);
    if (result == 0)
    {
        dev->rx_scan_active = 0;
        dev->rx_scan_freq_index = -1;
        result = FOBOS_ERR_OK;
    }
    return result;
}
//==============================================================================
int fobos_sdr_get_scan_index(struct fobos_sdr_dev_t * dev)
{
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    return dev->rx_scan_freq_index;
}
//==============================================================================
int fobos_sdr_is_scanning(struct fobos_sdr_dev_t * dev)
{
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    return dev->rx_scan_active;
}
//==============================================================================
int fobos_sdr_set_direct_sampling(struct fobos_sdr_dev_t * dev, unsigned int enabled)
{
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(%d);\n", __FUNCTION__, enabled);
#endif // FOBOS_SDR_PRINT_DEBUG
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (dev->rx_direct_sampling != enabled)
    {
        fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_SET_DIRECT, enabled);
        dev->rx_direct_sampling = enabled;
    }
    return result;
}
//==============================================================================
int fobos_sdr_set_lna_gain(struct fobos_sdr_dev_t * dev, unsigned int value)
{
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(%d)\n", __FUNCTION__, value);
#endif // FOBOS_SDR_PRINT_DEBUG
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (value > 3) value = 3;
    if (value != dev->rx_lna_gain)
    {
        fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_SET_LNA, value);
        dev->rx_lna_gain = value;
    }
    return result;
}
//==============================================================================
int fobos_sdr_set_vga_gain(struct fobos_sdr_dev_t * dev, unsigned int value)
{
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(%d)\n", __FUNCTION__, value);
#endif // FOBOS_SDR_PRINT_DEBUG
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    result = 0;
    if (value > 31) value = 31;
    if (value != dev->rx_vga_gain)
    {
        fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_SET_VGA, value);
        dev->rx_vga_gain = value;
    }
    return result;
}
//==============================================================================
int fobos_sdr_set_bandwidth(struct fobos_sdr_dev_t * dev, double value)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(%f)\n", __FUNCTION__, value);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    dev->rx_auto_bandwidth = 0.0; // disable the auto bandwidth mode
    if (dev->rx_bandwidth != value)
    {
        uint64_t val = (uint64_t)value;
        fobos_sdr_ctrl_out(dev, FOBOS_SDR_CMD, CMD_SET_BW, 0, (uint8_t*)&val, 8);
        dev->rx_bandwidth = value;
    }
    return result;
}
//==============================================================================
int fobos_sdr_set_auto_bandwidth(struct fobos_sdr_dev_t * dev, double value)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(%f)\n", __FUNCTION__, value);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if ((value < 0.0) || (value > 1.0))
    {
        return FOBOS_ERR_BAD_PARAM;
    }
    if (dev->rx_auto_bandwidth != value)
    {
        uint64_t val = (uint64_t)(value*1024.0);
        fobos_sdr_ctrl_out(dev, FOBOS_SDR_CMD, CMD_SET_AUTOBW, 0, (uint8_t*)&val, 8);
        dev->rx_bandwidth = value;
    }
    return result;
}
//==============================================================================
const double fobos30_sample_rates[] =
{
    50000000.0,
    40000000.0,
    32000000.0,
    25000000.0,
    20000000.0,
    16000000.0,
    12500000.0,
    10000000.0,
    8000000.0
};
//==============================================================================
const double fobos40_sample_rates[] =
{
    64000000.0,
    58000000.0,
    50000000.0,
    40000000.0,
    32000000.0,
    25000000.0,
    20000000.0,
    16000000.0,
    12500000.0,
    10000000.0,
    8000000.0
};
//==============================================================================
int fobos_sdr_get_samplerates(struct fobos_sdr_dev_t * dev, double * values, unsigned int * count)
{
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    result = FOBOS_ERR_UNSUPPORTED;
    if ((dev->fw_version[0] == '2') || (dev->fw_version[0] == '3'))
    {
        result = 0;
        if (count)
        {
            *count = sizeof(fobos30_sample_rates) / sizeof(fobos30_sample_rates[0]);
        }
        if (values)
        {
            memcpy(values, fobos30_sample_rates, sizeof(fobos30_sample_rates));
        }
    }
    if (dev->fw_version[0] == '4')
    {
        result = 0;
        if (count)
        {
            *count = sizeof(fobos40_sample_rates) / sizeof(fobos40_sample_rates[0]);
        }
        if (values)
        {
            memcpy(values, fobos40_sample_rates, sizeof(fobos40_sample_rates));
        }
    }
    return result;
}
//==============================================================================
int fobos_sdr_set_samplerate(struct fobos_sdr_dev_t * dev, double value)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(%f)\n", __FUNCTION__, value);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    uint64_t val = (uint64_t)value;
    fobos_sdr_ctrl_out(dev, FOBOS_SDR_CMD, CMD_SET_SR, 0, (uint8_t*)&val, 8);
    return result;
}
//==============================================================================
int fobos_sdr_set_clk_source(struct fobos_sdr_dev_t * dev, int value)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(%d)\n", __FUNCTION__, value);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_SET_CLK_SOURCE , value);
    return result;
}
//==============================================================================
void fobos_sdr_calibrate(struct fobos_sdr_dev_t * dev, void * data, size_t size)
{
    size_t complex_samples_count = size / 4; 
    int16_t * psample = (int16_t *)data;
    int64_t summ_re = 0ll;
    int64_t summ_im = 0ll;
    size_t chunks_count = complex_samples_count / 4;
    for (size_t i = 0; i < chunks_count; i++)
    {
        summ_re += psample[0];
        summ_im += psample[1];
        summ_re += psample[2];
        summ_im += psample[3];
        summ_re += psample[4];
        summ_im += psample[5];
        summ_re += psample[6];
        summ_im += psample[7];
        psample += 8;
    }
    summ_re /= complex_samples_count;
    summ_im /= complex_samples_count;
    int16_t avg_re = (int16_t)summ_re;
    int16_t avg_im = (int16_t)summ_im;
    psample = (int16_t *)data;
    for (size_t i = 0; i < chunks_count; i++)
    {
        summ_re += abs(psample[0] - avg_re);
        summ_im += abs(psample[1] - avg_im);
        summ_re += abs(psample[2] - avg_re);
        summ_im += abs(psample[3] - avg_im);
        summ_re += abs(psample[4] - avg_re);
        summ_im += abs(psample[5] - avg_im);
        summ_re += abs(psample[6] - avg_re);
        summ_im += abs(psample[7] - avg_im);
        psample += 8;
    }
    dev->rx_avg_re += 0.0001f* ((float)summ_re - dev->rx_avg_re);
    dev->rx_avg_im += 0.0001f* ((float)summ_im - dev->rx_avg_im);
    if ((dev->rx_avg_re > 0.0f) && (dev->rx_avg_im > 0.0f) && !dev->rx_direct_sampling)
    {
        float ratio = dev->rx_avg_re / dev->rx_avg_im;
#ifdef FOBOS_SDR_PRINT_DEBUG
        if (dev->rx_buff_counter % 256 == 0)
        {
            printf_internal("re/im scale = %f %d\n", ratio, dev->rx_buff_counter);
        }
#endif // FOBOS_SDR_PRINT_DEBUG
        if ((ratio < 1.6f) && (ratio > 0.625f))
        {
            dev->rx_scale_im = dev->rx_scale_re * ratio;
        }
    }
}
//==============================================================================
#define FOBOS_SWAP_IQ_HW 1
void fobos_sdr_convert_buff(struct fobos_sdr_dev_t * dev, unsigned char * data, size_t size, float * dst_samples)
{
    size_t complex_samples_count = size / 4;
    size_t chunks_count = complex_samples_count / 4;
    int16_t * src = (int16_t *)data;
    float * dst = dst_samples;
    int rx_swap_iq = dev->rx_swap_iq ^ FOBOS_SWAP_IQ_HW;
    float re = 0.0f;
    float im = 0.0f;
    float scale_re = 1.0f / 32768.0f;
    float scale_im = 1.0f / 32768.0f;
    if (dev->rx_direct_sampling)
    {
        rx_swap_iq = FOBOS_SWAP_IQ_HW;
    }
    else
    {
        fobos_sdr_calibrate(dev, data, size / 16);
        scale_re = dev->rx_scale_re;
        scale_im = dev->rx_scale_im;
    }
#ifdef FOBOS_PRINT_DEBUG
    if (dev->rx_buff_counter % 256 == 0)
    {
        print_buff(data, 64);
    }
#endif // FOBOS_PRINT_DEBUG
    float k = 0.0004f; // ~ play around
    float dc_re = dev->rx_dc_re;
    float dc_im = dev->rx_dc_im;
    if (rx_swap_iq)
    {
        for (size_t i = 0; i < chunks_count; i++)
        {
            // 0
            re = (float)(src[1] & 0x3FFF);
            im = (float)(src[0] & 0x3FFF);
            dc_re += k * (re - dc_re);
            dc_im += k * (im - dc_im);
            dst[0] = (re - dc_re) * scale_re;
            dst[1] = (im - dc_im) * scale_im;
            // 1
            re = (float)(src[3] & 0x3FFF);
            im = (float)(src[2] & 0x3FFF);
            dc_re += k * (re - dc_re);
            dc_im += k * (im - dc_im);
            dst[2] = (re - dc_re) * scale_re;
            dst[3] = (im - dc_im) * scale_im;
            // 2
            re = (float)(src[5] & 0x3FFF);
            im = (float)(src[4] & 0x3FFF);
            dc_re += k * (re - dc_re);
            dc_im += k * (im - dc_im);
            dst[4] = (re - dc_re) * scale_re;
            dst[5] = (im - dc_im) * scale_im;
            // 3
            re = (float)(src[7] & 0x3FFF);
            im = (float)(src[6] & 0x3FFF);
            dc_re += k * (re - dc_re);
            dc_im += k * (im - dc_im);
            dst[6] = (re - dc_re) * scale_re;
            dst[7] = (im - dc_im) * scale_im;
            //
            src += 8;
            dst += 8;
        }
    }
    else
    {
        for (size_t i = 0; i < chunks_count; i++)
        {
            // 0
            re = (float)(src[0] & 0x3FFF);
            im = (float)(src[1] & 0x3FFF);
            dc_re += k * (re - dc_re);
            dc_im += k * (im - dc_im);
            dst[0] = (re - dc_re) * scale_re;
            dst[1] = (im - dc_im) * scale_im;
            // 1
            re = (float)(src[2] & 0x3FFF);
            im = (float)(src[3] & 0x3FFF);
            dc_re += k * (re - dc_re);
            dc_im += k * (im - dc_im);
            dst[2] = (re - dc_re) * scale_re;
            dst[3] = (im - dc_im) * scale_im;
            // 2
            re = (float)(src[4] & 0x3FFF);
            im = (float)(src[5] & 0x3FFF);
            dc_re += k * (re - dc_re);
            dc_im += k * (im - dc_im);
            dst[4] = (re - dc_re) * scale_re;
            dst[5] = (im - dc_im) * scale_im;
            // 3
            re = (float)(src[6] & 0x3FFF);
            im = (float)(src[7] & 0x3FFF);
            dc_re += k * (re - dc_re);
            dc_im += k * (im - dc_im);
            dst[6] = (re - dc_re) * scale_re;
            dst[7] = (im - dc_im) * scale_im;
            //
            src += 8;
            dst += 8;
        }
    }
    dev->rx_dc_re = dc_re;
    dev->rx_dc_im = dc_im;
}
//==============================================================================
void fobos_sdr_convert_all(struct fobos_sdr_dev_t * dev, unsigned char * data, size_t size, float * dst_samples)
{
    //if (dev->rx_buff_counter % 256 == 0)
    //{
    //    print_buff(data, 32);
    //}
    uint16_t* ps = (uint16_t*)data;
    uint16_t p0 = ps[0];
    uint16_t p1 = ps[1];
    dev->rx_swap_iq = ((p0 & 0x8000) && (p1 & 0x8000));
    int is_swapped = ((p0 & 0x4000) && (p1 & 0x4000));
    size_t i0 = (p0 & p1 & 0x4000);
    size_t i1 = i0 ^ 0x4000;
    if (dev->rx_scan_active)
    {
        dev->rx_scan_freq_index = -1;
        ps += (i0 / 2);
        p0 = ps[0] & 0x3FFF;
        p1 = ps[1] & 0x3FFF;
        if ((p0 == 0x2AAA) && (p1 == 0x1555))
        {
            dev->rx_scan_freq_index = ps[2] & ps[3];
            //printf_internal("%d ", dev->rx_scan_freq_index);
            ps[0] = ps[4];
            ps[1] = ps[5];
            ps[2] = ps[6];
            ps[3] = ps[7];
        }
    }
    if (!dev->rx_direct_sampling)
    {
        fobos_sdr_calibrate(dev, data, size / 16);
    }
    if (is_swapped)
    {
        //printf_internal("w");
        size_t pairs_count = size / 0x8000;
        for (size_t p = 0; p < pairs_count; p++)
        {
            fobos_sdr_convert_buff(dev, data + p * 0x8000 + i0, 0x4000, dst_samples + p * 0x4000 + 0x0000);
            fobos_sdr_convert_buff(dev, data + p * 0x8000 + i1, 0x4000, dst_samples + p * 0x4000 + 0x2000);
        }
    }
    else
    {
        fobos_sdr_convert_buff(dev, data, size, dst_samples);
    }
}
//==============================================================================
int fobos_sdr_alloc_buffers(struct fobos_sdr_dev_t *dev)
{
    unsigned int i;
    int result = fobos_sdr_check(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (!dev->transfer)
    {
        dev->transfer = malloc(dev->transfers_count * sizeof(struct libusb_transfer *));
        if (dev->transfer)
        {
            for (i = 0; i < dev->transfers_count; i++)
            {
                dev->transfer[i] = libusb_alloc_transfer(0);
            }
        }
    }
    if (dev->transfer_buf)
    {
        return FOBOS_ERR_NO_MEM;
    }
    dev->transfer_buf = malloc(dev->transfers_count * sizeof(unsigned char *));
    if (dev->transfer_buf)
    {
        memset(dev->transfer_buf, 0, dev->transfers_count * sizeof(unsigned char*));
    }
#if defined(ENABLE_ZEROCOPY) && defined (__linux__) && LIBUSB_API_VERSION >= 0x01000105
    printf_internal("Allocating %d zero-copy buffers\n", dev->transfers_count);
    dev->use_zerocopy = 1;
    for (i = 0; i < dev->transfers_count; ++i)
    {
        dev->transfer_buf[i] = libusb_dev_mem_alloc(dev->libusb_devh, dev->transfer_buf_size);
        if (dev->transfer_buf[i])
        {
            if (dev->transfer_buf[i][0] || memcmp(dev->transfer_buf[i],
                dev->transfer_buf[i] + 1,
                dev->transfer_buf_size - 1))
            {
                printf_internal("Kernel usbfs mmap() bug, falling back to buffers\n");
                dev->use_zerocopy = 0;
                break;
            }
        }
        else
        {
            printf_internal("Failed to allocate zero-copy buffer for transfer %d\n", i);
            dev->use_zerocopy = 0;
            break;
        }
    }
    if (!dev->use_zerocopy)
    {
        for (i = 0; i < dev->transfers_count; ++i)
        {
            if (dev->transfer_buf[i])
            {
                libusb_dev_mem_free(dev->libusb_devh, dev->transfer_buf[i], dev->transfer_buf_size);
            }
        }
    }
#endif
    if (!dev->use_zerocopy)
    {
        for (i = 0; i < dev->transfers_count; ++i)
        {
            dev->transfer_buf[i] = (unsigned char *)malloc(dev->transfer_buf_size);

            if (!dev->transfer_buf[i])
            {
                return FOBOS_ERR_NO_MEM;
            }
        }
    }
    return FOBOS_ERR_OK;
}
//==============================================================================
int fobos_sdr_free_buffers(struct fobos_sdr_dev_t *dev)
{
    unsigned int i;
    int result = fobos_sdr_check(dev);
    if (result != 0)
    {
        return result;
    }
    if (dev->transfer)
    {
        for (i = 0; i < dev->transfers_count; ++i)
        {
            if (dev->transfer[i])
            {
                libusb_free_transfer(dev->transfer[i]);
            }
        }
        free(dev->transfer);
        dev->transfer = NULL;
    }
    if (dev->transfer_buf)
    {
        for (i = 0; i < dev->transfers_count; ++i)
        {
            if (dev->transfer_buf[i])
            {
                if (dev->use_zerocopy)
                {
#if defined (__linux__) && LIBUSB_API_VERSION >= 0x01000105
                    libusb_dev_mem_free(dev->libusb_devh, dev->transfer_buf[i], dev->transfer_buf_size);
#endif
                }
                else
                {
                    free(dev->transfer_buf[i]);
                }
            }
        }
        free(dev->transfer_buf);
        dev->transfer_buf = NULL;
    }
    return FOBOS_ERR_OK;
}
//==============================================================================
static void LIBUSB_CALL _libusb_callback(struct libusb_transfer *transfer)
{
    struct fobos_sdr_dev_t *dev = (struct fobos_sdr_dev_t *)transfer->user_data;
    if (LIBUSB_TRANSFER_COMPLETED == transfer->status)
    {
        if (transfer->actual_length == (int)dev->transfer_buf_size)
        {
            //printf_internal(".");
            if (FOBOS_CANCELING != dev->rx_async_status)
            {
                dev->rx_buff_counter++;
                fobos_sdr_convert_all(dev, transfer->buffer, transfer->actual_length, dev->rx_buff);
                uint32_t complex_samples_count = transfer->actual_length / 4;
                if (dev->rx_cb)
                {
                    dev->rx_cb(dev->rx_buff, complex_samples_count, dev, dev->rx_cb_usrer);
                }
            }
        }
        else
        {
            printf_internal("E");
            dev->rx_failures++;
        }
        libusb_submit_transfer(transfer);
        dev->transfer_errors = 0;
    }
    else if (LIBUSB_TRANSFER_CANCELLED != transfer->status)
    {
        printf_internal("transfer->status = %d\n", transfer->status);
#ifndef _WIN32
        if (LIBUSB_TRANSFER_ERROR == transfer->status)
        {
            dev->transfer_errors++;
        }
        if (dev->transfer_errors >= (int)dev->transfers_count || LIBUSB_TRANSFER_NO_DEVICE == transfer->status)
        {
            dev->dev_lost = 1;
            fobos_sdr_cancel_async(dev);
        }
#else
        dev->dev_lost = 1;
        fobos_sdr_cancel_async(dev);
#endif
    }
}
//==============================================================================
int fobos_sdr_read_async(struct fobos_sdr_dev_t * dev, fobos_sdr_cb_t cb, void *user, uint32_t buf_count, uint32_t buf_length)
{
    unsigned int i;
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s(%p, %p, %p, %d, %d)\n", __FUNCTION__, (void*)dev, (void*)cb, (void*)user, buf_count, buf_length);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (FOBOS_IDDLE != dev->rx_async_status)
    {
        return FOBOS_ERR_ASYNC_IN_SYNC;
    }
    result = FOBOS_ERR_OK;
    struct timeval tv0 = { 0, 0 };
    struct timeval tv1 = { 1, 0 };
    struct timeval tvx = { 0, 10000 };
    dev->rx_async_status = FOBOS_STARTING;
    dev->rx_async_cancel = 0;
    dev->rx_buff_counter = 0;
    dev->rx_cb = cb;
    dev->rx_cb_usrer = user;
    dev->rx_avg_re = 0.0f;
    dev->rx_avg_im = 0.0f;
    if (buf_count == 0)
    {
        buf_count = FOBOS_DEF_BUF_COUNT;
    }
    if (buf_count > FOBOS_MAX_BUF_COUNT)
    {
        buf_count = FOBOS_MAX_BUF_COUNT;
    }
    dev->transfers_count = buf_count;

    if (buf_length < FOBOS_MIN_BUF_LENGTH)
    {
        buf_length = FOBOS_DEF_BUF_LENGTH;
    }
    dev->packs_per_transfer = 2 * (buf_length / 8192); // should be even
    if (dev->packs_per_transfer < 16) // the minimal count for the scan
    {
        dev->rx_scan_active = 0;
    }
    buf_length = 4096 * dev->packs_per_transfer; // complex samples count

    dev->transfer_buf_size = buf_length * 4; //raw int16 buff size

    result = fobos_sdr_alloc_buffers(dev);
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }

    dev->rx_buff = (float*)malloc(buf_length * 2 * sizeof(float));

    fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_START, dev->packs_per_transfer);

    for (i = 0; i < dev->transfers_count; ++i)
    {
        libusb_fill_bulk_transfer(dev->transfer[i],
            dev->libusb_devh,
            LIBUSB_BULK_IN_ENDPOINT,
            dev->transfer_buf[i],
            dev->transfer_buf_size,
            _libusb_callback,
            (void *)dev,
            LIBUSB_BULK_TIMEOUT);

        result = libusb_submit_transfer(dev->transfer[i]);
        if (result < 0)
        {
            printf_internal("Failed to submit transfer #%i, err %i\n", i, result);
            dev->rx_async_status = FOBOS_CANCELING;
            break;
        }
    }

    dev->rx_async_status = FOBOS_RUNNING;
    printf_internal("FOBOS_RUNNING...\n");
    enum fobos_async_status status = FOBOS_IDDLE;
    while (FOBOS_IDDLE != dev->rx_async_status)
    {
        //printf_internal(" >%d< ", (int)dev->rx_async_status);
        result = libusb_handle_events_timeout_completed(dev->libusb_ctx, &tv1, &dev->rx_async_cancel);
        if (result < 0)
        {
            printf_internal("libusb_handle_events_timeout_completed returned: %d\n", result);
            if (result == LIBUSB_ERROR_INTERRUPTED)
            {
                continue;
            }
            else
            {
                break;
            }
        }
        if (FOBOS_CANCELING == dev->rx_async_status)
        {
            printf_internal("FOBOS_CANCELING...\n");
            status = FOBOS_IDDLE;
            if (!dev->transfer)
            {
                break;
            }
            for (i = 0; i < dev->transfers_count; ++i)
            {
                //printf_internal(" ~%d \n", i);
                if (!dev->transfer[i])
                    continue;
                if (LIBUSB_TRANSFER_CANCELLED != dev->transfer[i]->status)
                {
                    struct libusb_transfer * xf = dev->transfer[i];
                    printf_internal(" ~%08x", xf->flags);
                    
                    result = libusb_cancel_transfer(dev->transfer[i]);
                    libusb_handle_events_timeout_completed(dev->libusb_ctx, &tvx, NULL);
                    if (result < 0)
                    {
                        printf_internal("libusb_cancel_transfer[%d] returned: %d %s\n", i, result, libusb_error_name(result));
                        continue;
                    }
                    status = FOBOS_CANCELING;
                }
            }
            if (dev->dev_lost || FOBOS_IDDLE == status)
            {
                libusb_handle_events_timeout_completed(dev->libusb_ctx, &tvx, NULL);
                break;
            }
        }
    }
    fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_STOP, 0);
    fobos_sdr_free_buffers(dev);
    free(dev->rx_buff);
    dev->rx_buff = NULL;
    dev->rx_async_status = FOBOS_IDDLE;
    dev->rx_async_cancel = 0;
    return result;
}
//==============================================================================
int fobos_sdr_cancel_async(struct fobos_sdr_dev_t * dev)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s()\n", __FUNCTION__);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (FOBOS_RUNNING == dev->rx_async_status)
    {
        dev->rx_async_status = FOBOS_CANCELING;
        dev->rx_async_cancel = 1;
    }
    return 0;
}
//==============================================================================
int fobos_sdr_start_sync(struct fobos_sdr_dev_t * dev, uint32_t buf_length)
{
    int i = 0;
    int actual = 0;
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s()\n", __FUNCTION__);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (FOBOS_IDDLE != dev->rx_async_status)
    {
        return FOBOS_ERR_SYNC_IN_ASYNC;
    }
    if (dev->rx_sync_started)
    {
        return 0;
    }
    if (buf_length == 0)
    {
        buf_length = FOBOS_DEF_BUF_LENGTH;
    }
    dev->packs_per_transfer = 2 * (buf_length / 8192); // should be even
    if (dev->packs_per_transfer < 16) // the minimal count for the scan
    {
        dev->rx_scan_active = 0;
    }
    buf_length = 4096 * dev->packs_per_transfer; // complex samples count
    dev->transfer_buf_size = buf_length * 4;
    dev->rx_sync_buf = (unsigned char *)malloc(dev->transfer_buf_size);
    if (dev->rx_sync_buf == 0)
    {
        dev->transfer_buf_size = 0;
        return FOBOS_ERR_NO_MEM;
    }
    fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_START, dev->packs_per_transfer);
    dev->rx_sync_started = 1;
    return FOBOS_ERR_OK;
}
//==============================================================================
int fobos_sdr_read_sync(struct fobos_sdr_dev_t * dev, float * buf, uint32_t * actual_buf_length)
{
    int actual = 0;
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s()\n", __FUNCTION__);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (dev->rx_sync_started != 1)
    {
        return FOBOS_ERR_SYNC_NOT_STARTED;
    }
    result = libusb_bulk_transfer(
        dev->libusb_devh,
        LIBUSB_BULK_IN_ENDPOINT, 
        dev->rx_sync_buf, 
        dev->transfer_buf_size, 
        &actual, 
        LIBUSB_BULK_TIMEOUT);
    if (result == FOBOS_ERR_OK)
    {
        fobos_sdr_convert_all(dev, dev->rx_sync_buf, actual, buf);
        if (actual_buf_length)
        {
            *actual_buf_length = actual / 4;
        }
    }
    return result;
}
//==============================================================================
int fobos_sdr_stop_sync(struct fobos_sdr_dev_t * dev)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_SDR_PRINT_DEBUG
    printf_internal("%s()\n", __FUNCTION__);
#endif // FOBOS_SDR_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    result = 0;
    if (dev->rx_sync_started)
    {
        fobos_sdr_fx3_cmd(dev, FOBOS_SDR_CMD, CMD_STOP, 0);
        free(dev->rx_sync_buf);
        dev->rx_sync_buf = NULL;
        dev->rx_sync_started = 0;
    }
    return result;
}
//==============================================================================
int fobos_sdr_read_firmware(struct fobos_sdr_dev_t* dev, const char * file_name, int verbose)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%s)\n", __FUNCTION__, file_name);
#endif // FOBOS_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (dev->rx_sync_started || dev->rx_async_status != FOBOS_IDDLE)
    {
        return FOBOS_ERR_UNSUPPORTED;
    }
    FILE * f = fopen(file_name, "wb");
    if (f == 0)
    {
        return FOBOS_ERR_UNSUPPORTED;
    }
    result = 0;
    uint16_t xx_size = 1024;
    uint16_t xx_count = 192;
    uint8_t * xx_data = malloc(xx_size);
    uint16_t xsize;
    uint8_t req_code = 0xEC;
    if (verbose)
    {
        printf_internal("reading a firmware");
    }
    for (uint16_t i = 0; i < xx_count; i++)
    {
        if (verbose)
        {
            printf_internal(".");
        }
        xsize = libusb_control_transfer(dev->libusb_devh, CTRLI, req_code, 0, i, xx_data, xx_size, CTRL_TIMEOUT);
        if (xsize != xx_size)
        {
            result = FOBOS_ERR_CONTROL;
        }
        fwrite(xx_data, xx_size, 1, f);
    }
    fclose(f);
    if (verbose)
    {
        printf_internal("done.\n");
    }
    free(xx_data);
    return result;
}
//==============================================================================
int fobos_sdr_write_firmware(struct fobos_sdr_dev_t* dev, const char * file_name, int verbose)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%s)\n", __FUNCTION__, file_name);
#endif // FOBOS_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (dev->rx_sync_started || dev->rx_async_status != FOBOS_IDDLE)
    {
        return FOBOS_ERR_UNSUPPORTED;
    }
    FILE * f = fopen(file_name, "rb");
    if (f == 0)
    {
        return FOBOS_ERR_UNSUPPORTED;
    }
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    if ((file_size == 0) || (file_size > 0x3FFE0))
    {
        fclose(f);
        return FOBOS_ERR_UNSUPPORTED;
    }
    result = 0;
    uint16_t xx_size = 1024;
    uint16_t xx_count = (uint16_t)(file_size + xx_size - 1) / xx_size;
    uint8_t * file_data = (uint8_t * )malloc(xx_count * xx_size);
    fseek(f, 0, SEEK_SET);
    size_t read = fread(file_data, file_size, 1, f);
    fclose(f);
    if (read == 0)
    {
        free(file_data);
        return FOBOS_ERR_UNSUPPORTED;
    }
    memset(file_data + file_size, 0, xx_count * xx_size - file_size);
    uint16_t xsize;
    uint8_t req_code = 0xED;
    uint8_t * xx_data = file_data;
    if (verbose)
    {
        printf_internal("writing a firmware");
    }
    for (uint16_t i = 0; i < xx_count; i++)
    {
        if (verbose)
        {
            printf_internal(".");
        }
        xsize = libusb_control_transfer(dev->libusb_devh, CTRLO, req_code, 0, i, xx_data, xx_size, CTRL_TIMEOUT);
        if (xsize != xx_size)
        {
            result = FOBOS_ERR_CONTROL;
        }
        xx_data += xx_size;
    }
    if (verbose)
    {
        printf_internal("done.\n");
    }
    free(file_data);
    return result;
}
//==============================================================================
int fobos_sdr_read_user(struct fobos_sdr_dev_t * dev, void * data, int size)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%p, %d)\n", __FUNCTION__, file_name, data, size);
#endif // FOBOS_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (size > FOBOS_USER_DATA_SIZE)
    {
        return FOBOS_ERR_BAD_PARAM;
    }
    uint8_t req_code = 0xE3;
    uint16_t xsize = libusb_control_transfer(dev->libusb_devh, CTRLI, req_code, 0, 0, data, size, CTRL_TIMEOUT);
    if (xsize != size)
    {
        result = FOBOS_ERR_LIBUSB;
    }
    return result;
}
//==============================================================================
int fobos_sdr_write_user(struct fobos_sdr_dev_t * dev, void * data, int size)
{
    int result = fobos_sdr_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%p, %d)\n", __FUNCTION__, file_name, data, size);
#endif // FOBOS_PRINT_DEBUG
    if (result != FOBOS_ERR_OK)
    {
        return result;
    }
    if (size > FOBOS_USER_DATA_SIZE)
    {
        return FOBOS_ERR_BAD_PARAM;
    }
    uint8_t req_code = 0xE4;
    uint16_t xsize = libusb_control_transfer(dev->libusb_devh, CTRLO, req_code, 0, 0, data, size, CTRL_TIMEOUT);
    if (xsize != size)
    {
        result = FOBOS_ERR_LIBUSB;
    }
    return result;
}
//==============================================================================
const char * fobos_sdr_error_name(int error)
{
    switch (error)
    {
        case FOBOS_ERR_OK:               return "Ok";
        case FOBOS_ERR_NO_DEV:           return "No device spesified, dev == NUL";
        case FOBOS_ERR_NOT_OPEN:         return "Device is not open, please use fobos_sdr_open() first";
        case FOBOS_ERR_NO_MEM:           return "Memory allocation error";
        case FOBOS_ERR_ASYNC_IN_SYNC:    return "Could not read in async mode while sync mode started";
        case FOBOS_ERR_SYNC_IN_ASYNC:    return "Could not start sync mode while async reading";
        case FOBOS_ERR_SYNC_NOT_STARTED: return "Sync mode is not started";
        case FOBOS_ERR_UNSUPPORTED:      return "Unsuppotred parameter or mode";
        case FOBOS_ERR_LIBUSB:           return "libusb error";
        case FOBOS_ERR_BAD_PARAM:        return "Bad parameter";
        default:   return "Unknown error";
    }
}
//==============================================================================
