#include "dispatch.hpp"
#include "friend.pb.h"

// 好友子服务分发（本 TU 仅含 friend.pb.h + dispatch.hpp）
namespace im {
namespace gateway {

bool dispatch_friend(const Ctx& ctx, Result* out) {
  switch (ctx.type) {
    case REQ_TYPE_SEARCH_USER:
      forward<SearchUserReq, SearchUserResp, FriendService_Stub>(
          ctx, &FriendService_Stub::SearchUser, out);
      return true;
    case REQ_TYPE_SEND_FRIEND_APPLY:
      forward<SendFriendApplyReq, SendFriendApplyResp, FriendService_Stub>(
          ctx, &FriendService_Stub::SendFriendApply, out);
      return true;
    case REQ_TYPE_PROCESS_FRIEND_APPLY:
      forward<ProcessFriendApplyReq, ProcessFriendApplyResp, FriendService_Stub>(
          ctx, &FriendService_Stub::ProcessFriendApply, out);
      return true;
    case REQ_TYPE_DEL_FRIEND:
      forward<DelFriendReq, DelFriendResp, FriendService_Stub>(
          ctx, &FriendService_Stub::DelFriend, out);
      return true;
    case REQ_TYPE_GET_FRIEND_LIST:
      forward<GetFriendListReq, GetFriendListResp, FriendService_Stub>(
          ctx, &FriendService_Stub::GetFriendList, out);
      return true;
    case REQ_TYPE_GET_PENDING_EVENTS:
      forward<GetPendingEventsReq, GetPendingEventsResp, FriendService_Stub>(
          ctx, &FriendService_Stub::GetPendingEvents, out);
      return true;
    case REQ_TYPE_CREATE_GROUP_SESSION:
      forward<CreateGroupSessionReq, CreateGroupSessionResp, FriendService_Stub>(
          ctx, &FriendService_Stub::CreateGroupSession, out);
      return true;
    case REQ_TYPE_GET_SESSION_MEMBER:
      forward<GetSessionMemberReq, GetSessionMemberResp, FriendService_Stub>(
          ctx, &FriendService_Stub::GetSessionMember, out);
      return true;
    case REQ_TYPE_GET_SESSION_LIST:
      forward<GetChatSessionListReq, GetChatSessionListResp, FriendService_Stub>(
          ctx, &FriendService_Stub::GetChatSessionList, out);
      return true;
    default:
      return false;
  }
}

}  // namespace gateway
}  // namespace im
