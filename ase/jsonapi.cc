// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "jsonapi.hh"
#include "server.hh"
#include "main.hh"
#include "internal.hh"

namespace Ase {

static String subprotocol_authentication;

void
jsonapi_require_auth (const String &subprotocol)
{
  subprotocol_authentication = subprotocol;
}

// == JsonapiConnection ==
class JsonapiConnection;
using JsonapiConnectionP = std::shared_ptr<JsonapiConnection>;
using JsonapiConnectionW = std::weak_ptr<JsonapiConnection>;
static JsonapiConnectionP current_message_conection;

static bool
is_localhost (const String &url, int port)
{
  const char *p = url.c_str();
  if (strncmp (p, "http://", 7) == 0)
    p += 7;
  else if (strncmp (p, "https://", 8) == 0)
    p += 8;
  else
    return false;
  const String localhost = port > 0 ? string_format ("localhost:%u/", port) : "localhost/";
  const String local_127 = port > 0 ? string_format ("127.0.0.1:%u/", port) : "127.0.0.1/";
  if (strncmp (p, localhost.c_str(), localhost.size()) == 0)
    return true;
  if (strncmp (p, local_127.c_str(), local_127.size()) == 0)
    return true;
  return false;
}

class JsonapiConnection : public WebSocketConnection, public CustomDataContainer {
  Jsonipc::InstanceMap imap_;
  void
  log (const String &message) override
  {
    printerr ("%s: %s\n", nickname(), message);
  }
  int
  validate() override
  {
    using namespace AnsiColors;
    const auto C1 = color (BOLD), C0 = color (BOLD_OFF);
    const Info info = get_info();
    const String origin = info.header ("Origin") + "/";
    const bool localhost_origin = is_localhost (origin, info.lport);
    const bool subproto_ok = (info.subs.size() == 0 && subprotocol_authentication.empty()) ||
                             (info.subs.size() == 1 && subprotocol_authentication == info.subs[0]);
    if (localhost_origin && subproto_ok)
      return 0; // OK
    // log rejection
    String why;
    if (!localhost_origin)      why = "Bad Origin";
    else if (!subproto_ok)      why = "Bad Subprotocol";
    const String ua = info.header ("User-Agent");
    if (logflags_ & 2)
      log (string_format ("%sREJECT:%s  %s:%d/ (%s) - %s", C1, C0, info.remote, info.rport, why, ua));
    return -1; // reject
  }
  void
  opened() override
  {
    using namespace AnsiColors;
    const auto C1 = color (BOLD), C0 = color (BOLD_OFF);
    const Info info = get_info();
    const String ua = info.header ("User-Agent");
    if (logflags_ & 4)
      log (string_format ("%sACCEPT:%s  %s:%d/ - %s", C1, C0, info.remote, info.rport, ua));
  }
  void
  closed() override
  {
    using namespace AnsiColors;
    const auto C1 = color (BOLD), C0 = color (BOLD_OFF);
    if (logflags_ & 4)
      log (string_format ("%sCLOSED%s", C1, C0));
    trigger_destroy_hooks();
  }
  void
  message (const String &message) override
  {
    JsonapiConnectionP conp = std::dynamic_pointer_cast<JsonapiConnection> (shared_from_this());
    assert_return (conp);
    String reply;
    // operator+=() works synchronously
    main_jobs += [&message, &reply, conp, this] () {
      current_message_conection = conp;
      reply = handle_jsonipc (message);
      current_message_conection = nullptr;
    };
    // when queueing asynchronously, we have to use WebSocketConnectionP
    if (!reply.empty())
      send_text (reply);
  }
  String handle_jsonipc (const std::string &message);
  std::vector<JsTrigger> triggers_; // HINT: use unordered_map if this becomes slow
public:
  explicit JsonapiConnection (WebSocketConnection::Internals &internals, int logflags) :
    WebSocketConnection (internals, logflags)
  {}
  ~JsonapiConnection()
  {
    trigger_destroy_hooks();
  }
  JsTrigger
  trigger_lookup (const String &id)
  {
    for (auto it = triggers_.begin(); it != triggers_.end(); it++)
      if (id == it->id())
        return *it;
    return {};
  }
  void
  trigger_remove (const String &id)
  {
    trigger_lookup (id).destroy();
  }
  void
  trigger_create (const String &id)
  {
    using namespace Jsonipc;
    JsonapiConnectionP jsonapi_connection_p = std::dynamic_pointer_cast<JsonapiConnection> (shared_from_this());
    assert_return (jsonapi_connection_p);
    std::weak_ptr<JsonapiConnection> selfw = jsonapi_connection_p;
    const int logflags = logflags_;
    // marshal remote trigger
    auto trigger_remote = [selfw, id, logflags] (ValueS &&args)    // weak_ref avoids cycles
    {
      JsonapiConnectionP selfp = selfw.lock();
      return_unless (selfp);
      const String msg = jsonobject_to_string<Jsonipc::STRICT> ("method", id /*"Jsonapi/Trigger/_%%%"*/, "params", args);
      if (logflags & 8)
        selfp->log (string_format ("⬰ %s", msg));
      selfp->send_text (msg);
    };
    JsTrigger trigger = JsTrigger::create (id, trigger_remote);
    triggers_.push_back (trigger);
    // marshall remote destroy notification and erase triggers_ entry
    auto erase_trigger = [selfw, id, logflags] ()               // weak_ref avoids cycles
    {
      std::shared_ptr<JsonapiConnection> selfp = selfw.lock();
      return_unless (selfp);
      if (selfp->is_open())
        {
          ValueS args { id };
          const String msg = jsonobject_to_string<Jsonipc::STRICT> ("method", "Jsonapi/Trigger/killed", "params", args);
          if (logflags & 8)
            selfp->log (string_format ("↚ %s", msg));
          selfp->send_text (msg);
        }
      Aux::erase_first (selfp->triggers_, [id] (auto &t) { return id == t.id(); });
    };
    trigger.ondestroy (erase_trigger);
  }
  void
  trigger_destroy_hooks()
  {
    std::vector<JsTrigger> old;
    old.swap (triggers_); // speed up erase_trigger() searches
    for (auto &trigger : old)
      trigger.destroy();
    custom_data_destroy();
  }
};

WebSocketConnectionP
jsonapi_make_connection (WebSocketConnection::Internals &internals, int logflags)
{
  return std::make_shared<JsonapiConnection> (internals, logflags);
}

#define ERROR500(WHAT)                                          \
  Jsonipc::bad_invocation (-32500,                              \
                           __FILE__ ":"                         \
                           ASE_CPP_STRINGIFY (__LINE__) ": "    \
                           "Internal Server Error: "            \
                           WHAT)
#define assert_500(c) (__builtin_expect (static_cast<bool> (c), 1) ? (void) 0 : throw ERROR500 (#c) )

static Jsonipc::IpcDispatcher*
make_dispatcher()
{
  using namespace Jsonipc;
  static IpcDispatcher *dispatcher = [] () {
    dispatcher = new IpcDispatcher();
    dispatcher->add_method ("Jsonapi/initialize",
                            [] (CallbackInfo &cbi)
                            {
                              assert_500 (current_message_conection);
                              Server &server = ASE_SERVER;
                              std::shared_ptr<Server> serverp = shared_ptr_cast<Server> (&server);
                              cbi.set_result (to_json (serverp, cbi.allocator()).Move());
                            });
    dispatcher->add_method ("Jsonapi/Trigger/create",
                            [] (CallbackInfo &cbi)
                            {
                              assert_500 (current_message_conection);
                              const String triggerid = cbi.n_args() == 1 ? from_json<String> (cbi.ntharg (0)) : "";
                              if (triggerid.compare (0, 17, "Jsonapi/Trigger/_") != 0)
                                throw Jsonipc::bad_invocation (-32602, "Invalid params");
                              current_message_conection->trigger_create (triggerid);
                            });
    dispatcher->add_method ("Jsonapi/Trigger/remove",
                            [] (CallbackInfo &cbi)
                            {
                              assert_500 (current_message_conection);
                              const String triggerid = cbi.n_args() == 1 ? from_json<String> (cbi.ntharg (0)) : "";
                              if (triggerid.compare (0, 17, "Jsonapi/Trigger/_") != 0)
                                throw Jsonipc::bad_invocation (-32602, "Invalid params");
                              current_message_conection->trigger_remove (triggerid);
                            });
    return dispatcher;
  } ();
  return dispatcher;
}

String
JsonapiConnection::handle_jsonipc (const std::string &message)
{
  if (logflags_ & 8)
    log (string_format ("→ %s", message.size() > 1024 ? message.substr (0, 1020) + "..." + message.back() : message));
  Jsonipc::Scope message_scope (imap_, Jsonipc::Scope::PURGE_TEMPORARIES);
  String reply;
  { // enfore notifies *before* reply (and the corresponding log() messages)
    CoalesceNotifies coalesce_notifies; // coalesce multiple "notify:detail" emissions
    reply = make_dispatcher()->dispatch_message (message);
  } // coalesced notifications occour *here*
  if (logflags_ & 8)
    {
      const char *errorat = strstr (reply.c_str(), "\"error\":{");
      if (errorat && errorat > reply.c_str() && (errorat[-1] == ',' || errorat[-1] == '{'))
        {
          using namespace AnsiColors;
          auto R1 = color (BOLD) + color (FG_RED), R0 = color (FG_DEFAULT) + color (BOLD_OFF);
          log (string_format ("%s←%s %s", R1, R0, reply));
        }
      else
        log (string_format ("← %s", reply.size() > 1024 ? reply.substr (0, 1020) + "..." + reply.back() : reply));
    }
  return reply;
}

// == JsTrigger ==
class JsTrigger::Impl {
  using Func = std::function<void (ValueS)>;
  const String id;
  Func func;
  using VoidFunc = std::function<void()>;
  std::vector<VoidFunc> destroyhooks;
  friend class JsTrigger;
  /*ctor*/ Impl   () = delete;
  /*copy*/ Impl   (const Impl&) = delete;
  Impl& operator= (const Impl&) = delete;
public:
  ~Impl ()
  {
    destroy();
  }
  Impl (const Func &f, const String &_id) :
    id (_id), func (f)
  {}
  void
  destroy ()
  {
    func = nullptr;
    while (!destroyhooks.empty())
      {
        VoidFunc destroyhook = destroyhooks.back();
        destroyhooks.pop_back();
        destroyhook();
      }
  }
};

void
JsTrigger::ondestroy (const VoidFunc &vf)
{
  assert_return (p_);
  if (vf)
    p_->destroyhooks.push_back (vf);
}

void
JsTrigger::call (ValueS &&args) const
{
  assert_return (p_);
  if (p_->func)
    p_->func (std::move (args));
}

JsTrigger
JsTrigger::create (const String &triggerid, const JsTrigger::Impl::Func &f)
{
  JsTrigger trigger;
  trigger.p_ = std::make_shared<JsTrigger::Impl> (f, triggerid);
  assert_return (f != nullptr, trigger);
  return trigger;
}

String
JsTrigger::id () const
{
  return p_ ? p_->id : "";
}

void
JsTrigger::destroy ()
{
  if (p_)
    p_->destroy();
}

JsTrigger::operator bool () const noexcept
{
  return p_ && p_->func;
}

JsTrigger
ConvertJsTrigger::lookup (const String &triggerid)
{
  if (current_message_conection)
    return current_message_conection->trigger_lookup (triggerid);
  assert_return (current_message_conection, {});
  return {};
}

CustomDataContainer*
jsonapi_connection_data ()
{
  if (current_message_conection)
    return current_message_conection.get();
  return nullptr;
}

JsonapiBinarySender
jsonapi_connection_sender ()
{
  return_unless (current_message_conection, {});
  JsonapiConnectionW conw = current_message_conection;
  return [conw] (const String &blob) {
    JsonapiConnectionP conp = conw.lock();
    return conp ? conp->send_binary (blob) : false;
  };
}

} // Ase

#include "testing.hh"

namespace { // Anon

TEST_INTEGRITY (jsonapi_tests);
static void
jsonapi_tests()
{
  using namespace Ase;
  using IdStringMap = std::map<size_t,std::string>;
  IdStringMap tmap;
  for (size_t i = 1; i <= 99; i++)
    tmap[1000 - i] = string_format ("%d", i);
  TASSERT (tmap.size() == 99);
  for (auto it = tmap.begin(), next = it; it != tmap.end() ? ++next, 1 : 0; it = next) // keep next ahead of it, but avoid ++end
    tmap.erase (it);
  TASSERT (tmap.size() == 0);
}

} // Anon
