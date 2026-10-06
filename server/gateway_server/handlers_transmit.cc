#include "dispatch.hpp"
#include "message_transmit.pb.h"

// 消息转发子服务分发（发消息链路：网关 → message_server → 存储 + MQ 广播）
namespace im {
namespace gateway {

bool dispatch_transmit(const Ctx& ctx, Result* out) {
  switch (ctx.type) {
    case REQ_TYPE_TRANSMIT_MESSAGE:
      forward<MsgTransmitReq, MsgTransmitResp, MsgTransmitService_Stub>(
          ctx, &MsgTransmitService_Stub::GetTransmitTarget, out);
      return true;
    default:
      return false;
  }
}

}  // namespace gateway
}  // namespace im
