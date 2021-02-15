// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#ifndef __ASE_CXXAUX_HH__
#define __ASE_CXXAUX_HH__

#include <ase/sysconfig.h>
#include <sys/types.h>                  // uint, ssize
#include <cstdint>                      // uint64_t
#include <functional>
#include <vector>
#include <memory>
#include <mutex>
#include <map>

namespace Ase {

typedef uint32_t uint;                  ///< Provide 'uint' as convenience type.
static_assert (sizeof (uint) == 4, "");

// == type aliases ==
typedef uint8_t         uint8;          ///< An 8-bit unsigned integer.
typedef uint16_t        uint16;         ///< A 16-bit unsigned integer.
typedef uint32_t        uint32;         ///< A 32-bit unsigned integer.
typedef uint64_t        uint64;         ///< A 64-bit unsigned integer, use PRI*64 in format strings.
typedef int8_t          int8;           ///< An 8-bit signed integer.
typedef int16_t         int16;          ///< A 16-bit signed integer.
typedef int32_t         int32;          ///< A 32-bit signed integer.
typedef int64_t         int64;          ///< A 64-bit unsigned integer, use PRI*64 in format strings.
typedef uint32_t        unichar;        ///< A 32-bit unsigned integer used for Unicode characters.
static_assert (sizeof (uint8) == 1 && sizeof (uint16) == 2 && sizeof (uint32) == 4 && sizeof (uint64) == 8, "");
static_assert (sizeof (int8)  == 1 && sizeof (int16)  == 2 && sizeof (int32)  == 4 && sizeof (int64)  == 8, "");
static_assert (sizeof (int) == 4 && sizeof (uint) == 4 && sizeof (unichar) == 4, "");
using   std::map;
using   std::vector;
using   std::void_t;
typedef std::string String;             ///< Convenience alias for std::string.
typedef vector<String> StringVector;    ///< Convenience alias for a std::vector<std::string>.
using StringPair = std::pair<std::string, std::string>;
using VoidF = std::function<void()>;

// == Utility Macros ==
#define ASE_CPP_STRINGIFY(s)    ASE_CPP_STRINGIFY_ (s)                  ///< Convert macro argument into a C const char*.
#define ASE_CPP_STRINGIFY_(s)   #s                                      // Indirection helper, required to expand macros like __LINE__
#define ASE_CPP_PASTE2_(a,b)    a ## b                                  // Indirection helper, required to expand macros like __LINE__
#define ASE_CPP_PASTE2(a,b)     ASE_CPP_PASTE2_ (a,b)                   ///< Paste two macro arguments into one C symbol name
#define ASE_ISLIKELY(expr)      __builtin_expect (bool (expr), 1)       ///< Compiler hint to optimize for @a expr evaluating to true.
#define ASE_UNLIKELY(expr)      __builtin_expect (bool (expr), 0)       ///< Compiler hint to optimize for @a expr evaluating to false.
#define ASE_ABS(a)              ((a) < 0 ? -(a) : (a))                  ///< Yield the absolute value of @a a.
#define ASE_MIN(a,b)            ((a) <= (b) ? (a) : (b))                ///< Yield the smaller value of @a a and @a b.
#define ASE_MAX(a,b)            ((a) >= (b) ? (a) : (b))                ///< Yield the greater value of @a a and @a b.
#define ASE_CLAMP(v,mi,ma)      ((v) < (mi) ? (mi) : ((v) > (ma) ? (ma) : (v))) ///< Yield @a v clamped to [ @a mi .. @a ma ].
#define ASE_ARRAY_SIZE(array)   (sizeof (array) / sizeof ((array)[0]))          ///< Yield the number of C @a array elements.
#define ASE_ALIGN(size, base)   ((base) * ((size_t (size) + (base) - 1) / (base))) ///< Round up @a size to multiples of @a base.
#define ASE_DEPRECATED          __attribute__ ((__deprecated__))


// Ase macro shorthands for <a href="https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html">GCC Attributes</a>.
#define ASE_ALWAYS_INLINE       __attribute__ ((always_inline))
#define ASE_COLD                __attribute__ ((__cold__))
#define ASE_CONST               __attribute__ ((__const__))
#define ASE_CONSTRUCTOR	        __attribute__ ((constructor,used))      // gcc-3.3 also needs "used" to emit code
#define ASE_DEPRECATED          __attribute__ ((__deprecated__))
#define ASE_FORMAT(fx)          __attribute__ ((__format_arg__ (fx)))
#define ASE_HOT                 __attribute__ ((__hot__))
#define ASE_MALLOC              __attribute__ ((__malloc__))
#define ASE_MAY_ALIAS           __attribute__ ((may_alias))
#define ASE_NOINLINE	        __attribute__ ((noinline))
#define ASE_NORETURN            __attribute__ ((__noreturn__))
#define ASE_NO_INSTRUMENT       __attribute__ ((__no_instrument_function__))
#define ASE_PRINTF(fx, ax)      __attribute__ ((__format__ (__printf__, fx, ax)))
#define ASE_PURE                __attribute__ ((__pure__))
#define ASE_SCANF(fx, ax)       __attribute__ ((__format__ (__scanf__, fx, ax)))
#define ASE_SENTINEL            __attribute__ ((__sentinel__))
#define ASE_UNUSED              __attribute__ ((__unused__))
#define ASE_USE_RESULT          __attribute__ ((warn_unused_result))
#define ASE_USED                __attribute__ ((__used__))
#define ASE_WEAK                __attribute__ ((__weak__))

/// Return silently if @a cond does not evaluate to true, with return value @a ...
#define ASE_RETURN_UNLESS(cond, ...)      do { if (ASE_UNLIKELY (!bool (cond))) return __VA_ARGS__; } while (0)

/// Return from the current function if `expr` evaluates to false and issue an assertion warning.
#define ASE_ASSERT_RETURN(expr, ...)     do { if (ASE_ISLIKELY (expr)) break; ::Ase::assertion_failed (#expr); return __VA_ARGS__; } while (0)

/// Return from the current function and issue an assertion warning.
#define ASE_ASSERT_RETURN_UNREACHED(...) do { ::Ase::assertion_failed (""); return __VA_ARGS__; } while (0)

/// Issue an assertion warning if `expr` evaluates to false.
#define ASE_ASSERT_WARN(expr)            do { if (ASE_ISLIKELY (expr)) break; ::Ase::assertion_failed (#expr); } while (0)

/// Delete copy ctor and assignment operator.
#define ASE_CLASS_NON_COPYABLE(ClassName)  \
  /*copy-ctor*/ ClassName  (const ClassName&) = delete; \
  ClassName&    operator=  (const ClassName&) = delete
#ifdef __clang__ // clang++-3.8.0: work around 'variable length array of non-POD element type'
#define ASE_DECLARE_VLA(Type, var, count)          std::vector<Type> var (count)
#else // sane c++
#define ASE_DECLARE_VLA(Type, var, count)          Type var[count] ///< Declare a variable length array (clang++ uses std::vector<>).
#endif

// == Operations on flags enum classes ==
#define ASE_DEFINE_ENUM_EQUALITY(Enum)         \
  constexpr bool    operator== (Enum v, int64_t n) { return int64_t (v) == n; } \
  constexpr bool    operator== (int64_t n, Enum v) { return n == int64_t (v); } \
  constexpr bool    operator!= (Enum v, int64_t n) { return int64_t (v) != n; } \
  constexpr bool    operator!= (int64_t n, Enum v) { return n != int64_t (v); }
#define ASE_DEFINE_FLAGS_ARITHMETIC(Enum)      \
  constexpr int64_t operator>> (Enum v, int64_t n) { return int64_t (v) >> n; } \
  constexpr int64_t operator<< (Enum v, int64_t n) { return int64_t (v) << n; } \
  constexpr int64_t operator^  (Enum v, int64_t n) { return int64_t (v) ^ n; } \
  constexpr int64_t operator^  (int64_t n, Enum v) { return n ^ int64_t (v); } \
  constexpr Enum    operator^  (Enum v, Enum w)    { return Enum (int64_t (v) ^ w); } \
  constexpr int64_t operator|  (Enum v, int64_t n) { return int64_t (v) | n; } \
  constexpr int64_t operator|  (int64_t n, Enum v) { return n | int64_t (v); } \
  constexpr Enum    operator|  (Enum v, Enum w)    { return Enum (int64_t (v) | w); } \
  constexpr int64_t operator&  (Enum v, int64_t n) { return int64_t (v) & n; } \
  constexpr int64_t operator&  (int64_t n, Enum v) { return n & int64_t (v); } \
  constexpr Enum    operator&  (Enum v, Enum w)    { return Enum (int64_t (v) & w); } \
  constexpr int64_t operator~  (Enum v)            { return ~int64_t (v); } \
  constexpr int64_t operator+  (Enum v)            { return +int64_t (v); } \
  constexpr int64_t operator-  (Enum v)            { return -int64_t (v); } \
  constexpr int64_t operator+  (Enum v, int64_t n) { return int64_t (v) + n; } \
  constexpr int64_t operator+  (int64_t n, Enum v) { return n + int64_t (v); } \
  constexpr int64_t operator-  (Enum v, int64_t n) { return int64_t (v) - n; } \
  constexpr int64_t operator-  (int64_t n, Enum v) { return n - int64_t (v); } \
  constexpr int64_t operator*  (Enum v, int64_t n) { return int64_t (v) * n; } \
  constexpr int64_t operator*  (int64_t n, Enum v) { return n * int64_t (v); } \
  constexpr int64_t operator/  (Enum v, int64_t n) { return int64_t (v) / n; } \
  constexpr int64_t operator/  (int64_t n, Enum v) { return n / int64_t (v); } \
  constexpr int64_t operator%  (Enum v, int64_t n) { return int64_t (v) % n; } \
  constexpr int64_t operator%  (int64_t n, Enum v) { return n % int64_t (v); } \
  constexpr Enum&   operator^= (Enum &e, int64_t n) { e = Enum (e ^ n); return e; } \
  constexpr Enum&   operator|= (Enum &e, int64_t n) { e = Enum (e | n); return e; } \
  constexpr Enum&   operator&= (Enum &e, int64_t n) { e = Enum (e & n); return e; } \
  constexpr Enum&   operator+= (Enum &e, int64_t n) { e = Enum (e + n); return e; } \
  constexpr Enum&   operator-= (Enum &e, int64_t n) { e = Enum (e - n); return e; } \
  constexpr Enum&   operator*= (Enum &e, int64_t n) { e = Enum (e * n); return e; } \
  constexpr Enum&   operator/= (Enum &e, int64_t n) { e = Enum (e / n); return e; } \
  constexpr Enum&   operator%= (Enum &e, int64_t n) { e = Enum (e % n); return e; } \
  ASE_DEFINE_ENUM_EQUALITY (Enum)

/// Demangle identifier via libcc.
std::string string_demangle_cxx (const char *mangled_identifier);

/// Provide demangled stringified name for a @a Type.
template<class T> ASE_PURE static inline String
typeid_name()
{
  return string_demangle_cxx (typeid (T).name());
}

/// Common base type to allow casting between polymorphic classes.
struct VirtualBase {
protected:
  virtual ~VirtualBase() = 0;
};
using VirtualBaseP = std::shared_ptr<VirtualBase>;

/// Issue a warning about an assertion error.
void assertion_failed (const std::string &msg = "", const char *file = __builtin_FILE(),
                       int line = __builtin_LINE(), const char *func = __builtin_FUNCTION());

/// Test string equality at compile time.
extern inline constexpr bool
constexpr_equals (const char *a, const char *b, size_t n)
{
  return n == 0 || (a[0] == b[0] && (a[0] == 0 || constexpr_equals (a + 1, b + 1, n - 1)));
}

/// Call inplace new operator by automatically inferring the Type.
template<class Type, class ...Ts> __attribute__ ((always_inline)) inline void
new_inplace (Type &typemem, Ts &&... args)
{
  new (&typemem) Type (std::forward<Ts> (args)...);
}

/// Call inplace delete operator by automatically inferring the Type.
template<class Type> __attribute__ ((always_inline)) inline void
delete_inplace (Type &typemem)
{
  typemem.~Type();
}

/// REQUIRES<value> - Simplified version of std::enable_if<cond,bool>::type to use SFINAE in function templates.
template<bool value> using REQUIRES = typename ::std::enable_if<value, bool>::type;

/// REQUIRESv<value> - Simplified version of std::enable_if<cond,void>::type to use SFINAE in struct templates.
template<bool value> using REQUIRESv = typename ::std::enable_if<value, void>::type;

/**
 * A std::make_shared<>() wrapper class to access private ctor & dtor.
 * To call std::make_shared<T>() on a class @a T, its constructor and
 * destructor must be public. For classes with private or protected
 * constructor or destructor, this class can be used as follows:
 * @code{.cc}
 * class Type {
 *   Type (ctor_args...);                // Private ctor.
 *   friend class FriendAllocator<Type>; // Allow access to ctor/dtor of Type.
 * };
 * std::shared_ptr<Type> t = FriendAllocator<Type>::make_shared (ctor_args...);
 * @endcode
 */
template<class T>
class FriendAllocator : public std::allocator<T> {
public:
  /// Construct type @a C object, standard allocator requirement.
  template<typename C, typename... Args> static inline void
  construct (C *p, Args &&... args)
  {
    ::new ((void*) p) C (std::forward<Args> (args)...);
  }
  /// Delete type @a C object, standard allocator requirement.
  template<typename C> static inline void
  destroy (C *p)
  {
    p->~C ();
  }
  /**
   * Construct an object of type @a T that is wrapped into a std::shared_ptr<T>.
   * @param args        The list of arguments to pass into a T() constructor.
   * @return            A std::shared_ptr<T> owning the newly created object.
   */
  template<typename ...Args> static inline std::shared_ptr<T>
  make_shared (Args &&... args)
  {
    return std::allocate_shared<T> (FriendAllocator(), std::forward<Args> (args)...);
  }
};

/** Shorthand for std::dynamic_pointer_cast<>(shared_from_this()).
 * A shared_ptr_cast() takes a std::shared_ptr or a pointer to an @a object that
 * supports std::enable_shared_from_this::shared_from_this().
 * Using std::dynamic_pointer_cast(), the shared_ptr passed in (or retrieved via
 * calling shared_from_this()) is cast into a std::shared_ptr<@a Target>, possibly
 * resulting in an empty (NULL) std::shared_ptr if the underlying dynamic_cast()
 * was not successful or if a NULL @a object was passed in.
 * Note that shared_from_this() can throw a std::bad_weak_ptr exception if
 * the object has no associated std::shared_ptr (usually during ctor and dtor), in
 * which case the exception will also be thrown from shared_ptr_cast<Target>().
 * However a shared_ptr_cast<Target*>() call will not throw and yield an empty
 * (NULL) std::shared_ptr<@a Target>. This is analogous to dynamic_cast<T@amp> which
 * throws, versus dynamic_cast<T*> which yields NULL.
 * @return A std::shared_ptr<@a Target> storing a pointer to @a object or NULL.
 * @throws std::bad_weak_ptr if shared_from_this() throws, unless the @a Target* form is used.
 */
template<class Target, class Source> std::shared_ptr<typename std::remove_pointer<Target>::type>
shared_ptr_cast (Source *object)
{
  if (!object)
    return nullptr;
  // construct shared_ptr if possible
  typedef decltype (object->shared_from_this()) ObjectP;
  ObjectP sptr;
  if (std::is_pointer<Target>::value)
    try {
      sptr = object->shared_from_this();
    } catch (const std::bad_weak_ptr&) {
      return nullptr;
    }
  else // for non-pointers, allow bad_weak_ptr exceptions
    sptr = object->shared_from_this();
  // cast into target shared_ptr<> type
  return std::dynamic_pointer_cast<typename std::remove_pointer<Target>::type> (sptr);
}
/// See shared_ptr_cast(Source*).
template<class Target, class Source> const std::shared_ptr<typename std::remove_pointer<Target>::type>
shared_ptr_cast (const Source *object)
{
  return shared_ptr_cast<Target> (const_cast<Source*> (object));
}
/// See shared_ptr_cast(Source*).
template<class Target, class Source> std::shared_ptr<typename std::remove_pointer<Target>::type>
shared_ptr_cast (std::shared_ptr<Source> &sptr)
{
  return std::dynamic_pointer_cast<typename std::remove_pointer<Target>::type> (sptr);
}
/// See shared_ptr_cast(Source*).
template<class Target, class Source> const std::shared_ptr<typename std::remove_pointer<Target>::type>
shared_ptr_cast (const std::shared_ptr<Source> &sptr)
{
  return shared_ptr_cast<Target> (const_cast<std::shared_ptr<Source>&> (sptr));
}

/// Fetch `shared_ptr` from `wptr` and create `C` with `ctor` if needed.
template<class C> std::shared_ptr<C>
weak_ptr_fetch_or_create (std::weak_ptr<C> &wptr, const std::function<std::shared_ptr<C>()> &ctor)
{
  std::shared_ptr<C> cptr = wptr.lock();
  if (__builtin_expect (!!cptr, true))
    return cptr; // fast path
  std::shared_ptr<C> nptr = ctor();
  { // C++20 has: std::atomic<std::weak_ptr<C>>::compare_exchange
    static std::mutex mutex;
    std::lock_guard<std::mutex> locker (mutex);
    cptr = wptr.lock();
    if (!cptr)
      wptr = cptr = nptr;
  }
  return cptr;
}

/// Create an instance of `Class` on demand that is constructed and never destructed.
/// Due to its constexpr ctor and on-demand creation of `Class`, a Persistent<>
/// can be accessed at any time during the static ctor (or dtor) phases and will always yield
/// a properly initialized `Class`.
template<class Class>
class Persistent final {
  static_assert (std::is_class<Class>::value, "Persistent<Class> requires class template argument");
  uint64 mem_[(sizeof (Class) + sizeof (uint64) - 1) / sizeof (uint64)] = { 0, };
  Class *ptr_ = nullptr;
  void
  initialize() ASE_NOINLINE
  {
    static std::mutex mtx;
    std::unique_lock<std::mutex> lock (mtx);
    if (ptr_ == nullptr)
      ptr_ = new (mem_) Class(); // exclusive construction
  }
public:
  /// A constexpr constructor avoids the static initialization order fiasco
  constexpr Persistent    () noexcept {}
  /// Check if `this` stores a `Class` instance yet.
  explicit  operator bool () const ASE_PURE  { return ptr_ != nullptr; }
  /// Retrieve reference to `Class` instance, always returns the same reference.
  Class&    operator*     () ASE_PURE        { return *operator->(); }
  /// Retrieve pointer to `Class` instance, always returns the same pointer.
  Class*    operator->    () ASE_PURE
  {
    if (ASE_UNLIKELY (ptr_ == nullptr))
      initialize();
    return ptr_;
  }
};

} // Ase

#endif // __ASE_CXXAUX_HH__
