/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Скетч для передачи показаний с беспроводных датчиков Oregon Scientific на сервис “Народный Мониторинга” (narodmon.ru)
//с помощью Arduino-совместимых плат на основе ESP8266 (Wemos D1, NodeMCU).

//Для подключения необходимы:
//- Сам датчик Oregon Scientific THN132N, THGN132N, THGN123 и т.п.,
//- Плата микроконтроллера на основе ESP8266 (Wemos D1 или NodeMCU),
//- Приёмник OOK 433Мгц (Питание 3В, подключается к D7 платы микроконтроллера),
//- WiFi подключение к Интернет
//- Arduino IDE с установленной поддержкой ESP8266-совместимых устройств и библиотекой Oregon_NR
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Oregon_NR.h>
#include <ESP8266WiFi.h>

#define TEST_MODE        1              //Режим отладки (данные на narodmon.ru не отсылаются)

//Кол-во датчиков различных типов используемых в системе. 
//Для экономии памяти можно обнулить неиспользуемые типы датчиков
#define NOF_132     3     //THGN132
#define NOF_500     1     //THGN500
#define NOF_968     1     //BTHR968
#define NOF_129     5     //BTHGN129
#define NOF_318     5     //RTGN318
#define NOF_800     9     //THGR810
#define NOF_THP     8     //THP

#define SEND_INTERVAL 300000            //Интервал отсылки данных на сервер, мс
#define CONNECT_TIMEOUT 10000           //Время ожидания  соединения, мс
#define DISCONNECT_TIMEOUT 10000        //Время ожидания отсоединения, мс

#define mac       "#FF:FF:FF:FF:FF:FF"  //МАС-адрес на narodmon.ru
#define ssid      "ASUS"                //Параметры входа в WiFi
#define password  "asus"

//Анемометр
#define WIND_CORRECTION 0     //Коррекция севера на флюгере в градусах (используется при невозможности сориентировать датчик строго на север)
#define NO_WINDDIR      4     //Кол-во циклов передачи, необходимое для накопления данных для о направлении ветра

#define  N_OF_THP_SENSORS NOF_132 + NOF_500 + NOF_968 + NOF_129 + NOF_318 + NOF_800 + NOF_THP
//****************************************************************************************

Oregon_NR oregon(13, 13, 2, true); // Приёмник 433Мгц подключён к D7 (GPIO13), Светодиод на D2 подтянут к +пит.

//****************************************************************************************
//Структура для хранения полученных данных от термогигрометров:
struct BTHGN_sensor
{
  bool  isreceived = 0;           //Флаг о том, что по данному каналу приходил хоть один правильный пакет и данные актуальны
  byte  number_of_receiving = 0;  //сколько пакетов получено в процессе сбора данных
  unsigned long rcv_time = 7000000;// времена прихода последних пакетов
  byte  chnl;                     //канал передачи
  word  type;                     //Тип датчика
  float temperature;              //Температура
  float humidity;                 //Влажность. 
  float pressure;                 //Давление в мм.рт.ст.
  bool  battery;                  //Флаг батареи
  float voltage;                  //Напряжение батареи
};

BTHGN_sensor t_sensor[N_OF_THP_SENSORS];
//****************************************************************************************
//Структура для хранения полученных данных от анемометра:
struct WGR800_sensor
{
  bool  isreceived = 0;           //Флаг о том, что по данному каналу приходил хоть один правильный пакет и данные актуальны
  byte  number_of_receiving = 0;  //сколько пакетов получено в процессе сбора данных
  byte  number_of_dir_receiving = 0;  //сколько пакетов получено в процессе сбора данных
  unsigned long rcv_time = 7000000;// времена прихода последних пакетов
  
  float midspeed;                 //Средняя скорость ветра
  float maxspeed;                 //Порывы ветра
  float direction_x;              // Направление ветра
  float direction_y;
  byte dir_cycle = 0;             //Кол-во циклов накопления данных
  float dysp_wind_dir = -1;
  bool  battery;                 //Флаг батареи
};

WGR800_sensor wind_sensor;

//****************************************************************************************
//Структура для хранения УФ-индекса:
struct UVN800_sensor
{
  bool  isreceived = 0;           //Флаг о том, что по данному каналу приходил хоть один правильный пакет и данные актуальны
  byte  number_of_receiving = 0;  //сколько пакетов получено в процессе сбора данных
  unsigned long rcv_time = 7000000;// времена прихода последних пакетов
  
  float  index;                     //УФ-индекс
  bool battery;                    //Флаг батареи
};

