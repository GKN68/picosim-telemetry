// 피트(ESP32 #3) — nRF24로 받은 데이터를 노트북으로 보냄 (CSV 한 줄씩)
// 배선: nRF SCK 12, MOSI 11, MISO 13, CSN 15, CE 16, 어댑터 VCC 5V
// 노트북 화면(텔레메트리 HTML)이 이 출력을 읽음
//
// 출력 형식
//   D,수신시간ms,패킷번호,차체ms,ax,ay,az(g),gx,gy,gz(deg/s)
//   S,초당수신개수,손실개수        ← 1초마다 링크 상태
#include <SPI.h>
#include <RF24.h>

#define NRF_SCK 12
#define NRF_MISO 13
#define NRF_MOSI 11
#define NRF_CSN 15
#define NRF_CE 16
#define PA_LEVEL RF24_PA_LOW  // 차체 코드와 같게

RF24 radio(NRF_CE, NRF_CSN);
const uint8_t ADDR[6] = "FSAE1";

struct __attribute__((packed)) Packet {
  uint8_t type;
  uint16_t count;
  uint32_t ms;
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
};

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  Serial.println("# 피트 수신기 시작");

  pinMode(NRF_CSN, OUTPUT);
  digitalWrite(NRF_CSN, HIGH);
  SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI);
  if (!radio.begin(&SPI)) {
    Serial.println("# [nRF] 응답 없음 → 어댑터 5V, CE 16, CSN 15, SPI 확인");
    while (true) delay(1000);
  }
  radio.setPALevel(PA_LEVEL);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(100);
  radio.setPayloadSize(sizeof(Packet));
  radio.openReadingPipe(1, ADDR);
  radio.startListening();
  Serial.println("# [nRF] OK, 수신 대기");
}

void loop() {
  static uint32_t got = 0, lost = 0, lastLog = 0;
  static uint16_t lastCount = 0;
  static bool first = true;
  Packet p;

  while (radio.available()) {
    radio.read(&p, sizeof(p));
    if (p.type != 1) continue;
    if (!first) {
      uint16_t gap = (uint16_t)(p.count - lastCount);
      if (gap > 1 && gap < 1000) lost += gap - 1;
    }
    first = false;
    lastCount = p.count;
    got++;
    Serial.printf("D,%lu,%u,%lu,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n",
                  millis(), p.count, p.ms,
                  p.ax / 1000.0, p.ay / 1000.0, p.az / 1000.0,
                  p.gx / 10.0, p.gy / 10.0, p.gz / 10.0);
  }

  uint32_t now = millis();
  if (now - lastLog >= 1000) {
    lastLog = now;
    Serial.printf("S,%lu,%lu\n", got, lost);
    got = lost = 0;
  }
}
