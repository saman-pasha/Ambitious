%% include/amb.pl -- the macros through which a C unit uses a module's contract.
%%
%%   #include <amb.pl>              (cicilang -I include; amb.pl.h comes with it)
%%   amb_interface(dom);            at file scope: the module's types, its store type
%%                                  (store<dom_store_tag>, autodropped), its prototypes
%%   amb_implementation(dom);       in the module's own unit only: the interface, then
%%                                  the names, init, the checks, the invariants, the
%%                                  exporter, the traversal and the request dispatcher
%%
%% and the type macro behind the store, whose functions are made at their first
%% call: node_new(d), node_live(d, h), node_ref(d, h), names_intern(d, p, n),
%% text_add(d, p, n), children_append(d, o, i) ... (prolog/amb/gen_c.pl).
%%
%% EVERY PREDICATE OF THIS FILE IS A MACRO (cicilang's rule for an included .pl),
%% so it holds the four entry points and nothing else; the generator is loaded
%% by the directive, which makes none of its predicates a macro.
:- ensure_loaded(library(amb/gen_c)).

amb_interface(id(M), Items) :- ambm_interface(M, Items).
amb_implementation(id(M), Items) :- ambm_implementation(M, Items).
type_store([T], Type, Items) :- ambm_type_store(T, Type, Items).
fn_store(Name, [T], Items) :- ambm_fn_store(Name, T, Items).
