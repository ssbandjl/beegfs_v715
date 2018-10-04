#include <common/toolkit/serialization/Serialization.h>
#include <program/Program.h>
#include "NodeStoreClientsEx.h"

#include <mutex>

#define NODESTOREEX_TMPFILE_EXT  ".tmp" /* temporary extension for saved files until we rename */

/* maximum ID for client nodes. 0 is reserved. */
#define NODESTORECLIENTS_MAX_ID (UINT_MAX)

/**
 * Generate a new numeric node ID and return it.
 * This method will also first check whether this node already has a numeric ID assigned and just
 * didn't know of it yet (e.g. because of thread races.)
 *
 * Note: Caller must hold lock.
 * Note: Caller is expected to add the node to the activeNodes map and assign the numeric ID
 *       (because new IDs are picked based on assigned IDs in the activeNodes map).
 *
 * @return 0 if all available numIDs are currently assigned, so none are left
 */
NumNodeID NodeStoreClientsEx::generateID(Node& node) const
{
   // check whether this node's stringID is already associated with an active or deleted numID
   NumNodeID previousNumID = retrieveNumIDFromStringID(node.getID());
   if (previousNumID)
      return previousNumID;

   if (activeNodes.empty())
      return NumNodeID(1);

   if (activeNodes.rbegin()->first.val() < NODESTORECLIENTS_MAX_ID)
      return NumNodeID(activeNodes.rbegin()->first.val() + 1);

   if (activeNodes.begin()->first.val() > 1)
      return NumNodeID(1);

   const auto gap = std::adjacent_find(
         activeNodes.begin(), activeNodes.end(),
         [] (const auto& a, const auto& b) {
            return a.first.val() + 1 < b.first.val();
         });

   if (gap != activeNodes.end())
      return NumNodeID(gap->first.val() + 1);

   // still no new ID => all IDs used!
   LOG(GENERAL, ERR, "Ran out of client node IDs!");
   return {};
}

/**
 * Search activeNodes for a node with the given string ID and return it's associated numeric ID.
 *
 * Note: Caller must hold lock.
 *
 * @return 0 if no node with the given stringID was found, associated numeric ID otherwise.
 */
NumNodeID NodeStoreClientsEx::retrieveNumIDFromStringID(const std::string& nodeID) const
{
   for (const auto& entry : activeNodes)
      if (entry.second->getID() == nodeID)
         return entry.first;

   return NumNodeID();
}

/**
 * Note: setStorePath must be called before using this.
 */
bool NodeStoreClientsEx::loadFromFile(bool v6Format)
{
   /* note: we use a separate storeMutex here because we don't want to keep the standard mutex
      locked during disk access. */

   LogContext log("NodeStoreClientsEx (load)");
   
   bool retVal = false;
   boost::scoped_array<char> buf;
   int readRes;

   if(!this->storePath.length() )
      return false;

   const std::lock_guard<Mutex> lock(storeMutex);

   int fd = open(storePath.c_str(), O_RDONLY, 0);
   if(fd == -1)
   { // open failed
      LOG_DEBUG_CONTEXT(log, 4, "Unable to open nodes file: " + storePath + ". " +
         "SysErr: " + System::getErrString() );

      return false;
   }

   struct stat stat;
   if (fstat(fd, &stat) != 0)
   {
      LOG(GENERAL, ERR, "Could not stat nodes file", storePath, sysErr);
      goto err_close;
   }

   buf.reset(new char[stat.st_size]);
   readRes = read(fd, buf.get(), stat.st_size);
   if(readRes <= 0)
   { // reading failed
      LOG_DEBUG_CONTEXT(log, 4, "Unable to read nodes file: " + storePath + ". " +
         "SysErr: " + System::getErrString() );
   }
   else
   { // parse contents
      retVal = loadFromBuf(buf.get(), readRes, v6Format);
   }

err_close:
   close(fd);

   return retVal;
}


/**
 * Note: setStorePath must be called before using this.
 */
