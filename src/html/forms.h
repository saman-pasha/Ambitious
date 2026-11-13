/* src/html/forms.h -- HTML's form controls over the DOM (HTML 4.10), the one account of them
   that the parser, the scripts (src/script/), the cascade, layout and accessibility share
   (src/html/forms.c): an input's type, a control's form owner and disabled state, an input's
   checkedness and its radio button group, a value as HTML's value sanitization leaves it,
   and constraint validation (4.10.20): a control's validity flags, whether it is a candidate
   for it, and Chromium's English message for what it suffers from. A unit that includes this
   has said amb_interface(dom) first, and includes src/dom/dom.h.

   A function that takes a vector is declared in each unit that calls it, not here (a
   prototype after a vector<T> in a header is not registered: a toolchain finding):
     void form_sanitize(const dom_document *d, uint32_t x, const char *raw, size_t n, vector<char> *out);
     void form_options(const dom_document *d, uint32_t s, vector<uint32_t> *opts, vector<uint8_t> *how);
     void form_radios_missing(const dom_document *d, vector<uint8_t> *miss);
     void form_message(const dom_document *d, uint32_t x, uint32_t flags, const char *v, size_t n, vector<char> *out); */
#ifndef AMB_HTML_FORMS_H
#define AMB_HTML_FORMS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* an input's type, its type attribute's state (a value it does not know is text) */
enum { FORM_TEXT, FORM_HIDDEN, FORM_CHECKBOX, FORM_RADIO, FORM_RANGE, FORM_NUMBER, FORM_BUTTON, FORM_SUBMIT, FORM_RESET, FORM_IMAGE,
       FORM_SEARCH, FORM_EMAIL, FORM_TEL, FORM_URL, FORM_PASSWORD, FORM_DATE, FORM_MONTH, FORM_WEEK, FORM_TIME, FORM_DTL, FORM_COLOR,
       FORM_FILE };
/* x's type, x an input element */
int form_input_type(const dom_document *d, uint32_t x);
/* a type of text a user types: text, search, email, tel, url, password */
int form_texty(int ty);
/* x is an HTML element of that local name */
int form_is(const dom_document *d, uint32_t x, const char *name);
/* x's form owner (HTML 4.10.17.3): its form attribute's form in its tree, else its nearest
   form; 0 for none */
uint32_t form_owner(const dom_document *d, uint32_t x);
/* x is disabled (4.10.18.5): its disabled attribute, or a disabled fieldset's, but in that
   fieldset's first legend */
int form_disabled(const dom_document *d, uint32_t x);
/* x is a candidate for constraint validation, willValidate (4.10.20.1): a submittable element
   not barred (disabled, readonly, in a datalist, a hidden, reset, button or image input, a
   button that is no submit button) */
int form_will_validate(const dom_document *d, uint32_t x);

/* ---- checkedness (4.10.5.4) and radio button groups (4.10.5.1.18), as Chromium keeps them:
   a change of an input's checkedness but by its checked attribute (a click, the IDL attribute,
   its group, a reset) sets its dirty checkedness flag, and a radio button checked unchecks the
   one its group had. A radio is in a group when it has a name and a form owner or is
   connected: the radios of its name and form owner, or, without one, of its tree ---- */
int form_checked(const dom_document *d, uint32_t x);
/* x's checkedness set (Chromium's SetChecked), its group's other button unchecked */
void form_set_checked(dom_document *d, uint32_t x, int on);
/* the checked radio button of x's group other than x (0 for none) */
uint32_t form_group_checked(const dom_document *d, uint32_t x);
/* an attribute of the input x changed (name, n; had: it had one of that name before): a
   checked attribute added or removed sets the checkedness of a control whose flag is not
   set, and a radio's name, type or form attribute moves it to another group, which a
   checked one takes */
void form_attr_changed(dom_document *d, uint32_t x, const char *name, size_t n, int had);
/* an attribute whose change form_attr_changed answers (checked, name, type, form): a caller
   asks before it looks for what was there */
static inline int form_attr_matters(const char *s, size_t n) {
    return (n == 7 && memcmp(s, "checked", 7) == 0) || (n == 4 && (memcmp(s, "name", 4) == 0 || memcmp(s, "type", 4) == 0 || memcmp(s, "form", 4) == 0));
}
/* a subtree inserted (x its root): its checked radios, in tree order, take their groups */
void form_inserted(dom_document *d, uint32_t x);
/* the parse done: each group's last checked radio, in the parse's order, the one checked */
void form_parsed(dom_document *d);
/* an input's checkedness reset (4.10.5.4's reset algorithm): its attribute's, the flag cleared */
void form_reset_checked(dom_document *d, uint32_t x);

/* ---- constraint validation (4.10.20.2): the flags of a control's validity state ---- */
enum { FORM_V_MISSING = 1, FORM_V_TYPE = 2, FORM_V_PATTERN = 4, FORM_V_LONG = 8, FORM_V_SHORT = 16, FORM_V_UNDER = 32,
       FORM_V_OVER = 64, FORM_V_STEP = 128, FORM_V_BAD = 256, FORM_V_CUSTOM = 512 };
/* what the caller knows of the value: the user's edit changed it last (its dirty value flag
   set, too long and too short told then alone, as Chromium has it); the user's text is no
   value of the type (badInput); a custom validity message is set; a radio's group misses a
   checked button it needs, the caller having found it (else form_validity finds it) */
enum { FORM_HOW_EDITED = 1, FORM_HOW_BAD = 2, FORM_HOW_CUSTOM = 4, FORM_HOW_GROUP_KNOWN = 8, FORM_HOW_GROUP_MISSING = 16 };
/* the flags that hold of the control x, its value (sanitized) v[0, n): those HTML's
   ValidityState reads as Chromium answers them, whether x is a candidate or not */
uint32_t form_validity(const dom_document *d, uint32_t x, const char *v, size_t n, uint32_t how);
/* a radio's group needs a checked button and has none */
int form_radio_missing(const dom_document *d, uint32_t x);

/* ---- a value's numbers ---- */
/* p[0, n) is a valid floating-point number (HTML 2.3.4.3), in a float's range: 1 and *v */
int form_float(const char *p, size_t n, double *v);
/* a string of an input's type as HTML converts it to a number: 1 and *v */
int form_type_number(int ty, const char *p, size_t n, double *v);
/* a date, a time or a local date and time's type */
int form_dated(int ty);
/* a range input's value from its value attribute's text (HTML's default, kept in its range
   and put on its step), its least and greatest values */
double form_range_value(const dom_document *d, uint32_t x, const char *p, size_t n, double *mn, double *mx);

#endif
