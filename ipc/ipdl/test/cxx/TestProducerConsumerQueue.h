#ifndef mozilla__ipdltest_TestProducerConsumerQueue_h
#define mozilla__ipdltest_TestProducerConsumerQueue_h 1

#include "mozilla/_ipdltest/IPDLUnitTests.h"

#include "mozilla/_ipdltest/PTestProducerConsumerQueueParent.h"
#include "mozilla/_ipdltest/PTestProducerConsumerQueueChild.h"
#include "ProducerConsumerQueue.h"

using namespace mozilla::ipc;

namespace mozilla {
namespace _ipdltest {

class TestProducerConsumerQueueParent
    : public PTestProducerConsumerQueueParent {
 public:
  TestProducerConsumerQueueParent();
  virtual ~TestProducerConsumerQueueParent();

  static bool RunTestInProcesses() { return true; }
  static bool RunTestInThreads() { return true; }

  void Main();

 protected:
  virtual void ActorDestroy(ActorDestroyReason why) override {
    if (NormalShutdown != why) fail("unexpected destruction!");
    passed("ok");
    QuitParent();
  }
};

class TestProducerConsumerQueueChild : public PTestProducerConsumerQueueChild {
 public:
  TestProducerConsumerQueueChild();
  virtual ~TestProducerConsumerQueueChild();

  mozilla::ipc::IPCResult RecvConsume(UniquePtr<Consumer>&& aConsumer);

  virtual void ActorDestroy(ActorDestroyReason why) override {
    if (NormalShutdown != why) fail("unexpected destruction!");
    QuitChild();
  }
};

}  // namespace _ipdltest
}  // namespace mozilla

#endif  // ifndef mozilla__ipdltest_TestProducerConsumerQueue_h
