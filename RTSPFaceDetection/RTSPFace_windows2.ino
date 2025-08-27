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
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim(); // 移除空白與換行
        if (input.equalsIgnoreCase("exit")) {
            Serial.println("🔴 收到 exit 指令，正在關閉所有模組...");

            // 停止串流與神經網路功能
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
        // 程式結束，進入 idle 狀態（保留 Serial 可再輸入重啟命令）
        delay(1000);
        return;
    }

    // --- 感測邏輯 ---
    bool pirDetected = digitalRead(PIR_PIN) == HIGH;
    bool soundDetected = digitalRead(SOUND_PIN) == HIGH;

    // 優先判斷人體，其次聲音
    if (pirDetected) {
        Serial.println("🚶 偵測到人體活動！");
        digitalWrite(LED_R_PIN, HIGH);  // 紅燈亮
        digitalWrite(LED_G_PIN, LOW);   // 綠燈滅
        digitalWrite(LED_B_PIN, LOW);   // 藍燈滅
    } else if (soundDetected) {
        Serial.println("🎤 偵測到聲音！");
        digitalWrite(LED_R_PIN, LOW);
        digitalWrite(LED_G_PIN, LOW);
        digitalWrite(LED_B_PIN, HIGH);  // 藍燈亮
    } else {
        Serial.println("🟩 無活動（人/聲）");
        digitalWrite(LED_R_PIN, LOW);
        digitalWrite(LED_G_PIN, HIGH);  // 綠燈亮
        digitalWrite(LED_B_PIN, LOW);
    }
    
    if (!client.connected()) {
        Serial.println("🔄 嘗試連線到 Python 主機...");
        client.connect(remote_host_ip, remote_host_port);
        delay(100);  // 可加等待讓連線穩定
    }

    if (facedet.getResultCount() > 0 && client.connected()) {
        client.println("FACE_DETECTED");
        Serial.println("📤 已傳送 FACE_DETECTED 給 Python");
        delay(5000);  // cooldown：避免每幀都傳一次，造成 Python 過載
    }

    delay(300); // 每 0.3 秒偵測一次
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
    }

    OSD.update(CHANNEL);
}