import cv2
import serial
import time

# === 設定區 ===
SERIAL_PORT = 'COM5'          # 請根據實際狀況修改，Windows 是 COMx，Linux 是 /dev/ttyUSB0
# SERIAL_PORT = '/dev/tty.usbserial-0001'          # 請根據實際狀況修改，Windows 是 COMx，Linux 是 /dev/ttyUSB0
BAUDRATE = 115200
RTSP_URL = 'rtsp://172.20.10.4:554'
SAVE_PATH = "snapshot.jpg"

# === 初始化 Serial 串口 ===
ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
print(f"📡 正在監聽 Serial ({SERIAL_PORT}) 訊息...")

# === 等待串口回傳 "FACE_DETECTED" 訊息 ===
while True:
    line = ser.readline().decode('utf-8').strip()
    if line:
        print(f"[Serial] 收到訊息：{line}")
    if line == "FACE_DETECTED":
        print("✅ 偵測到人臉訊號，準備擷取畫面")
        break


# 正式抓圖
cap = cv2.VideoCapture("rtsp://172.20.10.4:554", cv2.CAP_FFMPEG)

# 跳過前幾幀（預熱用）
for i in range(30):
    cap.read()
ret, frame = cap.read()
if not ret:
    raise RuntimeError("擷取影格失敗")

# cv2.imwrite(r".\snapshot.jpg", frame) #windows
cv2.imwrite(r"./snapshot.jpg", frame)

cap.release()
print("✅ 已儲存到桌面：snapshot.jpg")
