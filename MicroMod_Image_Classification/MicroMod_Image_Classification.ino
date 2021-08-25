#include <stdint.h>
#include <stdio.h>
#include <SD.h>
#include <SPI.h>

#include <Wire.h>

#include "HM01B0.h"
#include "HM01B0_regs.h"
#include "constants.h"

#include <micromod-ml-demo_inferencing.h>

// Library supports 8-bit or 4-bit camera interfacing to the Teensy Micromod Processor via pin selection
// or supports the default camera configuration.
PROGMEM const char hmConfig[][48] = {
 "HM01B0_SPARKFUN_ML_CARRIER",    
 "HM01B0_FLEXIO_CUSTOM_LIKE_8_BIT",
 "HM01B0_FLEXIO_CUSTOM_LIKE_4_BIT"
};

#define _hmConfig 1 // select mode 

#if _hmConfig == 0
HM01B0 hm01b0(HM01B0_SPARKFUN_ML_CARRIER);

#elif _hmConfig == 1
/* We are doing manual settings: 
 * this one should duplicate the 8 bit ML Carrier:
 *    HM01B0(uint8_t mclk_pin, uint8_t pclk_pin, uint8_t vsync_pin, uint8_t hsync_pin, en_pin,
 *    uint8_t g0, uint8_t g1,uint8_t g2, uint8_t g3,
 *    uint8_t g4=0xff, uint8_t g5=0xff,uint8_t g6=0xff,uint8_t g7=0xff, TwoWire &wire=Wire); 
 */
HM01B0 hm01b0(7, 8, 33, 32, 2, 40, 41, 42, 43, 44, 45, 6, 9);

#elif _hmConfig == 2
/* We are doing manual settings: 
 * this sets the camera up using 4 bits only.
 *    HM01B0(uint8_t mclk_pin, uint8_t pclk_pin, uint8_t vsync_pin, uint8_t hsync_pin, en_pin,
 *    uint8_t g0, uint8_t g1,uint8_t g2, uint8_t g3,
 *    uint8_t g4=0xff, uint8_t g5=0xff,uint8_t g6=0xff,uint8_t g7=0xff, TwoWire &wire=Wire);
 */
HM01B0 hm01b0(7, 8, 33, 32, 2, 40, 41, 42, 43);
#endif

/*  Camera Configuration
 *  Two base configurations are used to initalize the camera.  The first is the Sparakfun configuration
 *  used in their library, the second is the configurations used by the OpenMV camera (modified) for the HM01B0
 *  If the define for USE_SPARKFUN is uncommented the sketch will use the Sparkfun configuration otherwise it will
 *  the OpenMV version which we modified slightly
 *  NOTE:
 *    1. If using 4 bit mode use set_framerate(60) with OpenMV config
 *    2. If using 8 bit/Sparkfun ML with the Sparkfun config use frameRate of 30. You do get flicker    
 *       using this combination.
 *    3. If using 8 bit/SparkfunML mode use set_framerate(60) with OpenMV config
 */
//#define USE_SPARKFUN 1

// If you want to use the SDCard to store images uncomment the following line
#define USE_SDCARD 1
File file;

/* The sketch supports 2 displays: ST7789 and ILI9431 displays.  The ST7789 library is installed automatically
 * when you install Teensyduino.  For the ILI931 to work properly you will need to download and install the 
 * ILI9341_t3n library - https://github.com/KurtE/ILI9341_t3n
 * Select display by uncommenting the one you want and commenting out the other one.
 */
//#define TFT_ST7789 1
#define TFT_ILI9341 1

#define TFT_DC  1   // "TX1" on left side of Sparkfun ML Carrier
#define TFT_CS  4   // "D0" on right side of Sparkfun ML Carrier
#define TFT_RST 0   // "RX1" on left side of Sparkfun ML Carrier
#define TFT_BL  5   // "D1" on right side of Sparkfun ML Carrier

