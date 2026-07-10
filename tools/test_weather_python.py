#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import time
import requests
import base64

try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.backends import default_backend
    HAS_CRYPTOGRAPHY = True
except ImportError:
    HAS_CRYPTOGRAPHY = False
    print("⚠️ 未安装 cryptography 库")

QWEATHER_HOST = "https://k56r72f3p6.re.qweatherapi.com"
LOCATION = "101020600"  # 上海
JWT_KID = "TB5C92CJVT"
JWT_SUB = "2BKUHC8B3P"

PRIVATE_KEY_PEM = """-----BEGIN PRIVATE KEY-----
MC4CAQAwBQYDK2VwBCIEIIEQycEYXcDq11wuEZg17bfynf6OLTyk6VbGuOfIhnQZ
-----END PRIVATE KEY-----"""

def base64_url_encode(input_bytes):
    encoded = base64.b64encode(input_bytes).decode('utf-8')
    encoded = encoded.replace('+', '-').replace('/', '_').replace('=', '')
    return encoded

def generate_jwt():
    header = '{"alg":"EdDSA","kid":"%s"}' % JWT_KID
    now = int(time.time())
    payload = '{"sub":"%s","iat":%d,"exp":%d}' % (JWT_SUB, now, now + 900)
    
    header_encoded = base64_url_encode(header.encode('utf-8'))
    payload_encoded = base64_url_encode(payload.encode('utf-8'))
    signing_input = header_encoded + "." + payload_encoded
    
    if HAS_CRYPTOGRAPHY:
        try:
            private_key = serialization.load_pem_private_key(
                PRIVATE_KEY_PEM.encode('utf-8'), None, default_backend())
            signature = private_key.sign(signing_input.encode('utf-8'))
            signature_encoded = base64_url_encode(signature)
            print("✅ 使用真实 Ed25519 签名")
        except Exception as e:
            print(f"❌ 签名失败: {e}")
            signature_encoded = "placeholder"
    else:
        signature_encoded = "placeholder"
    
    return signing_input + "." + signature_encoded

def get_headers():
    return {"Authorization": f"Bearer {generate_jwt()}"}

def get_current_weather():
    """获取当前天气"""
    print("\n" + "="*50)
    print("🌤️  获取当前天气 (/v7/weather/now)")
    print("="*50)
    
    url = f"{QWEATHER_HOST}/v7/weather/now?location={LOCATION}&gzip=n"
    print(f"📍 请求URL: {url}")
    
    response = requests.get(url, headers=get_headers(), timeout=10)
    print(f"\n📊 HTTP状态码: {response.status_code}")
    
    if response.status_code == 200:
        data = response.json()
        print("\n原始响应:")
        print(response.text)
        
        if data.get("code") == "200":
            now = data["now"]
            print(f"\n✅ 当前天气:")
            print(f"   天气: {now['text']}")
            print(f"   温度: {now['temp']}°C")
            print(f"   湿度: {now['humidity']}%")
            print(f"   风向: {now['windDir']} {now['windScale']}级")
            return True
    else:
        print(f"❌ 失败: {response.text}")
    return False

def get_3day_forecast():
    """获取3天天气预报"""
    print("\n" + "="*50)
    print("📅 获取3天天气预报 (/v7/weather/3d)")
    print("="*50)
    
    url = f"{QWEATHER_HOST}/v7/weather/3d?location={LOCATION}&gzip=n"
    print(f"📍 请求URL: {url}")
    
    response = requests.get(url, headers=get_headers(), timeout=10)
    print(f"\n📊 HTTP状态码: {response.status_code}")
    
    if response.status_code == 200:
        data = response.json()
        print("\n原始响应:")
        print(response.text)
        
        if data.get("code") == "200":
            daily = data.get("daily", [])
            print(f"\n✅ 3天天气预报:")
            for day in daily:
                print(f"\n📅 {day['fxDate']}")
                print(f"   天气: {day['textDay']}")
                print(f"   温度: {day['tempMin']}°C ~ {day['tempMax']}°C")
                print(f"   湿度: {day['humidity']}%")
                print(f"   风向: {day['windDirDay']} {day['windScaleDay']}级")
            return True
    else:
        print(f"❌ 失败: {response.text}")
    return False

def get_city_info(location_id):
    """获取城市信息"""
    print("\n" + "="*50)
    print("🌆 获取城市信息 (/geo/v2/city/lookup)")
    print("="*50)
    
    url = f"{QWEATHER_HOST}/geo/v2/city/lookup?location={location_id}"
    print(f"📍 请求URL: {url}")
    
    response = requests.get(url, headers=get_headers(), timeout=10)
    print(f"\n📊 HTTP状态码: {response.status_code}")
    
    if response.status_code == 200:
        data = response.json()
        print("\n原始响应:")
        print(response.text)
        
        if data.get("code") == "200":
            locations = data.get("location", [])
            if locations:
                loc = locations[0]
                print(f"\n✅ 获取城市成功:")
                print(f"   城市名称: {loc.get('name', '未知')}")
                print(f"   省级行政区: {loc.get('adm1', '未知')}")
                print(f"   市级行政区: {loc.get('adm2', '未知')}")
                print(f"   国家: {loc.get('country', '未知')}")
                print(f"   纬度: {loc.get('lat', '未知')}")
                print(f"   经度: {loc.get('lon', '未知')}")
                return True
    else:
        print(f"❌ 失败: {response.text}")
    return False

if __name__ == "__main__":
    print("="*50)
    print("🌐 和风天气API测试工具")
    print("="*50)
    
    # 测试当前天气
    get_current_weather()
    
    # 测试3天预报
    get_3day_forecast()
    
    # 测试城市信息
    get_city_info(LOCATION)
    
    print("\n" + "="*50)
    print("🎉 所有测试完成!")
    print("="*50)