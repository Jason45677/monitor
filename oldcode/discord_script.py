import discord  # 匯入 Discord API
from google import genai  # 匯入 Gemini API

# --- 1. 設定 Discord intents ---
intents = discord.Intents.default()
intents.message_content = True

client = discord.Client(intents=intents)

# --- 2. 設定 Gemini API ---
genai_client = genai.Client(api_key="AIzaSyBHcy7266nbu39z5fA1us4Iw24Tyqsllkw")

# --- 3. Discord 事件處理器 ---

@client.event
async def on_ready():
    print(f"登入的使用者 --> {client.user}")

@client.event
async def on_message(message):
    if message.author == client.user:
        return

    # 如果收到訊息是 "describe"，上傳圖片給 Gemini 生成描述並回傳
    if message.content == "describe":
        try:
            # 上傳圖片到 Gemini
            my_file = genai_client.files.upload(file="./snapshot.jpg")

            # 請求 Gemini 生成描述
            response = genai_client.models.generate_content(
                model="gemini-2.0-flash",
                contents=[my_file, "用繁體中文描述這張圖片"],
            )

            description = response.text  # 拿到描述文字

            # 傳回 Discord 頻道
            await message.channel.send(description)
        except Exception as e:
            await message.channel.send(f"出錯了: {e}")

# --- 4. 啟動機器人 ---
client.run("MTM4MTE1Njg2NTQxNjYyNjMyNg.Gwz17P.iw21Y7Pz-9YD1X5yUvOo8HHyQ6MVP6pwcbkkbY")
