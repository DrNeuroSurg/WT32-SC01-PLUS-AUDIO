/****************************************************************************************************************************
  This is for  WT32_SC01_PLUS boards only !

  Licensed under MIT license

  by DrNeurosurg 2024
  *****************************************************************************************************************************/

#include <Arduino.h>


//YOUR WIFI-CREDENTIALS
const char* ssid = "REPLACE_WITH_YOUR_SSID";
const char* password = "REPLACE_WITH_YOUR_PASSWORD";
const char* sampleStreamURL = "http://stream-uk1.radioparadise.com/aac-320";


// SOME INCLUDES, YOU MAY NEED IT LATER
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD_MMC.h>
#include <FS.h>
#include <LittleFS.h> 
#include <SPIFFS.h>
#include <FFat.h>


#define WT_USE_LVGL           // WE ARE USING LVGL
#define WT_USE_SDCARD         // SD-CARD ON-BOARD
//#define TFT_PORTRAIT        // UNCOMMENT THIS LINE, IF YOU WANT PORTRAT-MODE (DEFAULT IS LANDSCAPE)
#include <WT32_SC01_PLUS.h>   // <=== ALLMOST ALL YOU NEED IS HERE

// AUDIO LIB FROM wolle aka Schreibfaul1
#include <Audio.h>
#include <audiotask.h>

//******************** FORWARD DECLARATIONS **********************

static void event_handler_button(lv_event_t * event);
void createSimpleUI();
void initWiFi();

//******************** LVGL **************************************
SemaphoreHandle_t lvgl_mux;
//FOR vTask_delay
uint32_t ms;

lv_obj_t * tempLabel ;
lv_obj_t * buttonLabel;

bool _playing = false;      //TO REMEMBER, IF WE ARE PLAYING (FOR PLAY / STOP)

// ****************** LVGL  TASK **********************************

  #define LVGL_TASK_CORE 1
  #define LVGL_TASK_PRIO 5     

TaskHandle_t Task_lvgl;

void lvglTask(void *parameter) {
  while(true)
  {
    xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
     ms = lv_timer_handler();
    xSemaphoreGiveRecursive(lvgl_mux);
    vTaskDelay(pdMS_TO_TICKS(ms ));

  }
}

void createLVGL_Task() {
  xTaskCreatePinnedToCore(
        lvglTask,               /* Function to implement the task */
        "lvglTask",             /* Name of the task */
        10000,                  /* Stack size in words */
        NULL,                   /* Task input parameter */
        LVGL_TASK_PRIO,         /* Priority of the task */
        &Task_lvgl,             /* Task handle. */
        LVGL_TASK_CORE          /* Core where the task should run */
    );
}

void lvglTaskDelete(){
    vTaskDelete(Task_lvgl);
}

// ****************** SETUP **********************************

void setup() {

  // COMMENT NEXT LINES OUT, IF YOU WANT TO USE SERIAL.PRINT..

  Serial.begin(115200);
  delay(3000);                                  //NEEDED BECAUSE OF INITIALIZING USB-C
  Serial.setDebugOutput(true);
  Serial.flush();

  // LVGL
  lvgl_mux = xSemaphoreCreateRecursiveMutex();  // CREATE MUTEX !!
  init_display();                               // DO THIS AS EARLY AS POSSIBLE (MEMORY ALLOCATION) !!
  lv_tick_set_cb(xTaskGetTickCount);            //LVGL TICK CALLBACK

  // START LVGL TASK
  createLVGL_Task();
  LV_LOG_USER("\n INIT LVGL DONE");

  createSimpleUI();
  
}

void loop() {
  // NOTHING TO DO - EVERYTHING IN ITS OWN TASK
}

void createSimpleUI() {
  
  LV_LOG_USER("\n CREATE UI");

/*  YOU SHOULD (OR MUST) ENCLOSE EVERY lv_.. FUNCTION WITH

      xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
      ....
      xSemaphoreGiveRecursive(lvgl_mux);

      TO AVOID MEMORY CONFLICTS - IF NOT, SCREEN-FLICKERING IS POSSIBLE
  */


  // FOR LVGL-WIDGETS AND MORE: SEE DOCUMENTATIONON @ https://docs.lvgl.io/master/ 

  // 1ST:
  // SCREEN BLACK
  xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x00), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER,0); 
  xSemaphoreGiveRecursive(lvgl_mux);   

  // 2ND:
  // CREATE A SIMPLE LABEL ON SCREEN
  xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
    tempLabel = lv_label_create(lv_scr_act());                        //GLOBAL DEFINED, FOR LATER ACCESSIBLE         
    lv_obj_set_size(tempLabel, LV_SIZE_CONTENT, LV_SIZE_CONTENT); 
    lv_obj_align(tempLabel, LV_ALIGN_TOP_MID, 0, 50);                 //TOP ON PARENT , SOME PIXELS DOWN
    lv_obj_set_style_text_align(tempLabel, LV_TEXT_ALIGN_CENTER, 0);  //CENTER TXET
    lv_obj_set_style_text_color(tempLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_clear_flag(tempLabel, LV_OBJ_FLAG_SCROLLABLE);             //DON'T USE SCROLLBARS
    String ss = "WT32-SC01-PLUS Connecting to WiFi ..";               //YOU CAN ALSO USE char*
    lv_label_set_text(tempLabel, ss.c_str()) ;                        // SET TEXT 
  xSemaphoreGiveRecursive(lvgl_mux);  

  // 3RD:
  // CREATE A BUTTON WITH LABEL ON IT 
  xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
                                                        
    lv_obj_t * button = lv_button_create(lv_scr_act());                     // THE BUTTON
    lv_obj_set_size(button,160, 80);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 0);                            // CENTER ON SCREEN
    lv_obj_remove_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(button, event_handler_button, LV_EVENT_ALL, NULL);  // ADD EVENT-HANDLER
    buttonLabel = lv_label_create(button);                             // LABEL ON BUTTON (AS CAPTION)
    lv_label_set_text(buttonLabel, "PLAY");                                     // TEXT
    lv_obj_center(buttonLabel);                                                   // CENTER LABEL ON BUTTON
  xSemaphoreGiveRecursive(lvgl_mux);  
   
   initWiFi();

   //LETS PLAY SOMETHING ...

  //INITIALIZE THE AUDIO-TASK
   audioInit();
   audioSetVolume(5);                                                   // DEFAULT IS 0..21
}

//AUDIO CALLBACK
void audio_showstreamtitle(const char* info) {
  xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
  lv_label_set_text(tempLabel, info) ;  
  lv_label_set_text(buttonLabel, "STOP") ;  
  xSemaphoreGiveRecursive(lvgl_mux);
  _playing = true;
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
   
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
  lv_label_set_text(tempLabel, "CONNECTED !") ;  

}

static void event_handler_button(lv_event_t * event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if(code == LV_EVENT_CLICKED) {
        if(!_playing) {
          LV_LOG_USER("\n* PLAY CLICKED *");

          //PLAY STREAM
          audioConnecttohost(sampleStreamURL);

          //UPDATE BUTTON-TEXT
          xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
          lv_label_set_text(buttonLabel, "STOP");  
          xSemaphoreGiveRecursive(lvgl_mux);

        } else {

          LV_LOG_USER("\n* STOP CLICKED *");

          //STOP STREAM
          audioStopSong();

          //UPDATE LABEL & BUTTON
          xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
          lv_label_set_text(buttonLabel, "PLAY");   
          lv_label_set_text(tempLabel, "") ;  
          xSemaphoreGiveRecursive(lvgl_mux); 
          _playing = false;

        }
    }
}