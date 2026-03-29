import requests

from config import *

def relay_cmd(cmd):
    if cmd == "ON":
        url = BASE_URL + "/on"
    elif cmd == "OFF":
        url = BASE_URL + "/off"
    else:
        url = BASE_URL + "/status"

    try:
        r = requests.get(url, timeout=3)
        return r.json()
    except Exception as e:
        return {"error": str(e)}