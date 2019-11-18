// Copyright: 2019, Ableton AG, Berlin. All rights reserved.

#include "AudioHost.hpp"

#include "Thread.hpp"

#include <utility>

AudioHost::AudioHost(Setup setup,
                     RenderStarted renderStarted,
                     Process process,
                     RenderEnded renderEnded)
  : mDriver{[this](const auto... xs) { return this->render(xs...); }}
  , mSetup{std::move(setup)}
  , mRenderStarted{std::move(renderStarted)}
  , mProcess{std::move(process)}
  , mRenderEnded{std::move(renderEnded)}
{
  setupWorkerThreads(kDefaultNumWorkerThreads);
  setupBusyThreads(kDefaultNumBusyThreads);
}

AudioHost::~AudioHost()
{
  mDriver.stop();
  teardownWorkerThreads();
  teardownBusyThreads();
}

Driver& AudioHost::driver() { return mDriver; }

int AudioHost::preferredBufferSize() const { return mDriver.preferredBufferSize(); }
void AudioHost::setPreferredBufferSize(const int preferredBufferSize)
{
  if (preferredBufferSize != mDriver.preferredBufferSize())
  {
    // Recreate the worker threads in order to use the new buffer size when setting
    // the thread policy.

    const auto numWorkerThreads = int(mWorkerThreads.size());
    mDriver.stop();
    teardownWorkerThreads();
    mDriver.setPreferredBufferSize(preferredBufferSize);
    setupWorkerThreads(numWorkerThreads);
    mDriver.start();
  }
}

int AudioHost::numWorkerThreads() const { return int(mWorkerThreads.size()); }
void AudioHost::setNumWorkerThreads(const int numWorkerThreads)
{
  if (numWorkerThreads != int(mWorkerThreads.size()))
  {
    mDriver.stop();
    teardownWorkerThreads();
    setupWorkerThreads(numWorkerThreads);
    mDriver.start();
  }
}

int AudioHost::numBusyThreads() const { return int(mBusyThreads.size()); }
void AudioHost::setNumBusyThreads(const int numBusyThreads)
{
  if (numBusyThreads != int(mBusyThreads.size()))
  {
    teardownBusyThreads();
    setupBusyThreads(numBusyThreads);
  }
}

bool AudioHost::processInDriverThread() const { return mProcessInDriverThread; }
void AudioHost::setProcessInDriverThread(const bool isEnabled)
{
  mProcessInDriverThread = isEnabled;
}

bool AudioHost::isWorkIntervalOn() const { return mIsWorkIntervalOn; }
void AudioHost::setIsWorkIntervalOn(const bool isOn)
{
  if (isOn != mIsWorkIntervalOn)
  {
    const auto numWorkerThreads = int(mWorkerThreads.size());
    mDriver.stop();
    teardownWorkerThreads();
    mIsWorkIntervalOn = isOn;
    setupWorkerThreads(numWorkerThreads);
    mDriver.start();
  }
}

double AudioHost::minimumLoad() const { return mMinimumLoad; }
void AudioHost::setMinimumLoad(const double minimumLoad) { mMinimumLoad = minimumLoad; }

void AudioHost::setupWorkerThreads(const int numWorkerThreads)
{
  mSetup(numWorkerThreads);

  mAreWorkerThreadsActive = true;
  for (int i = 1; i <= numWorkerThreads; ++i)
  {
    mWorkerThreads.emplace_back(&AudioHost::workerThread, this, i);
  }
}

void AudioHost::teardownWorkerThreads()
{
  mAreWorkerThreadsActive = false;
  for (size_t i = 0; i < mWorkerThreads.size(); ++i)
  {
    mStartWorkingSemaphore.post();
  }
  for (auto& thread : mWorkerThreads)
  {
    thread.join();
  }
  mWorkerThreads.clear();
}

