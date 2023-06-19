/***
  CIRCUS PI XIAO SAMD21 ENVIRONMENT SENSE SAMPLE

  Hardware 
  - Seeeduino XIAO SAMD21
  - XIAO Expansion board
  - SHT30 module
  - SGP30 module

  library dependency:
  - Adafruit SHT31 Library
  - Adafruit SGP30 Sensor
  - U8g2
  - RTClib
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_SHT31.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>

#define DEBUG               false
#define LOG_FILE            "data.csv"
#define PIN_BUTTON          1
#define DEBOUNCE_DELAY      50
#define SENSE_INTERVAL      1000
#define DISPLAY_INTERVAL    1000
#define WRITEFILE_INTERVAL  5000

// 宣告 OLED instance
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
// 宣告 SHT3x instance
Adafruit_SHT31    sht31 = Adafruit_SHT31();
// 宣告 SGP30 instance
Adafruit_SGP30    sgp;
// 宣告 PCF8564 instance
RTC_PCF8563       rtc;

// 供 RTC 使用的星期 look up table，暫時用不到
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// 變數宣告
float           t, h;             // 溫度與濕度
File            myFile;           // 檔案描述，用來操作讀寫檔案內容
int             ledState = HIGH;  // 記錄內建 LED 狀態
int             buttonState;      // 記錄 XIAO Exapansion 擴展板上的按鈕狀態
int             lastButtonState = HIGH;   // 記錄 XIAO Exapansion 擴展板上的按鈕前次狀態
unsigned long   lastDebounceTime = 0;     // 任務時間戳記 for 按鈕
unsigned long   lastSenseTime = 0;        // 任務時間戳記 for 感測
unsigned long   lastDisplayTime = 0;      // 任務時間戳記 for 顯示
unsigned long   lastWriteFileTime = 0;    // 任務時間戳記 for 寫入檔案


uint32_t getAbsoluteHumidity(float temperature, float humidity) {
    // approximation formula from Sensirion SGP30 Driver Integration chapter 3.15
    const float absoluteHumidity = 216.7f * ((humidity / 100.0f) * 6.112f * exp((17.62f * temperature) / (243.12f + temperature)) / (273.15f + temperature)); // [g/m^3]
    const uint32_t absoluteHumidityScaled = static_cast<uint32_t>(1000.0f * absoluteHumidity); // [mg/m^3]
    return absoluteHumidityScaled;
}

// 讀取 SHT30 溫度&濕度數值，參照範例為 Adafruit SHT31 Library 
void senseSHT30() {
  t = sht31.readTemperature();
  h = sht31.readHumidity();

#if DEBUG
  if (! isnan(t)) {  // check if 'is not a number'
    Serial.print("Temp *C = "); Serial.print(t); Serial.print("\t\t");
  } else { 
    Serial.println("Failed to read temperature");
  }
  
  if (! isnan(h)) {  // check if 'is not a number'
    Serial.print("Hum. % = "); Serial.println(h);
  } else { 
    Serial.println("Failed to read humidity");
  }
#endif
}

// 讀取 SGP30 TVOC & eCO2 數值，參照範例為 Adafruit SGP30 Sensor
void senseSGP30() {
  static int counter = 0;
  sgp.setHumidity(getAbsoluteHumidity(t, h));

  if (! sgp.IAQmeasure()) {
    Serial.println("Measurement failed");
    return;
  }

#if DEBUG
  Serial.print("TVOC "); Serial.print(sgp.TVOC); Serial.print(" ppb\t");
  Serial.print("eCO2 "); Serial.print(sgp.eCO2); Serial.println(" ppm");

  if (! sgp.IAQmeasureRaw()) {
    Serial.println("Raw Measurement failed");
    return;
  }
  Serial.print("Raw H2 "); Serial.print(sgp.rawH2); Serial.print(" \t");
  Serial.print("Raw Ethanol "); Serial.print(sgp.rawEthanol); Serial.println("");
 
  counter++;
  if (counter == 30) {
    counter = 0;

    uint16_t TVOC_base, eCO2_base;
    if (! sgp.getIAQBaseline(&eCO2_base, &TVOC_base)) {
      Serial.println("Failed to get baseline readings");
      return;
    }
    Serial.print("****Baseline values: eCO2: 0x"); Serial.print(eCO2_base, HEX);
    Serial.print(" & TVOC: 0x"); Serial.println(TVOC_base, HEX);
  }
#endif
}

// setup 依序初始化 OLED, RTC, 
void setup(void) {
  int ret_rtc, ret_sht30, ret_sgp30, ret_sd;

  Serial.begin(115200);

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(LED_BUILTIN, ledState);

  u8g2.begin();
  u8g2.enableUTF8Print();
  
  u8g2.setFont(u8g2_font_courB12_tr);
  u8g2.setFontDirection(0);
  u8g2.clearDisplay();

  u8g2.firstPage();  
  do {
    u8g2.setCursor(0, 16);
    u8g2.print("initializing...");
    u8g2.sendBuffer();
  } while(u8g2.nextPage());
  delay(500);

  // setup rtc
  ret_rtc = rtc.begin();
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  rtc.start();

  // setup sht30
  ret_sht30 = sht31.begin(0x44);
  // setup sgp30
  ret_sgp30 = sgp.begin();
  // setup sd card
  pinMode(D2, OUTPUT);
  ret_sd = SD.begin(D2);
  
  u8g2.firstPage();  
  do {
    u8g2.setCursor(0, 16);
    if (!ret_rtc) {
      u8g2.print("PCF8563 Fail");
    } else {
      u8g2.print("PCF8563 OK");
    }
    u8g2.setCursor(0, 32);
    if (!ret_sht30) {
      u8g2.print("SHT30 Fail");
    } else {
      u8g2.print("SHT30 OK");
    }
    u8g2.setCursor(0, 48);
    if (!ret_sgp30) {
      u8g2.print("SGP30 Fail");
    } else {
      u8g2.print("SGP30 OK");
    }
    u8g2.setCursor(0, 64);
    if (!ret_sd) {
      u8g2.print("SD Card Fail");
    } else {
      u8g2.print("SD Card OK");
    }
    u8g2.sendBuffer();
  } while(u8g2.nextPage());
  
  if (!ret_rtc || !ret_sht30 || !ret_sgp30 || !ret_sd) {
    while(1);
  }

  delay(1500);
}

/*
  無窮迴圈固定任務：
  * task1: 按鈕事件，預設每 50ms 執行一次
  * task2: 讀取感測器 SHT30/SGP30，預設每 1000ms 執行一次
  * task3: 更新 OLED 顯示資訊，預設每 1000ms 執行一次
  * task4: 將 RTC 時間與感測數值寫入 SD Card，預設每 5000ms 執行一次
*/
void loop(void) {
  char str[64];

  // read the state of the switch into a local variable:
  int reading = digitalRead(PIN_BUTTON);
  
  // task1: 處理按鈕事件
  if (reading != lastButtonState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == HIGH) {
        ledState = !ledState;
      }
    }
  }

  digitalWrite(LED_BUILTIN, ledState);
  lastButtonState = reading;

  // task2: 讀取感測器
  if ((millis() - lastSenseTime) > SENSE_INTERVAL) {
    lastSenseTime = millis();
    senseSHT30();
    senseSGP30();
  }

  // task3: 更新OLED顯示資訊
  if ((millis() - lastDisplayTime) > DISPLAY_INTERVAL) {
    lastDisplayTime = millis();

    u8g2.firstPage();
    DateTime now = rtc.now();
    //以 ledState 狀態做為顯示頁面區分
    do {
      if (ledState == LOW) {
        u8g2.setCursor(0, 16);
        sprintf(str, "Temp: %02.2fC", t);
        u8g2.print(str);
        u8g2.setCursor(0, 32);
        sprintf(str, "Humi: %02.2f%%", h);
        u8g2.print(str);
      } else {
        u8g2.setCursor(0, 16);
        sprintf(str, "TVOC: %04d", sgp.TVOC);
        u8g2.print(str);
        u8g2.setCursor(0, 32);
        sprintf(str, "eCO2: %04d", sgp.eCO2);
        u8g2.print(str);
      }
      u8g2.setCursor(0, 48);
      sprintf(str, "Date: %02d/%02d", now.month(), now.day());
      u8g2.print(str);
      u8g2.setCursor(0, 64);
      sprintf(str, "Time: %02d:%02d", now.hour(), now.minute());
      u8g2.print(str);
    } while ( u8g2.nextPage() );
  }

  // task4: 記錄到 SD 卡
  if ((millis() - lastWriteFileTime) > WRITEFILE_INTERVAL) {
    lastWriteFileTime = millis();

    DateTime now = rtc.now();
    sprintf(str, "%04d-%02d-%02d.csv", now.year(), now.month(), now.day());
    myFile = SD.open(LOG_FILE, FILE_WRITE);
    if (myFile) {      
      sprintf(str, "%02d:%02d:%02d,%2.2f,%2.2f,%d,%d", now.hour(), now.minute(), now.second(), t, h, sgp.TVOC, sgp.eCO2);
      myFile.println(str);
      // close the file:
      myFile.close();
#if DEBUG
      Serial.print("Writing to log file...");
      Serial.println(str);
#endif
    } else {
      // if the file didn't open, print an error:
      Serial.println("error opening log file");
    }
  }
}
