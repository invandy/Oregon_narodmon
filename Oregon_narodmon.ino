/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Скетч для передачи показаний с беспроводных датчиков Oregon Scientific THN132N или THGN132N на сервис “Народный Мониторинг” (narodmon.ru)
//с помощью Arduino-совместимых плат на основе ESP8266 (Wemos D1, NodeMCU).

//Для подключения необходимы:
//- Сам датчик Oregon Scientific THN132N, THGN132N, THGN123 и т.п.,
//- ПРоцессорная плата на основе ESP8266 (Wemos D1 или NodeMCU),
//- Приёмник OOK 433Мгц (Питание 3В, подключается к D7 процессорной платы),
//- WiFi подключение к Интернет
//- Arduino IDE с установленной поддержкой ESP8266-совместимых устройств и библиотекой Oregon_NR
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Oregon_NR.h>
#include <ESP8266WiFi.h>

//Режимы работы
#define TEST_MODE       1             //Режим отладки (данные на narodmon.ru не отсылаются)

#define SEND_INTERVAL 300000          //Как часто отсылать данные на сервер
#define CONNECT_TIMEOUT 10000         //Время ожидания  соединения
#define DISCONNECT_TIMEOUT 10000      //Время ожидания отсоединения

#define mac "#FF:FF:FF:FF:FF:FF"      //МАС-адрес на narodmon.ru


const char* ssid = "ASUS";            //Параметры входа в WiFi
const char* password = "ASUS";

//****************************************************************************************

