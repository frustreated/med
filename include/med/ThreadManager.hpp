#ifndef THREAD_MANAGER
#define THREAD_MANAGER

#include <functional>
#include <vector>
#include <future>
#include <condition_variable>
#include <mutex>

typedef std::function<void()> TMTask;

class ThreadManager {
public:
  explicit ThreadManager(int maxThread = 4);
  virtual ~ThreadManager();

  void queueTask(TMTask* fn);
  void clear();
  void start();

  void setMaxThreads(int num);

private:
  std::vector<TMTask*> container;
  int maxThreads;
  int numOfRunningThreads;
  int currThreadIndex;

  std::condition_variable cv;
  std::mutex mut;

  std::future<void> startTask(int index);
};

#endif
