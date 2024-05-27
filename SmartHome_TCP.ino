// 스마트홈
#include "DHT.h"

//RFID 설정
#include <MFRC522DriverSPI.h>
#include <MFRC522v2.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
class MFRC522DriverPinSimple sda_pin(53); //RFID포트 53,52,51,50
class MFRC522DriverSPI driver {sda_pin};
class MFRC522 mfrc522 {driver};
const String MASTER_CARD_UID {String("D78B4019")}; //관리자카드

//DHT11(온습도센서) 설정
#include "DHT.h"
const uint8_t TEMP_HUMID_PIN {A0}; // 온습도센서 포트 A0
class DHT dht(TEMP_HUMID_PIN, 11);

//LCD 설정
#include <LiquidCrystal_I2C.h>
class LiquidCrystal_I2C lcd (0x27, 16, 2);

//핀설정
enum CONTROL_PINS {
  TEMPER_HUMID = A0,
  RELAY = 5U,
  SERVO = 6U,
  COOLER = 8U,
  HEATER,
  HUMIDIF,
  DEHUMIDIF
};
enum ShiftRegisterPIN { // 시프트레지스터 포트 24~26
  DS_PIN = 22U,
  LATCH_PIN,
  CLOCK_PIN
};
const uint8_t RFID_LED {30U};
const uint8_t BUZZ_PIN {31U};

//전역변수설정
const uint16_t tone_first {500U}; // 부저 소리 주파수 설정 1
const uint16_t tone_second {700U}; // 부저 소리 주파수 설정 2

volatile bool login_check {false}; // 관리자 체크인여부
volatile bool dht11_state {false}; //온습도센서
volatile bool door_state {false}; // 현관문 상태
volatile int16_t loop_count {0}; // 루프 카운터
volatile float desired_temp {25.0}; // 희망온도
volatile float desired_humid {50.0}; // 희망습도
volatile bool cooling {false};
volatile bool heating {false};
volatile bool humidifying {false};
volatile bool dehumidifying {false};

//고정값 지정
#define ST_DELAY 200UL // 기본딜레이 -> 현재 최소딜레이 동작 200ms
#define MAX_LOOP 10UL // 최대 루프 수
#define TEMP_OFFSET 0.5 // 온도 오프셋
#define HUMID_OFFSET 5 // 습도 오프셋

//커스텀 함수 목록
void initAll(); // 시스템초기화(전원공급릴레이 및 RFID제외)
void lcdClear(); // LCD 정리 함수
void myShift(uint8_t leds); // 시프트레지스터 제어 함수
void doorOpen(); // 현관문 제어 함수
void allLED(bool ononff); // 모든 LED 일괄 켜기/끄기

void setup() {
  Serial.begin(115200UL);
  mfrc522.PCD_Init();
  
  //핀모드
  pinMode(RELAY,OUTPUT);
  pinMode(SERVO,OUTPUT);
  pinMode(COOLER,OUTPUT);
  pinMode(HEATER,OUTPUT);
  pinMode(HUMIDIF,OUTPUT);
  pinMode(DEHUMIDIF,OUTPUT);
  pinMode(DS_PIN,OUTPUT);
  pinMode(LATCH_PIN,OUTPUT);
  pinMode(CLOCK_PIN,OUTPUT);
  pinMode(RFID_LED,OUTPUT);
  pinMode(BUZZ_PIN,OUTPUT);

  digitalWrite(COOLER,LOW);
  digitalWrite(HEATER,LOW);
  digitalWrite(HUMIDIF,LOW);
  digitalWrite(DEHUMIDIF,LOW);
  digitalWrite(RFID_LED,LOW);

  lcd.init(); // LCD 초기화
  lcd.noBacklight();
}

