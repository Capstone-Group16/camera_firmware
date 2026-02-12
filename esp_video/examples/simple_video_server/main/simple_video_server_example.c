/*
 * Cleaned TCP Streaming Server for ESP32-P4
 * Mode: Raw TCP Client -> Python Server
 */

#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "example_video_common.h" // Setup for Camera IO
#include "esp_netif.h"
#include "esp_eth.h"

// --- CONFIGURATION ---
#define TARGET_IP       "192.168.1.10"
#define TARGET_PORT     5000
#define MY_STATIC_IP    "192.168.1.20"
#define MY_GATEWAY      "192.168.1.10"
#define MY_NETMASK      "255.255.255.0"

#define EXAMPLE_CAMERA_VIDEO_BUFFER_NUMBER  CONFIG_EXAMPLE_CAMERA_VIDEO_BUFFER_NUMBER
#define EXAMPLE_JPEG_ENC_QUALITY            CONFIG_EXAMPLE_JPEG_COMPRESSION_QUALITY

static const char *TAG = "TCP_STREAM";

// --- CAMERA STRUCTS ---
typedef struct web_cam_video {
    int fd;
    uint8_t index;
    example_encoder_handle_t encoder_handle;
    uint8_t *jpeg_out_buf;
    uint32_t jpeg_out_size;
    uint8_t *buffer[EXAMPLE_CAMERA_VIDEO_BUFFER_NUMBER];
    uint32_t buffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint8_t jpeg_quality;
    uint32_t frame_rate;
    SemaphoreHandle_t sem;
    uint32_t support_control_jpeg_quality : 1;
} web_cam_video_t;

typedef struct web_cam {
    uint8_t video_count;
    web_cam_video_t video[0];
} web_cam_t;

typedef struct web_cam_video_config {
    const char *dev_name;
    uint32_t buffer_count;
} web_cam_video_config_t;


