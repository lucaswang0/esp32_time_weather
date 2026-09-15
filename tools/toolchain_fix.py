import os

# RISC-V 工具链的 riscv32-esp-elf/bin 目录缺少 as.exe/ld.exe，
# GCC 调用 as/ld 时会从 PATH 中找到 MinGW 的错误版本，
# 导致 "invalid -march= option: rv32imc" 错误。
# 此脚本将包含正确 as.exe/ld.exe 的目录注入 PATH 最前面。

Import("env")

fix_dir = os.path.join(env.subst("$PROJECT_DIR"), "tools", "toolchain_fix", "bin")
if os.path.isdir(fix_dir):
    env.PrependENVPath("PATH", fix_dir)
    print("[toolchain_fix] Injected PATH: %s" % fix_dir)
