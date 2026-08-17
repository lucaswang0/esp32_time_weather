import pygetwindow as gw

windows = gw.getAllWindows()

print(f"{'序号':<4} {'窗口标题':<40} {'位置/大小'}")
print("-" * 80)

for i, win in enumerate(windows, 1):
    title = win.title
    # 过滤掉标题为空的窗口
    if title:
        # 获取窗口位置和大小信息
        try:
            rect = f"({win.left}, {win.top}) {win.width}x{win.height}"
        except:
            rect = "信息获取失败"
        
        # 限制标题长度避免换行
        display_title = title[:37] + "..." if len(title) > 40 else title
        print(f"{i:<4} {display_title:<40} {rect}")

print("-" * 80)
print(f"共找到 {len(windows)} 个窗口")