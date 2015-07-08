#include "KineticIoFactory.hh"
#include "FileIo.hh"
#include "FileAttr.hh"
#include "ClusterMap.hh"
#include "Utility.hh"

using namespace kio;


std::shared_ptr<FileIoInterface> Factory::sharedFileIo()
{
  return std::make_shared<FileIo>();
}

std::unique_ptr<FileIoInterface> Factory::uniqueFileIo()
{
  return std::unique_ptr<FileIoInterface>(new FileIo());
}


std::shared_ptr<ClusterInterface> attrCluster(const char* path)
{
  auto cluster = cmap().getCluster(utility::extractClusterID(path));

  std::shared_ptr<const std::string> empty;
  auto status = cluster->get(std::make_shared<const std::string>(path), true, empty, empty);

  if(!status.ok())
    return std::shared_ptr<ClusterInterface>();

  return cluster;
}


std::shared_ptr<FileAttrInterface> Factory::sharedFileAttr(const char* path)
{
  auto cluster = attrCluster(path);
  if(cluster)
    return std::make_shared<FileAttr>(path, cluster);
  return std::shared_ptr<FileAttrInterface>();
}

std::unique_ptr<FileAttrInterface> Factory::uniqueFileAttr(const char* path)
{
  auto cluster = attrCluster(path);
  if(cluster)
    return std::unique_ptr<FileAttr>(new FileAttr(path, cluster));
  return std::unique_ptr<FileAttrInterface>();
}