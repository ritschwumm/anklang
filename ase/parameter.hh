// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#ifndef __ASE_PARAMETER_HH__
#define __ASE_PARAMETER_HH__

#include <ase/api.hh>
#include <ase/object.hh> // EmittableImpl
#include <ase/memory.hh>

namespace Ase {

/// Min, max, stepping for double ranges.
using MinMaxStep = std::tuple<double,double,double>;
using ChoicesFunc = std::function<ChoiceS(const CString&)>;

/// Structured initializer for Parameter
struct Param {
  using InitialVal = std::variant<bool,int8_t,uint8_t,int16_t,uint16_t,int32_t,uint32_t,int64_t,uint64_t,float,double,const char*,std::string>;
  struct ExtraVals : std::variant<MinMaxStep,ChoiceS,ChoicesFunc> {
    ExtraVals () = default;
    ExtraVals (const MinMaxStep&);
    ExtraVals (const std::initializer_list<Choice>&);
    ExtraVals (const ChoiceS&);
    ExtraVals (const ChoicesFunc&);
  };
  String     label;       ///< Preferred user interface name.
  String     nick;        ///< Abbreviated user interface name, usually not more than 6 characters.
  InitialVal initial = 0; ///< Initial value for float, int, choice types.
  String     unit;        ///< Units of the values within range.
  ExtraVals  extras;      ///< Min, max, stepping for double ranges or array of choices to select from.
  String     hints;       ///< Hints for parameter handling.
  String     blurb;       ///< Short description for user interface tooltips.
  String     descr;       ///< Elaborate description for help dialogs.
  String     group;       ///< Group for parameters of similar function.
  String     ident;       ///< Identifier used for serialization (can be derived from untranslated label).
  StringS    details;     ///< Array of "key=value" pairs.
  static inline const String STORAGE = ":r:w:S:";
  static inline const String STANDARD = ":r:w:S:G:";
};

/// Structure to provide information about properties or preferences.
struct Parameter {
  CString       cident;
  bool          has         (const String &key) const;
  String        fetch       (const String &key) const;
  void          store       (const String &key, const String &value);
  String        ident       () const   { return cident; }
  String        label       () const   { return fetch ("label"); }
  String        nick        () const;
  String        unit        () const   { return fetch ("unit"); }
  String        hints       () const   { return fetch ("hints"); }
  String        blurb       () const   { return fetch ("blurb"); }
  String        descr       () const   { return fetch ("descr"); }
  String        group       () const   { return fetch ("group"); }
  Value         initial     () const   { return initial_; }
  bool          has_hint    (const String &hint) const;
  ChoiceS       choices     () const;
  MinMaxStep    range       () const;   ///< Min, max, stepping for double ranges.
  bool          is_numeric  () const;
  bool          is_choice   () const   { return has_hint ("choice"); }
  bool          is_text     () const   { return has_hint ("text"); }
  double        normalize   (double val) const;
  double        rescale     (double t) const;
  Value         constrain   (const Value &value) const;
  void          initialsync (const Value &v);
  /*ctor*/      Parameter   () = default;
  /*ctor*/      Parameter   (const Param&);
  /*copy*/      Parameter   (const Parameter&) = default;
  Parameter&    operator=   (const Parameter&) = default;
  // helpers
  String    value_to_text   (const Value &value) const;
  Value     value_from_text (const String &text) const;
private:
  using ExtrasV = std::variant<MinMaxStep,ChoiceS,ChoicesFunc>;
  StringS       details_;
  ExtrasV       extras_;
  Value         initial_ = 0;
};
using ParameterC = std::shared_ptr<const Parameter>;

/// Abstract base type for Property implementations with Parameter meta data.
class ParameterProperty : public EmittableImpl, public virtual Property {
protected:
  ParameterC parameter_;
public:
  String     identifier     () override           { return parameter_->cident; }
  String     label          () override           { return parameter_->label(); }
  String     nick           () override	          { return parameter_->nick(); }
  String     unit           () override	          { return parameter_->unit(); }
  String     hints          () override	          { return parameter_->hints(); }
  String     blurb          () override	          { return parameter_->blurb(); }
  String     descr          () override	          { return parameter_->descr(); }
  String     group          () override	          { return parameter_->group(); }
  double     get_min        () override	          { return std::get<0> (parameter_->range()); }
  double     get_max        () override	          { return std::get<1> (parameter_->range()); }
  double     get_step       () override	          { return std::get<2> (parameter_->range()); }
  bool       is_numeric     () override	          { return parameter_->is_numeric(); }
  ChoiceS    choices        () override           { return parameter_->choices(); }
  void       reset          () override	          { set_value (parameter_->initial()); }
  double     get_normalized () override	          { return !is_numeric() ? 0 : parameter_->normalize (get_double()); }
  bool       set_normalized (double v) override   { return is_numeric() && set_value (parameter_->rescale (v)); }
  String     get_text       () override           { return parameter_->value_to_text (get_value()); }
  bool       set_text       (String txt) override { set_value (parameter_->value_from_text (txt)); return !txt.empty(); }
  Value      get_value      () override = 0;
  bool       set_value      (const Value &v) override = 0;
  double     get_double     ()                    { return !is_numeric() ? 0 : get_value().as_double(); }
  ParameterC parameter      () const              { return parameter_; }
  Value      initial        () const              { return parameter_->initial(); }
  MinMaxStep range          () const              { return parameter_->range(); }
};

/// Find a suitable 3-letter abbreviation for a Parameter without nick.
String parameter_guess_nick (const String &parameter_label);

} // Ase

#endif // __ASE_PARAMETER_HH__
