# 客户端界面截图（本地自检留档）

> 采集方式：`mock_gateway` 本地 Mock 网关（离线数据）+ `scripts/win_ui_shot.ps1` 截图
> + `scripts/win_ui_tool.ps1` 模拟点击输入。用途：无服务器环境下验证界面/交互并纵向对比改动。
> 最新轮次：2026-10-13 微信风格 UI 优化（侧边栏 + 列表面板 + 会话预览 + 红色角标）。

## 微信风格轮（2026-10-13，当前）

| 截图 | 内容 | 说明 |
|---|---|---|
| `34_login_wx.png` | 登录页（微信式） | 绿色品牌 Logo（圆角方块+气泡）+ 等分标签头（账号密码/验证码登录/注册，绿下划线）+ 灰底圆角输入框 + 绿色主按钮 |
| `35_login_sms.png` | 验证码登录标签 | 手机号预填 + 验证码行内「获取验证码」 |
| `36_login_reg.png` | 注册标签 | 四项表单 + 「注册并登录」 |
| `32_final_main.png` | 主窗（微信式） | 深色侧边栏（头像/聊天/通讯录+红点/退出）+ 搜索框 "+" 菜单 + 会话行（头像/昵称/预览/时间/红色角标）|
| `22_unread_badge.png` | 未读角标 | WS 推送到达且未聚焦 → 红色「1」+ 预览即时更新 + 会话置顶 |
| `25_contacts2.png` | 通讯录页 | 「新的朋友(1)」入口 + 红点 + 好友行（头像/昵称/签名两行）|
| `26_search_filter.png` | 搜索过滤 | 输入「阿杰」→ 列表即时过滤 |
| `28_chat_open.png` | 聊天窗 | 圆角方头像（微信式）、时间戳仅间隔 >5 分钟显示 |
| `29_chat_sent.png` | 发送消息 | 回车发送上屏（绿气泡）|
| `30_main_synced.png` | 预览同步 | 发出消息后主窗预览/时间/排序即时同步 |
| `31_unread_final.png` | 未读+排序复验 | 推送后会话置顶 + 角标 + 预览「刚到家，明天见～」|

## 历史轮（2026-10-12：时间戳去噪优化，对比用）

| 截图 | 内容 | 说明 |
|---|---|---|
| `04_login_mock_ok.png` | 登录页（旧主题） | QSettings 读取验证 |
| `08_chat.png` → `09_chat_timeslim.png` | 聊天窗优化前→后 | **每行时间戳 → 间隔 5 分钟才显示**（本次优化的动机对比）|

## 采集方法备忘

- 截图：`powershell -File scripts/win_ui_shot.ps1 -Out <png> -Title <regex>`（窗口置顶 + 定点拷贝）
- 交互：`powershell -File scripts/win_ui_tool.ps1 -Title <regex> -Actions "click:x:y;dblclick:x:y;type:文本;key:{ENTER};wait:ms"`
- 窗口枚举（含不可见）：`powershell -File scripts/win_enum.ps1`
- **坑**：脚本内 Do-DblClick 两次按下间隔必须 <500ms（Windows 双击阈值），此前两次 click
  间隔 ~450ms 在边界上导致双击时灵时不灵（已修，见 win_ui_tool.ps1 注释）
- **坑**：QSettings 在 Windows 走注册表，键 `server/host` 实际是子键 `server` 下的值 `host`
  （不能写成字面值名 `server\host`）
