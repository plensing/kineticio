//------------------------------------------------------------------------------
//! @file ClusterMap.hh
//! @author Paul Hermann Lensing
//! @brief Providing access to cluster instances and the data io cache.
//------------------------------------------------------------------------------
#ifndef KINETICIO_CLUSTERMAP_HH
#define KINETICIO_CLUSTERMAP_HH

/*----------------------------------------------------------------------------*/
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <mutex>
#include <json-c/json.h>
#include "ClusterInterface.hh"
#include "RedundancyProvider.hh"
#include "SocketListener.hh"
#include "DataCache.hh"
#include "KineticAdminCluster.hh"
#include "kio/KineticIoFactory.hh"
/*----------------------------------------------------------------------------*/

namespace kio {
  
//--------------------------------------------------------------------------
//! All information required to create a cluster object 
//--------------------------------------------------------------------------
struct ClusterInformation
{
  //! the number of data blocks in a stripe
  size_t numData;
  //! the number of parity blocks in a stripe
  size_t numParity;
  //! the size of a single data / parity block in bytes
  size_t blockSize;
  //! minimum interval between reconnection attempts to a drive (rate limit)
  std::chrono::seconds min_reconnect_interval;
  //! interval after which an operation will timeout without response
  std::chrono::seconds operation_timeout;
  //! the unique ids of drives belonging to this cluster
  std::vector<std::string> drives;
};


//------------------------------------------------------------------------------
//! Providing access to cluster instances and the data io cache. Threadsafe.
//------------------------------------------------------------------------------
class ClusterMap
{
public:
  //--------------------------------------------------------------------------
  //! Obtain an input-output class for the supplied identifier.
  //!
  //! @param id the unique identifier for the cluster
  //! @return a valid cluster object
  //--------------------------------------------------------------------------
  std::shared_ptr<ClusterInterface> getCluster(const std::string& id, RedundancyType redundancy);

  //--------------------------------------------------------------------------
  //! Obtain an admin cluster instance for the supplied identifier.
  //!
  //! @param id the unique identifier for the cluster
  //! @param target the type of keys affected by cluster operations 
  //! @param numthreads number of background io threads during scan operations
  //! @return a valid admin cluster object
  //--------------------------------------------------------------------------
  std::unique_ptr<KineticAdminCluster> getAdminCluster(const std::string& id, RedundancyType redundancy);

  //--------------------------------------------------------------------------
  //! Reset the object with supplied configuration
  //! 
  //! @param clusterInfo containing id <-> cluster info mapping for all clusters
  //! @param driveInfo containing id <-> drive info mapping for all clusters
  //--------------------------------------------------------------------------
  void reset(
    std::unordered_map<std::string, ClusterInformation> clusterInfo, 
    std::unordered_map<std::string, std::pair<kinetic::ConnectionOptions, kinetic::ConnectionOptions>> driveInfo
  );

  //--------------------------------------------------------------------------
  //! Constructor.
  //--------------------------------------------------------------------------
  explicit ClusterMap();

private:
  //! the cluster id <-> cluster info
  std::unordered_map<std::string, ClusterInformation> clusterInfoMap;
  
  //! the drive map id <-> connection info
  std::unordered_map<std::string, std::pair<kinetic::ConnectionOptions, kinetic::ConnectionOptions>> driveInfoMap;
  
  //! the cluster cache for erasure coding clusters
  std::unordered_map<std::string,  std::shared_ptr<ClusterInterface> > ecClusterCache;
  
  //! the cluster cache for replication clusters
  std::unordered_map<std::string,  std::shared_ptr<ClusterInterface> > replClusterCache;

  //! RedundancyProvider instances of the same type (nData,nParity) can be shared
  //! among multiple cluster instances
  std::unordered_map<std::string, std::shared_ptr<RedundancyProvider>> rpCache;
  
  //! epoll listener loop shared among all connections of clusters in this cluster map 
  std::unique_ptr<SocketListener> listener;  
  
  //! concurrency control
  std::mutex mutex;

private:
  //--------------------------------------------------------------------------
  //! Set redundancy provider and connection options based on the
  //! information available in the supplied ClusterInfo structure.
  //!
  //! @param cinfo Cluster Information
  //! @param rp redundancy provider instance to set
  //! @param cops Connection Options to fill
  //--------------------------------------------------------------------------
  void fillArgs(const ClusterInformation &cinfo,
                std::shared_ptr<RedundancyProvider>& rp,
                std::vector<std::pair<kinetic::ConnectionOptions, kinetic::ConnectionOptions>>& cops
  );
};

}


#endif	/* KINETICDRIVEMAP_HH */

