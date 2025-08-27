#include "WiFi.h"
#include "StreamIO.h"
#include "VideoStream.h"
#include "RTSP.h"
#include "NNFaceDetection.h"
#include "VideoStreamOverlay.h"

// === 攝影機與偵測影像設定 ===
#define CHANNEL   1
#define CHANNELNN 3
#define NNWIDTH   576
#define NNHEIGHT  320

// === 感測器與 RGB LED 腳位 ===
#define PIR_PIN     2   // 人體紅外線感測器
#define SOUND_PIN   6   // 聲音感測器接 A0（analog）
#define LED_G_PIN   4   // 綠燈（無偵測）
#define LED_R_PIN   5   // 紅燈（人體）
#define LED_B_PIN   7   // 藍燈（聲音）
const int SOUND_THRESHOLD = 50;  // 聲音感測閾值，可調整

// === 各模組物件 ===
VideoSetting config(VIDEO_FHD, 30, VIDEO_H264, 0);
VideoSetting configNN(NNWIDTH, NNHEIGHT, 10, VIDEO_RGB, 0);
NNFaceDetection facedet;
RTSP rtsp;
StreamIO videoStreamer(1, 1);
StreamIO videoStreamerNN(1, 1);

// === WiFi 設定 ===
char ssid[] = "JiPhone";
char pass[] = "Jason921104";
int status = WL_IDLE_STATUS;
IPAddress ip;

// === 傳送資料的 TCP 客戶端設定 ===
const char* remote_host_ip = "172.20.10.2";
const uint16_t remote_host_port = 8888;
WiFiClient client;

// === 其他全域變數 ===
int rtsp_portnum;
bool shouldExit = false;
volatile bool faceDetectedFlag = false;

// === Cooldown 設定（避免連續傳送）===
const unsigned long FACE_COOLDOWN = 10000;
const unsigned long SOUND_COOLDOWN = 10000;
const unsigned long PIR_COOLDOWN = 10000;
const unsigned long LOOP_INTERVAL_MS = 500;

unsigned long lastFaceSend = 0;
unsigned long lastSoundSend = 0;
unsigned long lastPIRSend = 0;
unsigned long lastLoopTime = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);  // 穩定 Serial 初始化
    Serial.println("系統啟動中...");

    // === WiFi 連線嘗試 ===
    while (status != WL_CONNECTED) {
        Serial.print("嘗試連接 WiFi：");
        Serial.println(ssid);
        status = WiFi.begin(ssid, pass);
        delay(2000);
    }
    ip = WiFi.localIP();
    Serial.print("已連接，IP：");
    Serial.println(ip);

    // === 攝影機與模型初始化 ===
    config.setBitrate(2 * 1024 * 1024);
    Camera.configVideoChannel(CHANNEL, config);
    Camera.configVideoChannel(CHANNELNN, configNN);
    Camera.videoInit();

    rtsp.configVideo(config);
    rtsp.begin();
    rtsp_portnum = rtsp.getPort();

    facedet.configVideo(configNN);
    facedet.setResultCallback(FDPostProcess);
    facedet.modelSelect(FACE_DETECTION, NA_MODEL, DEFAULT_SCRFD, NA_MODEL);
    facedet.begin();

    videoStreamer.registerInput(Camera.getStream(CHANNEL));
    videoStreamer.registerOutput(rtsp);
    if (videoStreamer.begin() != 0)
        Serial.println("RTSP StreamIO 啟動失敗");

    Camera.channelBegin(CHANNEL);

    videoStreamerNN.registerInput(Camera.getStream(CHANNELNN));
    videoStreamerNN.setStackSize();
    videoStreamerNN.setTaskPriority();
    videoStreamerNN.registerOutput(facedet);
    if (videoStreamerNN.begin() != 0)
        Serial.println("NN StreamIO 啟動失敗");

    Camera.channelBegin(CHANNELNN);

    OSD.configVideo(CHANNEL, config);
    OSD.begin();

    Serial.print("📺 RTSP 位址：rtsp://");
    Serial.print(ip);
    Serial.print(":");
    Serial.println(rtsp_portnum);

    // === 感測器與 LED 腳位設定 ===
    pinMode(PIR_PIN, INPUT);
    // analogRead 不需設定 SOUND_PIN
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
}

