#include <common/net/message/nodes/RemoveNodeRespMsg.h>
#include <common/toolkit/MessagingTk.h>
#include <program/Program.h>
#include "RemoveNodeMsgEx.h"


bool RemoveNodeMsgEx::processIncoming(ResponseContext& ctx)
{
   App* app = Program::getApp();

   LOG_DBG(GENERAL, DEBUG, "Received a RemoveNodeMsg.", ctx.peerName());

   LOG_DBG(GENERAL, SPAM, "Removing node.", getNodeNumID());

   if (getNodeType() == NODETYPE_Meta || getNodeType() == NODETYPE_Storage)
   {
      NodeStoreServersEx* nodes = app->getServerStoreFromType(getNodeType());
      auto node = nodes->referenceNode(getNodeNumID());
      bool delRes = nodes->deleteNode(getNodeNumID());

      // log
      if (delRes)
      {
         LOG(GENERAL, WARNING, "Node removed.", as("node", node->getNodeIDWithTypeStr()));
         LOG(GENERAL, WARNING, "Number of nodes in the system:",
               as("meta", app->getMetaNodes()->getSize()),
               as("storage", app->getStorageNodes()->getSize()));
      }
   }

   if (!acknowledge(ctx))
      ctx.sendResponse(RemoveNodeRespMsg(0));

   return true;
}
