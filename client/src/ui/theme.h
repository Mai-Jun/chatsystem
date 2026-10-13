#pragma once
#include <QtGlobal>

namespace im {

// 微信风格全局 QSS（M9 第三轮美化）
// 主色：微信绿 #07C160；链接蓝 #576B95；窗口底 #EDEDED；面板白 #FFFFFF；
// 气泡：自己 #95EC69（右），对方 #FFFFFF（左）；文字 #191919 / 次要 #9A9A9A
inline const char* kAppStyleSheet = R"(
* {
  font-family: "Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC", sans-serif;
  font-size: 13px;
  color: #191919;
}
QDialog, QMainWindow { background: #EDEDED; }

/* ---------- 按钮 ---------- */
QPushButton {
  background: #FFFFFF; border: 1px solid #D9D9D9; border-radius: 4px;
  padding: 6px 14px;
}
QPushButton:hover { background: #F2F2F2; }
QPushButton:pressed { background: #E5E5E5; }
QPushButton:disabled { background: #F5F5F5; color: #AAAAAA; border-color: #E5E5E5; }
QPushButton#primary {
  background: #07C160; border: none; color: #FFFFFF; padding: 9px 14px; font-size: 14px;
}
QPushButton#primary:hover { background: #06AD56; }
QPushButton#primary:pressed { background: #05984B; }
QPushButton#primary:disabled { background: #93DCB6; color: #EFFAF4; }
QPushButton#linkBtn { background: transparent; border: none; color: #576B95; padding: 2px 6px; }
QPushButton#linkBtn:hover { color: #40638A; text-decoration: underline; }
QPushButton#toolBtn { background: transparent; border: none; padding: 6px 12px; }
QPushButton#toolBtn:hover { background: #FFFFFF; border-radius: 4px; }

/* ---------- 输入框 ---------- */
QLineEdit {
  background: #F7F7F7; border: 1px solid #F7F7F7; border-radius: 6px; padding: 9px 12px;
  selection-background-color: #07C160; selection-color: #FFFFFF;
}
QLineEdit:hover { background: #F2F2F2; border-color: #F2F2F2; }
QLineEdit:focus { background: #FFFFFF; border: 1px solid #07C160; }
QLineEdit#addrEdit { border: none; background: transparent; color: #8A8A8A; padding: 2px; }
QLineEdit#addrEdit:hover, QLineEdit#addrEdit:focus { background: transparent; border: none; }

/* ---------- 登录窗（微信式：白底 + 绿色 Logo + 等分标签头） ---------- */
QDialog#loginDialog { background: #FFFFFF; }
QPushButton#loginTab {
  background: transparent; border: none; border-bottom: 2px solid transparent;
  border-radius: 0px; padding: 10px 0px; color: #8A8A8A; font-size: 14px;
}
QPushButton#loginTab:hover:!checked { color: #191919; }
QPushButton#loginTab:checked {
  color: #07C160; font-weight: 600; border-bottom: 2px solid #07C160;
}
QLabel#loginStatus { color: #9A9A9A; font-size: 12px; }

/* ---------- 列表 ---------- */
QListWidget { background: #FFFFFF; border: none; outline: 0; }
QListWidget::item { padding: 9px 8px; border-bottom: 1px solid #F2F2F2; }
QListWidget::item:hover { background: #F7F7F7; }
QListWidget::item:selected { background: #E5E5E5; color: #191919; }
QListWidget#msgList { background: #F5F5F5; }
QListWidget#msgList::item { padding: 0px; border-bottom: none; }
QListWidget#msgList::item:hover { background: transparent; }
QListWidget#msgList::item:selected { background: transparent; }

/* ---------- 标签页 ---------- */
QTabWidget::pane { border: 1px solid #E0E0E0; border-radius: 4px; background: #FFFFFF; top: -1px; }
QTabBar::tab { background: transparent; padding: 9px 20px; color: #7A7A7A; border: none; }
QTabBar::tab:selected { color: #07C160; border-bottom: 2px solid #07C160; }
QTabBar::tab:hover:!selected { color: #191919; }

/* ---------- 状态栏 ---------- */
QStatusBar { background: #F7F7F7; color: #9A9A9A; border-top: 1px solid #E5E5E5; }
QStatusBar QLabel { color: #9A9A9A; font-size: 12px; }

/* ---------- 标签 ---------- */
QLabel#title { font-size: 21px; font-weight: 600; color: #191919; }
QLabel#subtitle { color: #9A9A9A; font-size: 12px; }
QLabel#hint { color: #9A9A9A; font-size: 12px; }
QLabel#formLabel { color: #666666; }
QLabel#linkLabel { color: #576B95; font-size: 12px; }
QLabel#msgTime { color: #9A9A9A; font-size: 11px; }
QLabel#msgSender { color: #9A9A9A; font-size: 11px; }
QLabel#fileName { font-weight: 600; }
QLabel#fileSize { color: #9A9A9A; font-size: 11px; }

/* ---------- 聊天气泡 ---------- */
QLabel#bubbleSelf { background: #95EC69; border-radius: 6px; }
QLabel#bubblePeer { background: #FFFFFF; border-radius: 6px; }
QFrame#bubbleSelf { background: #95EC69; border-radius: 6px; }
QFrame#bubblePeer { background: #FFFFFF; border-radius: 6px; }

/* ---------- 卡片 ---------- */
QFrame#card { background: #FFFFFF; border-radius: 8px; border: 1px solid #E7E7E7; }
QWidget#inputPanel { background: #FFFFFF; }

/* ---------- 主窗（微信风格：深色侧边栏 + 白色列表面板） ---------- */
QWidget#sideBar { background: #2E2E2E; }
QPushButton#sideNav {
  background: transparent; border: none; border-radius: 4px;
  color: #9B9B9B; font-size: 11px; padding: 0px;
}
QPushButton#sideNav:hover { background: #3D3D3D; }
QPushButton#sideNav:checked { color: #07C160; }
QWidget#listPanel { background: #FFFFFF; }
QLineEdit#searchBox {
  background: #F5F5F5; border: none; border-radius: 6px; padding: 6px 10px;
}
QLineEdit#searchBox:focus { background: #EFEFEF; }
QPushButton#plusBtn {
  background: transparent; border: none; border-radius: 4px;
  color: #7A7A7A; font-size: 17px; padding: 0px;
}
QPushButton#plusBtn:hover { background: #EFEFEF; }
QPushButton#contactEntry {
  background: transparent; border: none; text-align: left;
  padding: 11px 10px; color: #191919;
}
QPushButton#contactEntry:hover { background: #F2F2F2; }

/* ---------- 会话/好友行（自绘行控件内的子标签） ---------- */
QLabel#nameLabel { font-size: 14px; color: #191919; }
QLabel#preview { color: #9A9A9A; font-size: 12px; }
QLabel#timeLabel { color: #B2B2B2; font-size: 11px; }
QLabel#badge {
  background: #FA5151; color: #FFFFFF; border-radius: 9px;
  font-size: 11px; padding: 0px 5px;
}
QLabel#redDot { background: #FA5151; border-radius: 4px; }
QListWidget#listView { background: #FFFFFF; }
QListWidget#listView::item { border-bottom: 1px solid #F2F2F2; padding: 0px; }
QListWidget#listView::item:hover { background: #F2F2F2; }
QListWidget#listView::item:selected { background: #E4E4E4; }
)";

}  // namespace im