void loop() {
  if (!login_check){//관리자 부재시
    if(!mfrc522.PICC_IsNewCardPresent()) return;
    if(!mfrc522.PICC_ReadCardSerial()) return;
    String tagID = "";
    for(uint8_t i {0u} ; i < 4 ; ++i){
      tagID += String(mfrc522.uid.uidByte[i],HEX);
    }
    tagID.toUpperCase();
    mfrc522.PICC_HaltA();
    if(tagID == MASTER_CARD_UID){
      login_check = true;
    }
    else {
      return;
    }
  }
  else {
    digitalWrite(RELAY,HIGH); // 전원연결
    delay(20UL); //서지 현상 방지
    initAll(); // 시스템초기화

    //변수선언
    float temperature {0.0};
    float humidity {0.0};

    //시작
    for(;;){
      if(loop_count >= MAX_LOOP) {
        loop_count = 0;
      }
      //시리얼입력
      //온습도센서
      if(dht.read()){
        temperature = dht.readTemperature();
        humidity = dht.readHumidity();
      }

      const String sending_data {
          String(temperature)+","+
          String(humidity)+","+
          String(cooling ? "1" : "0") + String(heating ? "1" : "0") + String(humidifying ? "1" : "0") + String(dehumidifying ? "1" : "0")
        };
      Serial.println(sending_data);
      uint8_t roomLight = 0;
      //시리얼출력
      if(Serial.available())
      {
        const String incomming_data {Serial.readStringUntil('\n')};
        //시리얼형식 : "전등 8개에 대한 바이너리8자리" + "," + "희망온도" +  "," + "희망습도"
          
        String sub_income_data {incomming_data};

        for(int i=0;i<2;++i){
          int commaIndex = sub_income_data.indexOf(',');
          if(commaIndex != -1) {
            switch(i) {
              case 0:
                roomLight = strtol(sub_income_data.substring(0, commaIndex).c_str(), NULL,2);
                break;
              case 1:
                desired_temp = atof(sub_income_data.substring(0,commaIndex).c_str());
                break;
            }
            sub_income_data = sub_income_data.substring(commaIndex+1);
          }
        }
        desired_humid = atof(sub_income_data.c_str());
        //방 전등 : 시프트레지스터
        myShift(roomLight);
      }
      //기타
      //온도조절
      if(temperature - TEMP_OFFSET > desired_temp){ // 희망온도보다 높으면
        digitalWrite(COOLER, (cooling = true));
        digitalWrite(HEATER, (heating = false));
      } else if(temperature + TEMP_OFFSET < desired_temp){ // 희망온도보다 낮으면
        digitalWrite(COOLER, (cooling = false));
        digitalWrite(HEATER, (heating = true));
      } else {
        digitalWrite(COOLER, (cooling = false));
        digitalWrite(HEATER, (heating = false));
      }

      //습도조절
      if(humidity - HUMID_OFFSET > desired_humid){ // 희망습도보다 높으면
        digitalWrite(HUMIDIF, (humidifying=false));
        digitalWrite(DEHUMIDIF, (dehumidifying=true));
      } else if(humidity + HUMID_OFFSET < desired_humid){ // 희망습도보다 낮으면
        digitalWrite(HUMIDIF, (humidifying=true));
        digitalWrite(DEHUMIDIF, (dehumidifying=false));
      } else {
        digitalWrite(HUMIDIF, (humidifying=false));
        digitalWrite(DEHUMIDIF, (dehumidifying=false));
      }

      //LCD표현
      String roomLightBinary = "";
      for (int i = 7; i >= 0; i--) {
        roomLightBinary += ((roomLight >> i) & 1) ? '1' : '0';
      }
      lcdClear();
      lcd.print(String(F("LIGHT : ")) + String(roomLightBinary));
      lcd.setCursor(0,1);
      lcd.print(String(temperature) + String(F("C / ")) + String(humidity) + String(F("%")));
      lcd.setCursor(0,0);

      //RFID 체크
      if(mfrc522.PICC_IsNewCardPresent()){
        if(mfrc522.PICC_ReadCardSerial()){
          String tagID = "";
          for(uint8_t i {0u} ; i < 4 ; ++i){
            tagID += String(mfrc522.uid.uidByte[i],HEX);
          }
          tagID.toUpperCase();
          mfrc522.PICC_HaltA();
          if( tagID == MASTER_CARD_UID){
            digitalWrite(RFID_LED,HIGH);
            //체크아웃
            login_check = false;
            // 전원 차단 전 처리
            delay(ST_DELAY*20);
            allLED(false);
            lcdClear(); // LCD 출력 초기화
            lcd.noBacklight();
            digitalWrite(RFID_LED,LOW);
            digitalWrite(RELAY,LOW);
            break;
          }
          else{
            tone(BUZZ_PIN,tone_first);
            delay(ST_DELAY);
            noTone(BUZZ_PIN);
          }
        }
      }
      ++loop_count;
      delay(ST_DELAY);
    }
  }
}

void initAll(){ // 시스템 초기화 함수
  doorOpen();
  dht.begin(); // DHT11 초기화
  lcd.backlight(); // LCD 백라이트 ON
  lcdClear(); // LCD 출력 초기화
  for( int i {0}; i < 3 ;++i){// LED 동작확인
    allLED(true);
    delay(1000UL);
    allLED(false);
    delay(1000UL);
  }
  allLED(false);
}

void allLED(bool onoff) {
  if(onoff) {
    myShift(B11111111);
    digitalWrite(COOLER,HIGH);
    digitalWrite(HEATER,HIGH);
    digitalWrite(HUMIDIF,HIGH);
    digitalWrite(DEHUMIDIF,HIGH);
    digitalWrite(RFID_LED,HIGH);
  } else {
    myShift(B00000000);
    digitalWrite(COOLER,LOW);
    digitalWrite(HEATER,LOW);
    digitalWrite(HUMIDIF,LOW);
    digitalWrite(DEHUMIDIF,LOW);
    digitalWrite(RFID_LED,LOW);
  }
}

void lcdClear(){ // LCD 출력 초기화 함수
  lcd.home();
  lcd.print(F("                "));
  lcd.setCursor(0,1);
  lcd.print(F("                "));
  lcd.home();
}

void myShift(uint8_t leds){// 시프트레지스터 제어 함수
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DS_PIN,CLOCK_PIN, MSBFIRST, leds);
  digitalWrite(LATCH_PIN, HIGH);
}

void doorOpen(){  
  digitalWrite(RFID_LED,HIGH);
  for(int i {0};i<128;i+=2){
    analogWrite(SERVO,i);
    delay(static_cast<int>(ST_DELAY/10));
  }
  delay(ST_DELAY*10);
  for(int i {127};i>=0;i-=2){
    analogWrite(SERVO,i);
    delay(static_cast<int>(ST_DELAY/10));
  }
  digitalWrite(RFID_LED,LOW);
}