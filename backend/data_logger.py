import requests, time, csv, os
from datetime import datetime
from config import*

print(f"Connecting to {BASE_URL}...")
print(f"Logging to {FILE_PATH}")

# ตรวจสอบว่ามีไฟล์อยู่แล้วไหม? ถ้าไม่มีให้สร้างหัวตาราง (Header) ก่อน
if not os.path.exists(FILE_PATH):
    with open(FILE_PATH, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(["Timestamp", "Current_Motor(A)"]) # Header

while True:
    try:
        # 1. Request ไปหา 
        response = requests.get(f"{BASE_URL}/api/status", timeout=3)
        
        if response.status_code == 200:
            data = response.json()                       # แปลง JSON เป็น Dictionary
            
            # 2. ดึงค่าเฉพาะที่ต้องการ
            current_val = data.get('currentA', 0.0)      # ถ้าหาไม่เจอให้ค่าเป็น 0.0
            timestamp   = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            # 3. บันทึกลงไฟล์ (Append Mode)
            with open(FILE_PATH, mode='a', newline='') as file:
                writer = csv.writer(file)
                writer.writerow([timestamp, current_val])

            print(f"Saved: {timestamp} | Current Motor: {current_val} A")

        else:
            print(f"❌ Error: Wemos ไม่ตอบรับ (Status code ไม่ใช่ 200)")

    except Exception as e:
        print(f"⚠️ Connection Lost: {e}")

    # รอ 3 วินาที ค่อยขอใหม่
    time.sleep(INTERVAL)