#include "hal/i2s_types.h"
#pragma once
#include <driver/i2s_std.h>

#define I2S_BCLK  GPIO_NUM_5
#define I2S_LRCLK GPIO_NUM_4
#define I2S_DOUT  GPIO_NUM_6
#define I2S_DIN   GPIO_NUM_7

const uint16_t Sample_Rate_S = 22050;
const uint16_t Sample_Rate_M = 11025;

class I2SManager {
  public:
    i2s_chan_handle_t tx_handle = NULL;
    i2s_chan_handle_t rx_handle = NULL;

    void setupSpeaker() {
      i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
      i2s_new_channel(&chan_cfg, &tx_handle, NULL);

      i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(Sample_Rate_S),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = I2S_BCLK,
          .ws   = I2S_LRCLK,
          .dout = I2S_DOUT,
          .din  = I2S_GPIO_UNUSED,
        }
      };
      i2s_channel_init_std_mode(tx_handle, &std_cfg);
    }

    void setupMic() {
      i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
      i2s_new_channel(&chan_cfg, NULL, &rx_handle);

      i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(Sample_Rate_M),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = I2S_BCLK,
          .ws   = I2S_LRCLK,
          .dout = I2S_GPIO_UNUSED,
          .din  = I2S_DIN,
        }
      };
      i2s_channel_init_std_mode(rx_handle, &std_cfg);
    }

    void switchToMic() {
      // 1. Clean up Speaker Safely
      if (tx_handle != NULL) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL; 
      }
      // 2. Setup Mic if needed
      if (rx_handle == NULL) {
        setupMic();
      }
      // 3. Enable it immediately because recordAudio expects it
      i2s_channel_enable(rx_handle);
    }

    void switchToSpeaker() {
      // 1. Clean up Mic Safely
      if (rx_handle != NULL) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
      }

      // 2. Only setup Speaker if it's not already there
      if (tx_handle == NULL) {
        setupSpeaker();
        // newly created channel is already disabled, no need to call disable
      }
    }
};