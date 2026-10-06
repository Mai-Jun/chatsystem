#include "dispatch.hpp"
#include "file.pb.h"
#include "message_storage.pb.h"
#include "speech.pb.h"

// 消息存储 + 文件 + 语音分发（三个 pb.h 均较小，合并为一个 TU）
namespace im {
namespace gateway {

static bool dispatch_msg_impl(const Ctx& ctx, Result* out) {
  switch (ctx.type) {
    case REQ_TYPE_GET_HISTORY:
      forward<GetHistoryReq, GetHistoryResp, MsgStorageService_Stub>(
          ctx, &MsgStorageService_Stub::GetHistoryMessage, out);
      return true;
    case REQ_TYPE_SEARCH_HISTORY:
      forward<SearchHistoryReq, SearchHistoryResp, MsgStorageService_Stub>(
          ctx, &MsgStorageService_Stub::SearchHistoryMessage, out);
      return true;
    default:
      return false;
  }
}

bool dispatch_msg(const Ctx& ctx, Result* out) { return dispatch_msg_impl(ctx, out); }

bool dispatch_file(const Ctx& ctx, Result* out) {
  switch (ctx.type) {
    case REQ_TYPE_PUT_SINGLE_FILE:
      forward<PutSingleReq, PutSingleResp, FileService_Stub>(
          ctx, &FileService_Stub::PutSingle, out);
      return true;
    case REQ_TYPE_PUT_BATCH_FILE:
      forward<PutBatchReq, PutBatchResp, FileService_Stub>(
          ctx, &FileService_Stub::PutBatch, out);
      return true;
    case REQ_TYPE_GET_SINGLE_FILE:
      forward<GetSingleReq, GetSingleResp, FileService_Stub>(
          ctx, &FileService_Stub::GetSingle, out);
      return true;
    case REQ_TYPE_GET_BATCH_FILE:
      forward<GetBatchReq, GetBatchResp, FileService_Stub>(
          ctx, &FileService_Stub::GetBatch, out);
      return true;
    default:
      return false;
  }
}

bool dispatch_speech(const Ctx& ctx, Result* out) {
  switch (ctx.type) {
    case REQ_TYPE_SPEECH_RECOGNITION:
      forward<SpeechRecognitionReq, SpeechRecognitionResp, SpeechService_Stub>(
          ctx, &SpeechService_Stub::SpeechRecognition, out);
      return true;
    default:
      return false;
  }
}

}  // namespace gateway
}  // namespace im
