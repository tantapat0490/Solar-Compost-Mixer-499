from flask import Flask, jsonify
import random, socket
import sys
import logging
import itertools

app = Flask(__name__)

# ปิดการปริ้นท์ Log ของ Flask ให้เหลือแต่ Error
log = logging.getLogger('werkzeug')
log.setLevel(logging.ERROR)

motor_status = "OFF"

# ตัวหมุน Spinner
spinner = itertools.cycle(['⠋', '⠙', '⠹', '⠸', '⠼', '⠴', '⠦', '⠧', '⠇', '⠏'])

@app.route('/api/status', methods=['GET'])
def get_status():
    t = round(random.uniform(30.0, 35.0), 2)
    h = round(random.uniform(50.0, 60.0), 2)
    
    if motor_status == "ON":
        curr = round(random.uniform(5.0, 15.0), 2)
        
        # จังหวะกระแสกระชาก
        if random.randint(1, 100) > 90:
            curr = round(random.uniform(25.0, 30.0), 2)
            # เด้งบรรทัดใหม่โชว์ว่าพัง จะได้เห็นชัดๆ
            sys.stdout.write(f"\n💥 [Mock] จำลองจังหวะกระแสกระชาก! พุ่งไปที่ {curr}A\n")
    else:
        curr = 0.0

    # ทำอนิเมชั่นสถานะบรรทัดเดียว
    spin = next(spinner)
    color = "\033[92m" if motor_status == "ON" else "\033[90m" # สีเขียวถ้า ON, สีเทาถ้า OFF
    reset = "\033[0m"
    
    # พิมพ์ทับบรรทัดเดิมตลอดเวลาที่มีคนมาดึง API
    sys.stdout.write(f"\r{spin} [Mock] Relay: {color}{motor_status}{reset} | Load: {curr:05.2f}A | Temp: {t}°C | Humi: {h}%  ")
    sys.stdout.flush()

    return jsonify({
        "current": curr,
        "temp": t,
        "humi": h,
        "relay": motor_status,
        "mode": "auto_mock",
        "ip": "127.0.0.1"
    })

@app.route('/api/on', methods=['GET'])
def turn_on():
    global motor_status
    motor_status = "ON"
    sys.stdout.write(f"\n🟢 [Mock] ได้รับคำสั่ง: ON\n")
    return jsonify({"status": "ON", "relay": "ON"})

@app.route('/api/off', methods=['GET'])
def turn_off():
    global motor_status
    motor_status = "OFF"
    sys.stdout.write(f"\n🔴 [Mock] ได้รับคำสั่ง: OFF\n")
    return jsonify({"status": "OFF", "relay": "OFF"})

if __name__ == '__main__':
    print("เริ่มจำลอง Wemos D1 R1 ที่พอร์ต 8080... (ซ่อน Log รกๆ แล้ว)")
    # หา IP เครื่องตัวเองในวง LAN
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    
    print(f"Mock Server กำลังทำงานที่:")
    print(f"Local:   http://localhost:8080")
    print(f"Network: http://{local_ip}:8080")
    print("---------------------------------------")
    
    app.run(host='0.0.0.0', port=8080)
