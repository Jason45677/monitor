import socket
import serial
import cv2
import time
import asyncio
import discord
from google import genai
import threading

# === 設定區 ===
HOST = '0.0.0.0'
PORT = 8888

SERIAL_PORT = None         # 設為 None 或 "" 表示不使用 Serial，例如 'COM5'、'/dev/ttyUSB0'
BAUDRATE = 115200

RTSP_URL = 'rtsp://172.20.10.4:554'
SAVE_PATH = "snapshot.jpg"

DISCORD_TOKEN = "MTM4MTE1Njg2NTQxNjYyNjMyNg.Gwz17P.iw21Y7Pz-9YD1X5yUvOo8HHyQ6MVP6pwcbkkbY"
GEMINI_API_KEY = "AIzaSyBHcy7266nbu39z5fA1us4Iw24Tyqsllkw"
TARGET_CHANNEL_ID = 1381158562507522079

# === 初始化 Gemini ===
genai_client = genai.Client(api_key=GEMINI_API_KEY)

# === 初始化 Discord 客戶端 ===
intents = discord.Intents.default()
intents.message_content = True
client = discord.Client(intents=intents)

# === Discord 全域變數 ===
discord_channel = None
sentinel_mode = True  # 🟢 預設啟用哨兵模式


@client.event
async def on_ready():
    global discord_channel
    print(f"✅ 登入成功：{client.user}")
    discord_channel = client.get_channel(TARGET_CHANNEL_ID)
    if discord_channel:
        print(f"✅ 綁定至頻道：{discord_channel.name}")
    else:
        print("❌ 找不到指定的頻道 ID，請確認 BOT 已加入伺服器並擁有該頻道權限")


@client.event
async def on_message(message):
    global sentinel_mode

    if message.author == client.user:
        return

    content = message.content.strip().lower()

    if content == "!sentinel on":
        sentinel_mode = True
        await message.channel.send("🟢 哨兵模式已啟用。")

    elif content == "!sentinel off":
        sentinel_mode = False
        await message.channel.send("🔴 哨兵模式已停用。")


# === Gemini 圖片描述函式 ===
async def handle_gemini_description(channel):
    try:
        with open(SAVE_PATH, 'rb') as f:
            await channel.send(file=discord.File(f, filename="snapshot.jpg"))

        my_file = genai_client.files.upload(file="./snapshot.jpg")
        response = genai_client.models.generate_content(
            model="gemini-2.0-flash",
            contents=[my_file, "用繁體中文簡短描述這張圖片"],
        )
        description = response.text
        await channel.send(description)

    except Exception as e:
        await channel.send(f"❌ 出錯了：{e}")


# === 背景監聽程式（Serial）===
def background_listener():
    use_serial = SERIAL_PORT and SERIAL_PORT.strip() != ""
    ser = None
    if use_serial:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
            print(f"📡 Serial 已啟用：{SERIAL_PORT}")
        except Exception as e:
            print(f"❌ Serial 初始化失敗：{e}")
            use_serial = False

    last_trigger_time = 0
    TRIGGER_COOLDOWN = 10  # 秒數限制：每次擷取需間隔 10 秒以上
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((HOST, PORT))
        s.listen()
        print(f"📡 等待 ESP 發來訊息 on port {PORT}...")

        while True:
            conn, addr = s.accept()
            print(f"✅ ESP 已連接：{addr}")
            with conn:
                while True:
                    msg, line = "", ""

                    try:
                        conn.settimeout(0.5)
                        data = conn.recv(1024)
                        if not data:
                            print("⚠️ ESP 斷線，等待重新連線...")
                            break
                        msg = data.decode().strip()
                        print(f"[ESP] 收到訊息：{msg}")
                    except socket.timeout:
                        pass

                    if use_serial and ser.in_waiting:
                        line = ser.readline().decode('utf-8').strip()
                        if line:
                            print(f"[Serial] 收到訊息：{line}")

                    if (msg in ["FACE_DETECTED", "PIR_DETECTED", "SOUND_DETECTED"] or
                        line in ["FACE_DETECTED", "PIR_DETECTED", "SOUND_DETECTED"]) and \
                            (time.time() - last_trigger_time > TRIGGER_COOLDOWN):
                        last_trigger_time = time.time()
                        print("✅ 偵測到事件，準備擷取畫面")
                        cap = cv2.VideoCapture(RTSP_URL, cv2.CAP_FFMPEG)
                        for _ in range(30):
                            cap.read()
                        ret, frame = cap.read()
                        cap.release()
                        if ret:
                            cv2.imwrite(SAVE_PATH, frame)
                            time.sleep(0.5)
                            print(f"📸 擷取完成，儲存為 {SAVE_PATH}")

                            event_type = msg or line
                            caption = {
                                "FACE_DETECTED": "🧠 偵測到人臉！",
                                "PIR_DETECTED": "🚶 偵測到人體移動！",
                                "SOUND_DETECTED": "🎤 偵測到聲音！"
                            }.get(event_type, "📸 擷取畫面")

                            if sentinel_mode and discord_channel:
                                asyncio.run_coroutine_threadsafe(
                                    discord_channel.send(caption),
                                    client.loop
                                )
                                asyncio.run_coroutine_threadsafe(
                                    handle_gemini_description(discord_channel),
                                    client.loop
                                )
                            else:
                                print("⚠️ 哨兵模式已停用，不傳送到 Discord/Gemini。")
                        else:
                            print("❌ 擷取影格失敗")


# === 啟動背景執行緒 ===
threading.Thread(target=background_listener, daemon=True).start()

# === 啟動 Discord Bot ===
client.run(DISCORD_TOKEN)