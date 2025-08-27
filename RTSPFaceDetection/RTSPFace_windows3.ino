#include "WiFi.h"
#include "StreamIO.h"
#include "VideoStream.h"
#include "RTSP.h"
#include "NNFaceDetection.h"
#include "VideoStreamOverlay.h"

#define CHANNEL   1
#define CHANNELNN 3
#define NNWIDTH   576
#define NNHEIGHT  320

// 感測與 RGB LED 腳位定義
#define PIR_PIN     2   // 人體感測器
#define SOUND_PIN   6   // 聲音感測器
#define LED_G_PIN   4   // 綠燈
#define LED_R_PIN   5   // 紅燈
#define LED_B_PIN   7   // 藍燈
const int SOUND_THRESHOLD = 40;  // 可以自己調整，太低會誤觸


VideoSetting config(VIDEO_FHD, 30, VIDEO_H264, 0);
VideoSetting configNN(NNWIDTH, NNHEIGHT, 10, VIDEO_RGB, 0);
NNFaceDetection facedet;
RTSP rtsp;
StreamIO videoStreamer(1, 1);
StreamIO videoStreamerNN(1, 1);

char ssid[] = "JiPhone";
char pass[] = "Jason921104";
int status = WL_IDLE_STATUS;

IPAddress ip;
int rtsp_portnum;
bool shouldExit = false;



const char* remote_host_ip = "172.20.10.2";
const uint16_t remote_host_port = 8888;
WiFiClient client;

// 加在全域變數
volatile bool faceDetectedFlag = false;
unsigned long lastSendTime = 0;
const unsigned long cooldown = 5000;
unsigned long lastLoopTime = 0;
const unsigned long LOOP_INTERVAL_MS = 300;

void setup() {
    Serial.begin(115200);

    while (status != WL_CONNECTED) {
        Serial.print("Attempting to connect to WPA SSID: ");
        Serial.println(ssid);
        status = WiFi.begin(ssid, pass);
        delay(2000);
    }
    ip = WiFi.localIP();

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
    if (videoStreamer.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }

    Camera.channelBegin(CHANNEL);

    videoStreamerNN.registerInput(Camera.getStream(CHANNELNN));
    videoStreamerNN.setStackSize();
    videoStreamerNN.setTaskPriority();
    videoStreamerNN.registerOutput(facedet);
    if (videoStreamerNN.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }

    Camera.channelBegin(CHANNELNN);

    OSD.configVideo(CHANNEL, config);
    OSD.begin();

    Serial.print("RTSP Streaming: rtsp://");
    Serial.print(ip);
    Serial.print(":");
    Serial.println(rtsp_portnum);

    // 初始化感測器與 LED 腳位
    pinMode(PIR_PIN, INPUT);
    pinMode(SOUND_PIN, INPUT);
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
}

void loop() {
    // 檢查是否收到 exit 指令（保留原本邏輯）
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.equalsIgnoreCase("exit")) {
            Serial.println("🔴 收到 exit 指令，正在關閉所有模組...");
            videoStreamer.end();
            videoStreamerNN.end();
            rtsp.end();
            facedet.end();
            Camera.channelEnd(CHANNEL);
            Camera.channelEnd(CHANNELNN);
            Serial.println("✅ 所有模組已關閉。系統將停止執行。");
            shouldExit = true;
        }
    }

    if (shouldExit) {
        delay(1000);
        return;
    }

    // ➤ 控制 loop 執行頻率：每 300ms 執行一次
    if (millis() - lastLoopTime < LOOP_INTERVAL_MS) return;
    lastLoopTime = millis();

    // 感測器與 LED 邏輯
    bool pirDetected = digitalRead(PIR_PIN) == HIGH;

    // 類比方式讀取聲音強度（A0）
    int soundVal = analogRead(SOUND_PIN);
    bool soundDetected = soundVal > SOUND_THRESHOLD;
    Serial.println(soundVal);
  
    if (pirDetected) {
        Serial.println("🚶 偵測到人體活動！");
        digitalWrite(LED_R_PIN, HIGH);
        digitalWrite(LED_G_PIN, LOW);
        digitalWrite(LED_B_PIN, LOW);
    } else if (soundDetected) {
        Serial.println("🎤 偵測到聲音！");
        digitalWrite(LED_R_PIN, LOW);
        digitalWrite(LED_G_PIN, LOW);
        digitalWrite(LED_B_PIN, HIGH);
    } else {
        Serial.println("🟩 無活動（人/聲）");
        digitalWrite(LED_R_PIN, LOW);
        digitalWrite(LED_G_PIN, HIGH);
        digitalWrite(LED_B_PIN, LOW);
    }

    // 重新嘗試連線主機（每 3 秒一次）
    static unsigned long lastTryConnect = 0;
    if (!client.connected() && millis() - lastTryConnect > 3000) {
        Serial.println("🔄 嘗試連線到 Python 主機...");
        client.connect(remote_host_ip, remote_host_port);
        lastTryConnect = millis();
    }

    // 人臉訊息 cooldown 控制
    if (client.connected() && faceDetectedFlag && (millis() - lastSendTime > cooldown)) {
      client.println("FACE_DETECTED");
      Serial.println("📤 已傳送 FACE_DETECTED 給 Python");
      lastSendTime = millis();
      faceDetectedFlag = false;
    }

    // --- 聲音通知 Cooldown 控制 ---
    static unsigned long lastSoundSend = 0;
    if (client.connected() && soundDetected && millis() - lastSoundSend > cooldown) {
        client.println("SOUND_DETECTED");
        Serial.println("📤 已傳送 SOUND_DETECTED 給 Python");
        lastSoundSend = millis();
    }
    
    // --- 人體通知 Cooldown 控制 ---
    static unsigned long lastPIRSend = 0;
    if (client.connected() && pirDetected && millis() - lastPIRSend > cooldown) {
        client.println("PIR_DETECTED");
        Serial.println("📤 已傳送 PIR_DETECTED 給 Python");
        lastPIRSend = millis();
    }
}


// Face detection callback
void FDPostProcess(std::vector<FaceDetectionResult> results) {
    uint16_t im_h = config.height();
    uint16_t im_w = config.width();

    printf("Total number of faces detected = %d\r\n", facedet.getResultCount());
    OSD.createBitmap(CHANNEL);

    if (facedet.getResultCount() > 0) {
        Serial.println("FACE_DETECTED");

        for (uint32_t i = 0; i < facedet.getResultCount(); i++) {
            FaceDetectionResult item = results[i];
            int xmin = (int)(item.xMin() * im_w);
            int xmax = (int)(item.xMax() * im_w);
            int ymin = (int)(item.yMin() * im_h);
            int ymax = (int)(item.yMax() * im_h);

            printf("Face %d confidence %d:\t%d %d %d %d\n\r", i, item.score(), xmin, xmax, ymin, ymax);
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
        
        // FDPostProcess 最後加一行：
        faceDetectedFlag = true;
    }

    OSD.update(CHANNEL);
}