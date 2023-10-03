// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "engine.hh"
#include "processor.hh"
#include "utils.hh"
#include "loop.hh"
#include "driver.hh"
#include "server.hh"
#include "datautils.hh"
#include "atomics.hh"
#include "project.hh"
#include "wave.hh"
#include "main.hh"      // main_loop_autostop_mt
#include "memory.hh"
#include "internal.hh"

#define EDEBUG(...)             Ase::debug ("engine", __VA_ARGS__)

namespace Ase {

static std::atomic<uint> pref_synth_latency;
static Preference synth_latency_p = Preference ("audio.synth_latency",
                                                { _("Latency"), "", 15, { 0, 3000, 5 }, "ms", STANDARD,
                                                  "", _("Processing duration between input and output of a single sample, smaller values increase CPU load") },
                                                [] (const CString &ident, const Value &value) { pref_synth_latency = value.as_int(); printerr ("audio.synth_latency: %d\n", pref_synth_latency); });

// == decls ==
using VoidFunc = std::function<void()>;
using StartQueue = AsyncBlockingQueue<char>;
ASE_CLASS_DECLS (EngineMidiInput);
constexpr uint fixed_sample_rate = 48000;

// == EngineJobImpl ==
struct EngineJobImpl {
  VoidFunc func;
  std::atomic<EngineJobImpl*> next = nullptr;
  explicit EngineJobImpl (const VoidFunc &jobfunc) : func (jobfunc) {}
};
static inline std::atomic<EngineJobImpl*>&
atomic_next_ptrref (EngineJobImpl *j)
{
  return j->next;
}

// == AudioEngineThread ==
class AudioEngineThread : public AudioEngine {
  PcmDriverP                   null_pcm_driver_, pcm_driver_;
  MidiDriverS                  midi_drivers_;
  static constexpr uint        fixed_n_channels = 2;
  constexpr static size_t      MAX_BUFFER_SIZE = AUDIO_BLOCK_MAX_RENDER_SIZE;
  size_t                       buffer_size_ = MAX_BUFFER_SIZE; // mono buffer size
  float                        chbuffer_data_[MAX_BUFFER_SIZE * fixed_n_channels] = { 0, };
  uint64                       write_stamp_ = 0, render_stamp_ = AUDIO_BLOCK_MAX_RENDER_SIZE;
  std::vector<AudioProcessor*> schedule_;
  EngineMidiInputP             midi_proc_;
  bool                         schedule_invalid_ = true;
  bool                         output_needsrunning_ = false;
  AtomicIntrusiveStack<EngineJobImpl> async_jobs_, const_jobs_, trash_jobs_;
  const VoidF                  owner_wakeup_;
  std::thread                 *thread_ = nullptr;
  MainLoopP                    event_loop_ = MainLoop::create();
  Emittable::Connection        onchange_prefs_;
  AudioProcessorS              oprocs_;
  ProjectImplP                 project_;
  WaveWriterP                  wwriter_;
  FastMemory::Block            transport_block_;
public:
  std::atomic<uint64>          autostop_ = U64MAX;
  struct UserNoteJob {
    std::atomic<UserNoteJob*> next = nullptr;
    UserNote note;
  };
  AtomicIntrusiveStack<UserNoteJob> user_notes_;
public:
  virtual        ~AudioEngineThread      ();
  explicit        AudioEngineThread      (const VoidF&, uint, SpeakerArrangement, const FastMemory::Block&);
  void            schedule_clear         ();
  void            schedule_add           (AudioProcessor &aproc, uint level);
  void            schedule_queue_update  ();
  uint64          frame_counter          () const               { return render_stamp_; }
  void            schedule_render        (uint64 frames);
  void            enable_output          (AudioProcessor &aproc, bool onoff);
  void            start_threads          ();
  void            stop_threads           ();
  void            wakeup_thread_mt       ();
  void            capture_start          (const String &filename, bool needsrunning);
  void            capture_stop           ();
  bool            ipc_pending            ();
  void            ipc_dispatch           ();
  AudioProcessorP get_event_source       ();
  void            add_job_mt             (EngineJobImpl *aejob, const AudioEngine::JobQueue *jobqueue);
  bool            pcm_check_write        (bool write_buffer, int64 *timeout_usecs_p = nullptr);
  bool            driver_dispatcher      (const LoopState &state);
  bool            process_jobs           (AtomicIntrusiveStack<EngineJobImpl> &joblist);
  void            update_drivers         (bool fullio, uint latency);
  void            run                    (StartQueue *sq);
  void            swap_midi_drivers_sync (const MidiDriverS &midi_drivers);
  void            queue_user_note        (const String &channel, UserNote::Flags flags, const String &text);
  void            set_project            (ProjectImplP project);
  ProjectImplP    get_project            ();
};

static std::thread::id audio_engine_thread_id = {};
const ThreadId &AudioEngine::thread_id = audio_engine_thread_id;

static inline std::atomic<AudioEngineThread::UserNoteJob*>&
atomic_next_ptrref (AudioEngineThread::UserNoteJob *j)
{
  return j->next;
}

template<int ADDING> static void
interleaved_stereo (const size_t n_frames, float *buffer, AudioProcessor &proc, OBusId obus)
{
  if (proc.n_ochannels (obus) >= 2)
    {
      const float *src0 = proc.ofloats (obus, 0);
      const float *src1 = proc.ofloats (obus, 1);
      float *d = buffer, *const b = d + n_frames;
      do {
        if_constexpr (ADDING == 0)
          {
            *d++ = *src0++;
            *d++ = *src1++;
          }
        else
          {
            *d++ += *src0++;
            *d++ += *src1++;
          }
      } while (d < b);
    }
  else if (proc.n_ochannels (obus) >= 1)
    {
      const float *src = proc.ofloats (obus, 0);
      float *d = buffer, *const b = d + n_frames;
      do {
        if_constexpr (ADDING == 0)
          {
            *d++ = *src;
            *d++ = *src++;
          }
        else
          {
            *d++ += *src;
            *d++ += *src++;
          }
      } while (d < b);
    }
}

void
AudioEngineThread::schedule_queue_update()
{
  schedule_invalid_ = true;
}

void
AudioEngineThread::schedule_clear()
{
  while (schedule_.size() != 0)
    {
      AudioProcessor *cur = schedule_.back();
      schedule_.pop_back();
      while (cur)
        {
          AudioProcessor *const proc = cur;
          cur = proc->sched_next_;
          proc->flags_ &= ~AudioProcessor::SCHEDULED;
          proc->sched_next_ = nullptr;
        }
    }
  schedule_invalid_ = true;
}

void
AudioEngineThread::schedule_add (AudioProcessor &aproc, uint level)
{
  return_unless (0 == (aproc.flags_ & AudioProcessor::SCHEDULED));
  assert_return (aproc.sched_next_ == nullptr);
  if (schedule_.size() <= level)
    schedule_.resize (level + 1);
  aproc.sched_next_ = schedule_[level];
  schedule_[level] = &aproc;
  aproc.flags_ |= AudioProcessor::SCHEDULED;
  if (aproc.render_stamp_ != render_stamp_)
    aproc.reset_state (render_stamp_);
}

void
AudioEngineThread::schedule_render (uint64 frames)
{
  assert_return (0 == (frames & (8 - 1)));
  // render scheduled AudioProcessor nodes
  const uint64 target_stamp = render_stamp_ + frames;
  for (size_t l = 0; l < schedule_.size(); l++)
    {
      AudioProcessor *proc = schedule_[l];
      while (proc)
        {
          proc->render_block (target_stamp);
          proc = proc->sched_next_;
        }
    }
  // render output buffer interleaved
  constexpr auto MAIN_OBUS = OBusId (1);
  size_t n = 0;
  for (size_t i = 0; i < oprocs_.size(); i++)
    if (oprocs_[i]->n_obuses())
      {
        if (n++ == 0)
          interleaved_stereo<0> (buffer_size_ * fixed_n_channels, chbuffer_data_, *oprocs_[i], MAIN_OBUS);
        else
          interleaved_stereo<1> (buffer_size_ * fixed_n_channels, chbuffer_data_, *oprocs_[i], MAIN_OBUS);
        static_assert (2 == fixed_n_channels);
      }
  if (n == 0)
    floatfill (chbuffer_data_, 0.0, buffer_size_ * fixed_n_channels);
  render_stamp_ = target_stamp;
  transport_.advance (frames);
}

void
AudioEngineThread::enable_output (AudioProcessor &aproc, bool onoff)
{
  AudioProcessorP procp = shared_ptr_cast<AudioProcessor> (&aproc);
  assert_return (procp != nullptr);
  if (onoff && !(aproc.flags_ & AudioProcessor::ENGINE_OUTPUT))
    {
      oprocs_.push_back (procp);
      aproc.flags_ |= AudioProcessor::ENGINE_OUTPUT;
      schedule_queue_update();
    }
  else if (!onoff && (aproc.flags_ & AudioProcessor::ENGINE_OUTPUT))
    {
      const bool foundproc = Aux::erase_first (oprocs_, [procp] (AudioProcessorP c) { return c == procp; });
      aproc.flags_ &= ~AudioProcessor::ENGINE_OUTPUT;
      schedule_queue_update();
      assert_return (foundproc);
    }
}

void
AudioEngineThread::update_drivers (bool fullio, uint latency)
{
  Error er = {};
  // PCM fallback
  PcmDriverConfig pconfig { .n_channels = fixed_n_channels, .mix_freq = fixed_sample_rate,
                            .latency_ms = latency, .block_length = AUDIO_BLOCK_MAX_RENDER_SIZE };
  const String null_driver = "null";
  if (!null_pcm_driver_)
    {
      null_pcm_driver_ = PcmDriver::open (null_driver, Driver::WRITEONLY, Driver::WRITEONLY, pconfig, &er);
      if (!null_pcm_driver_ || er != 0)
        fatal_error ("failed to open internal PCM driver ('%s'): %s", null_driver, ase_error_blurb (er));
    }
  if (!pcm_driver_)
    pcm_driver_ = null_pcm_driver_;
  // MIDI Processor
  if (!midi_proc_)
    swap_midi_drivers_sync ({});
  if (!fullio)
    return;
  // PCM Output
  if (pcm_driver_ == null_pcm_driver_)
    {
      const String pcm_driver_name = ServerImpl::instancep()->preferences().pcm_driver;
      er = {};
      if (pcm_driver_name != null_driver)
        {
          PcmDriverP new_pcm_driver = PcmDriver::open (pcm_driver_name, Driver::WRITEONLY, Driver::WRITEONLY, pconfig, &er);
          if (new_pcm_driver)
            pcm_driver_ = new_pcm_driver;
          else
            {
              auto s = string_format ("# Audio I/O Error\n"
                                      "Failed to open audio device:\n"
                                      "%s:\n"
                                      "%s",
                                      pcm_driver_name, ase_error_blurb (er));
              queue_user_note ("pcm-driver", UserNote::CLEAR, s);
              printerr ("%s\n", string_replace (s, "\n", " "));
            }
        }
    }
  buffer_size_ = std::min (MAX_BUFFER_SIZE, size_t (pcm_driver_->block_length()));
  floatfill (chbuffer_data_, 0.0, buffer_size_ * fixed_n_channels);
  write_stamp_ = render_stamp_ - buffer_size_; // force zeros initially
  EDEBUG ("AudioEngineThread::update_drivers: PCM: channels=%d pcmblock=%d enginebuffer=%d\n",
          fixed_n_channels, pcm_driver_->block_length(), buffer_size_);
  // MIDI Driver List
  MidiDriverS old_drivers = midi_drivers_, new_drivers;
  const auto midi_driver_names = {
    ServerImpl::instancep()->preferences().midi_driver_1,
    ServerImpl::instancep()->preferences().midi_driver_2,
    ServerImpl::instancep()->preferences().midi_driver_3,
    ServerImpl::instancep()->preferences().midi_driver_4,
  };
  int midi_errors = 0;
  auto midi_err = [&] (const String &devid, int nth, Error er) {
    auto s = string_format ("## MIDI I/O Failure\n"
                            "Failed to open MIDI device #%u:\n"
                            "%s:\n"
                            "%s",
                            nth, devid, ase_error_blurb (er));
    queue_user_note ("midi-driver", midi_errors++ == 0 ? UserNote::CLEAR : UserNote::APPEND, s);
    printerr ("%s\n", string_replace (s, "\n", " "));
  };
  int midi_dev = 0;
  for (const auto &devid : midi_driver_names)
    {
      midi_dev += 1;
      if (devid == null_driver)
        continue;
      if (Aux::contains (new_drivers, [&] (auto &d) {
        return d->devid() == devid; }))
        {
          midi_err (devid, midi_dev, Error::DEVICE_BUSY);
          continue;
        }
      MidiDriverP d;
      for (MidiDriverP o : old_drivers)
        if (o->devid() == devid)
          d = o;
      if (d)
        {
          Aux::erase_first (old_drivers, [&] (auto &o) { return o == d; });
          new_drivers.push_back (d);    // keep opened driver
          continue;
        }
      er = {};
      d = MidiDriver::open (devid, Driver::READONLY, &er);
      if (d)
        new_drivers.push_back (d);      // add new driver
      else
        midi_err (devid, midi_dev, er);
    }
  midi_drivers_ = new_drivers;
  swap_midi_drivers_sync (midi_drivers_);
  while (!old_drivers.empty())
    {
      MidiDriverP old = old_drivers.back();
      old_drivers.pop_back();
      old->close();                     // close old driver *after* sync
    }
}

void
AudioEngineThread::capture_start (const String &filename, bool needsrunning)
{
  const uint sample_rate = transport_.samplerate;
  capture_stop();
  output_needsrunning_ = needsrunning;
  if (string_endswith (filename, ".wav"))
    {
      wwriter_ = wave_writer_create_wav (sample_rate, fixed_n_channels, filename);
      if (!wwriter_)
        printerr ("%s: failed to open file: %s\n", filename, strerror (errno));
    }
  else if (string_endswith (filename, ".opus"))
    {
      wwriter_ = wave_writer_create_opus (sample_rate, fixed_n_channels, filename);
      if (!wwriter_)
        printerr ("%s: failed to open file: %s\n", filename, strerror (errno));
    }
  else if (string_endswith (filename, ".flac"))
    {
      wwriter_ = wave_writer_create_flac (sample_rate, fixed_n_channels, filename);
      if (!wwriter_)
        printerr ("%s: failed to open file: %s\n", filename, strerror (errno));
    }
  else if (!filename.empty())
    printerr ("%s: unknown sample file: %s\n", filename, strerror (ENOSYS));
}

void
AudioEngineThread::capture_stop()
{
  if (wwriter_)
    {
      wwriter_->close();
      wwriter_ = nullptr;
    }
}

void
AudioEngineThread::run (StartQueue *sq)
{
  assert_return (pcm_driver_);
  // FIXME: assert owner_wakeup and free trash
  this_thread_set_name ("AudioEngine-0"); // max 16 chars
  audio_engine_thread_id = std::this_thread::get_id();
  sched_fast_priority (this_thread_gettid());
  event_loop_->exec_dispatcher (std::bind (&AudioEngineThread::driver_dispatcher, this, std::placeholders::_1));
  sq->push ('R'); // StartQueue becomes invalid after this call
  sq = nullptr;
  event_loop_->run();
}

bool
AudioEngineThread::process_jobs (AtomicIntrusiveStack<EngineJobImpl> &joblist)
{
  EngineJobImpl *const jobs = joblist.pop_reversed(), *last = nullptr;
  for (EngineJobImpl *job = jobs; job; last = job, job = job->next)
    job->func();
  if (last)
    {
      if (trash_jobs_.push_chain (jobs, last))
        owner_wakeup_();
    }
  return last != nullptr;
}

bool
AudioEngineThread::pcm_check_write (bool write_buffer, int64 *timeout_usecs_p)
{
  int64 timeout_usecs = INT64_MAX;
  const bool can_write = pcm_driver_->pcm_check_io (&timeout_usecs) || timeout_usecs == 0;
  if (timeout_usecs_p)
    *timeout_usecs_p = timeout_usecs;
  if (!write_buffer)
    return can_write;
  if (!can_write || write_stamp_ >= render_stamp_)
    return false;
  pcm_driver_->pcm_write (buffer_size_ * fixed_n_channels, chbuffer_data_);
  if (wwriter_ && fixed_n_channels == 2 && write_stamp_ < autostop_ &&
      (!output_needsrunning_ || transport_.running()))
    wwriter_->write (chbuffer_data_, buffer_size_);
  write_stamp_ += buffer_size_;
  if (write_stamp_ >= autostop_)
    main_loop_autostop_mt();
  assert_warn (write_stamp_ == render_stamp_);
  return false;
}

bool
AudioEngineThread::driver_dispatcher (const LoopState &state)
{
  int64 *timeout_usecs = nullptr;
  switch (state.phase)
    {
    case LoopState::PREPARE:
      timeout_usecs = const_cast<int64*> (&state.timeout_usecs);
      /* fall-through */
    case LoopState::CHECK:
      if (atquit_triggered())
        return false;   // stall engine once program is aborted
      if (!const_jobs_.empty() || !async_jobs_.empty())
        return true; // jobs pending
      if (render_stamp_ <= write_stamp_)
        return true; // must render
      // FIXME: add pcm driver pollfd with 1-block threshold
      return pcm_check_write (false, timeout_usecs);
    case LoopState::DISPATCH:
      pcm_check_write (true);
      if (render_stamp_ <= write_stamp_)
        {
          process_jobs (async_jobs_); // apply pending modifications before render
          if (schedule_invalid_)
            {
              schedule_clear();
              for (AudioProcessorP &proc :  oprocs_)
                proc->schedule_processor();
              schedule_invalid_ = false;
            }
          schedule_render (buffer_size_);
          pcm_check_write (true); // minimize drop outs
        }
      if (!const_jobs_.empty()) {   // owner may be blocking for const_jobs_ execution
        process_jobs (async_jobs_); // apply pending modifications first
        process_jobs (const_jobs_);
      }
      if (ipc_pending())
        owner_wakeup_(); // owner needs to ipc_dispatch()
      return true; // keep alive
    default: ;
    }
  return false;
}

void
AudioEngineThread::queue_user_note (const String &channel, UserNote::Flags flags, const String &text)
{
  UserNoteJob *uj = new UserNoteJob { nullptr, { 0, flags, channel, text } };
  if (user_notes_.push (uj))
    owner_wakeup_();
}

bool
AudioEngineThread::ipc_pending ()
{
  const bool have_jobs = !trash_jobs_.empty() || !user_notes_.empty();
  return have_jobs || AudioProcessor::enotify_pending();
}

void
AudioEngineThread::ipc_dispatch ()
{
  UserNoteJob *uj = user_notes_.pop_reversed();
  while (uj)
    {
      ASE_SERVER.user_note (uj->note.text, uj->note.channel, uj->note.flags);
      UserNoteJob *const old = uj;
      uj = old->next;
      delete old;
    }
  if (AudioProcessor::enotify_pending())
    AudioProcessor::enotify_dispatch();
  EngineJobImpl *job = trash_jobs_.pop_all();
  while (job)
    {
      EngineJobImpl *old = job;
      job = job->next;
      delete old;
    }
}

void
AudioEngineThread::wakeup_thread_mt()
{
  assert_return (event_loop_);
  event_loop_->wakeup();
}

void
AudioEngineThread::start_threads()
{
  schedule_.reserve (8192);
  assert_return (thread_ == nullptr);
  const uint latency = SERVER->preferences().synth_latency;
  update_drivers (false, latency);
  schedule_queue_update();
  StartQueue start_queue;
  thread_ = new std::thread (&AudioEngineThread::run, this, &start_queue);
  const char reply = start_queue.pop(); // synchronize with thread start
  assert_return (reply == 'R');
  onchange_prefs_ = ASE_SERVER.on_event ("change:prefs", [this, latency] (auto...) {
    update_drivers (true, latency);
  });
  update_drivers (true, latency);
}

void
AudioEngineThread::stop_threads()
{
  AudioEngineThread &engine = *dynamic_cast<AudioEngineThread*> (this);
  assert_return (engine.thread_ != nullptr);
  onchange_prefs_.reset();
  engine.event_loop_->quit (0);
  engine.thread_->join();
  audio_engine_thread_id = {};
  auto oldthread = engine.thread_;
  engine.thread_ = nullptr;
  delete oldthread;
}

void
AudioEngineThread::add_job_mt (EngineJobImpl *job, const AudioEngine::JobQueue *jobqueue)
{
  assert_return (job != nullptr);
  AudioEngineThread &engine = *dynamic_cast<AudioEngineThread*> (this);
  // engine not running, run job right away
  if (!engine.thread_)
    {
      job->func();
      delete job;
      return;
    }
  // enqueue async_jobs
  if (jobqueue == &async_jobs)  // non-blocking, via async_jobs_ queue
    { // run asynchronously
      const bool was_empty = engine.async_jobs_.push (job);
      if (was_empty)
        wakeup_thread_mt();
      return;
    }
  // blocking jobs, queue wrapper that synchronizes via Semaphore
  ScopedSemaphore sem;
  VoidFunc jobfunc = job->func;
  std::function<void()> wrapper = [&sem, jobfunc] () {
    jobfunc();
    sem.post();
  };
  job->func = wrapper;
  bool need_wakeup;
  if (jobqueue == &const_jobs)  // blocking, via const_jobs_ queue
    need_wakeup = engine.const_jobs_.push (job);
  else if (jobqueue == &synchronized_jobs)  // blocking, via async_jobs_ queue
    need_wakeup = engine.async_jobs_.push (job);
  else
    assert_unreached();
  if (need_wakeup)
    wakeup_thread_mt();
  sem.wait();
}

void
AudioEngineThread::set_project (ProjectImplP project)
{
  if (project)
    {
      assert_return (project_ == nullptr);
      assert_return (!project->is_active());
    }
  if (project_)
    project_->_deactivate();
  const ProjectImplP old = project_;
  project_ = project;
  if (project_)
    project_->_activate();
  // dtor of old runs here
}

ProjectImplP
AudioEngineThread::get_project ()
{
  return project_;
}

AudioEngineThread::~AudioEngineThread ()
{
  FastMemory::Block transport_block = transport_block_; // keep alive until after ~AudioEngine
  main_jobs += [transport_block] () { ServerImpl::instancep()->telemem_release (transport_block); };
}

AudioEngineThread::AudioEngineThread (const VoidF &owner_wakeup, uint sample_rate, SpeakerArrangement speakerarrangement,
                                      const FastMemory::Block &transport_block) :
  AudioEngine (*this, *new (transport_block.block_start) AudioTransport (speakerarrangement, sample_rate)),
  owner_wakeup_ (owner_wakeup), transport_block_ (transport_block)
{
  oprocs_.reserve (16);
  assert_return (transport_.samplerate == 48000);
}

AudioEngine&
make_audio_engine (const VoidF &owner_wakeup, uint sample_rate, SpeakerArrangement speakerarrangement)
{
  FastMemory::Block transport_block = ServerImpl::instancep()->telemem_allocate (sizeof (AudioTransport));
  return *new AudioEngineThread (owner_wakeup, sample_rate, speakerarrangement, transport_block);
}

// == AudioEngine ==
AudioEngine::AudioEngine (AudioEngineThread &audio_engine_thread, AudioTransport &transport) :
  transport_ (transport), async_jobs (audio_engine_thread), const_jobs (audio_engine_thread), synchronized_jobs (audio_engine_thread)
{}

AudioEngine::~AudioEngine()
{
  // some ref-counted objects keep AudioEngine& members around
  fatal_error ("AudioEngine must not be destroyed");
}

uint64
AudioEngine::frame_counter () const
{
  const AudioEngineThread &impl = static_cast<const AudioEngineThread&> (*this);
  return impl.frame_counter();
}

void
AudioEngine::set_autostop (uint64_t nsamples)
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  impl.autostop_ = nsamples;
}

