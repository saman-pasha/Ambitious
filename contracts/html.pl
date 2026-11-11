%% contracts/html.pl -- the HTML module's contract (docs/contract.md is the format).
%%
%% The module turns bytes into a DOM document by the WHATWG algorithms: the
%% tokenizer (HTML 13.2.5) makes the token table of this module's store, and the
%% tree builder (13.2.6) makes the document from it through the DOM's own
%% operations only -- the parser's output contract is the DOM contract, never
%% its storage. Parse errors are records here, and facts for the reasoner.

html_contract(module(html, '0.1.0', 'HTML: the WHATWG tokenizer and tree builder, bytes to a DOM document, with its parse errors')).

%% ---- TYPES ------------------------------------------------------------------

html_contract(status([ok, stale_handle, wrong_kind, dom_refused])).

html_contract(enum(token_kind, [doctype, start_tag, end_tag, comment, characters, eof])).

%% the tokenizer's errors (13.2.2), and the tree builder's, which are one kind
%% with a detail
html_contract(enum(error_code, [
    unexpected_null_character, unexpected_question_mark_instead_of_tag_name, eof_in_tag, eof_before_tag_name, invalid_first_character_of_tag_name,
    missing_end_tag_name, unexpected_equals_sign_before_attribute_name,
    unexpected_character_in_attribute_name, missing_attribute_value,
    unexpected_character_in_unquoted_attribute_value, missing_whitespace_between_attributes,
    unexpected_solidus_in_tag, eof_in_comment, abrupt_closing_of_empty_comment, nested_comment,
    incorrectly_closed_comment, incorrectly_opened_comment, eof_in_doctype, missing_doctype_name,
    missing_whitespace_before_doctype_name, invalid_character_sequence_after_doctype_name,
    missing_doctype_public_identifier, missing_doctype_system_identifier,
    abrupt_doctype_public_identifier, abrupt_doctype_system_identifier,
    unexpected_character_after_doctype_system_identifier, missing_quote_before_doctype_public_identifier,
    missing_quote_before_doctype_system_identifier, missing_whitespace_after_doctype_public_keyword,
    missing_whitespace_after_doctype_system_keyword,
    missing_whitespace_between_doctype_public_and_system_identifiers, cdata_in_html_content, eof_in_cdata,
    missing_semicolon_after_character_reference, unknown_named_character_reference,
    absence_of_digits_in_numeric_character_reference, null_character_reference,
    character_reference_outside_unicode_range, surrogate_character_reference,
    noncharacter_character_reference, control_character_reference, duplicate_attribute,
    end_tag_with_attributes, end_tag_with_trailing_solidus, eof_in_script_html_comment_like_text,
    non_void_html_element_start_tag_with_trailing_solidus, tree_construction])).

%% a token: its kind, its name (a tag's or a doctype's), its data (characters or
%% a comment), its attributes, and where it began
html_contract(record(token, [
    field(kind,         enum(token_kind)),
    field(name,         span(bytes)),
    field(data,         span(bytes)),
    field(first_attr,   ref(tattr)),
    field(last_attr,    ref(tattr)),
    field(self_closing, bool),
    field(force_quirks, bool),
    field(public_id,    span(bytes)),
    field(system_id,    span(bytes)),
    field(ids,          u8),
    field(line,         u32),
    field(col,          u32)
])).
html_contract(record(tattr, [
    field(token, ref(token)),
    field(name,  span(bytes)),
    field(value, span(bytes)),
    field(next,  ref(tattr))
])).
html_contract(record(error, [
    field(code,   enum(error_code)),
    field(line,   u32),
    field(col,    u32),
    field(detail, span(bytes))
])).

html_contract(handle(token, tok)).
html_contract(handle(tattr, tat)).
html_contract(handle(error, err)).

%% ---- OWNERSHIP --------------------------------------------------------------

html_contract(store(parser, [table(token), table(tattr), table(error), pool(bytes)])).
html_contract(owns(parser, token, store)).
html_contract(owns(parser, tattr, store)).
html_contract(owns(parser, error, store)).
html_contract(owns(token, tattr, list(attributes))).
html_contract(tie(handle(token), parser)).
html_contract(list(attributes, token, tattr, [first(first_attr), last(last_attr), next(next), up(token)])).

%% ---- OPERATIONS -------------------------------------------------------------

%% the token table of src (its newlines normalized first), its errors recorded
html_contract(operation(tokenize, [param(src, text)], status, [], [creates(token)], [mutates])).
%% the document built from the token table, by the tree construction algorithm
html_contract(operation(build, [param(doc, store(dom))], status, [], [], [mutates])).
%% both: the HTML document of src into doc
html_contract(operation(parse, [param(doc, store(dom)), param(src, text)], status, [], [], [mutates])).
%% the fragment parsing algorithm (13.4): src parsed as the children of the element
%% context names -- its local name in the HTML namespace, or "svg NAME" or "math
%% NAME" in SVG's or MathML's; the nodes it makes become the document node's children
html_contract(operation(parse_fragment, [param(doc, store(dom)), param(context, text), param(src, text)], status,
    [], [], [mutates])).

%% ---- INVARIANTS -------------------------------------------------------------

html_contract(invariant(attr_links, 'Each attribute is listed once, by its token', list_consistent(attributes))).
html_contract(invariant(live_links, 'Every link names a live record', refs_live)).

%% ---- REASONING --------------------------------------------------------------

html_contract(fact(html_error(handle, field(code), field(line), field(col), field(detail)), table(error), true)).

html_contract(rules('rules/html')).
html_contract(derived(html_error_count/2, 'how many parse errors of a code the page has')).
html_contract(derived(html_conforming/0, 'the page parsed with no error')).

html_contract(generate([c_types, c_store, c_accessors, c_checks, c_exporter, c_invariants, schema, doc])).
html_contract(test(exec, 'tests/html')).
