//==============================================================================
//  Fobos SDR (agile) API library test application
//  V.T.
//  LGPL-2.1+
//  2024.12.07
//==============================================================================
#include <stdio.h>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <string.h>
#include <fobos_sdr.h>
#include <wav/wav_file.h>
//==============================================================================
typedef struct rx_ctx_t
{
    struct wav_file_t * wav;
    int buff_count;
    int max_buff_count;
} rx_ctx_t;
//==============================================================================
void read_samples_callback(float *buf, uint32_t buf_length, struct fobos_sdr_dev_t* sender, void *user)
{
    struct rx_ctx_t * rx_ctx = (struct rx_ctx_t *)user;
    rx_ctx->buff_count++;

    printf("+");
    fflush(stdout);
    
    if (rx_ctx->buff_count >= rx_ctx->max_buff_count)
    {
        printf("canceling...");
        fobos_sdr_cancel_async(sender);
    }

    if (rx_ctx->wav)
    {
        wav_file_write_data(rx_ctx->wav, (void*)buf, buf_length * 2 * sizeof(float));
        wav_file_write_header(rx_ctx->wav);
    }
}
//==============================================================================
#define INFO_SIZE       64
//==============================================================================
void test_recorder(void)
{
    struct fobos_sdr_dev_t* dev = NULL;
    int result = 0;
    char lib_version[INFO_SIZE];
    char drv_version[INFO_SIZE];
    char serials[256] = {0};

    int index = 0; // the device index to open

    char hw_revision[INFO_SIZE];
    char fw_version[INFO_SIZE];
    char manufacturer[INFO_SIZE];
    char product[INFO_SIZE];
    char serial[INFO_SIZE];

    fobos_sdr_get_api_info(lib_version, drv_version);

    printf("API Info lib: %s drv: %s\n", lib_version, drv_version);

    int count = fobos_sdr_get_device_count(); // just gets devices count

    printf("found devices: %d\n", count);

    count = fobos_sdr_list_devices(serials); // enumerates all connected devices with serial numbers

    if (count > 0)
    {
        char* pserial = strtok(serials, " ");
        for (int i = 0; i < count; i++)
        {
            printf("   sn: %s\n", pserial);
            pserial = strtok(0, " ");
        }

        result = fobos_sdr_open(&dev, index);

        if (result == 0)
        {
            result = fobos_sdr_get_board_info(dev, hw_revision, fw_version, manufacturer, product, serial);
            if (result != 0)
            {
                printf("fobos_sdr_get_board_info - error!\n");
            }
            else
            {
                printf("board info\n");
                printf("    hw_revision:  %s\n", hw_revision);
                printf("    fw_version:   %s\n", fw_version);
                printf("    manufacturer: %s\n", manufacturer);
                printf("    product:      %s\n", product);
                printf("    serial:       %s\n", serial);
            }

            printf("test: streaming\n");
            double frequency = 100000000.0;   // Hz
            double samplerate = 25000000.0;   // samples per second
            unsigned int direct_sampling = 0; // 0 - RF, 1 - HF1+HF2
            unsigned int lna_gain = 0;        // ow noise amplifier 0..2
            unsigned int vga_gain = 0;        // variable gain amplifier 0..15
            unsigned int clk_source = 0;      // 0 - internal clock (default); 1 - external

            result = fobos_sdr_set_frequency(dev, frequency);
            if (result != 0)
            {
                printf("fobos_sdr_set_frequency - error!\n");
            }

            result = fobos_sdr_set_direct_sampling(dev, direct_sampling);
            if (result != 0)
            {
                printf("fobos_sdr_set_direct_sampling - error!\n");
            }

            result = fobos_sdr_set_lna_gain(dev, lna_gain);
            if (result != 0)
            {
                printf("fobos_sdr_set_lna_gain - error!\n");
            }

            result = fobos_sdr_set_vga_gain(dev, vga_gain);
            if (result != 0)
            {
                printf("fobos_sdr_set_vga_gain - error!\n");
            }

            result = fobos_sdr_set_samplerate(dev, samplerate);
            if (result != 0)
            {
                printf("fobos_sdr_set_samplerate - error!\n");
            }

            result = fobos_sdr_set_clk_source(dev, clk_source);
            if (result != 0)
            {
                printf("fobos_sdr_set_samplerate - error!\n");
            }

            struct rx_ctx_t rx_ctx;
            rx_ctx.buff_count = 0;
            rx_ctx.max_buff_count = 2048; // number of buffers to record

            struct wav_file_t* wav = wav_file_create();
            wav->channels_count = 2;
            wav->sample_rate = (int)samplerate;
            wav->bytes_per_sample = 4;   // record in native float32 format
            wav->audio_format = 3;

            const char* file_name = "rx.iq.wav";

            result = wav_file_open(wav, file_name, "w");
            if (result != 0)
            {
                printf("could not create file %s\n", file_name);
            }

            rx_ctx.wav = wav;

            result = fobos_sdr_read_async(dev, read_samples_callback, &rx_ctx, 16, 65536);
            if (result == 0)
            {
                printf("fobos_sdr_read_async - ok!\n");
            }
            else
            {
                printf("fobos_sdr_read_async - error!\n");
            }

            wav_file_destroy(wav);
            wav = 0;

            fobos_sdr_close(dev);
        }
        else
        {
            printf("could not open device! err (%i)\n", result);
        }
    }
    else
    {
        printf("no Fobos SDR (agile) compatible devices found!\n");
    }
}
//==============================================================================
int main(int argc, char** argv)
{
    printf("Fobos SDR (agile) API recorder test applications\n");
    for(int i = 0; i < argc; i++)
    {
        printf("arg[%d]=%s\n", i, argv[i]);
    }
    printf("machine: x%ld\n", sizeof(void*)*8);

    test_recorder();

#ifdef _WIN32
    system("pause");
#endif

    return 0;
}
//==============================================================================