void
AudioEngine::schedule_queue_update()
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  impl.schedule_queue_update();
}

void
AudioEngine::schedule_add (AudioProcessor &aproc, uint level)
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  impl.schedule_add (aproc, level);
}

void
AudioEngine::enable_output (AudioProcessor &aproc, bool onoff)
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  return impl.enable_output (aproc, onoff);
}

void
AudioEngine::start_threads()
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  return impl.start_threads();
}

void
AudioEngine::stop_threads()
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  return impl.stop_threads();
}

void
AudioEngine::queue_capture_start (CallbackS &callbacks, const String &filename, bool needsrunning)
{
  AudioEngineThread *impl = static_cast<AudioEngineThread*> (this);
  String file = filename;
  callbacks.push_back ([impl,file,needsrunning] () {
    impl->capture_start (file, needsrunning);
  });
}

void
AudioEngine::queue_capture_stop (CallbackS &callbacks)
{
  AudioEngineThread *impl = static_cast<AudioEngineThread*> (this);
  callbacks.push_back ([impl] () {
    impl->capture_stop();
  });
}

void
AudioEngine::wakeup_thread_mt ()
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  return impl.wakeup_thread_mt();
}

bool
AudioEngine::ipc_pending ()
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  return impl.ipc_pending();
}

void
AudioEngine::ipc_dispatch ()
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  return impl.ipc_dispatch();
}

