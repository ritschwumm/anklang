// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#ifndef __ASE_TESTING_HH__
#define __ASE_TESTING_HH__

#include <ase/utils.hh>

namespace Ase {
namespace Test {

// Test Macros
#define TSTART(...)             Ase::Test::test_output ('S', ::Ase::string_format (__VA_ARGS__))  ///< Print message once a test case starts.
#define TDONE()                 Ase::Test::test_output ('D', "")                                  ///< Print message for test case end.
#define TPASS(...)              Ase::Test::test_output ('P', ::Ase::string_format (__VA_ARGS__))  ///< Print a message about a passing test.
#define TBENCH(...)             Ase::Test::test_output ('B', ::Ase::string_format (__VA_ARGS__))  ///< Print a message about a passing benchmark.
#define TNOTE(...)              Ase::Test::test_output ('I', ::Ase::string_format (__VA_ARGS__))  ///< Print a message from a test program.
#define TCHECK(cond, ...)       Ase::Test::test_output (bool (cond) ? 'P' : 'F', \
                                                        ::Ase::string_format (__VA_ARGS__))       ///< Verbose assertion, calls TPASS() on success.
#define TCMPS(a,cmp,b)          TCMP_op (a,cmp,b,#a,#b,Ase::Test::_as_strptr)                     ///< Variant of TCMP() for C strings.
#define TCMP(a,cmp,b)           TCMP_op (a,cmp,b,#a,#b,)        ///< Compare @a a and @a b according to operator @a cmp, verbose on failiure.
#define TASSERT(cond)           TASSERT__AT (__LINE__, cond)    ///< Unconditional test assertion, enters breakpoint if not fullfilled.
#define TASSERT_AT(LINE, cond)  TASSERT__AT (LINE, cond)        ///< Unconditional test assertion for deputy __LINE__.
#define TOK()                   do {} while (0)                 ///< Deprecated progress indicator, tests generally need to run fast.

/// Register a function to run as part of the unit test suite.
#define TEST_ADD(fun)           static const ::Ase::Test::TestChain ASE_CPP_PASTE2 (__Ase__Test__TestChain_, __LINE__) (fun, ASE_CPP_STRINGIFY (fun), ::Ase::Test::PLAIN)
#define TEST_SLOW(fun)          static const ::Ase::Test::TestChain ASE_CPP_PASTE2 (__Ase__Test__TestChain_, __LINE__) (fun, ASE_CPP_STRINGIFY (fun), ::Ase::Test::SLOW)
#define TEST_BENCH(fun)         static const ::Ase::Test::TestChain ASE_CPP_PASTE2 (__Ase__Test__TestChain_, __LINE__) (fun, ASE_CPP_STRINGIFY (fun), ::Ase::Test::BENCH)
#define TEST_BROKEN(fun)        static const ::Ase::Test::TestChain ASE_CPP_PASTE2 (__Ase__Test__TestChain_, __LINE__) (fun, ASE_CPP_STRINGIFY (fun), ::Ase::Test::BROKEN)

/** Class for profiling benchmark tests.
 * UseCase: Benchmarking function implementations, e.g. to compare sorting implementations.
 */
class Timer {
  const double   deadline_;
  std::vector<double> samples_;
  double         test_duration_;
  int64          n_reps_;
  int64          loops_needed ();
  void           reset        ();
  void           submit       (double elapsed, int64 repetitions);
  static double  bench_time   ();
public:
  /// Create a Timer() instance, specifying an optional upper bound for test durations.
  explicit       Timer        (double deadline_in_secs = 0);
  virtual       ~Timer        ();
  int64          n_reps       () const { return n_reps_; }             ///< Number of benchmark repetitions to execute
  double         test_elapsed () const { return test_duration_; }      ///< Seconds spent in benchmark()
  double         min_elapsed  () const;         ///< Minimum time benchmarked for a @a callee() call.
  double         max_elapsed  () const;         ///< Maximum time benchmarked for a @a callee() call.
  /**
   * @param callee        A callable function or object.
   * Method to benchmark the execution time of @a callee.
   * @returns Minimum runtime in seconds,
   */
  template<typename Callee>
  double         benchmark    (Callee callee);
};

// == test array ==
enum Kind {
  PLAIN         = 0,
  SLOW          = 1,
  BENCH         = 2,
  BROKEN        = 4,
  INTEGRITY     = 8,
};
struct TestEntry {
  std::string ident;
  int64       flags = 0;
  TestEntry (const std::string &identi = "", int64 flagsi = 0) :
    ident (identi), flags (flagsi)
  {}
};
using TestEntries = std::vector<TestEntry>;
TestEntries list_tests ();
int         run_test   (const std::string &test_identifier);

// === test maintenance ===
int     run             ();     ///< Run all registered tests.
int     run             (const StringS &test_names);     ///< Run named tests.
bool    verbose         ();     ///< Indicates whether tests should run verbosely.
bool    slow            ();     ///< Indicates whether slow tests should be run.
void    test_output     (int kind, const String &string);

/// == Stringify Args ==
inline String                   stringify_arg  (const char   *a, const char *str_a) { return a ? string_to_cquote (a) : "(__null)"; }
template<class V> inline String stringify_arg  (const V      *a, const char *str_a) { return string_format ("%p", a); }
template<class A> inline String stringify_arg  (const A      &a, const char *str_a) { return str_a; }
template<> inline String stringify_arg<float>  (const float  &a, const char *str_a) { return string_format ("%.8g", a); }
template<> inline String stringify_arg<double> (const double &a, const char *str_a) { return string_format ("%.17g", a); }
template<> inline String stringify_arg<bool>   (const bool   &a, const char *str_a) { return string_format ("%u", a); }
template<> inline String stringify_arg<int8>   (const int8   &a, const char *str_a) { return string_format ("%d", a); }
template<> inline String stringify_arg<int16>  (const int16  &a, const char *str_a) { return string_format ("%d", a); }
template<> inline String stringify_arg<int32>  (const int32  &a, const char *str_a) { return string_format ("%d", a); }
template<> inline String stringify_arg<int64>  (const int64  &a, const char *str_a) { return string_format ("%lld", a); }
template<> inline String stringify_arg<uint8>  (const uint8  &a, const char *str_a) { return string_format ("0x%02x", a); }
template<> inline String stringify_arg<uint16> (const uint16 &a, const char *str_a) { return string_format ("0x%04x", a); }
template<> inline String stringify_arg<uint32> (const uint32 &a, const char *str_a) { return string_format ("0x%08x", a); }
template<> inline String stringify_arg<uint64> (const uint64 &a, const char *str_a) { return string_format ("0x%08Lx", a); }
template<> inline String stringify_arg<String> (const String &a, const char *str_a) { return string_to_cquote (a); }
inline const char* _as_strptr (const char *s) { return s; } // implementation detail

// == Deterministic random numbers for tests ===
uint64_t random_int64           ();                                     ///< Return random int for reproduceble tests.
int64_t  random_irange          (int64_t begin, int64_t end);           ///< Return random int within range for reproduceble tests.
double   random_float           ();                                     ///< Return random double for reproduceble tests.
double   random_frange          (double begin, double end);             ///< Return random double within range for reproduceble tests.


// == Implementations ==
/// @cond

#define TASSERT__AT(LINE, cond)                 \
  do { if (ASE_ISLIKELY (cond)) break;          \
    ::Ase::assertion_failed (#cond); } while (0)

#define TCMP_op(a,cmp,b,sa,sb,cast)                                     \
  do {                                                                  \
    const auto &__tcmp_va = a ;                                         \
    const auto &__tcmp_vb = b ;                                         \
    if (__tcmp_va cmp __tcmp_vb) break;                                 \
    Ase::String __tcmp_sa = Ase::Test::stringify_arg (cast (__tcmp_va), #a); \
    Ase::String __tcmp_sb = Ase::Test::stringify_arg (cast (__tcmp_vb), #b), \
                __tcmp_as = Ase::string_format ("'%s %s %s': %s %s %s", \
                                                sa, #cmp, sb,           \
                                                __tcmp_sa.c_str(),      \
                                                #cmp,                   \
                                                __tcmp_sb.c_str());     \
    ::Ase::assertion_failed (__tcmp_as.c_str());                        \
  } while (0)

template<typename Callee> double
Timer::benchmark (Callee callee)
{
  reset();
  for (int64 runs = loops_needed(); runs; runs = loops_needed())
    {
      int64 n = runs;
      const double start = bench_time();
      while (ASE_ISLIKELY (n--))
        callee();
      const double stop = bench_time();
      submit (stop - start, runs);
    }
  return min_elapsed();
}

class TestChain {
public:
  explicit         TestChain (std::function<void()> tfunc, const std::string &tname, Kind kind);
  static void      run       (ptrdiff_t internal_token, const StringS *test_names);
  const TestChain* next      () const { return next_; }
  std::string      name      () const { return name_; }
  int64            flags     () const { return kind_; }
  void             run       () const { func_(); }
private:
  const std::string     name_;
  std::function<void()> func_;
  const TestChain      *const next_ = NULL;
  Kind                  kind_ = PLAIN;
};

/// @endcond

} // Test
} // Ase

#endif /* __ASE_TESTING_HH__ */
