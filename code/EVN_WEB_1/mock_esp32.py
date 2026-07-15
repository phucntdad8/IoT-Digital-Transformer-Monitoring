import paho.mqtt.client as mqtt
import time
import json
import random

# --- CẤU HÌNH KẾT NỐI THINGSBOARD ---
THINGSBOARD_HOST = 'thingsboard.cloud'
# BẠN HÃY XÓA DÒNG CHỮ TRONG NGOẶC KÉP VÀ DÁN ACCESS TOKEN CỦA BẠN VÀO ĐÂY:
ACCESS_TOKEN = 'STN07LefRPt2LpU1lbdM' 


# Ngưỡng cài đặt chuyên ngành điện
U_NOMINAL = 220.0  # Điện áp định mức
U_LOW = 198.0      # Sụt áp (90% U định mức)
I_LIMIT = 150.0    # Giả sử dòng định mức của TBA là 150A
UNBALANCE_THRESHOLD = 0.15 # Ngưỡng lệch pha 15%

def analyze_power_quality(ua, ub, uc, ia, ib, ic):
    reasons = []
    
    # 1. Phát hiện Sụt áp
    if any(u < U_LOW for u in [ua, ub, uc]):
        reasons.append("Sụt áp")
    
    # 2. Phát hiện Quá tải
    if any(i > I_LIMIT for i in [ia, ib, ic]):
        reasons.append("Quá tải")
            
    return ", ".join(reasons) if reasons else "Bình thường"

client = mqtt.Client()
client.username_pw_set(ACCESS_TOKEN)
client.connect(THINGSBOARD_HOST, 1883, 60)
client.loop_start()

print("🚀 Hệ thống giám sát TBA đang chạy (Đã loại bỏ đo nhiệt độ)...")

try:
    while True:
        # Giả lập dữ liệu có biến động để tạo ra lỗi ngẫu nhiên
        ua, ub, uc = [round(random.uniform(195.0, 230.0), 1) for _ in range(3)]
        ia, ib, ic = [round(random.uniform(20.0, 180.0), 1) for _ in range(3)]
        
        # Phân tích trạng thái
        status_msg = analyze_power_quality(ua, ub, uc, ia, ib, ic)
        
        # Tính công suất tổng (P = U*I*0.95)
        p_total = round(((ua*ia + ub*ib + uc*ic) * 0.95) / 1000, 2)
        
        telemetry = {
            "uA": ua, "uB": ub, "uC": uc,
            "iA": ia, "iB": ib, "iC": ic,
            "pTotal": p_total,
            "status": status_msg
        }
        
        client.publish('v1/devices/me/telemetry', json.dumps(telemetry), 1)
        
        color = "🟢" if status_msg == "Bình thường" else "🔴"
        print(f"{color} [{status_msg}] U_avg: {round((ua+ub+uc)/3,1)}V | P_total: {p_total}W")
        
        time.sleep(5)

except KeyboardInterrupt:
    client.disconnect()