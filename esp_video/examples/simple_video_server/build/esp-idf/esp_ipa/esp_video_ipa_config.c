/*
* SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: ESPRESSIF MIT
*/

#include <string.h>
#include "esp_ipa.h"

typedef struct esp_video_ipa_index {
    const char *name;
    const esp_ipa_config_t *ipa_config;
} esp_video_ipa_index_t;

static const esp_ipa_adn_bf_t s_ipa_adn_bf_OV5647_config[] = {
    {
        .gain = 1000,
        .bf = {
            .level = 5,
            .matrix = {
                {1, 2, 1},
                {2, 4, 2},
                {1, 2, 1}
            }
        }
    },
};

static const esp_ipa_adn_config_t s_ipa_adn_OV5647_config = {
    .bf_table = s_ipa_adn_bf_OV5647_config,
    .bf_table_size = ARRAY_SIZE(s_ipa_adn_bf_OV5647_config),
};

static const esp_ipa_aen_gamma_unit_t s_esp_ipa_aen_gamma_OV5647_table[] = {
    {
        .luma = 71.1000,
        .gamma_param = 0.7200,
    },
};

static const esp_ipa_aen_gamma_config_t s_ipa_aen_gamma_OV5647_config = {
    .model = 0,
    .use_gamma_param = true,
    .luma_env = "ae.luma.avg",
    .luma_min_step = 16.0000,
    .gamma_table = s_esp_ipa_aen_gamma_OV5647_table,
    .gamma_table_size = 1,
};

static const esp_ipa_aen_sharpen_t s_ipa_aen_sharpen_OV5647_config[] = {
    {
        .gain = 1000,
        .sharpen = {
            .h_thresh = 56,
            .l_thresh = 10,
            .h_coeff = 0.4250,
            .m_coeff = 0.6250,
            .matrix = {
                {1, 2, 1},
                {2, 2, 2},
                {1, 2, 1}
            }
        }
    },
};

static const esp_ipa_aen_con_t s_ipa_aen_con_OV5647_config[] = {
    {
        .gain = 1000,
        .contrast = 134
    },
};

static const esp_ipa_aen_config_t s_ipa_aen_OV5647_config = {
    .gamma = &s_ipa_aen_gamma_OV5647_config,
    .sharpen_table = s_ipa_aen_sharpen_OV5647_config,
    .sharpen_table_size = ARRAY_SIZE(s_ipa_aen_sharpen_OV5647_config),
    .con_table = s_ipa_aen_con_OV5647_config,
    .con_table_size = ARRAY_SIZE(s_ipa_aen_con_OV5647_config),
};

static const esp_ipa_ian_luma_ae_config_t s_esp_ipa_ian_luma_ae_OV5647_config = {                 
    .weight = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    },
};

static const esp_ipa_ian_luma_config_t s_esp_ipa_ian_luma_OV5647_config = {
    .ae = &s_esp_ipa_ian_luma_ae_OV5647_config,
};

static const esp_ipa_ian_config_t s_ipa_ian_OV5647_config = {
    .luma = &s_esp_ipa_ian_luma_OV5647_config,
};

static const esp_ipa_acc_sat_t s_ipa_acc_sat_OV5647_config[] = {
    {
        .color_temp = 0,
        .saturation = 128
    },
};

static const esp_ipa_acc_ccm_unit_t s_esp_ipa_acc_ccm_OV5647_table[] = {
    {
        .color_temp = 0,
        .ccm = {
            .matrix = {
                { 2.0000, -0.5459, -0.4541 },
                { -0.4751, 1.7696, -0.2945 },
                { -0.2002, -0.7998, 2.0000 }
            }
        }
    },
};

static const esp_ipa_acc_ccm_config_t s_esp_ipa_acc_ccm_OV5647_config = {
    .model = 0,
    .luma_env = "ae.luma.avg",
    .luma_low_threshold = 28.0000,
    .luma_low_ccm = {
        .matrix = {
            { 1.0000, 0.0000, 0.0000 },
            { 0.0000, 1.0000, 0.0000 },
            { 0.0000, 0.0000, 1.0000 }
        }
    }
    ,
    .ccm_table = s_esp_ipa_acc_ccm_OV5647_table,
    .ccm_table_size = 1,
};

static const esp_ipa_acc_config_t s_ipa_acc_OV5647_config = {
    .sat_table = s_ipa_acc_sat_OV5647_config,
    .sat_table_size = ARRAY_SIZE(s_ipa_acc_sat_OV5647_config),
    .ccm = &s_esp_ipa_acc_ccm_OV5647_config,
};

static const esp_ipa_af_config_t s_ipa_af_OV5647_config = {
    .windows = {
        [0] = {
            .top_left = {
                .x = 680,
                .y = 300,
            },
            .btm_right = {
                .x = 1069,
                .y = 709,
            },
        },
        [1] = {
            .top_left = {
                .x = 2,
                .y = 2,
            },
            .btm_right = {
                .x = 5,
                .y = 5,
            },
        },
        [2] = {
            .top_left = {
                .x = 2,
                .y = 2,
            },
            .btm_right = {
                .x = 5,
                .y = 5,
            },
        },
    },
    .weight_table = {
        1, 0, 0
    },
    .edge_thresh = 32,
    .l1_scan_points_num = 10,
    .l2_scan_points_num = 10,
    .max_pos = 500,
    .min_pos = 0,
    .definition_high_threshold_ratio = 1.3000,
    .definition_low_threshold_ratio = 0.7000,
    .luminance_high_threshold_ratio = 1.1000,
    .luminance_low_threshold_ratio = 0.9000,
    .max_change_time = 2000000
};

static const char *s_ipa_OV5647_names[] = {
    "esp_ipa_adn",
    "esp_ipa_aen",
    "esp_ipa_ian",
    "esp_ipa_acc",
    "esp_ipa_af",
};

static const esp_ipa_config_t s_ipa_OV5647_config = {
    .names = s_ipa_OV5647_names,
    .nums = ARRAY_SIZE(s_ipa_OV5647_names),
    .version = 1,
    .adn = &s_ipa_adn_OV5647_config,
    .aen = &s_ipa_aen_OV5647_config,
    .ian = &s_ipa_ian_OV5647_config,
    .acc = &s_ipa_acc_OV5647_config,
    .af = &s_ipa_af_OV5647_config,
};

static const esp_video_ipa_index_t s_video_ipa_configs[] = {
    {
        .name = "OV5647",
        .ipa_config = &s_ipa_OV5647_config
    },
};

const esp_ipa_config_t *esp_ipa_pipeline_get_config(const char *name)
{
    for (int i = 0; i < ARRAY_SIZE(s_video_ipa_configs); i++) {
        if (!strcmp(name, s_video_ipa_configs[i].name)) {
            return s_video_ipa_configs[i].ipa_config;
        }
    }
    return NULL;
}

/* Json file: /Users/Bill/Documents/capstone_camera/camera_firmware/esp_cam_sensor/sensors/ov5647/cfg/ov5647_default.json */

