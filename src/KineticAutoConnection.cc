#include "KineticAutoConnection.hh"
#include "LoggingException.hh"
#include <sstream>
#include <Logging.hh>

using namespace kinetic;
using namespace kio;

KineticAutoConnection::KineticAutoConnection(
    SocketListener &sw,
    std::pair<kinetic::ConnectionOptions, kinetic::ConnectionOptions> o,
    std::chrono::seconds r) :
    connection(), healthy(false), fd(0), options(o), timestamp(), ratelimit(r),
    mutex(), bg(1,0), sockwatch(sw), mt()
{
  std::random_device rd;
  mt.seed(rd());
  logstring = utility::Convert::toString(
      "(",options.first.host, ":", options.first.port, " and ", options.second.host, ":", options.second.port,")"
  );
}

KineticAutoConnection::~KineticAutoConnection()
{
  if (fd) {
    sockwatch.unsubscribe(fd);
  }
}

const std::string& KineticAutoConnection::getName()
{
  return logstring;
}

void KineticAutoConnection::setError()
{
  std::lock_guard<std::mutex> lock(mutex);
  if (fd) {
    sockwatch.unsubscribe(fd);
    fd = 0;
  }
  healthy = false;
}

std::shared_ptr<kinetic::ThreadsafeNonblockingKineticConnection> KineticAutoConnection::get()
{
  std::call_once(intial_connect, &KineticAutoConnection::connect, this);

  std::lock_guard<std::mutex> lock(mutex);
  if (healthy)
    return connection;

  /* Rate limit connection attempts. */
  using namespace std::chrono;
  auto duration = duration_cast<seconds>(system_clock::now() - timestamp);
  if (duration > ratelimit) {
    if(bg.try_run(std::bind(&KineticAutoConnection::connect, this)))
      kio_debug("Attempting background reconnect. Last reconnect attempt has been ",
               duration," seconds ago. ratelimit is ", ratelimit, " seconds ", logstring);
  }
  throw kio_exception(ENXIO, "No valid connection ", logstring);
}


class ConnectCallback : public kinetic::SimpleCallbackInterface
{
public:
  void Success() { }

  void Failure(kinetic::KineticStatus error) { }

  ConnectCallback() { }

  ~ConnectCallback() { }
};

void KineticAutoConnection::connect()
{
  /* Choose connection to prioritize at random. */
  auto r = mt() % 2;
  auto& primary = r ? options.first : options.second;
  auto& secondary = r ? options.second : options.first;

  auto tmpfd = 0;
  std::shared_ptr<ThreadsafeNonblockingKineticConnection> tmpcon;
  KineticConnectionFactory factory = NewKineticConnectionFactory();

  if (factory.NewThreadsafeNonblockingConnection(primary, tmpcon).ok() ||
      factory.NewThreadsafeNonblockingConnection(secondary, tmpcon).ok()) {
    auto cb = std::make_shared<ConnectCallback>();
    fd_set a;
    tmpcon->NoOp(cb);
    tmpcon->Run(&a, &a, &tmpfd);
  }
  else{
    kio_debug("Connection attempt failed ",logstring);
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    timestamp = std::chrono::system_clock::now();
    if (tmpfd) {
      tmpfd--;
      try{
        sockwatch.subscribe(tmpfd, this);
      }catch(const std::exception& e){
        kio_warning(e.what());
        throw e;
      }
      fd = tmpfd;
      connection = std::move(tmpcon);
      healthy=true;
      kio_debug("connection attempt succeeded ", logstring);
    }
  }
}