Oregon_NR oregon(13, 13, 2, true); // Приёмник 433Мгц подключён к D7 (GPIO13), Светодиод на D2 подтянут к +пит.

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Массивы для хранения полученных данных для датчиков Oregon:
    float r_tmp[3];                //Температура
    float  r_hmdty[3];             //Влажность. Если = 101 - значит нет данных
    bool  r_bat[3];                //Флаг батареи
    bool  r_isreceived[3];         //Флаг о том, что по данному каналу приходил хоть один правильный пакет и данные актуальны
    
    word  r_type[3];               //Тип датчика
    unsigned long rcv_time[3];     // времена прихода последних пакетов
    float  number_of_receiving[3]; //сколько пакетов получено в процессе сбора данных

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
  //Инициализация памяти
  for (int i = 0; i < 3; i++)
  {
    rcv_time[i] = 7000000;
    r_isreceived[i] = 0;
    number_of_receiving[i] = 0;
  }
 
  
  /////////////////////////////////////////////////////
  //Запуск Serial-ов

  Serial.begin(115200);
  Serial.println("");
  
  if (TEST_MODE) Serial.println("Test mode");

  
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
  for (int i = 0; i < 3; i++){
    if (r_isreceived[i]) is_a_data_to_send = 1;                 // Есть ли данные для отправки?
  }
  //////////////////////////////////////////////////////////////////////
  //Отправка данных на narodmon.ru/////////////////////////////////////
  //////////////////////////////////////////////////////////////////////  
  
  if (millis() - lastConnectionTime > postingInterval && is_a_data_to_send)  {
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


  //////////////////////////////////////////////////////////////////////
  //Захват пакета,//////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////  
  oregon.capture(0);
  //
  //Захваченные данные годны до следующего вызова capture

  //////////////////////////////////////////////////////////////////////
  //ОБработка полученного пакета//////////////////////////////////////////////
  if (oregon.captured) 
  {
    //Вывод информации в Serial
    Serial.print("TIME:\t");
    Serial.print(millis()/1000);
    Serial.print("sec\t");
      
    for (int q = 0;q < PACKET_LENGTH; q++)
    if (oregon.valid_p[q] == 0x0F) Serial.print(oregon.packet[q], HEX);
    else Serial.print(" ");
    if ((oregon.sens_type == THGN132 || oregon.sens_type == THN132) && oregon.crc_c){

      
      Serial.print(" TYPE: ");
      if (oregon.sens_type == THGN132) Serial.print("THGN132N");
      if (oregon.sens_type == THN132) Serial.print("THN132N ");
      Serial.print(" CHNL: ");
      Serial.print(oregon.sens_chnl);
      if (oregon.sens_tmp > 0 && oregon.sens_tmp < 10) Serial.print(" TMP:  ");
      if (oregon.sens_tmp < 0 && oregon.sens_tmp >-10) Serial.print(" TMP: ");
      if (oregon.sens_tmp <= -10) Serial.print(" TMP:");
      if (oregon.sens_tmp >= 10) Serial.print(" TMP: ");
      Serial.print(oregon.sens_tmp, 1);
      Serial.print("C ");
      if (oregon.sens_type == THGN132) {
        Serial.print("HUM: ");
        Serial.print(oregon.sens_hmdty, 1);
        Serial.print("%");
      }
            
      Serial.print(" BAT: ");
      if (oregon.sens_battery) Serial.print("F "); else Serial.print("e ");
      Serial.print("ID: ");
      Serial.print(oregon.sens_id, HEX);
      //Serial.print(" PACKETS: ");
      //Serial.print(oregon.packets_received);
      //Serial.print(" PROC. TIME: ");
      //Serial.print(oregon.work_time);
      //Serial.println("ms ");
      
               
      byte _chnl = oregon.sens_chnl - 1;
      number_of_receiving[ _chnl]++;
      r_type[_chnl] = oregon.sens_type;
      r_bat[_chnl] = oregon.sens_battery;
      r_isreceived[_chnl] = 1;
      r_tmp[_chnl] = r_tmp[_chnl] * ((number_of_receiving[_chnl] - 1) / number_of_receiving[_chnl]) + oregon.sens_tmp / number_of_receiving[_chnl];
      r_hmdty[_chnl] = r_hmdty[_chnl] * ((number_of_receiving[_chnl] - 1) / number_of_receiving[_chnl]) + oregon.sens_hmdty / number_of_receiving[_chnl];
      rcv_time[_chnl] = millis();         
    }
    Serial.println("");
    
  }
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

  //Если соединения с сервером нет, по переподключаемся
  if (WiFi.status() != WL_CONNECTED) wifi_connect();
  
  bool what_return = false;
  if (client.connect(nardomon_server, port)) {  
    //Отправляем MAC-адрес
    Serial.println(' ');
    String s = mac;
    Serial.println(s);
    if (!TEST_MODE) client.println(s);
    //Отправляем данные Oregon
    sendOregonData();
        
    //Завершаем передачу
    client.println("##");
    Serial.println("##");
    //Ждём отключения клиента
    cur_mark = millis();
    do {
      yield();
      if (millis() > cur_mark + DISCONNECT_TIMEOUT) break;
    } while (!client.connected());
      
    Serial.println(' ');
    client.stop();
    what_return = true;
  } 
  else {
    Serial.println("connection to narodmon.ru failed");
    client.stop();
  }
  lastConnectionTime = millis();

  //Обнуляем флаги полученных данных
  for (int i = 0; i < 3; i++) {
    r_isreceived[i] = 0;
    number_of_receiving[i] = 0;
  }
  
  return what_return;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void sendOregonData() {
  String s;
  if (r_isreceived[0]) {
    s = "#T1#";
    s += r_tmp[0];
    if (r_hmdty[0] > 0 && r_hmdty[0] <= 100 ){
      s += "\n#H1#";
      s += r_hmdty[0];
    }
    
    Serial.println(s);
    if (!TEST_MODE) client.println(s);

  }
  if (r_isreceived[1]) {
    s = "#T2#";
    s += r_tmp[1];
    if (r_hmdty[1] > 0 && r_hmdty[1] <= 100 ){
      s += "\n#H2#";
      s += r_hmdty[1];
    }
    Serial.println(s);
    if (!TEST_MODE) client.println(s);
  }
  if (r_isreceived[2]) {
    s = "#T3#";
    s += r_tmp[2];
    if (r_hmdty[2] > 0 && r_hmdty[2] <= 100 ){
      s += "\n#H3#";
      s += r_hmdty[2];
    }
    Serial.println(s);
    if (!TEST_MODE) client.println(s);
  }
}
////////////////////////////////////////////////////////////////////////////////////////
// ЗАМЕНА DELAY, которая работает и не приводит к вылету...
////////////////////////////////////////////////////////////////////////////////////////
void wait_timer(int del){
  unsigned long tm_marker = millis();
  while (millis() - tm_marker < del) yield();
  return;
    
}