bool NodeStoreClientsEx::saveToFile()
{
   /* note: we use a separate storeMutex here because we don't want to keep the standard mutex
      locked during disk access. */

   LogContext log("NodeStoreClientsEx (save)");
   
   bool retVal = false;

   boost::scoped_array<char> buf;
   ssize_t bufLen;
   ssize_t writeRes;
   int renameRes;

   if(!this->storePath.length() )
      return false;

   std::string storePathTmp(storePath + NODESTOREEX_TMPFILE_EXT);

   const std::lock_guard<Mutex> lock(storeMutex);

   // create/trunc file
   int openFlags = O_CREAT|O_TRUNC|O_WRONLY;

   int fd = open(storePathTmp.c_str(), openFlags, 0666);
   if(fd == -1)
   { // error
      log.logErr("Unable to create nodes file: " + storePathTmp + ". " +
         "SysErr: " + System::getErrString() );

      return false;
   }

   // file created => store data
   bufLen = saveToBuf(buf);
   if (bufLen < 0)
   {
      log.logErr("Unable to serialize nodes file: " + storePathTmp + ".");
      goto err_closefile;
   }

   writeRes = write(fd, buf.get(), bufLen);
   if(writeRes != (ssize_t)bufLen)
   {
      log.logErr("Unable to store nodes file: " + storePathTmp + ". " +
         "SysErr: " + System::getErrString() );

      goto err_closefile;
   }

   close(fd);
   
   renameRes = rename(storePathTmp.c_str(), storePath.c_str() );
   if(renameRes == -1)
   {
      log.logErr("Unable to rename nodes file: " + storePathTmp + ". " +
         "SysErr: " + System::getErrString() );

      goto err_unlink;
   }

   retVal = true;

   LOG_DEBUG_CONTEXT(log, Log_DEBUG, "Nodes file stored: " + storePath);

   return retVal;

   // error compensation
err_closefile:
   close(fd);

err_unlink:
   unlink(storePathTmp.c_str() );

   return retVal;
}

/**
 * Note: Does not require locking (uses the locked addOrUpdate() method).
 */
bool NodeStoreClientsEx::loadFromBuf(const char* buf, unsigned bufLen, bool v6Format)
{
   LogContext log("NodeStoreClientsEx (load)");

   Deserializer des(buf, bufLen);

   if (!v6Format) {
      uint32_t version;

      des % version;
      if (!des.good()) {
         LOG(GENERAL, ERR, "Unable to deserialize version from buffer.");
         return false;
      }

      if (version > BEEGFS_DATA_VERSION) {
         LOG(GENERAL, ERR, "Failed to load node store: version mismatch.");
         return false;
      }
   }

   // nodeList
   std::vector<NodeHandle> nodeList;

   if (v6Format)
      des % Node::inV6Format(nodeList);
   else
      des % nodeList;

   if (!des.good())
   { // preprocessing failed
      log.logErr("Unable to deserialize node from buffer (preprocessing error)");
      return false;
   }

   // add all nodes
   for (auto iter = nodeList.begin(); iter != nodeList.end(); iter++)
   {
      auto& node = *iter;
      std::string nodeID = node->getID();

      // set local nic capabilities...

      Node& localNode = Program::getApp()->getLocalNode(); /* (note: not the one from this store) */
      NicAddressList localNicList(localNode.getNicList() );
      NicListCapabilities localNicCaps;

      NetworkInterfaceCard::supportedCapabilities(&localNicList, &localNicCaps);
      node->getConnPool()->setLocalNicCaps(&localNicCaps);

      // actually add the node to the store...

      /* note: using NodeStoreClientsEx::addOrUpdate() here would deadlock, because it would try to
         acquire the storeMutex. */

      bool nodeAdded = NodeStoreClients::addOrUpdateNode(std::move(node));

      LOG_DEBUG_CONTEXT(log, Log_DEBUG, "Added new node from buffer: " + nodeID);
      IGNORE_UNUSED_VARIABLE(nodeAdded);
      IGNORE_UNUSED_VARIABLE(&nodeID);
   }

   return true;
}

/**
 * Note: The NodeStoreClients mutex may not be acquired when this is called (because this uses the
 * locked referenceAllNodes() method).
 */
ssize_t NodeStoreClientsEx::saveToBuf(boost::scoped_array<char>& buf)
{
   auto data = std::make_pair(uint32_t(BEEGFS_DATA_VERSION), referenceAllNodes());
   return serializeIntoNewBuffer(data, buf);
}
