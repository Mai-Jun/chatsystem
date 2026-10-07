#include "media/wav_recorder.hpp"

#include <QAudioDevice>
#include <QAudioSource>
#include <QDateTime>
#include <QIODevice>
#include <QMediaDevices>

namespace im {

namespace {

void put_u16(QByteArray& b, quint16 v) {
  b.append(static_cast<char>(v & 0xff));
  b.append(static_cast<char>((v >> 8) & 0xff));
}

void put_u32(QByteArray& b, quint32 v) {
  b.append(static_cast<char>(v & 0xff));
  b.append(static_cast<char>((v >> 8) & 0xff));
  b.append(static_cast<char>((v >> 16) & 0xff));
  b.append(static_cast<char>((v >> 24) & 0xff));
}

// 标准 44 字节 PCM WAV 头 + 数据
QByteArray wrap_wav(const QByteArray& pcm, const QAudioFormat& fmt) {
  const quint16 channels = static_cast<quint16>(fmt.channelCount());
  const quint32 rate = static_cast<quint32>(fmt.sampleRate());
  const quint16 bits = static_cast<quint16>(fmt.bytesPerSample() * 8);
  const quint16 block_align = static_cast<quint16>(channels * bits / 8);
  const quint32 byte_rate = rate * block_align;

  QByteArray out;
  out.reserve(44 + pcm.size());
  out.append("RIFF", 4);
  put_u32(out, static_cast<quint32>(36 + pcm.size()));
  out.append("WAVE", 4);
  out.append("fmt ", 4);
  put_u32(out, 16);          // fmt 块长度
  put_u16(out, 1);           // PCM
  put_u16(out, channels);
  put_u32(out, rate);
  put_u32(out, byte_rate);
  put_u16(out, block_align);
  put_u16(out, bits);
  out.append("data", 4);
  put_u32(out, static_cast<quint32>(pcm.size()));
  out.append(pcm);
  return out;
}

}  // namespace

WavRecorder::WavRecorder(QObject* parent) : QObject(parent) {}

WavRecorder::~WavRecorder() { stop(); }

bool WavRecorder::available() { return !QMediaDevices::defaultAudioInput().isNull(); }

bool WavRecorder::start(QString* errmsg) {
  if (recording_) return true;

  const QAudioDevice device = QMediaDevices::defaultAudioInput();
  if (device.isNull()) {
    if (errmsg) *errmsg = QStringLiteral("未检测到麦克风设备");
    return false;
  }

  fmt_ = QAudioFormat();
  fmt_.setSampleRate(16000);
  fmt_.setChannelCount(1);
  fmt_.setSampleFormat(QAudioFormat::Int16);
  if (!device.isFormatSupported(fmt_)) fmt_ = device.preferredFormat();

  if (source_ != nullptr) {  // 上一轮残留
    source_->deleteLater();
    source_ = nullptr;
  }
  source_ = new QAudioSource(device, fmt_, this);
  pcm_.clear();
  io_ = source_->start();  // 拉模式：从 io_ 读取 PCM
  if (io_ == nullptr) {
    if (errmsg) *errmsg = QStringLiteral("麦克风打开失败");
    source_->deleteLater();
    source_ = nullptr;
    return false;
  }
  connect(io_, &QIODevice::readyRead, this, [this]() {
    if (io_ != nullptr) pcm_.append(io_->readAll());
  });

  started_ms_ = QDateTime::currentMSecsSinceEpoch();
  recording_ = true;
  return true;
}

QByteArray WavRecorder::stop(int* duration_ms) {
  if (!recording_) return QByteArray();
  recording_ = false;

  if (source_ != nullptr) {
    source_->stop();
    if (io_ != nullptr) {
      pcm_.append(io_->readAll());  // 收尾：stop 后仍有缓冲数据
      io_ = nullptr;
    }
    source_->deleteLater();
    source_ = nullptr;
  }
  if (duration_ms) {
    *duration_ms = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - started_ms_);
  }
  return wrap_wav(pcm_, fmt_);
}

}  // namespace im