#ifdef TFT_ST7789
//ST7735 Adafruit 320x240 display
#include <ST7789_t3.h>
ST7789_t3 tft = ST7789_t3(TFT_CS, TFT_DC, TFT_RST);
#define TFT_BLACK ST77XX_BLACK
#define TFT_YELLOW ST77XX_YELLOW
#define TFT_RED   ST77XX_RED
#define TFT_GREEN ST77XX_GREEN
#define TFT_BLUE  ST77XX_BLUE

#else
#include "ILI9341_t3n.h" // https://github.com/KurtE/ILI9341_t3n
ILI9341_t3n tft = ILI9341_t3n(TFT_CS, TFT_DC, TFT_RST);
#define TFT_BLACK ILI9341_BLACK
#define TFT_YELLOW ILI9341_YELLOW
#define TFT_RED   ILI9341_RED
#define TFT_GREEN ILI9341_GREEN
#define TFT_BLUE  ILI9341_BLUE
#endif

// Configs frameBuffers for use with the camera
uint16_t FRAME_WIDTH, FRAME_HEIGHT;
uint8_t frameBuffer[(324) * 244] DMAMEM;
uint8_t resizedFrameBuf[EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT] DMAMEM;
uint8_t resizedRGBFrameBuf[EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3] DMAMEM;

//Sketch defines
bool g_continuous_flex_mode = false;
void * volatile g_new_flexio_data = nullptr;
uint32_t g_flexio_capture_count = 0;
uint32_t g_flexio_redraw_count = 0;
elapsedMillis g_flexio_runtime;
bool g_dma_mode = false;

ae_cfg_t aecfg;

void setup()
{
#ifdef TFT_ILI9341
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  delay(100);
  tft.begin();
#else
  tft.init(240, 320);           // Init ST7789 320x240
#endif
  tft.setRotation(1);
  tft.fillScreen(TFT_RED);
  delay(500);
  tft.fillScreen(TFT_GREEN);
  delay(500);
  tft.fillScreen(TFT_BLUE);
  delay(500);
  tft.fillScreen(TFT_BLACK);
  delay(500);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_BLUE);
  tft.setTextSize(5);
  tft.println("Waiting for Arduino Serial Monitor...");

  Serial.begin(921600);

  Serial.println("HM01B0 Camera Test");
  Serial.println( hmConfig[_hmConfig] );
  Serial.println("------------------");

  tft.fillScreen(TFT_BLACK);

  uint16_t ModelID;
  ModelID = hm01b0.get_modelid();
  if (ModelID == 0x01B0) {
    Serial.printf("SENSOR DETECTED :-) MODEL HM0%X\n", ModelID);
  } else {
    Serial.println("SENSOR NOT DETECTED! :-(");
    while (1) {}
  }


  uint8_t status;
#if defined(USE_SPARKFUN)
  status = hm01b0.loadSettings(LOAD_SHM01B0INIT_REGS);  //hangs the TMM.
#else
  status = hm01b0.loadSettings(LOAD_DEFAULT_REGS);
#endif

  if(_hmConfig == 2){
    status = hm01b0.set_framesize(FRAMESIZE_QVGA4BIT);
  } else {
    status = hm01b0.set_framesize(FRAMESIZE_QVGA);
  }
  if (status != 0) {
    Serial.println("Settings failed to load");
    while (1) {}
  }
  hm01b0.set_framerate(60);  //15, 30, 60

  /* Gain Ceilling
   * GAINCEILING_1X
   * GAINCEILING_4X
   * GAINCEILING_8X
   * GAINCEILING_16X
   */
  hm01b0.set_gainceiling(GAINCEILING_2X);
  
  /* Brightness
   *  Can be 1, 2, or 3
   */
  hm01b0.set_brightness(3);
  hm01b0.set_autoExposure(true, 1500);  //higher the setting the less saturaturation of whiteness
  hm01b0.cmdUpdate();  //only need after changing auto exposure settings

  hm01b0.set_mode(HIMAX_MODE_STREAMING, 0); // turn on, continuous streaming mode

  FRAME_HEIGHT = hm01b0.height();
  FRAME_WIDTH  = hm01b0.width();
  Serial.printf("ImageSize (w,h): %d, %d\n", FRAME_WIDTH, FRAME_HEIGHT);
}

