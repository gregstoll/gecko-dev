#include "TestProducerConsumerQueue.h"

#include "base/platform_thread.h"
#include "IPDLUnitTests.h"  // fail etc.

static mozilla::LazyLogModule sLogModule("test");
#define TEST_LOG(...) MOZ_LOG(sLogModule, LogLevel::Debug, (__VA_ARGS__))
const uint32_t MAX_MS_WAIT = 1000;  // 1s

struct BigStruct {
  char junk[17];  // must be larger than the PCQ size
};

struct SmallStruct {
  char junk[1];
};

namespace IPC {
MAKE_PCQTYPEINFO(BigStruct, PcqTypeInfo_UserStart)
MAKE_PCQTYPEINFO(SmallStruct, PcqTypeInfo_UserStart + 1)
}  // namespace IPC

namespace mozilla {

namespace ipc {
// (De)serialize as fixed size
template <>
struct PcqParamTraits<::BigStruct> {
  static PcqStatus Write(ProducerView& aProducerView, const BigStruct& aArg) {
    return aProducerView.Write(&aArg, sizeof(::BigStruct));
  }

  /**
   * Read data from the PCQ into aArg, or just skip the data if aArg is null.
   * It is an error to read less than is reported by MinSize(aArg).
   */
  static PcqStatus Read(ConsumerView& aConsumerView, BigStruct* aArg) {
    return aConsumerView.Read(&aArg, sizeof(::BigStruct));
  }

  /**
   * The minimum number of bytes needed to represent this object in the queue.
   * It is intended to be a very fast estimate but most cases can easily
   * compute the exact value.
   * If aArg is null then this should be the minimum ever required (it is only
   * null when checking for deserialization, since the argument is obviously
   * not yet available).  It is an error for the queue to require less room
   * than MinSize() reports.  A MinSize of 0 is always valid (albeit wasteful).
   */
  template <typename View>
  static size_t MinSize(View& aView, const BigStruct* aArg) {
    return sizeof(::BigStruct);
  }
};
}  // namespace ipc

namespace _ipdltest {

//-----------------------------------------------------------------------------
// parent

TestProducerConsumerQueueParent::TestProducerConsumerQueueParent() {
  MOZ_COUNT_CTOR(TestProducerConsumerQueueParent);
}

TestProducerConsumerQueueParent::~TestProducerConsumerQueueParent() {
  MOZ_COUNT_DTOR(TestProducerConsumerQueueParent);
}

void Produce(UniquePtr<Producer>&& producer) {
  TEST_LOG("Producing 1 and 2");
  PcqStatus status = producer->TryInsert(1, 2);
  if (status != PcqStatus::Success) {
    // add 8 bytes.  3 remain
    fail("first produce - %d", status);
  }
  TEST_LOG("produced 1 and 2");

  TEST_LOG("Attempting to produce BigStruct");
  status = producer->TryInsert(BigStruct());
  if (status != PcqStatus::PcqTooSmall) {
    fail("reject large object - %d", status);
  }
  TEST_LOG("Properly failed to produce BigStruct");

  TimeStamp start = TimeStamp::Now();
  TimeDuration maxWait = TimeDuration::FromMilliseconds(MAX_MS_WAIT);
  TEST_LOG("Attempting to produce 3");
  do {
    PlatformThread::YieldCurrentThread();
    status = producer->TryInsert(3);
  } while ((status == PcqStatus::PcqNotReady) &&
           ((TimeStamp::Now() - start) < maxWait));

  if (status == PcqStatus::PcqNotReady) {
    fail("producer timed out waiting for consumer");
  }

  if (status != PcqStatus::Success) {
    fail("misc error in producer: %d", status);
  }

  TEST_LOG("Properly produced 3.  Producer is finished.");
}

void TestProducerConsumerQueueParent::Main() {
  TEST_LOG("Creating PCQ");
  UniquePtr<ProducerConsumerQueue> pcq(ProducerConsumerQueue::Create(this, 16));
  if (!pcq) {
    fail("making PCQ");
  }

  UniquePtr<Consumer> consumer = std::move(pcq->mConsumer);
  if (!consumer) {
    fail("serializing consumer");
  }

  TEST_LOG("Sending consumer to child process");
  if (!SendConsume(std::move(consumer))) {
    fail("sending Ping");
  }

  UniquePtr<Producer> producer = std::move(pcq->mProducer);
  if (!producer) {
    fail("failed to make producer");
  }

  Produce(std::move(producer));
}

//-----------------------------------------------------------------------------
// child process

int ConsumeInt(UniquePtr<Consumer>& consumer) {
  TimeStamp start = TimeStamp::Now();
  TimeDuration maxWait = TimeDuration::FromMilliseconds(MAX_MS_WAIT);
  PcqStatus status;
  int ret;
  do {
    PlatformThread::YieldCurrentThread();
    TEST_LOG("Attempting to consume element");
    status = consumer->TryRemove(ret);
  } while ((status == PcqStatus::PcqNotReady) &&
           ((TimeStamp::Now() - start) < maxWait));

  if (status == PcqStatus::PcqNotReady) {
    fail("consumer timed out waiting for producer");
  }

  if (status != PcqStatus::Success) {
    fail("misc error in consumer: %d", status);
  }

  TEST_LOG("Consumed element");
  return ret;
}

void Consume(UniquePtr<Consumer>&& consumer) {
  const int expected[] = {1, 2, 3};
  for (uint32_t i = 0; i < arraysize(expected); ++i) {
    int found = ConsumeInt(consumer);
    if (expected[i] != found) {
      fail(
          "incorrect value obtained in deserialization.  "
          "Expected: %d.  Found: %d",
          expected[i], found);
    }
  }
  TEST_LOG("Consumer is finished");
}

mozilla::ipc::IPCResult TestProducerConsumerQueueChild::RecvConsume(
    UniquePtr<Consumer>&& aConsumer) {
  TEST_LOG("Received Consumer in child process");
  if (!aConsumer) {
    fail("serializing consumer");
  }

  Consume(std::move(aConsumer));

  Close();

  return IPC_OK();
}

TestProducerConsumerQueueChild::TestProducerConsumerQueueChild() {
  MOZ_COUNT_CTOR(TestProducerConsumerQueueChild);
}

TestProducerConsumerQueueChild::~TestProducerConsumerQueueChild() {
  MOZ_COUNT_DTOR(TestProducerConsumerQueueChild);
}

}  // namespace _ipdltest
}  // namespace mozilla