void loop() {
    // === 檢查是否輸入 exit 指令 ===
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.equalsIgnoreCase("exit")) {
            Serial.println("收到 exit，關閉所有模組...");
            videoStreamer.end();
            videoStreamerNN.end();
            rtsp.end();
            facedet.end();
            Camera.channelEnd(CHANNEL);
            Camera.channelEnd(CHANNELNN);
            Serial.println("所有模組已關閉。");
            shouldExit = true;
        }
    }

    if (shouldExit) {
        delay(1000);
        return;
    }

    // === 控制執行頻率 ===
    if (millis() - lastLoopTime < LOOP_INTERVAL_MS) return;
    lastLoopTime = millis();

    // === 感測器讀取 ===
    bool pirDetected = digitalRead(PIR_PIN) == HIGH;
    int soundVal = analogRead(SOUND_PIN);
    bool soundDetected = soundVal > SOUND_THRESHOLD;

    Serial.print("聲音強度 = ");
    Serial.println(soundVal);

    // === RGB LED 控制 ===
    if (pirDetected) {
        Serial.println("偵測到人體活動！");
        digitalWrite(LED_R_PIN, HIGH);
        digitalWrite(LED_G_PIN, LOW);
        digitalWrite(LED_B_PIN, LOW);
    } else if (soundDetected) {
        Serial.println("偵測到聲音！");
        digitalWrite(LED_R_PIN, LOW);
        digitalWrite(LED_G_PIN, LOW);
        digitalWrite(LED_B_PIN, HIGH);
    } else {
        Serial.println("無活動（人/聲）");
        digitalWrite(LED_R_PIN, LOW);
        digitalWrite(LED_G_PIN, HIGH);
        digitalWrite(LED_B_PIN, LOW);
    }

    // === 嘗試連線至 Python 主機 ===
    static unsigned long lastTryConnect = 0;
    if (!client.connected() && millis() - lastTryConnect > 3000) {
        Serial.println("嘗試連線到 Python 主機...");
        client.connect(remote_host_ip, remote_host_port);
        lastTryConnect = millis();
    }

    // === 傳送人臉事件（有 cooldown）===
    if (client.connected() && faceDetectedFlag && (millis() - lastFaceSend > FACE_COOLDOWN)) {
        client.println("FACE_DETECTED");
        Serial.println("傳送：FACE_DETECTED");
        lastFaceSend = millis();
        faceDetectedFlag = false;
    }

    // === 傳送聲音事件（有 cooldown）===
    if (client.connected() && soundDetected && (millis() - lastSoundSend > SOUND_COOLDOWN)) {
        client.println("SOUND_DETECTED");
        Serial.println("傳送：SOUND_DETECTED");
        lastSoundSend = millis();
    }

    // === 傳送人體事件（有 cooldown）===
    if (client.connected() && pirDetected && (millis() - lastPIRSend > PIR_COOLDOWN)) {
        client.println("PIR_DETECTED");
        Serial.println("傳送：PIR_DETECTED");
        lastPIRSend = millis();
    }
}

// === 人臉辨識回呼處理 ===
void FDPostProcess(std::vector<FaceDetectionResult> results) {
    uint16_t im_h = config.height();
    uint16_t im_w = config.width();

    printf("Total number of faces detected = %d\r\n", facedet.getResultCount());
    OSD.createBitmap(CHANNEL);

    if (facedet.getResultCount() > 0) {
        Serial.println("偵測到人臉");

        for (uint32_t i = 0; i < facedet.getResultCount(); i++) {
            FaceDetectionResult item = results[i];
            int xmin = (int)(item.xMin() * im_w);
            int xmax = (int)(item.xMax() * im_w);
            int ymin = (int)(item.yMin() * im_h);
            int ymax = (int)(item.yMax() * im_h);

            printf("Face %d score %d:\t%d %d %d %d\n", i, item.score(), xmin, xmax, ymin, ymax);
            OSD.drawRect(CHANNEL, xmin, ymin, xmax, ymax, 3, OSD_COLOR_WHITE);

            char text_str[40];
            snprintf(text_str, sizeof(text_str), "%s %d", item.name(), item.score());
            OSD.drawText(CHANNEL, xmin, ymin - OSD.getTextHeight(CHANNEL), text_str, OSD_COLOR_CYAN);

            for (int j = 0; j < 5; j++) {
                int x = (int)(item.xFeature(j) * im_w);
                int y = (int)(item.yFeature(j) * im_h);
                OSD.drawPoint(CHANNEL, x, y, 8, OSD_COLOR_RED);
            }
        }

        faceDetectedFlag = true;
    }

    OSD.update(CHANNEL);
}
