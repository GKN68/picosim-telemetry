// 차체(데이터 ESP32 #1) — BMI270 읽어서 nRF24로 피트에 전송
//
// nRF24 (기본 SPI):  SCK 12, MOSI 11, MISO 13, CSN 15, CE 16, 어댑터 VCC 5V
// BMI270 (별도 SPI): SCL→21, MI→47, MO→10, CS→14, VCC 3.3V
//   nRF 모듈이 MISO를 놓지 않아서 BMI270은 다른 SPI 선을 씀
//
// 라이브러리: RF24 (TMRh20), SparkFun BMI270 Arduino Library
#include <SPI.h>
#include <RF24.h>
#include <SparkFun_BMI270_Arduino_Library.h>

// ---- nRF ----
#define NRF_SCK 12
#define NRF_MISO 13
#define NRF_MOSI 11
#define NRF_CSN 15
#define NRF_CE 16

// ---- BMI270 ----
#define IMU_SCK 21
#define IMU_MOSI 47  // 모듈 MI
#define IMU_MISO 10  // 모듈 MO
#define IMU_CS 14

#define SEND_HZ 50          // 무선 전송 속도
#define PA_LEVEL RF24_PA_LOW  // 책상: LOW / 실차: RF24_PA_MAX

SPIClass imuSPI(HSPI);
RF24 radio(NRF_CE, NRF_CSN);
BMI270 imu;
const uint8_t ADDR[6] = "FSAE1";

// 32바이트 이하 (nRF 한 패킷 최대치)
struct __attribute__((packed)) Packet {
  uint8_t type;      // 1 = IMU (나중에 엔진·휠속도 등 추가용)
  uint16_t count;    // 패킷 번호 (손실 계산)
  uint32_t ms;       // 차체 시간
  int16_t ax, ay, az;  // 가속도 ×1000 (mg)
  int16_t gx, gy, gz;  // 자이로 ×10 (0.1 deg/s)
};
Packet pkt;

bool imuOk = false;
uint32_t sent = 0, acked = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  Serial.println("\n차체 텔레메트리");

  // BMI270
  pinMode(IMU_CS, OUTPUT);
  digitalWrite(IMU_CS, HIGH);
  imuSPI.begin(IMU_SCK, IMU_MISO, IMU_MOSI, IMU_CS);
  for (int i = 0; i < 3 && !imuOk; i++) {
    imuOk = imu.beginSPI(IMU_CS, 5000000, imuSPI) == BMI2_OK;
    if (!imuOk) delay(300);
  }
  if (imuOk) {
    imu.setAccelODR(BMI2_ACC_ODR_200HZ);
    imu.setGyroODR(BMI2_GYR_ODR_200HZ);
    Serial.println("[BMI270] OK");
  } else {
    Serial.println("[BMI270] 응답 없음 → SCL 21, MI 47, MO 10, CS 14, 3.3V 확인");
  }

  // nRF
  pinMode(NRF_CSN, OUTPUT);
  digitalWrite(NRF_CSN, HIGH);
  SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI);
  if (!radio.begin(&SPI)) {
    Serial.println("[nRF] 응답 없음 → 어댑터 5V, CE 16, CSN 15, SCK 12, MOSI 11, MISO 13 확인");
    while (true) delay(1000);
  }
  radio.setPALevel(PA_LEVEL);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(100);
  radio.setPayloadSize(sizeof(Packet));
  radio.setRetries(2, 3);  // 실시간이라 재전송은 짧게
  radio.openWritingPipe(ADDR);
  radio.stopListening();
  Serial.println("[nRF] OK");
}

int16_t clamp16(float v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return (int16_t)lroundf(v);
}

void loop() {
  static uint32_t next = millis(), lastLog = 0;
  uint32_t now = millis();

  if ((int32_t)(now - next) >= 0) {
    next += 1000 / SEND_HZ;
    if (imuOk && imu.getSensorData() == BMI2_OK) {
      pkt.ax = clamp16(imu.data.accelX * 1000);
      pkt.ay = clamp16(imu.data.accelY * 1000);
      pkt.az = clamp16(imu.data.accelZ * 1000);
      pkt.gx = clamp16(imu.data.gyroX * 10);
      pkt.gy = clamp16(imu.data.gyroY * 10);
      pkt.gz = clamp16(imu.data.gyroZ * 10);
    }
    pkt.type = 1;
    pkt.count++;
    pkt.ms = now;
    sent++;
    if (radio.write(&pkt, sizeof(pkt))) acked++;
  }

  if (now - lastLog >= 1000) {
    lastLog = now;
    Serial.printf("가속 %+.2f %+.2f %+.2f g | 전송 %lu 수신확인 %lu (%.0f%%)\n",
                  pkt.ax / 1000.0, pkt.ay / 1000.0, pkt.az / 1000.0,
                  sent, acked, sent ? 100.0 * acked / sent : 0.0);
    sent = acked = 0;
  }
}
