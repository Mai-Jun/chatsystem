# 客户端界面截图（本地+公网自检留档）

> 采集方式：`mock_gateway` 本地 Mock 网关（离线）与真实服务器两种环境 +
> `scripts/win_ui_shot.ps1` 截图 + `scripts/win_ui_tool.ps1` 模拟点击输入。
> 最新轮次：2026-10-13 微信风格 UI 优化 + 登录页微信化 + 公网端到端验收。

## 公网端到端验收（2026-10-13，真实服务器 47.112.192.119）

| 截图 | 内容 | 说明 |
|---|---|---|
| `40_login_real.png` | 登录页（真实环境） | 微信式登录页连接线上网关（47.112.192.119） |
| `41_main_real.png` | 主窗（真实数据） | 真实会话「端B」+ 末条消息预览「你好」+ 微信式时间「周三」+ 推送已连接；端B 的真实上传头像正确显示 |
| `42_chat_real.png` | 聊天窗（真实历史） | 50 条真实历史（含图片/语音消息）+ 时间戳按 5 分钟间隔显示 + 真实头像 |
| `43_chat_real_sent.png` | 公网发送 | 「公网验收通过-微信风格UI」发送成功，MySQL 已落库（298 条消息） |

## 登录页微信化（本地 Mock，2026-10-13）

| 截图 | 内容 | 说明 |
|---|---|---|
| `34_login_wx.png` | 账号密码标签 | 绿色品牌 Logo（圆角方块+气泡）+ 等分标签头（绿下划线）+ 灰底圆角输入框 + 绿色主按钮 |
| `35_login_sms.png` | 验证码登录标签 | 手机号预填 + 行内「获取验证码」 |
| `36_login_reg.png` | 注册标签 | 四项表单 + 「注册并登录」 |

## 微信风格主窗/聊天（本地 Mock，2026-10-13）

| 截图 | 内容 | 说明 |
|---|---|---|
| `32_final_main.png` | 主窗（微信式） | 深色侧边栏（头像/聊天/通讯录+红点/退出）+ 搜索框 "+" 菜单 + 会话行（头像/昵称/预览/时间/红色角标）|
| `22_unread_badge.png` | 未读角标 | WS 推送到达且未聚焦 → 红色「1」+ 预览即时更新 + 会话置顶 |
| `25_contacts2.png` | 通讯录页 | 「新的朋友(1)」入口 + 红点 + 好友行（头像/昵称/签名两行）|
| `26_search_filter.png` | 搜索过滤 | 输入「阿杰」→ 列表即时过滤 |
| `29_chat_sent.png` / `31_unread_final.png` | 发送/未读复验 | 绿气泡上屏；推送后角标+置顶+预览 |

## 采集方法备忘

- 截图：`powershell -File scripts/win_ui_shot.ps1 -Out <png> -Title <regex>`（窗口置顶 + 定点拷贝）
- 交互：`powershell -File scripts/win_ui_tool.ps1 -Title <regex> -Actions "click:x:y;dblclick:x:y;type:文本;key:{ENTER};wait:ms"`
- 窗口枚举（含不可见）：`powershell -File scripts/win_enum.ps1`
- **坑**：脚本内 Do-DblClick 两次按下间隔必须 <500ms（Windows 双击阈值），此前两次 click
  间隔 ~450ms 在边界上导致双击时灵时不灵（已修，见 win_ui_tool.ps1 注释）
- **坑**：QSettings 在 Windows 走注册表，键 `server/host` 实际是子键 `server` 下的值 `host`
  （不能写成字面值名 `server\host`）
- **坑**：`win_ui_tool.ps1` 的窗口-相对坐标受 DPI 缩放影响（125% 缩放下截图物理像素与
  逻辑坐标比值 1.25），点击坐标按「截图实测像素」直接使用即可（脚本已 DPI-aware）
