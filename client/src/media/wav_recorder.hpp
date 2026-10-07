#pragma once
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>

class QAudioSource;
class QIODevice;

namespace im {

// 麦克风录音（M9 语音消息）
// - 目标格式 16kHz / 单声道 / 16bit PCM（百度短语音识别的推荐输入）
//   设备不支持时回退到设备首选格式，由服务端 ASR 自行适配
// - 产出带 44 字节 RIFF 头的完整 WAV 字节流（服务端 try_asr 按 wav 解析）
// - 无输入设备（无头服务器/未接麦克风）时 start() 返回 false 并给出原因
class WavRecorder : public QObject {
  Q_OBJECT
 public:
  explicit WavRecorder(QObject* parent = nullptr);
  ~WavRecorder() override;

  static bool available();  // 是否存在可用输入设备

  bool start(QString* errmsg = nullptr);
  // 停止录音，返回 WAV 字节流（未在录音时返回空），duration_ms 为实际时长
  QByteArray stop(int* duration_ms = nullptr);
  bool recording() const { return recording_; }

 private:
  QAudioSource* source_ = nullptr;
  QIODevice* io_ = nullptr;  // 拉模式读取端，归 source_ 所有
  QAudioFormat fmt_;
  QByteArray pcm_;
  qint64 started_ms_ = 0;
  bool recording_ = false;
};

}  // namespace im
