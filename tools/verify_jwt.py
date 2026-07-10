#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import base64
import time
import hashlib
import struct

# 私钥 PEM
PRIVATE_KEY_PEM = """-----BEGIN PRIVATE KEY-----
MC4CAQAwBQYDK2VwBCIEIIEQycEYXcDq11wuEZg17bfynf6OLTyk6VbGuOfIhnQZ
-----END PRIVATE KEY-----"""

# 从 PEM 提取 Base64 内容
lines = PRIVATE_KEY_PEM.strip().split('\n')
base64_content = ''.join(lines[1:-1])
print(f"Base64 内容: {base64_content}")

# Base64 解码
decoded = base64.b64decode(base64_content)
print(f"解码后长度: {len(decoded)} 字节")
print(f"解码后十六进制: {decoded.hex()}")

# 私钥种子位于 offset 13-44
seed = decoded[13:45]
print(f"\n私钥种子 (32字节): {seed.hex()}")

# 使用 Python 的 cryptography 库生成正确签名
try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.backends import default_backend

    # 加载私钥
    private_key = serialization.load_pem_private_key(
        PRIVATE_KEY_PEM.encode('utf-8'), None, default_backend())

    # 获取公钥
    public_key = private_key.public_key()
    public_key_bytes = public_key.public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw
    )
    print(f"\n公钥 (32字节): {public_key_bytes.hex()}")

    # 生成 JWT
    header = '{"alg":"EdDSA","kid":"TB5C92CJVT"}'
    now = int(time.time())
    payload = '{"sub":"2BKUHC8B3P","iat":%d,"exp":%d}' % (now - 30, now + 900)

    def base64_url_encode(input_bytes):
        encoded = base64.b64encode(input_bytes).decode('utf-8')
        encoded = encoded.replace('+', '-').replace('/', '_').replace('=', '')
        return encoded

    header_encoded = base64_url_encode(header.encode('utf-8'))
    payload_encoded = base64_url_encode(payload.encode('utf-8'))
    signing_input = header_encoded + "." + payload_encoded

    print(f"\nHeader: {header}")
    print(f"Payload: {payload}")
    print(f"Signing Input: {signing_input}")

    # 签名
    signature = private_key.sign(signing_input.encode('utf-8'))
    signature_encoded = base64_url_encode(signature)

    jwt = signing_input + "." + signature_encoded
    print(f"\n完整 JWT:")
    print(jwt)
    print(f"\nJWT 长度: {len(jwt)} 字符")

    # 提取签名
    print(f"\n签名 (64字节): {signature.hex()}")

    # 生成 C 代码
    print("\n" + "="*60)
    print("C 代码:")
    print("="*60)
    print("\n// Ed25519 原始私钥种子")
    print("static const unsigned char ED25519_SEED[32] = {")
    for i in range(0, 32, 8):
        print("    " + ", ".join(f"0x{seed[j]:02x}" for j in range(i, min(i+8, 32))) + ",")
    print("};")

    print("\n// JWT Header")
    print(f'const char* JWT_HEADER = "{header_encoded}";')
    print("\n// JWT Payload")
    print(f'const char* JWT_PAYLOAD = "{payload_encoded}";')
    print("\n// JWT Signature (Base64URL)")
    print(f'const char* JWT_SIGNATURE = "{signature_encoded}";')
    print("\n// 完整 JWT")
    print(f'const char* JWT_TOKEN = "{jwt}";')

except ImportError:
    print("\n需要安装 cryptography 库:")
    print("pip install cryptography")