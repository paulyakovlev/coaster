// ES8311 codec + NS4150B amplifier on the Waveshare ESP32-S3-ePaper-1.54.
// Register sequence follows the vendor's esp_codec_dev ES8311 driver
// (08_Audio_Test/src/esp_codec_dev/device/es8311/es8311.c): open, set_fs and
// start for DAC-only playback, slave mode, MCLK = 256 x sample rate.

#ifdef ARDUINO_ARCH_ESP32

#include "sound.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>

// Source: vendor 08_Audio_Test codec_board/board_cfg.txt (S3_ePaper_1_54)
// and user_config.h (Audio_PWR_PIN)
#define I2S_MCLK_PIN   14
#define I2S_BCLK_PIN   15
#define I2S_WS_PIN     38
#define I2S_DOUT_PIN   45
#define I2S_DIN_PIN    16
#define AMP_EN_PIN     46  // NS4150B enable, active high
#define AUDIO_PWR_PIN  42  // audio rail, active low

#define ES8311_ADDR    0x18  // 0x30 in the vendor code is the 8-bit form

// Half of full scale: loud enough, no clipping in the amplifier
#define AMPLITUDE      0.5f

static I2SClass i2s;
static bool     running = false;
static uint8_t  volume  = 0xBF;  // 0 dB

static bool wr(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static uint8_t rd(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1) != 1) return 0;
    return Wire.read();
}

static bool codec_init() {
    bool ok = true;
    // open(): the first write sometimes fails, so the vendor writes it twice
    wr(0x44, 0x08);
    ok &= wr(0x44, 0x08);
    ok &= wr(0x01, 0x30);
    ok &= wr(0x02, 0x00);
    ok &= wr(0x03, 0x10);
    ok &= wr(0x16, 0x24);
    ok &= wr(0x04, 0x10);
    ok &= wr(0x05, 0x00);
    ok &= wr(0x0B, 0x00);
    ok &= wr(0x0C, 0x00);
    ok &= wr(0x10, 0x1F);
    ok &= wr(0x11, 0x7F);
    ok &= wr(0x00, 0x80);  // slave mode
    ok &= wr(0x01, 0x3F);  // clocks on, MCLK from the pin, not inverted
    ok &= wr(0x06, rd(0x06) & ~0x20);  // BCLK not inverted
    ok &= wr(0x13, 0x10);
    ok &= wr(0x1B, 0x0A);
    ok &= wr(0x1C, 0x6A);

    // set_fs(): 16-bit I2S, 16 kHz with a 4.096 MHz MCLK
    // (coeff row {4096000, 16000, 1, 1, 1, 1, 0, 0, 0xff, 4, 0x10, 0x20})
    ok &= wr(0x09, (rd(0x09) & 0xFC) | 0x0C);
    ok &= wr(0x0A, (rd(0x0A) & 0xFC) | 0x0C);
    ok &= wr(0x02, rd(0x02) & 0x07);  // pre_div 1, pre_multi 1
    ok &= wr(0x05, 0x00);             // adc_div 1, dac_div 1
    ok &= wr(0x03, (rd(0x03) & 0x80) | 0x10);  // single speed, adc_osr
    ok &= wr(0x04, (rd(0x04) & 0x80) | 0x20);  // dac_osr
    ok &= wr(0x07, rd(0x07) & 0xC0);  // lrck_h
    ok &= wr(0x08, 0xFF);             // lrck_l
    ok &= wr(0x06, (rd(0x06) & 0xE0) | (4 - 1));  // bclk_div

    // start(): DAC on, ADC off
    ok &= wr(0x00, 0x80);
    ok &= wr(0x01, 0x3F);
    ok &= wr(0x09, rd(0x09) & ~0x40);
    ok &= wr(0x0A, rd(0x0A) | 0x40);
    ok &= wr(0x17, 0xBF);
    ok &= wr(0x0E, 0x02);
    ok &= wr(0x12, 0x00);
    ok &= wr(0x14, 0x1A);
    ok &= wr(0x0D, 0x01);
    ok &= wr(0x15, 0x40);
    ok &= wr(0x37, 0x08);
    ok &= wr(0x45, 0x00);

    ok &= wr(0x31, rd(0x31) & 0x9F);  // unmute
    ok &= wr(0x32, volume);
    return ok;
}

// suspend() from the vendor driver
static void codec_suspend() {
    wr(0x32, 0x00);
    wr(0x17, 0x00);
    wr(0x0E, 0xFF);
    wr(0x12, 0x02);
    wr(0x14, 0x00);
    wr(0x0D, 0xFA);
    wr(0x15, 0x00);
    wr(0x02, 0x10);
    wr(0x00, 0x00);
    wr(0x00, 0x1F);
    wr(0x01, 0x30);
    wr(0x01, 0x00);
    wr(0x45, 0x00);
    wr(0x0D, 0xFC);
    wr(0x02, 0x00);
}

bool sound_begin() {
    pinMode(AMP_EN_PIN, OUTPUT);
    digitalWrite(AMP_EN_PIN, LOW);
    pinMode(AUDIO_PWR_PIN, OUTPUT);
    digitalWrite(AUDIO_PWR_PIN, LOW);
    delay(10);

    // MCLK has to run before the codec is set up
    i2s.setPins(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
    if (!i2s.begin(I2S_MODE_STD, SOUND_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        return false;
    }
    if (!codec_init()) {
        i2s.end();
        return false;
    }
    digitalWrite(AMP_EN_PIN, HIGH);
    running = true;
    return true;
}

void sound_end() {
    pinMode(AMP_EN_PIN, OUTPUT);
    digitalWrite(AMP_EN_PIN, LOW);
    if (running) {
        codec_suspend();
        i2s.end();
        running = false;
    }
    pinMode(AUDIO_PWR_PIN, OUTPUT);
    digitalWrite(AUDIO_PWR_PIN, HIGH);
}

#define CHUNK 128

static void play(SoundPlayer *p) {
    static int16_t mono[CHUNK];
    static int16_t stereo[CHUNK * 2];
    size_t n;
    while ((n = sound_fill(p, mono, CHUNK)) > 0) {
        for (size_t i = 0; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
        i2s.write((const uint8_t *)stereo, n * 4);
    }
    // Push silence through the DMA buffers so the end of the sound plays
    memset(stereo, 0, sizeof(stereo));
    for (int i = 0; i < 8; i++) i2s.write((const uint8_t *)stereo, sizeof(stereo));
}

void sound_play(SoundId id) {
    if (!running) return;
    SoundPlayer p;
    sound_start(&p, id, AMPLITUDE);
    play(&p);
}

void sound_tone(uint16_t hz, uint16_t ms) {
    if (!running) return;
    SoundNote note = {hz, hz, ms};
    SoundPlayer p  = {&note, 1, 0, 0, 0, AMPLITUDE};
    play(&p);
}

void sound_set_volume(uint8_t reg) {
    volume = reg;
    if (running) wr(0x32, volume);
}

uint8_t sound_volume() {
    return volume;
}

#endif
