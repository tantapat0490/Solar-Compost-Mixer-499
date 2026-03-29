import yaml, requests, time, csv, logging
from datetime import datetime, date
from pathlib import Path
from modules.colors import Color


def setup_path():
    """เตรียมค่าคงที่และจัดการโครงสร้างไฟล์"""
    current_file = Path(__file__).resolve().parent                  # หาตำแหน่งของไฟล์ data_log.py ปัจจุบัน
    config_path  = current_file / 'config' / 'config.yml'           # ถอยกลับไป 1 ระดับ (ไปที่ BACKEND) แล้วเข้าหา config/config.yml

    # โหลด Config
    with open(config_path, 'r', encoding='utf-8') as f:
        config = yaml.safe_load(f)

    # เรียกใช้งาน IP URL
    BASE_URL     = config['network']['base_url']
    print(f"{Color.GREEN}Connecting to {BASE_URL}...{Color.RESET}")

    # [Energy_budget]
    max_run_time     = config['energy_budget']['max_run_time']      # รันสูงสุดไม่เกิน 5 นาที
    critical_current = config['energy_budget']['critical_current']
    base_cooldown    = config['energy_budget']['base_cooldown']

    # กำหนด Path สำหรับไฟล์ Log 
    LOG_FOLDER    = current_file / 'log'
    LOG_FILE_NAME = config['storage']['file_path']
    LOG_FILE_PATH = LOG_FOLDER / f"{date.today()}_{LOG_FILE_NAME}"
    interval      = config['app_settings']['interval']

    # ตรวจสอบว่าโฟลเดอร์ log มีอยู่จริงไหม ถ้าไม่มีให้สร้างก่อน
    LOG_FOLDER.mkdir(parents=True, exist_ok=True)

    # ตรวจสอบว่ามีไฟล์อยู่แล้วไหม? ถ้าไม่มีให้สร้างหัวตาราง (Header)
    if not LOG_FILE_PATH.exists():
        with open(LOG_FILE_PATH, mode='w', newline='', encoding='utf-8') as file:
            writer = csv.writer(file)
            writer.writerow(["Timestamp", "Current_Motor(A), Temp(C), Humi(%RH)"]) # Header
        print(f"สร้างไฟล์ใหม่พร้อม Header ที่: {LOG_FILE_PATH}")
    else:
        print(f"ใช้ไฟล์เดิมที่: {LOG_FILE_PATH}")

    # [app_settings]
    sun_start    = config['app_settings']['sun_start']
    sun_end      = config['app_settings']['sun_end']

    return BASE_URL, LOG_FOLDER, LOG_FILE_PATH, max_run_time, critical_current, base_cooldown, interval, sun_start, sun_end


def save_to_csv(log_file_path, timestamp, current_val, temp, humi):
    # บันทึกลงไฟล์ (Append Mode)
    with open(log_file_path, mode='a', newline='') as file:
        writer = csv.writer(file)
        writer.writerow([timestamp, current_val, temp, humi])


def relay_cmd(cmd, base_url):
    """ส่งคำสั่งควบคุมและดึงสถานะ"""
    
    if cmd == "ON":
        url = f"{base_url}/api/on"
    elif cmd == "OFF":
        url = f"{base_url}/api/off"
    else:
        url = f"{base_url}/api/status"

    # Request พร้อมเกราะป้องกัน (Try-Catch)
    try:
        r = requests.get(url, timeout=3)
        return r.json() 
        
    except Exception as e:
        return {"error": str(e)}