UVN800_sensor uv_sensor;
//****************************************************************************************
//Структура для хранения полученных данных от счётчика осадков:
struct PCR800_sensor
{
  bool  isreceived = 0;           //Флаг о том, что по данному каналу приходил хоть один правильный пакет и данные актуальны
  byte  number_of_receiving = 0;  //сколько пакетов получено в процессе сбора данных
  unsigned long rcv_time = 7000000;// времена прихода последних пакетов
  
  float  rate;                    //Интенсивность осадков
  float  counter;                 //счётчик осадков
  bool battery;                   //Флаг батареи
};

PCR800_sensor rain_sensor;
//****************************************************************************************

#define BLUE_LED 2      //Индикация подключения к WiFi
#define GREEN_LED 14    //Индикатор успешной доставки пакета а народмон
#define RED_LED 255     //Индикатор ошибки доставки пакета на народмон


//Параметоы соединения с narodmon:
//IPAddress nardomon_server(94,19,113,221);
char nardomon_server[] = "narodmon.ru"; 
int port=8283;
WiFiClient client; //Клиент narodmon


const unsigned long postingInterval = SEND_INTERVAL; 
unsigned long lastConnectionTime = 0;                   
boolean lastConnected = false;                          
unsigned long cur_mark;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//SETUP//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup()
{

    
  pinMode(BLUE_LED, OUTPUT);        
  pinMode(GREEN_LED, OUTPUT);        
  pinMode(RED_LED, OUTPUT);        
  
  /////////////////////////////////////////////////////
  //Запуск Serial-ов

  Serial.begin(115200);
  Serial.println("");
  Serial.println("");
  
  if (TEST_MODE) Serial.println("TEST MODE");

  
  /////////////////////////////////////////////////////
  //Запуск Wifi
  

  wifi_connect();
  /////////////////////////////////////////////////////
  

  digitalWrite(BLUE_LED, HIGH);    
  if (test_narodmon_connection()){
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, LOW);
    
    
  }
  else {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, HIGH);

  }


  //вкючение прослушивания радиоканала  
  oregon.start(); 
  

}
//////////////////////////////////////////////////////////////////////
//LOOP//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