void loop()
{
    memset(frameBuffer, 0, sizeof(frameBuffer));
    hm01b0.set_mode(HIMAX_MODE_STREAMING_NFRAMES, 1);
    hm01b0.readFrame(frameBuffer);

    tft.setOrigin(-2, -2);

    tft.writeRect8BPP(0, 0, FRAME_WIDTH, FRAME_HEIGHT, frameBuffer, mono_palette);
    tft.setOrigin(0, 0);

    resizeImage(FRAME_WIDTH, FRAME_HEIGHT, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, frameBuffer, resizedFrameBuf);
    monochromeToRGB(EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, resizedFrameBuf, resizedRGBFrameBuf);

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_get_camera_data;

    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR ei_error = run_classifier(&signal, &result, false);
    if (ei_error != EI_IMPULSE_OK) {
        ei_printf("Failed to run impulse (%d)\n", ei_error);
    }
    
    ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \n",
                    result.timing.dsp, result.timing.classification, result.timing.anomaly);
    
    uint8_t max_result_ix = 0;
    float max_val = 0.0;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf("    %s: \t%f\r\n", result.classification[ix].label, result.classification[ix].value);
        if(result.classification[ix].value > max_val) {
            max_val = result.classification[ix].value;
            max_result_ix = ix;
        }
    }

    tft.setCursor(100, 120);
    tft.print(result.classification[max_result_ix].label);

}

// Pass 8-bit (each) R,G,B, get back 16-bit packed color
uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void calAE() {
  // Calibrate Autoexposure
  Serial.println("Calibrating Auto Exposure...");
  memset((uint8_t*)frameBuffer, 0, sizeof(frameBuffer));
  if (hm01b0.cal_ae(10, frameBuffer, FRAME_WIDTH * FRAME_HEIGHT, &aecfg) != HM01B0_ERR_OK) {
    Serial.println("\tnot converged");
  } else {
    Serial.println("\tconverged!");
    hm01b0.cmdUpdate();
  }
}

/**
 * @brief Use nearest-neighbor algorithm to resize image
 * 
 * @param originalWidth # of horizontal pixels in image to scale
 * @param originalHeight # of vertical pixels in image to scale
 * @param newWidth # of horizontal pixels in resulting image
 * @param newHeight # of vertical pixels in resulting image
 * @param[in] originalFrameBuf Image data to scale
 * @param[out] newFrameBuf New image's data goes here
 */
void resizeImage(uint16_t originalWidth, uint16_t originalHeight, uint16_t newWidth, uint16_t newHeight, uint8_t *originalFrameBuf, uint8_t *newFrameBuf) {
    double scaleWidth = (double)newWidth / (double)originalWidth;
    double scaleHeight = (double)newHeight / (double)originalHeight;

    for(int cy = 0; cy < newHeight; cy++) {
        for(int cx = 0; cx < newWidth; cx++) {
            int pixel = (cy * newWidth) + cx;
            // get pixel from original that's closest to the one in new image
            int nearestMatch = (((int)(cy / scaleHeight) * originalWidth) + ((int)(cx / scaleWidth)));

            newFrameBuf[pixel] = originalFrameBuf[nearestMatch];
        }
    }
}

/**
 * @brief 
 * 
 * @param width Width of image to convert
 * @param height Height of image to convert
 * @param[in] frameBuf Data of image
 * @param[out] convertedImage Data of converted image - USES 3X THE SPACE!
 */
void monochromeToRGB(uint16_t width, uint16_t height, uint8_t *frameBuf, uint8_t *convertedImage) {
    for(uint32_t i = 0; i < width * height; i++) {
        uint8_t pixelData = frameBuf[i];

        // fill in the RGB data for the new pixel
        for(uint8_t offset = 0; offset < 3; offset++) {
            convertedImage[3 * i + offset] = pixelData;
        }
    }
}

int ei_get_camera_data(size_t offset, size_t length, float *out_ptr) {
    for(size_t i = 0; i < length; i++) {
        out_ptr[i] = (resizedRGBFrameBuf[3 * i] << 16) + (resizedRGBFrameBuf[3 * i + 1] << 8) + resizedRGBFrameBuf[3 * i + 2];
    }

    return 0;
}