AudioProcessorP
AudioEngine::get_event_source ()
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  return impl.get_event_source();
}

void
AudioEngine::set_project (ProjectImplP project)
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  return impl.set_project (project);
}

ProjectImplP
AudioEngine::get_project ()
{
  AudioEngineThread &impl = static_cast<AudioEngineThread&> (*this);
  return impl.get_project();
}

AudioEngine::JobQueue::JobQueue (AudioEngine &aet) :
  queue_tag_ (ptrdiff_t (this) - ptrdiff_t (&aet))
{
  assert_return (ptrdiff_t (this) < 256 + ptrdiff_t (&aet));
}

void
AudioEngine::JobQueue::operator+= (const std::function<void()> &job)
{
  AudioEngine *audio_engine = reinterpret_cast<AudioEngine*> (ptrdiff_t (this) - queue_tag_);
  AudioEngineThread &audio_engine_thread = static_cast<AudioEngineThread&> (*audio_engine);
  return audio_engine_thread.add_job_mt (new EngineJobImpl (job), this);
}

// == MidiInput ==
// Processor providing MIDI device events
class EngineMidiInput : public AudioProcessor {
  void
  initialize (SpeakerArrangement busses) override
  {
    prepare_event_output();
  }
  void
  reset (uint64 target_stamp) override
  {
    MidiEventStream &estream = get_event_output();
    estream.clear();
    estream.reserve (256);
  }
  void
  render (uint n_frames) override
  {
    MidiEventStream &estream = get_event_output();
    estream.clear();
    for (size_t i = 0; i < midi_drivers_.size(); i++)
      midi_drivers_[i]->fetch_events (estream, sample_rate());
  }
public:
  MidiDriverS midi_drivers_;
  EngineMidiInput (AudioEngine &engine) :
    AudioProcessor (engine)
  {}
};

template<class T> struct Mutable {
  mutable T value;
  Mutable (const T &v) : value (v) {}
  operator T& () { return value; }
};

void
AudioEngineThread::swap_midi_drivers_sync (const MidiDriverS &midi_drivers)
{
  if (!midi_proc_)
    {
      AudioProcessorP aprocp = AudioProcessor::create_processor<EngineMidiInput> (*this);
      assert_return (aprocp);
      midi_proc_ = std::dynamic_pointer_cast<EngineMidiInput> (aprocp);
      assert_return (midi_proc_);
      EngineMidiInputP midi_proc = midi_proc_;
      async_jobs += [midi_proc] () {
        midi_proc->enable_engine_output (true);
      };
    }
  EngineMidiInputP midi_proc = midi_proc_;
  Mutable<MidiDriverS> new_drivers { midi_drivers };
  synchronized_jobs += [midi_proc, new_drivers] () {
    midi_proc->midi_drivers_.swap (new_drivers.value);
    // use swap() to defer dtor to user thread
  };
}

AudioProcessorP
AudioEngineThread::get_event_source ()
{
  return midi_proc_;
}

} // Ase
