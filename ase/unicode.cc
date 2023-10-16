// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "unicode.hh"
#include "blob.hh"

#include <glib.h>

namespace Ase {

/// Decode valid UTF-8 sequences, invalid sequences are treated as Latin-1 characters.
template<bool CODEPOINT> static inline size_t // returns length of unicode char
utf8character (const char *str, uint32_t *unicode)
{
  /* https://en.wikipedia.org/wiki/UTF-8
    : 0000 0001 0010 0011 0100 0101 0110 0111 1000 1001 1010 1011 1100 1101 1110 1111
    :  000  001  002  003  004  005  006  007  010  011  012  013  014  015  016  017
    :    0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
    :  0x0  0x1  0x2  0x3  0x4  0x5  0x6  0x7  0x8  0x9  0xA  0xB  0xC  0xD  0xE  0xF
   */
  const uint8_t c = str[0];
  // optimized for one-byte sequences
  if (__builtin_expect (c < 0xc0, true))
    {
      if (CODEPOINT)
        *unicode = c;                                   // valid if c <= 0x7F
      return 1;                                         // treat as Latin-1 otherwise
    }
  // multi-byte sequences
  switch (c & 0xF8)
    {
      uint8_t d, e, f;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8:         // 2-byte sequence
      d = str[1];
      if (__builtin_expect ((d & 0xC0) != 0x80, false))
        goto one_byte;
      if (CODEPOINT)
        *unicode = ((c & 0x1f) << 6) + (d & 0x3f);
      return 2;                                         // valid
    case 0xE0: case 0xE8:                               // 3-byte sequence
      d = str[1];
      if (__builtin_expect ((d & 0xC0) != 0x80, false))
        goto one_byte;
      e = str[2];
      if (__builtin_expect ((e & 0xC0) != 0x80, false))
        goto one_byte;
      if (CODEPOINT)
        *unicode = ((c & 0x0f) << 12) + ((d & 0x3f) << 6) + (e & 0x3f);
      return 3;                                         // valid
    case 0xF0:                                          // 4-byte sequence
      d = str[1];
      if (__builtin_expect ((d & 0xC0) != 0x80, false))
        goto one_byte;
      e = str[2];
      if (__builtin_expect ((e & 0xC0) != 0x80, false))
        goto one_byte;
      f = str[3];
      if (__builtin_expect ((f & 0xC0) != 0x80, false))
        goto one_byte;
      if (CODEPOINT)
        *unicode = ((c & 0x07) << 18) + ((d & 0x3f) << 12) + ((e & 0x3f) << 6) + (f & 0x3f);
      return 4;                                         // valid
    default: one_byte:
      if (CODEPOINT)
        *unicode = c;                                   // treat as Latin-1 otherwise
      return 1;
    }
}

/// Returns length of unicode character in bytes
static inline size_t
utf8codepoint (const char *str, uint32_t *unicode)
{
  return utf8character<true> (str, unicode);
}

/// Returns length of unicode character in bytes
static inline size_t
utf8skip (const char *str)
{
  return utf8character<false> (str, NULL);
}

/// Count valid UTF-8 sequences, invalid sequences are counted as Latin-1 characters.
size_t
utf8len (const char *str)
{
  size_t l;
  for (l = 0; __builtin_expect (*str != 0, true); l++)
    str += utf8skip (str);
  return l;
}

/// Count valid UTF-8 sequences, invalid sequences are counted as Latin-1 characters.
size_t
utf8len (const std::string &str)
{
  const char *c = str.data(), *e = c + str.size();
  size_t l = 0;
  while (c < e)
    {
      c += utf8skip (c);
      l += 1;
    }
  return l;
}

/// Convert valid UTF-8 sequences to Unicode codepoints, invalid sequences are treated as Latin-1 characters.
/// The array @a codepoints must be able to hold at least as many elements as are characters stored in @a str.
/// Returns the number of codepoints stored in @a codepoints.
size_t
utf8_to_unicode (const char *str, uint32_t *codepoints)
{
  // assuming sizeof codepoints[] >= sizeof str[]
  size_t l;
  for (l = 0; __builtin_expect (*str != 0, true); l++)
    str += utf8codepoint (str, &codepoints[l]);
  return l;
}

/// Convert valid UTF-8 sequences to Unicode codepoints, invalid sequences are treated as Latin-1 characters.
/// Returns the number of codepoints newly stored in @a codepoints.
size_t
utf8_to_unicode (const std::string &str, std::vector<uint32_t> &codepoints)
{
  const size_t l = codepoints.size();
  if (l >= 8)
    codepoints.reserve (codepoints.size() + l / 4);
  const char *c = str.data(), *const e = c + str.size();
  while (c < e)
    {
      uint32_t codepoint;
      c += utf8codepoint (c, &codepoint);
      codepoints.push_back (codepoint);
    }
  return codepoints.size() - l;
}

/// Convert @a codepoints into an UTF-8 string, using the shortest possible encoding.
std::string
string_from_unicode (const uint32_t *codepoints, size_t n_codepoints)
{
  std::string str;
  str.reserve (n_codepoints);
  for (size_t i = 0; i < n_codepoints; i++)
    {
      const uint32_t u = codepoints[i];
      if (__builtin_expect (u <= 0x7F, true))
        {
          str.push_back (u);
          continue;
        }
      switch (u)
        {
        case 0x00000080 ... 0x000007FF:
          str.push_back (0xC0 + (u >> 6));
          str.push_back (0x80 + (u & 0x3F));
          break;
        case 0x00000800 ... 0x0000FFFF:
          str.push_back (0xE0 +  (u >> 12));
          str.push_back (0x80 + ((u >>  6) & 0x3F));
          str.push_back (0x80 +  (u & 0x3F));
          break;
        case 0x00010000 ... 0x0010FFFF:
          str.push_back (0xF0 +  (u >> 18));
          str.push_back (0x80 + ((u >> 12) & 0x3F));
          str.push_back (0x80 + ((u >>  6) & 0x3F));
          str.push_back (0x80 +  (u & 0x3F));
          break;
        default:
          break;
        }
    }
  return str;
}

/// Convert @a codepoints into an UTF-8 string, using the shortest possible encoding.
std::string
string_from_unicode (const std::vector<uint32_t> &codepoints)
{
  return string_from_unicode (codepoints.data(), codepoints.size());
}

/** Check `c` to be a NameStartChar, according to the QName EBNF.
 * See https://en.wikipedia.org/wiki/QName
 */
static bool
codepoint_is_namestartchar (uint32_t c)
{
  const bool ok =
    std::isalpha (c) || c == '_' ||
    (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) ||
    (c >= 0xF8 && c <= 0x2FF) || (c >= 0x370 && c <= 0x37D) || (c >= 0x37F && c <= 0x1FFF) ||
    (c >= 0x200C && c <= 0x200D) || (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) ||
    (c >= 0x3001 && c <= 0xD7FF) || (c >= 0xF900 && c <= 0xFDCF) || (c >= 0xFDF0 && c <= 0xFFFD) ||
    (c >= 0x10000 && c <= 0xEFFFF);
  return ok;
}

/** Check `c` to be a NameChar, according to the QName EBNF.
 * See https://en.wikipedia.org/wiki/QName
 */
static bool
codepoint_is_ncname (uint32_t c)
{
  const bool ok =
    codepoint_is_namestartchar (c) ||
    c == '-' || c == '.' || (c >= '0' && c <= '9') ||
    c == 0xB7 || (c >= 0x0300 && c <= 0x036F) || (c >= 0x203F && c <= 0x2040);
    return ok;
}

/** Check `input` to be a NCName, according to the QName EBNF.
 * See https://en.wikipedia.org/wiki/QName
 */
bool
string_is_ncname (const String &input)
{
  std::vector<uint32_t> tmp;
  utf8_to_unicode (input, tmp);
  for (auto c : tmp)
    if (!codepoint_is_ncname (c))
      return false;
  return true;
}

/** Convert `input` to a NCName, according to the QName EBNF.
 * See https://en.wikipedia.org/wiki/QName
 */
String
string_to_ncname (const String &input, uint32_t substitute)
{
  std::vector<uint32_t> ucstring;
  utf8_to_unicode (input, ucstring);
  for (auto it = ucstring.begin(); it != ucstring.end(); /**/)
    if (!codepoint_is_ncname (*it)) {
      if (substitute)
        *it++ = substitute;
      else
        it = ucstring.erase (it);
    } else
      ++it;
  if (!ucstring.empty() && !codepoint_is_namestartchar (ucstring[0]))
    ucstring.insert (ucstring.begin(), '_');
  return string_from_unicode (ucstring);
}

} // Ase