// ============================================================================
//  TCP STREAM TASK (The Main Worker)
// ============================================================================
static void tcp_stream_task(void *args)
{
    web_cam_video_t *video = (web_cam_video_t *)args;
    struct v4l2_buffer buf;
    
    // 1. Setup Destination
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(TARGET_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TARGET_PORT);

    while (1) { // Connection Retry Loop
        ESP_LOGI(TAG, "TCP: Attempting to connect to %s:%d...", TARGET_IP, TARGET_PORT);
        
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 2. Connect
        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "TCP: Connected! Streaming...");

        // 3. Streaming Loop
        while (1) {
            uint32_t final_size = 0;
            uint8_t *final_buffer = NULL;
            bool locked = false;

            // A. Capture Frame
            memset(&buf, 0, sizeof(buf));
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (ioctl(video->fd, VIDIOC_DQBUF, &buf) != 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            // B. Handle JPEG vs Raw
            if (video->pixel_format == V4L2_PIX_FMT_JPEG) {
                final_buffer = video->buffer[buf.index];
                final_size = buf.bytesused;
            } else {
                if (xSemaphoreTake(video->sem, portMAX_DELAY) == pdPASS) {
                    locked = true;
                    if (example_encoder_process(video->encoder_handle, 
                                                video->buffer[buf.index], video->buffer_size,
                                                video->jpeg_out_buf, video->jpeg_out_size, 
                                                &final_size) == ESP_OK) {
                        final_buffer = video->jpeg_out_buf;
                    }
                }
            }

            // C. Send Data (Header + Body)
            if (final_buffer && final_size > 0) {
                // Send Size (4 Bytes)
                int sent_header = send(sock, &final_size, sizeof(uint32_t), 0);
                // Send Image
                int sent_body = send(sock, final_buffer, final_size, 0);

                if (sent_header < 0 || sent_body < 0) {
                    ESP_LOGE(TAG, "TCP Send Failed");
                    if (locked) xSemaphoreGive(video->sem);
                    ioctl(video->fd, VIDIOC_QBUF, &buf);
                    break; // Break inner loop to reconnect
                }
            }

            // D. Cleanup & Throttle
            if (locked) xSemaphoreGive(video->sem);
            ioctl(video->fd, VIDIOC_QBUF, &buf);

            // 12 FPS Limit (83ms)
            vTaskDelay(pdMS_TO_TICKS(83));
        }

        close(sock);
        ESP_LOGW(TAG, "TCP: Disconnected. Retrying...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    vTaskDelete(NULL);
}

// ============================================================================
//  CAMERA INIT HELPER FUNCTIONS
// ============================================================================
static esp_err_t set_camera_jpeg_quality(web_cam_video_t *video, int quality)
{
    if (video->pixel_format == V4L2_PIX_FMT_JPEG) {
        struct v4l2_ext_controls controls = {0};
        struct v4l2_ext_control control[1];
        controls.ctrl_class = V4L2_CID_JPEG_CLASS;
        controls.count = 1;
        controls.controls = control;
        control[0].id = V4L2_CID_JPEG_COMPRESSION_QUALITY;
        control[0].value = quality;
        if (ioctl(video->fd, VIDIOC_S_EXT_CTRLS, &controls) == 0) {
            video->jpeg_quality = quality;
            return ESP_OK;
        }
    } else {
        return example_encoder_set_jpeg_quality(video->encoder_handle, quality);
    }
    return ESP_FAIL;
}

static esp_err_t init_web_cam_video(web_cam_video_t *video, const web_cam_video_config_t *config, int index)
{
    int fd;
    struct v4l2_requestbuffers req;
    
    fd = open(config->dev_name, O_RDWR);
    if (fd < 0) return ESP_ERR_NOT_FOUND;

    // Request Buffers
    memset(&req, 0, sizeof(req));
    req.count  = EXAMPLE_CAMERA_VIDEO_BUFFER_NUMBER;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(fd, VIDIOC_REQBUFS, &req);

    // Map Buffers
    for (int i = 0; i < EXAMPLE_CAMERA_VIDEO_BUFFER_NUMBER; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        ioctl(fd, VIDIOC_QUERYBUF, &buf);
        video->buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        video->buffer_size = buf.length;
        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    struct v4l2_format format;
    memset(&format, 0, sizeof(struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_G_FMT, &format);

    video->fd = fd;
    video->width = format.fmt.pix.width;
    video->height = format.fmt.pix.height;
    video->pixel_format = format.fmt.pix.pixelformat;

    if (video->pixel_format == V4L2_PIX_FMT_JPEG) {
        set_camera_jpeg_quality(video, EXAMPLE_JPEG_ENC_QUALITY);
    } else {
        example_encoder_config_t encoder_config = {0};
        encoder_config.width = video->width;
        encoder_config.height = video->height;
        encoder_config.pixel_format = video->pixel_format;
        encoder_config.quality = EXAMPLE_JPEG_ENC_QUALITY;
        example_encoder_init(&encoder_config, &video->encoder_handle);
        example_encoder_alloc_output_buffer(video->encoder_handle, &video->jpeg_out_buf, &video->jpeg_out_size);
    }

    video->sem = xSemaphoreCreateBinary();
    xSemaphoreGive(video->sem);
    return ESP_OK;
}

static esp_err_t new_web_cam(const web_cam_video_config_t *config, int config_count, web_cam_t **ret_wc)
{
    web_cam_t *wc = calloc(1, sizeof(web_cam_t) + config_count * sizeof(web_cam_video_t));
    wc->video_count = config_count;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    for (int i = 0; i < config_count; i++) {
        wc->video[i].index = i;
        if (init_web_cam_video(&wc->video[i], &config[i], i) == ESP_OK) {
             ioctl(wc->video[i].fd, VIDIOC_STREAMON, &type);
             ESP_LOGI(TAG, "Camera %d Initialized: %dx%d", i, wc->video[i].width, wc->video[i].height);
        }
    }
    *ret_wc = wc;
    return ESP_OK;
}

// ============================================================================
//  NETWORKING
// ============================================================================
static void set_static_ip(esp_netif_t *netif)
{
    esp_netif_dhcpc_stop(netif);
    esp_netif_ip_info_t ip_info;
    esp_netif_str_to_ip4(MY_STATIC_IP, &ip_info.ip);
    esp_netif_str_to_ip4(MY_GATEWAY, &ip_info.gw);
    esp_netif_str_to_ip4(MY_NETMASK, &ip_info.netmask);
    esp_netif_set_ip_info(netif, &ip_info);
}

static esp_eth_handle_t eth_init_manual(esp_netif_t *eth_netif)
{
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_EXAMPLE_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_EXAMPLE_ETH_PHY_RST_GPIO;

    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_gpio.mdc_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;
    
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    esp_eth_phy_t *phy = NULL;

#if defined(CONFIG_EXAMPLE_ETH_PHY_IP101)
    phy = esp_eth_phy_new_ip101(&phy_config);
#elif defined(CONFIG_EXAMPLE_ETH_PHY_RTL8201)
    phy = esp_eth_phy_new_rtl8201(&phy_config);
#elif defined(CONFIG_EXAMPLE_ETH_PHY_LAN87xx)
    phy = esp_eth_phy_new_lan87xx(&phy_config);
#elif defined(CONFIG_EXAMPLE_ETH_PHY_DP83848)
    phy = esp_eth_phy_new_dp83848(&phy_config);
#elif defined(CONFIG_EXAMPLE_ETH_PHY_KSZ8041)
    phy = esp_eth_phy_new_ksz8041(&phy_config);
#endif

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    esp_eth_driver_install(&config, &eth_handle);
    void *glue = esp_eth_new_netif_glue(eth_handle);
    esp_netif_attach(eth_netif, glue);
    return eth_handle;
}

// ============================================================================
//  MAIN
// ============================================================================
void app_main(void)
{
    // 1. System Init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_event_loop_create_default();
    example_video_init(); // Init Camera Hardware Clock

    // 2. Network Init
    esp_netif_init();
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    esp_eth_handle_t eth_handle = eth_init_manual(eth_netif);
    set_static_ip(eth_netif);
    esp_eth_start(eth_handle);

    // 3. Camera Config
    web_cam_video_config_t config[] = {
        #if EXAMPLE_ENABLE_MIPI_CSI_CAM_SENSOR
        { .dev_name = ESP_VIDEO_MIPI_CSI_DEVICE_NAME },
        #endif
        #if EXAMPLE_ENABLE_DVP_CAM_SENSOR
        { .dev_name = ESP_VIDEO_DVP_DEVICE_NAME },
        #endif
    };
    int config_count = sizeof(config) / sizeof(config[0]);

    // 4. Start Camera & TCP Task
    web_cam_t *web_cam;
    if (new_web_cam(config, config_count, &web_cam) == ESP_OK) {
        xTaskCreate(tcp_stream_task, "tcp_stream", 4096, &web_cam->video[0], 5, NULL);
        ESP_LOGI(TAG, "System Started. Waiting for TCP connection...");
    } else {
        ESP_LOGE(TAG, "Camera Init Failed");
    }
}