void AudioHost::setupBusyThreads(const int numBusyThreads)
{
  mAreBusyThreadsActive = true;
  for (int i = 0; i < numBusyThreads; ++i)
  {
    mBusyThreads.emplace_back(&AudioHost::busyThread, this);
  }
}

void AudioHost::teardownBusyThreads()
{
  mAreBusyThreadsActive = false;
  for (auto& busyThread : mBusyThreads)
  {
    busyThread.join();
  }
  mBusyThreads.clear();
}

void AudioHost::ensureMinimumLoad(const std::chrono::time_point<Clock> bufferStartTime,
                                  const int numFrames)
{
  const auto bufferDuration =
    std::chrono::duration<double>{numFrames / mDriver.sampleRate()};
  hardwareDelayUntil(bufferStartTime + (bufferDuration * double(mMinimumLoad)));
}

OSStatus AudioHost::render(AudioUnitRenderActionFlags* ioActionFlags,
                           const AudioTimeStamp* inTimeStamp,
                           UInt32 inBusNumber,
                           UInt32 inNumberFrames,
                           AudioBufferList* ioData)
{
  const auto startTime = Clock::now();
  mNumFrames = inNumberFrames;

  mRenderStarted(inNumberFrames);

  for (size_t i = 0; i < mWorkerThreads.size(); ++i)
  {
    mStartWorkingSemaphore.post();
  }

  if (mProcessInDriverThread)
  {
    mProcess(0, inNumberFrames);
  }

  for (size_t i = 0; i < mWorkerThreads.size(); ++i)
  {
    mFinishedWorkSemaphore.wait();
  }

  const AudioBuffer* pOutputBuffers = ioData->mBuffers;
  const StereoAudioBufferPtrs outputBuffer{static_cast<float*>(pOutputBuffers[0].mData),
                                           static_cast<float*>(pOutputBuffers[1].mData)};
  mRenderEnded(outputBuffer, inTimeStamp->mHostTime, inNumberFrames);

  if (mProcessInDriverThread)
  {
    ensureMinimumLoad(startTime, inNumberFrames);
  }

  return noErr;
}

void AudioHost::workerThread(const int threadIndex)
{
  setThreadTimeConstraintPolicy(
    pthread_self(),
    TimeConstraintPolicy{mDriver.nominalBufferDuration(), kRealtimeThreadQuantum,
                         mDriver.nominalBufferDuration()});

  bool needToJoinWorkInterval = mIsWorkIntervalOn;
  while (1)
  {
    mStartWorkingSemaphore.wait();
    if (!mAreWorkerThreadsActive)
    {
      break;
    }

    // Join after waking from the semaphore to ensure that the CoreAudio thread is
    // active so that findAndJoinWorkInterval() can find its work interval.
    if (needToJoinWorkInterval)
    {
      findAndJoinWorkInterval();
      needToJoinWorkInterval = false;
    }

    const auto startTime = Clock::now();
    const auto numFrames = mNumFrames.load();
    mProcess(threadIndex, numFrames);
    mFinishedWorkSemaphore.post();
    ensureMinimumLoad(startTime, numFrames);
  }

  if (mIsWorkIntervalOn)
  {
    leaveWorkInterval();
  }
}

// A low-priority thread that constantly performs low-energy work
void AudioHost::busyThread()
{
  constexpr auto kLowEnergyDelayDuration = std::chrono::milliseconds{10};
  constexpr auto kSleepDuration = std::chrono::milliseconds{5};

  sched_param param{};
  param.sched_priority = sched_get_priority_min(SCHED_OTHER);
  pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);

  while (mAreBusyThreadsActive)
  {
    const auto delayUntilTime = Clock::now() + kLowEnergyDelayDuration;
    hardwareDelayUntil(delayUntilTime);

    // Sleep to avoid being terminated when running in the background by violating the
    // iOS CPU usage limit
    std::this_thread::sleep_until(delayUntilTime + kSleepDuration);
  }
}