// == Testing ==
#include "testing.hh"
#include "internal.hh"

namespace { // Anon
using namespace Ase;

TEST_INTEGRITY (unicode_tests);
static void
unicode_tests()
{
  Blob b = Blob::from_file ("/etc/mailcap");
  const std::string str = b.string();
  size_t ase_utf8len, glib_utf8len;
  ase_utf8len = utf8len (str.c_str());
  glib_utf8len = g_utf8_strlen (str.c_str(), -1);
  TCMP (ase_utf8len, ==, glib_utf8len);
  std::vector<uint32_t> codepoints;
  size_t nc = 0, cc = 0, pc = 0;
  for (size_t i = 0; i <= unicode_last_codepoint; i++)
    {
      TASSERT (unicode_is_assigned (i) <= unicode_is_valid (i));
      TASSERT (unicode_is_noncharacter (i) != unicode_is_character (i));
      nc += unicode_is_noncharacter (i);
      cc += unicode_is_control_code (i);
      pc += unicode_is_private (i);
      if (i && unicode_is_assigned (i))
        codepoints.push_back (i);
    }
  TASSERT (nc == 66);
  TASSERT (cc == 65);
  TASSERT (pc == 6400 + 65534 + 65534);
  std::string big = string_from_unicode (codepoints);
  ase_utf8len = utf8len (big.c_str());
  glib_utf8len = g_utf8_strlen (big.c_str(), -1);
  TCMP (ase_utf8len, ==, glib_utf8len);
  TCMP (ase_utf8len, ==, codepoints.size());
  if (true)
    {
      std::vector<uint32_t> tmp;
      const size_t tmp_result = utf8_to_unicode (big, tmp);
      TASSERT (tmp_result == tmp.size() && codepoints.size() == tmp_result);
      for (size_t i = 0; i < codepoints.size(); ++i)
        TASSERT (tmp[i] == codepoints[i]);
    }
  TCMP (false, ==, string_is_ncname ("0abc@def^foo"));
  TCMP ("_0abcdeffoo", ==, string_to_ncname ("0abc@def^foo"));
  TCMP ("abc_def_foo", ==, string_to_ncname ("abc@def^foo", '_'));
  TCMP (true, ==, string_is_ncname ("_0abc_def_foo"));
}

} // Anon