if __name__ == "__main__":
    BASE_URL, LOG_FOLDER, LOG_FILE_PATH, max_run_time, critical_current, base_cooldown, interval, sun_start, sun_end = setup_path()

    print(f"{Color.GREEN}Control Started...{Color.RESET}")

    # สร้างตัวแปรไว้จับเวลานอกลูป
    start_run      = 0
    last_log_time  = 0
    cooldown_until = 0  # เอาไว้ดักไม่ให้มันเปิดซ้ำทันทีตอนเพิ่งตัดไฟ

    # ตั้งค่าการบันทึก log
    logging.basicConfig(
        filename=LOG_FOLDER / 'command.log', 
        filemode='a',
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        level=logging.DEBUG
    )

    while True:
        try:
            current_time = time.time()
            ts           = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            now          = datetime.now().strftime("%H:%M")
            
            # 1. ถามสถานะจริงจาก Wemos ทุกรอบลูป
            res = requests.get(f"{BASE_URL}/api/status", timeout=2)
            if res.status_code != 200:
                print(f"{Color.RED}❌ Can't contact ESP8266...{Color.RESET}")
                time.sleep(5)
                continue
                
            data = res.json()
            curr = data.get('current', 0.0)
            temp = data.get('temp', 0.0)
            humi = data.get('humi', 0.0)
            
            # ดึงสถานะจาก API มาเลย (Wemos มึงต้องส่ง key ชื่อ 'status' มา)
            motor_status = data.get('relay', 'OFF')

            # 2. เช็ค Emergency Stop ตลอดเวลา
            if curr > critical_current:
                if motor_status == "ON": # ถ้ามอเตอร์ยังเปิดอยู่ ค่อยสั่งปิด
                    relay_cmd("OFF", BASE_URL)

                # save ลง command.log
                logging.critical(f"[{ts}] EMERGENCY STOP @ {curr}A)  !!!!!!!!!!!!!")

                print(f"{Color.RED}🚨 EMERGENCY STOP! ({curr}A){Color.RESET}")
                cooldown_until = current_time + base_cooldown                       # บังคับพักยาวตาม base_cooldown
                time.sleep(base_cooldown)
                continue

            # 3. ลอจิก State Machine (เริ่มทำงานตามเวลา)
            if sun_start <= now <= sun_end:
                
                # ถ้าอยู่ในช่วงโดนสั่งพัก (เช่น เพิ่งทำงานเสร็จ หรือเพิ่งกระแสเกิน) ให้ข้ามไปก่อน
                if current_time < cooldown_until:
                    print(f"⏳ Resting and waiting for the cooldown to finish...")
                    time.sleep(base_cooldown)
                    continue

                # State A: เครื่องปิดอยู่ -> สั่งเปิด และเริ่มจับเวลา
                if motor_status == "OFF":
                    print(f"{Color.GREEN}[{now}] Motor Start...{Color.RESET}")

                    relay_cmd("ON", BASE_URL)
                    logging.info(f"[{ts}] ON")

                    start_run     = current_time
                    last_log_time = 0 
                    time.sleep(interval) # รอรีเลย์สับแป๊บนึง ค่อยวนลูปใหม่
                    continue

                # State B: เครื่องเปิดอยู่ -> เช็คเวลาและเก็บข้อมูล
                if motor_status == "ON":
                    # เช็คว่าทำงานลากยาวเกิน max_run_time หรือยัง?
                    if current_time - start_run >= max_run_time:
                        print(f"{Color.CYAN}🧊 Cooldown {base_cooldown/60:.1f} min...{Color.RESET}")

                        relay_cmd("OFF", BASE_URL)
                        logging.info(f"[{ts}] OFF")

                        cooldown_until = current_time + interval # สั่งพักเครื่องเท่ากับเวลา interval ก่อนรอบต่อไป
                        continue
                    
                    # ลอจิกแยกสำหรับเก็บ Log ตามรอบเวลา interval
                    if current_time - last_log_time >= interval:
                        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                        save_to_csv(LOG_FILE_PATH, ts, curr, temp, humi)
                        print(f"{Color.BRIGHT_MAGENTA}Logging: {ts} | {curr}A | {temp}°C | {humi}%RH{Color.RESET}")
                        last_log_time = current_time

            # 4. นอกเวลาทำงาน (Standby)
            else:
                if motor_status == "ON": # ถ้าดันเปิดค้างอยู่ ให้สั่งปิด
                    relay_cmd("OFF", BASE_URL)
                print(f"🌑 [{now}] ({sun_start}-{sun_end}) Standby Mode...")
                time.sleep(60)

            # หน่วงเวลาลูปหลักแค่ 1 วิ เพื่อให้ระบบตอบสนองได้ไว
            time.sleep(1)

        except requests.exceptions.ConnectionError:
            print(f"{Color.YELLOW}🔕 Target Offline: ไม่สามารถติดต่อ Wemous D ได้...{Color.RESET}")
            time.sleep(10)
        except Exception as e:
            print(f"{Color.YELLOW}⚠️ Connection Lost: {e}{Color.RESET}")
            time.sleep(10)