void loop() 
{
  //////////////////////////////////////////////////////////////////////
  //Защита от подвисаний/////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////  
  if  (micros() > 0xFFF00000) while ( micros() < 0xFFF00000); //Висим секунду до переполнения
  if  (millis() > 0xFFFFFC0F) while ( millis() < 0xFFFFFC0F); //Висим секунду до переполнения


  //////////////////////////////////////////////////////////////////////
  //Проверка полученных данных,/////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////  
  bool is_a_data_to_send = false;
  for (int i = 0; i < N_OF_THP_SENSORS; i++){
    if (t_sensor[i].number_of_receiving) is_a_data_to_send = 1;                 // Есть ли данные для отправки?
  }
   if (wind_sensor.number_of_receiving) is_a_data_to_send = 1;                 // Есть ли данные для отправки?
   if (rain_sensor.number_of_receiving) is_a_data_to_send = 1;                 // Есть ли данные для отправки?
   if (uv_sensor.number_of_receiving) is_a_data_to_send = 1;                 // Есть ли данные для отправки?
  //////////////////////////////////////////////////////////////////////
  //Отправка данных на narodmon.ru/////////////////////////////////////
  //////////////////////////////////////////////////////////////////////  
  
  if (millis() - lastConnectionTime > postingInterval && is_a_data_to_send)  {

    if (is_a_data_to_send)
    {
    //Обязательно отключить прослушивание канала
    oregon.stop();
    

    digitalWrite(BLUE_LED, HIGH);    
    if (send_data()){
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED, LOW);
      
    }
    else {
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED, HIGH);
      
    }

    oregon.start();    
    }
    else Serial.println("No data to send");
  }


  //////////////////////////////////////////////////////////////////////
  //Захват пакета,//////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////  
  oregon.capture(0);
  //
  //Захваченные данные годны до следующего вызова capture

  //////////////////////////////////////////////////////////////////////
  //ОБработка полученного пакета//////////////////////////////////////////////
  //ОБработка полученного пакета//////////////////////////////////////////////
  if (oregon.captured)  
  {
    yield();
    //Вывод информации в Serial
    Serial.print ((float) millis() / 1000, 1); //Время
    Serial.print ("s\t\t");
    //Версия протокола
    if (oregon.ver == 2) Serial.print("  ");
    if (oregon.ver == 3) Serial.print("3 ");
    
    //Информация о восстановлени пакета
    if (oregon.restore_sign & 0x01) Serial.print("s"); //восстановлены одиночные такты
    else  Serial.print(" ");
    if (oregon.restore_sign & 0x02) Serial.print("d"); //восстановлены двойные такты
    else  Serial.print(" ");
    if (oregon.restore_sign & 0x04) Serial.print("p "); //исправленна ошибка при распознавании версии пакета
    else  Serial.print("  ");
    if (oregon.restore_sign & 0x08) Serial.print("r "); //собран из двух пакетов (для режима сборки в v.2)
    else  Serial.print("  ");

    //Вывод полученного пакета.
    for (int q = 0;q < oregon.packet_length; q++)
      if (oregon.valid_p[q] == 0x0F) Serial.print(oregon.packet[q], HEX);
      else Serial.print(" ");
        
    //Время обработки пакета
    Serial.print("  ");
    Serial.print(oregon.work_time);
    Serial.print("ms ");
    
    if ((oregon.sens_type == THGN132 ||
    (oregon.sens_type & 0x0FFF) == RTGN318 ||
    (oregon.sens_type & 0x0FFF) == RTHN318 ||
    oregon.sens_type == THGR810 ||
    oregon.sens_type == THN132 ||
    oregon.sens_type == THN800 ||
    oregon.sens_type == BTHGN129 ||
    oregon.sens_type == BTHR968 ||
    oregon.sens_type == THGN500) && oregon.crc_c)
    {
      Serial.print("\t");
      if (oregon.sens_type == THGN132) Serial.print("THGN132N");
      if (oregon.sens_type == THGN500) Serial.print("THGN500 ");
      if (oregon.sens_type == THGR810) Serial.print("THGR810 ");
      if ((oregon.sens_type & 0x0FFF) == RTGN318) Serial.print("RTGN318 ");
      if ((oregon.sens_type & 0x0FFF) == RTHN318) Serial.print("RTHN318 ");
      if (oregon.sens_type == THN132 ) Serial.print("THN132N ");
      if (oregon.sens_type == THN800 ) Serial.print("THN800  ");
      if (oregon.sens_type == BTHGN129 ) Serial.print("BTHGN129");
      if (oregon.sens_type == BTHR968 ) Serial.print("BTHR968 ");

      if (oregon.sens_type != BTHR968 && oregon.sens_type != THGN500)
      {
        Serial.print(" CHNL: ");
        Serial.print(oregon.sens_chnl);
      }
      else Serial.print("        ");
      Serial.print(" BAT: ");
      if (oregon.sens_battery) Serial.print("F "); else Serial.print("e ");
      Serial.print("ID: ");
      Serial.print(oregon.sens_id, HEX);
      
      if (oregon.sens_tmp >= 0 && oregon.sens_tmp < 10) Serial.print(" TMP:  ");
      if (oregon.sens_tmp < 0 && oregon.sens_tmp >-10) Serial.print(" TMP: ");
      if (oregon.sens_tmp <= -10) Serial.print(" TMP:");
      if (oregon.sens_tmp >= 10) Serial.print(" TMP: ");
      Serial.print(oregon.sens_tmp, 1);
      Serial.print("C ");
      if (oregon.sens_type == THGN132 ||
          oregon.sens_type == THGR810 ||
          oregon.sens_type == BTHGN129 ||
          oregon.sens_type == BTHR968 ||
          (oregon.sens_type & 0x0FFF) == RTGN318 ||
          oregon.sens_type == THGN500 ) {
        Serial.print("HUM: ");
        Serial.print(oregon.sens_hmdty, 0);
        Serial.print("%");
      }
      else Serial.print("        ");

      if (oregon.sens_type == BTHGN129 ||  oregon.sens_type == BTHR968)
      {
      Serial.print(" PRESS: ");
      Serial.print(oregon.get_pressure(), 1);
      Serial.print("Hgmm ");
      }

      if (oregon.sens_type == THGN132 && oregon.sens_chnl > NOF_132) {Serial.println(); return;}
      if (oregon.sens_type == THGN500 && NOF_500 == 0) {Serial.println(); return;}
      if (oregon.sens_type == BTHR968 && NOF_968 == 0) {Serial.println(); return;}
      if (oregon.sens_type == THGR810 && oregon.sens_chnl > NOF_800) {Serial.println(); return;}
      if (oregon.sens_type == BTHGN129 && oregon.sens_chnl > NOF_129) {Serial.println(); return;}
      if ((oregon.sens_type & 0x0FFF) == RTGN318 && oregon.sens_chnl > NOF_318) {Serial.println(); return;}
      
      byte _chnl = oregon.sens_chnl - 1;
           
      if (oregon.sens_type == THGN500) _chnl = NOF_132;
      if (oregon.sens_type == BTHR968 ) _chnl = NOF_132 + NOF_500; 
      if (oregon.sens_type == BTHGN129 ) _chnl = NOF_132 + NOF_500 + NOF_968; 
      if (oregon.sens_type == THGR810 || oregon.sens_type == THN800) _chnl = NOF_132 + NOF_500 + NOF_968 + NOF_129;
      if ((oregon.sens_type & 0x0FFF) == RTGN318 || (oregon.sens_type & 0x0FFF) == RTHN318) _chnl  = NOF_132 + NOF_500 + NOF_968 + NOF_129 + NOF_800;

      t_sensor[ _chnl].chnl = oregon.sens_chnl;
      t_sensor[ _chnl].number_of_receiving++;
      t_sensor[ _chnl].type = oregon.sens_type;
      t_sensor[ _chnl].battery = oregon.sens_battery;
      t_sensor[ _chnl].pressure = t_sensor[ _chnl].pressure * ((float)(t_sensor[ _chnl].number_of_receiving - 1) / (float)t_sensor[ _chnl].number_of_receiving) + oregon.get_pressure() / t_sensor[ _chnl].number_of_receiving;
      t_sensor[ _chnl].temperature = t_sensor[ _chnl].temperature * ((float)(t_sensor[ _chnl].number_of_receiving - 1) / (float)t_sensor[ _chnl].number_of_receiving) + oregon.sens_tmp / t_sensor[ _chnl].number_of_receiving;
      t_sensor[ _chnl].humidity = t_sensor[ _chnl].humidity * ((float)(t_sensor[ _chnl].number_of_receiving - 1) / (float)t_sensor[ _chnl].number_of_receiving) + oregon.sens_hmdty / t_sensor[ _chnl].number_of_receiving;
      t_sensor[ _chnl].rcv_time = millis();         
    }
    
    if (oregon.sens_type == PCR800 && oregon.crc_c)
    {
      Serial.print("\tPCR800  ");
      Serial.print("        ");
      Serial.print(" BAT: ");
      if (oregon.sens_battery) Serial.print("F "); else Serial.print("e ");
      Serial.print(" ID: ");
      Serial.print(oregon.sens_id, HEX);
      Serial.print("   TOTAL: ");
      Serial.print(oregon.get_total_rain(), 1);
      Serial.print("mm  RATE: ");
      Serial.print(oregon.get_rain_rate(), 1);
      Serial.print("mm/h");
      rain_sensor.number_of_receiving++;
      rain_sensor.battery = oregon.sens_battery;
      rain_sensor.rate = oregon.get_rain_rate();
      rain_sensor.counter = oregon.get_total_rain();
      rain_sensor.rcv_time = millis();         
    }    
    
  if (oregon.sens_type == WGR800 && oregon.crc_c){
      Serial.print("\tWGR800  ");
      Serial.print("        ");
      Serial.print(" BAT: ");
      if (oregon.sens_battery) Serial.print("F "); else Serial.print("e ");
      Serial.print("ID: ");
      Serial.print(oregon.sens_id, HEX);
      
      Serial.print(" AVG: ");
      Serial.print(oregon.sens_avg_ws, 1);
      Serial.print("m/s  MAX: ");
      Serial.print(oregon.sens_max_ws, 1);
      Serial.print("m/s  DIR: "); //N = 0, E = 4, S = 8, W = 12
      switch (oregon.sens_wdir)
      {
      case 0: Serial.print("N"); break;
      case 1: Serial.print("NNE"); break;
      case 2: Serial.print("NE"); break;
      case 3: Serial.print("NEE"); break;
      case 4: Serial.print("E"); break;
      case 5: Serial.print("SEE"); break;
      case 6: Serial.print("SE"); break;
      case 7: Serial.print("SSE"); break;
      case 8: Serial.print("S"); break;
      case 9: Serial.print("SSW"); break;
      case 10: Serial.print("SW"); break;
      case 11: Serial.print("SWW"); break;
      case 12: Serial.print("W"); break;
      case 13: Serial.print("NWW"); break;
      case 14: Serial.print("NW"); break;
      case 15: Serial.print("NNW"); break;
      }

      wind_sensor.battery = oregon.sens_battery;
      wind_sensor.number_of_receiving++;
      wind_sensor.number_of_dir_receiving++;
            
      //Средняя скорость
      wind_sensor.midspeed = wind_sensor.midspeed * (((float)wind_sensor.number_of_receiving - 1) / (float)wind_sensor.number_of_receiving) + oregon.sens_avg_ws / wind_sensor.number_of_receiving;
      
      //Порывы
      if (oregon.sens_max_ws > wind_sensor.maxspeed || wind_sensor.number_of_receiving == 1) wind_sensor.maxspeed = oregon.sens_max_ws;
      
      //Направление
      //Вычисляется вектор - его направление и модуль.
      if (wind_sensor.number_of_dir_receiving == 1 && (wind_sensor.direction_x != 0 || wind_sensor.direction_x != 0))
      {
        float wdiv = sqrt((wind_sensor.direction_x * wind_sensor.direction_x) + (wind_sensor.direction_y * wind_sensor.direction_y));
        wind_sensor.direction_x /= wdiv;
        wind_sensor.direction_y /= wdiv;
      }
      
      float wind_module = 1;
      if (oregon.sens_wdir == 0) {
        wind_sensor.direction_x += 1 * wind_module;
      }
      if (oregon.sens_wdir == 1) {
        wind_sensor.direction_x += 0.92 * wind_module;
        wind_sensor.direction_y -= 0.38 * wind_module;
      }
      if (oregon.sens_wdir == 2) {
        wind_sensor.direction_x += 0.71 * wind_module;
        wind_sensor.direction_y -= 0.71 * wind_module;
      }
      if (oregon.sens_wdir == 3) {
        wind_sensor.direction_x += 0.38 * wind_module;
        wind_sensor.direction_y -= 0.92 * wind_module;
      }
      if (oregon.sens_wdir == 4) {
        wind_sensor.direction_y -= 1 * wind_module;
      }
      
      if (oregon.sens_wdir == 5) {
        wind_sensor.direction_x -= 0.38 * wind_module;
        wind_sensor.direction_y -= 0.92 * wind_module;
      }
      if (oregon.sens_wdir == 6) {
        wind_sensor.direction_x -= 0.71 * wind_module;
        wind_sensor.direction_y -= 0.71 * wind_module;
      }
      if (oregon.sens_wdir == 7) {
        wind_sensor.direction_x -= 0.92 * wind_module;
        wind_sensor.direction_y -= 0.38 * wind_module;
      }
      if (oregon.sens_wdir == 8) {
        wind_sensor.direction_x -= 1 * wind_module;
      }
      if (oregon.sens_wdir == 9) {
        wind_sensor.direction_x -= 0.92 * wind_module;
        wind_sensor.direction_y += 0.38 * wind_module;
      }
      if (oregon.sens_wdir == 10) {
        wind_sensor.direction_x -= 0.71 * wind_module;
        wind_sensor.direction_y += 0.71 * wind_module;
      
      }
      if (oregon.sens_wdir == 11) {
        wind_sensor.direction_x -= 0.38 * wind_module;
        wind_sensor.direction_y += 0.92 * wind_module;
      }
      if (oregon.sens_wdir == 12) {
        wind_sensor.direction_y += 1 * wind_module;
      }
      if (oregon.sens_wdir == 13) {
        wind_sensor.direction_x += 0.38 * wind_module;
        wind_sensor.direction_y += 0.92 * wind_module;
      }
      if (oregon.sens_wdir == 14) {
        wind_sensor.direction_x += 0.71 * wind_module;
        wind_sensor.direction_y += 0.71 * wind_module;
      }
      if (oregon.sens_wdir == 15) {
        wind_sensor.direction_x += 0.92 * wind_module;
        wind_sensor.direction_y += 0.38 * wind_module;
      }
        wind_sensor.rcv_time = millis();
    }    

    if (oregon.sens_type == UVN800 && oregon.crc_c)
    {
      Serial.print("\tUVN800  ");
      Serial.print("        ");
      Serial.print(" BAT: ");
      if (oregon.sens_battery) Serial.print("F "); else Serial.print("e ");
      Serial.print("ID: ");
      Serial.print(oregon.sens_id, HEX);
      
      Serial.print(" UV IDX: ");
      Serial.print(oregon.UV_index);
      
      uv_sensor.number_of_receiving++;
      uv_sensor.battery = oregon.sens_battery;
      uv_sensor.index = uv_sensor.index * ((float)(uv_sensor.number_of_receiving - 1) / (float)uv_sensor.number_of_receiving) + oregon.UV_index / uv_sensor.number_of_receiving;
      uv_sensor.rcv_time = millis();         
    }    

    if (oregon.sens_type == RFCLOCK && oregon.crc_c){
      Serial.print("\tRF CLOCK");
      Serial.print(" CHNL: ");
      Serial.print(oregon.sens_chnl);
      Serial.print(" BAT: ");
      if (oregon.sens_battery) Serial.print("F "); else Serial.print("e ");
      Serial.print("ID: ");
      Serial.print(oregon.sens_id, HEX);
      Serial.print(" TIME: ");
      Serial.print(oregon.packet[6] & 0x0F, HEX);
      Serial.print(oregon.packet[6] & 0xF0 >> 4, HEX);
      Serial.print(':');
      Serial.print(oregon.packet[5] & 0x0F, HEX);
      Serial.print(oregon.packet[5] & 0xF0 >> 4, HEX);
      Serial.print(':');
      Serial.print(':');
      Serial.print(oregon.packet[4] & 0x0F, HEX);
      Serial.print(oregon.packet[4] & 0xF0 >> 4, HEX);
      Serial.print(" DATE: ");
      Serial.print(oregon.packet[7] & 0x0F, HEX);
      Serial.print(oregon.packet[7] & 0xF0 >> 4, HEX);
      Serial.print('.');
      if (oregon.packet[8] & 0x0F ==1 || oregon.packet[8] & 0x0F ==3)   Serial.print('1');
      else Serial.print('0');
      Serial.print(oregon.packet[8] & 0xF0 >> 4, HEX);
      Serial.print('.');
      Serial.print(oregon.packet[9] & 0x0F, HEX);
      Serial.print(oregon.packet[9] & 0xF0 >> 4, HEX);
      
    }    

    if (oregon.sens_type == PCR800 && oregon.crc_c){
      Serial.print("\tPCR800  ");
      Serial.print("        ");
      Serial.print(" BAT: ");
      if (oregon.sens_battery) Serial.print("F "); else Serial.print("e ");
      Serial.print(" ID: ");
      Serial.print(oregon.sens_id, HEX);
      Serial.print("   TOTAL: ");
      Serial.print(oregon.get_total_rain(), 1);
      Serial.print("mm  RATE: ");
      Serial.print(oregon.get_rain_rate(), 1);
      Serial.print("mm/h");
      
    }    
    
#if ADD_SENS_SUPPORT == 1
      if ((oregon.sens_type & 0xFF00) == THP && oregon.crc_c) {
      Serial.print("\tTHP     ");
      Serial.print(" CHNL: ");
      Serial.print(oregon.sens_chnl);
      Serial.print(" BAT: ");
      Serial.print(oregon.sens_voltage, 2);
      Serial.print("V");
      if (oregon.sens_tmp > 0 && oregon.sens_tmp < 10) Serial.print(" TMP:  ");
      if (oregon.sens_tmp < 0 && oregon.sens_tmp > -10) Serial.print(" TMP: ");
      if (oregon.sens_tmp <= -10) Serial.print(" TMP:");
      if (oregon.sens_tmp >= 10) Serial.print(" TMP: ");
      Serial.print(oregon.sens_tmp, 1);
      Serial.print("C ");
      Serial.print("HUM: ");
      Serial.print(oregon.sens_hmdty, 1);
      Serial.print("% ");
      Serial.print("PRESS: ");
      Serial.print(oregon.sens_pressure, 1);
      Serial.print("Hgmm");
      yield();

      if (oregon.sens_chnl > NOF_THP - 1) {Serial.println(); return;}
      
      byte _chnl = oregon.sens_chnl  + NOF_132 + NOF_500 + NOF_968 + NOF_129 + NOF_800 + NOF_318;
      t_sensor[ _chnl].chnl = oregon.sens_chnl + 1;
      t_sensor[ _chnl].number_of_receiving++;
      t_sensor[ _chnl].type = oregon.sens_type;
      t_sensor[ _chnl].pressure = t_sensor[ _chnl].pressure * ((float)(t_sensor[ _chnl].number_of_receiving - 1) / (float)t_sensor[ _chnl].number_of_receiving) + oregon.sens_pressure / t_sensor[ _chnl].number_of_receiving;
      t_sensor[ _chnl].temperature = t_sensor[ _chnl].temperature * ((float)(t_sensor[ _chnl].number_of_receiving - 1) / (float)t_sensor[ _chnl].number_of_receiving) + oregon.sens_tmp / t_sensor[ _chnl].number_of_receiving;
      t_sensor[ _chnl].humidity = t_sensor[ _chnl].humidity * ((float)(t_sensor[ _chnl].number_of_receiving - 1) / (float)t_sensor[ _chnl].number_of_receiving) + oregon.sens_hmdty / t_sensor[ _chnl].number_of_receiving;
      t_sensor[ _chnl].voltage = t_sensor[ _chnl].voltage * ((float)(t_sensor[ _chnl].number_of_receiving - 1) / (float)t_sensor[ _chnl].number_of_receiving) + oregon.sens_voltage / t_sensor[ _chnl].number_of_receiving;
      t_sensor[ _chnl].rcv_time = millis();         
      
    }
#endif
    Serial.println();
  }
  yield();  
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//***************************************************************************************************************************************
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void wifi_connect() {
  
  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(ssid);
  unsigned long cur_mark = millis();
  bool blink = 0;
  //WiFi.config(ip, gateway, subnet); 
  WiFi.begin(ssid, password);
  do {
      while (WiFi.status() != WL_CONNECTED) {
      if (blink) {
        digitalWrite(BLUE_LED, LOW);
        
      }
      else {
        digitalWrite(BLUE_LED, HIGH);

      }
      blink = !blink;
      delay(500);
      Serial.print(".");
      //Подключаемся слишком долго. Переподключаемся....
      if ((millis() - cur_mark) > CONNECT_TIMEOUT){
        blink = 0; 
        digitalWrite(BLUE_LED, HIGH);
        WiFi.disconnect();
        delay(3000);
        cur_mark = millis();
        WiFi.begin(ssid, password);
      }
    }
  } while (WiFi.status() != WL_CONNECTED);

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());

}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool test_narodmon_connection() {
  if (TEST_MODE) return true;
  if (client.connect(nardomon_server, port)) {  
    client.println("##");
    cur_mark = millis();
    do {
      wait_timer(10);
      if ((millis() - cur_mark) > CONNECT_TIMEOUT) {
        Serial.println("narodmon.ru is not responding");
        client.stop();
        return 0;
      }
    } while (!client.connected());
    Serial.println("narodmon.ru is attainable");
    client.stop();
    return 1;
  } 
  else {
    Serial.println("connection to narodmon.ru failed");
    client.stop();
    return 0;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool send_data() {

  wind_sensor.dir_cycle++;   //Накапливаем циклы для расчёта направления ветра
  //Если соединения с сервером нет, по переподключаемся
  if (WiFi.status() != WL_CONNECTED) wifi_connect();
  
  bool what_return = false;
  bool is_connect = true;
  if (!TEST_MODE) is_connect = client.connect(nardomon_server, port);
  
  if (is_connect) {  
    //Отправляем MAC-адрес
    Serial.println(' ');
    String s = mac;
    Serial.println(s);
    if (!TEST_MODE) client.println(s);
    //Отправляем данные Oregon
    sendOregonData();
        
    //Завершаем передачу
    if (!TEST_MODE) client.println("##");
    Serial.println("##");
    //Ждём отключения клиента
    cur_mark = millis();
    if (!TEST_MODE)
    {
      do 
      {
        yield();
        if (millis() > cur_mark + DISCONNECT_TIMEOUT) break;
      }
      while (!client.connected());
    } 
     
    Serial.println(' ');
    if (!TEST_MODE) client.stop();
    what_return = true;
  } 
  else {
    Serial.println("connection to narodmon.ru failed");
    if (!TEST_MODE) client.stop();
  }
  lastConnectionTime = millis();

  //Обнуляем флаги полученных данных
  for (int i = 0; i < N_OF_THP_SENSORS; i++) 
    t_sensor[i].number_of_receiving = 0;
    
  if (wind_sensor.dir_cycle >= NO_WINDDIR)
  {
    wind_sensor.number_of_dir_receiving = 0;  
    wind_sensor.dir_cycle = 0;
  }
    
  rain_sensor.number_of_receiving = 0;
  wind_sensor.number_of_receiving = 0;
  uv_sensor.number_of_receiving = 0;
  wind_sensor.number_of_receiving = 0; 
  
  
  return what_return;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void sendOregonData() 
{
  String s = "", pref;
  
  for (byte i = 0; i < N_OF_THP_SENSORS; i++)
  {
    if (t_sensor[i].number_of_receiving > 0) 
    {
      if (t_sensor[i].type == BTHGN129) pref = "20";
      if (t_sensor[i].type == THGN132 ||t_sensor[i].type == THN132) pref = "30";
      if ((oregon.sens_type & 0x0FFF) == RTGN318 || (oregon.sens_type & 0x0FFF) == RTHN318) pref = "40";
      if (t_sensor[i].type == THGN500) pref = "50";
      if ((t_sensor[i].type & 0xFF00) == THP) pref = "70";
      if (t_sensor[i].type == THGR810 ||t_sensor[i].type == THN800) pref = "80";
      if (t_sensor[i].type == BTHR968) pref = "90";
      
      s += "#T";
      s += pref;
      s += t_sensor[i].chnl;
      s += "#";
      s += t_sensor[i].temperature;
      s += "\n";
    
      if (t_sensor[i].humidity > 0 && t_sensor[i].humidity <= 100  &&
      (t_sensor[i].type == THGN132 || 
      t_sensor[i].type == THGN500 ||
      t_sensor[i].type == THGR810 ||
      (t_sensor[i].type & 0x0FFF) == RTGN318) ||
      (t_sensor[i].type == BTHGN129  ||
      #if ADD_SENS_SUPPORT == 1
      (t_sensor[i].type & 0xFF00) == THP  ||
      #endif
      t_sensor[i].type == BTHR968))
      {
        s += "#H";
        s += pref;
        s += t_sensor[i].chnl;
        s += "#";
        s += t_sensor[i].humidity;
        s += "\n";
      }

      if ((t_sensor[i].type == BTHGN129  || 
      #if ADD_SENS_SUPPORT == 1
      (t_sensor[i].type & 0xFF00) == THP  ||
      #endif
      t_sensor[i].type == BTHR968))
      {
        s += "#P";
        s += pref;
        s += t_sensor[i].chnl;
        s += "#";
        s += t_sensor[i].pressure;
        s += "\n";
      }
      #if ADD_SENS_SUPPORT == 1
      if ((t_sensor[i].type & 0xFF00) == THP)
      {
        s += "#V";
        s += pref;
        s += t_sensor[i].chnl;
        s += "#";
        s += t_sensor[i].voltage;
        s += "\n";
      }
    #endif
    }
  }
  //Отправляем данные WGR800
  if (wind_sensor.number_of_receiving > 0)
  {
    s += "#WSMID#";
    s += wind_sensor.midspeed;
    s += '\n';
    s += "#WSMAX#";
    s += wind_sensor.maxspeed;
    s += '\n';    
  } 
  
  if (wind_sensor.number_of_dir_receiving > 0 && wind_sensor.dir_cycle >= NO_WINDDIR) 
  {
    s += "#DIR#";
    s += calc_wind_direction(&wind_sensor);
    s += '\n';      
  }
    
    
  //Отправляем данные PCR800
  if (rain_sensor.isreceived > 0)
  {
    s += "#RAIN#";
    s += rain_sensor.counter;
    s += '\n';
  }
    
  //Отправляем данные UVN800
  if (uv_sensor.isreceived > 0)
  {
    s += "#UV#";
    s += uv_sensor.index;
    s += '\n';
  }
    
  Serial.print(s);
  if (!TEST_MODE) client.print(s);
}
////////////////////////////////////////////////////////////////////////////////////////
// Рассчёт направления ветра
////////////////////////////////////////////////////////////////////////////////////////
float calc_wind_direction(WGR800_sensor* wdata)
{
 if (wdata->direction_x == 0) wdata->direction_x = 0.01;
 float otn = abs(wdata->direction_y / wdata->direction_x);
 float angle = (asin(otn / sqrt(1 + otn * otn))) * 180 / 3.14;
  
 //Определяем направление
 if (wdata->direction_x > 0 && wdata->direction_y < 0) otn = angle; 
 if (wdata->direction_x < 0 && wdata->direction_y < 0) otn = 180 - angle;
 if (wdata->direction_x < 0 && wdata->direction_y >= 0) otn = 180 + angle;
 if (wdata->direction_x > 0 && wdata->direction_y >= 0) otn = 360 - angle;
  
 angle = otn + WIND_CORRECTION; // Если маркер флюгера направлен не на север
 if (angle >= 360) angle -= 360;
 
 return angle;
}

////////////////////////////////////////////////////////////////////////////////////////
// ЗАМЕНА DELAY, которая работает и не приводит к вылету...
////////////////////////////////////////////////////////////////////////////////////////
void wait_timer(int del){
  unsigned long tm_marker = millis();
  while (millis() - tm_marker < del) yield();
  return;
    
}
