%% prolog/amb/gen_c.pl -- the C that a module's contract becomes, at C read time.
%%
%% include/amb.pl's four macros call the four entry points here:
%%
%%   ambm_interface(+M, -Items)        the module's types, its store type, its prototypes
%%   ambm_implementation(+M, -Items)   the interface, then the definitions the module's
%%                                     own unit holds (names, init, checks, traversal,
%%                                     log, invariants, exporter, dispatcher)
%%   ambm_type_store(+T, -Type, -Items) the store's struct, its own buffers and its drop
%%   ambm_fn_store(+Name, +T, -Items)  one function of the store, at its first call
%%
%% Each is C TEXT read by cicilang's own grammar (ccl_c_items/3). The text is
%% generated whole, its names filled in here, so it carries no holes. The same
%% text generators answer outside cicilang (ambm_interface_text/2,
%% ambm_implementation_text/2), so the golden tests read exactly what the
%% compiler reads.
%%
%% THE NAMES. For module M: M_E an enum (constants M_E_V, upper case), M_status
%% (M_OK, M_STALE_HANDLE, ...), M_op (M_OP_NAME), M_R a record, M_R_h its handle,
%% M_S the store's typedef (over the instance store<M_store_tag>), M_Op an
%% operation, M_check_Op its preconditions, M_L_first/next/up/count a list's
%% traversal by handle. The store's own functions are found by their short names
%% through the store: node_new(d), node_live(d, h), text_add(d, p, n) ...
%%
%% Every predicate here is ambm_*. A generated text never names a macro of the
%% C library's headers (NULL, stdout): the text is not preprocessed.
:- ensure_loaded(library(amb/contract)).
:- ensure_loaded(library(amb/gen_schema)).

%% ---- entry points -------------------------------------------------------------

ambm_interface(M, Items) :-
    ambm_ready(M),
    ambm_interface_text(M, T),
    ambm_read(T, Items).

ambm_implementation(M, Items) :-
    ambm_ready(M),
    ambm_interface_text(M, T1),
    ambm_implementation_text(M, T2),
    atom_concat(T1, T2, T),
    ambm_read(T, Items).

ambm_type_store(T, base([], [typedef(N)]), Items) :-
    ambm_store_module(T, M),
    ambm_ready(M),
    ambm_instance(T, N),
    ambm_store_text(M, N, Text),
    ambm_read(Text, Items).

ambm_fn_store(Name, T, Items) :-
    ambm_store_module(T, M),
    ambm_instance(T, N),
    ambm_fn_text(M, N, Name, Text),
    ambm_read(Text, Items).

ambm_read(Text, Items) :- atom_codes(Text, Cs), ccl_c_items(Cs, [], Items).

%% the contract must be valid before any C is made of it
ambm_ready(M) :-
    amb_validate(M, Ds),
    (   Ds = [diag(D, Msg)|_]
    ->  format(atom(A), 'the contract of ~w is not valid: ~w, in ~q', [M, Msg, D]),
        ccl_macro_error(A)
    ;   true
    ).

%% store<M_store_tag>: the module is the tag's name before _store_tag
ambm_store_module(base(_, [typedef(Tag)]), M) :- atom_concat(M, '_store_tag', Tag).

ambm_instance(T, N) :-
    (   current_predicate(ccl_instance_name/3) -> ccl_instance_name(store, [T], N)
    ;   T = base(_, [typedef(Tag)]), atom_concat('store_', Tag, N)
    ).

%% ---- names --------------------------------------------------------------------

ambm_t(M, X, N) :- atomic_list_concat([M, '_', X], N).
ambm_k(Parts, C) :- atomic_list_concat(Parts, '_', A), upcase_atom(A, C).

ambm_store(M, S, Parts) :- amb_c(M, store(S, Parts)), !.
ambm_store_type(M, T) :- ambm_store(M, S, _), ambm_t(M, S, T).

%% the status codes: the contract's, then what the request path answers
ambm_status_codes(M, Codes) :-
    amb_c(M, status(C0)), !,
    findall(X, ( member(X, [stale_handle, bad_request, not_requestable, stale_revision]), \+ memberchk(X, C0) ), Extra),
    append(C0, Extra, Codes).
ambm_status_c(M, S, C) :- ambm_k([M, S], C).
ambm_ops(M, Ops) :- findall(Op, amb_c(M, operation(Op, _, _, _, _, _)), Ops).
ambm_op_c(M, Op, C) :- ambm_k([M, op, Op], C).

ambm_ctype(_, u8, uint8_t) :- !.
ambm_ctype(_, u16, uint16_t) :- !.
ambm_ctype(_, u32, uint32_t) :- !.
ambm_ctype(_, u64, uint64_t) :- !.
ambm_ctype(_, i32, int32_t) :- !.
ambm_ctype(_, i64, int64_t) :- !.
ambm_ctype(_, f64, double) :- !.
ambm_ctype(_, bool, uint8_t) :- !.
ambm_ctype(M, enum(op), T) :- !, ambm_t(M, op, T).
ambm_ctype(M, enum(E), T) :- !, ambm_t(M, E, T).
ambm_ctype(_, ref(_), uint32_t) :- !.
ambm_ctype(_, atom(_), uint32_t) :- !.
ambm_ctype(_, span(_), amb_span) :- !.

ambm_rec_t(M, R, T) :- ambm_t(M, R, T).
ambm_handle_t(M, R, T) :- ambm_t(M, R, R1), atom_concat(R1, '_h', T).
ambm_handle_f(M, R, F) :- amb_c(M, handle(R, F)), !.

ambm_param_c(_, param(P, text), A) :- !, format(atom(A), 'const char *~w, size_t ~w_len', [P, P]).
ambm_param_c(_, param(P, stream), A) :- !, format(atom(A), 'FILE *~w', [P]).
ambm_param_c(M, param(P, handle(R)), A) :- !, ambm_handle_t(M, R, HT), format(atom(A), '~w ~w', [HT, P]).
ambm_param_c(M, param(P, T), A) :- ambm_ctype(M, T, CT), format(atom(A), '~w ~w', [CT, P]).

ambm_result_c(M, status, T) :- !, ambm_t(M, status, T).
ambm_result_c(_, void, void) :- !.
ambm_result_c(M, handle(R), T) :- !, ambm_handle_t(M, R, T).
ambm_result_c(M, T, C) :- ambm_ctype(M, T, C).

ambm_params_c(M, Ps, A) :- maplist(ambm_param_c(M), Ps, As), ambm_join_args(As, A).
ambm_join_args([], '') :- !.
ambm_join_args(As, A) :- atomic_list_concat(As, ', ', A0), atom_concat(', ', A0, A).

ambm_lines(Ls, T) :- atomic_list_concat(Ls, '\n', T0), atom_concat(T0, '\n', T).

%% fill a template: '$X' replaced by its value, for each X=V of the pairs
ambm_fill(T0, [], T) :- !, ambm_text_atom(T0, T).
ambm_fill(T0, [K=V|KVs], T) :-
    ambm_text_atom(T0, A0),
    atomic_list_concat(Parts, K, A0), atomic_list_concat(Parts, V, A1),
    ambm_fill(A1, KVs, T).

ambm_text_atom(T, A) :- atom(T), !, A = T.
ambm_text_atom(T, A) :- is_list(T), !, atom_codes(A, T).
ambm_text_atom(T, A) :- atom_string(A, T).

%% ---- the interface --------------------------------------------------------------

ambm_interface_text(M, Text) :-
    amb_contract_fingerprint(M, FP),
    ambm_store_type(M, ST),
    format(atom(Head), '/* Ambitious: the interface of module ~w, generated from contracts/~w.pl (~w). Do not edit. */', [M, M, FP]),
    ambm_status_codes(M, Codes),
    ambm_enum_line(M, status, Codes, L1),
    findall(L, ( amb_c(M, enum(E, Vs)), ambm_enum_line(M, E, Vs, L) ), Ls2),
    ambm_ops(M, Ops),
    ( Ops == [] -> Ls3 = [] ; ambm_enum_line(M, op, Ops, L3), Ls3 = [L3] ),
    findall(L, ( amb_c(M, handle(R, _)), ambm_handle_t(M, R, HT),
                 format(atom(L), 'typedef struct ~w { uint32_t doc, index, gen; } ~w;', [HT, HT]) ), Ls4),
    findall(L, ( amb_c(M, record(R, Fs)), ambm_record_line(M, R, Fs, L) ), Ls5),
    findall(L, ( amb_c(M, event(E, Fs)), ambm_event_line(M, E, Fs, L) ), Ls6),
    format(atom(L7a), 'typedef struct ~w_store_tag { char unused; } ~w_store_tag;', [M, M]),
    format(atom(L7b), 'typedef store<~w_store_tag> ~w;', [M, ST]),
    ambm_protos(M, ST, Ls8),
    append([[Head, L1], Ls2, Ls3, Ls4, Ls5, Ls6, [L7a, L7b], Ls8], All),
    ambm_lines(All, Text).

ambm_enum_line(M, E, Vs, L) :-
    ambm_t(M, E, T),
    findall(C, ( member(V, Vs), ( E == status -> ambm_status_c(M, V, C) ; E == op -> ambm_op_c(M, V, C) ; ambm_k([M, E, V], C) ) ), Cs),
    atomic_list_concat(Cs, ', ', CA),
    format(atom(L), 'typedef enum ~w { ~w } ~w;', [T, CA, T]).

ambm_record_line(M, R, Fs, L) :-
    ambm_rec_t(M, R, T),
    findall(FL, ( member(field(F, Ty), Fs), ambm_ctype(M, Ty, CT), format(atom(FL), ' ~w ~w;', [CT, F]) ), FLs),
    atomic_list_concat(FLs, '', FA),
    format(atom(L), 'typedef struct ~w { uint32_t gen, live, free_next;~w } ~w;', [T, FA, T]).

ambm_event_line(M, E, Fs, L) :-
    ambm_t(M, E, T),
    findall(FL, ( member(field(F, Ty), Fs), ambm_ctype(M, Ty, CT), format(atom(FL), ' ~w ~w;', [CT, F]) ), FLs),
    atomic_list_concat(FLs, '', FA),
    format(atom(L), 'typedef struct ~w {~w } ~w;', [T, FA, T]).

ambm_protos(M, ST, Ls) :-
    format(atom(P1), 'extern const char ~w_fingerprint[];', [M]),
    findall(P, ( member(E-ET, [status-status, op-op]), ( E == op -> ambm_ops(M, Os), Os \== [] ; true ),
                 ambm_t(M, ET, T), format(atom(P), 'const char *~w_~w_name(~w x);', [M, ET, T]) ), Ps2),
    findall(P, ( amb_c(M, enum(E, _)), ambm_t(M, E, T), format(atom(P), 'const char *~w_name(~w x);', [T, T]) ), Ps3),
    format(atom(P4), 'void ~w_init(~w *d, uint32_t id);', [M, ST]),
    format(atom(P5), 'uint64_t ~w_revision(const ~w *d);', [M, ST]),
    findall(P, ( amb_c(M, root(R, _, _)), ambm_handle_t(M, R, HT), format(atom(P), '~w ~w_root(const ~w *d);', [HT, M, ST]) ), Ps6),
    findall(P, ( amb_c(M, operation(Op, Ps, Res, _, _, Opts)), ambm_op_proto(M, ST, Op, Ps, Res, Opts, P0), atom_concat(P0, ';', P) ), Ps7),
    findall(P, ( amb_c(M, operation(Op, Ps, _, _, _, _)), ambm_check_proto(M, ST, Op, Ps, P0), atom_concat(P0, ';', P) ), Ps8),
    findall(P, ( amb_c(M, list(L, O, I, Links)), ambm_trav_proto(M, ST, L, O, I, Links, P0), atom_concat(P0, ';', P) ), Ps9),
    findall(P, ( ambm_store(M, _, Parts), member(log(E), Parts), amb_c(M, event(E, Fs)), ambm_log_proto(M, ST, E, Fs, P0), atom_concat(P0, ';', P) ), Ps10),
    format(atom(P11), 'int ~w_check_invariants(const ~w *d, FILE *report);', [M, ST]),
    format(atom(P12), 'void ~w_export(const ~w *d, FILE *out);', [M, ST]),
    format(atom(P13), '~w_status ~w_dispatch(~w *d, const char *t, size_t n, uint64_t batch_rev, FILE *report);', [M, M, ST]),
    append([[P1], Ps2, Ps3, [P4, P5], Ps6, Ps7, Ps8, Ps9, Ps10, [P11, P12, P13]], Ls).

ambm_op_proto(M, ST, Op, Ps, Res, Opts, P) :-
    ambm_result_c(M, Res, RT), ambm_params_c(M, Ps, PA),
    ( memberchk(readonly, Opts) -> C = 'const ' ; C = '' ),
    format(atom(P), '~w ~w_~w(~w~w *d~w)', [RT, M, Op, C, ST, PA]).

ambm_check_proto(M, ST, Op, Ps, P) :-
    ambm_params_c(M, Ps, PA),
    format(atom(P), '~w_status ~w_check_~w(const ~w *d~w)', [M, M, Op, ST, PA]).

%% a list's traversal by handle: first, next, up (and last, prev where it has them), count
ambm_trav_proto(M, ST, L, O, I, Links, P) :-
    ambm_handle_t(M, O, OH), ambm_handle_t(M, I, IH),
    (   P0 = first-OH-IH ; P0 = next-IH-IH ; P0 = up-IH-OH
    ;   memberchk(last(_), Links), P0 = last-OH-IH
    ;   memberchk(prev(_), Links), P0 = prev-IH-IH
    ),
    P0 = What-In-Out,
    format(atom(P), '~w ~w_~w_~w(const ~w *d, ~w h)', [Out, M, L, What, ST, In]).
ambm_trav_proto(M, ST, L, O, _, _, P) :-
    ambm_handle_t(M, O, OH),
    format(atom(P), 'size_t ~w_~w_count(const ~w *d, ~w h)', [M, L, ST, OH]).

%% an event logged: its fields as parameters, but the revision, which the log fills
ambm_log_proto(M, ST, E, Fs, P) :-
    findall(A, ( member(field(F, T), Fs), F \== revision, ambm_ctype(M, T, CT), format(atom(A), '~w ~w', [CT, F]) ), As),
    ambm_join_args(As, PA),
    format(atom(P), 'void ~w_log_~w(~w *d~w)', [M, E, ST, PA]).

%% ---- the store's struct and drop ------------------------------------------------

ambm_store_text(M, N, Text) :-
    ambm_store(M, _, Parts),
    findall(F, ( member(P, Parts), ambm_part_fields(M, P, F) ), Fs),
    atomic_list_concat(Fs, '\n', FA),
    findall(F, ( member(P, Parts), ambm_part_frees(P, F) ), Frees0), append(Frees0, Frees),
    atomic_list_concat(Frees, ' ', FrA),
    format(atom(Text),
           '/* the store of module ~w, an instance of store<~w_store_tag>: one own buffer a part */~ntypedef struct ~w {~n    uint32_t id;~n    uint64_t revision;~n~w~n} ~w;~nvoid ~w_drop(~w *s) { ~w }~n',
           [M, M, N, FA, N, N, N, FrA]).

ambm_part_fields(M, table(R), F) :- ambm_rec_t(M, R, T),
    format(atom(F), '    size_t ~w_len, ~w_cap;~n    uint32_t ~w_free;~n    own ~w *~w;', [R, R, R, T, R]).
ambm_part_fields(_, pool(P), F) :-
    format(atom(F), '    size_t ~w_len, ~w_cap;~n    own char *~w;', [P, P, P]).
ambm_part_fields(_, atoms(P), F) :-
    format(atom(F), '    size_t ~w_len, ~w_cap, ~w_n, ~w_ncap, ~w_scap;~n    own char *~w;~n    own amb_span *~w_at;~n    own uint32_t *~w_slots;', [P, P, P, P, P, P, P, P]).
ambm_part_fields(M, log(E), F) :- ambm_t(M, E, T),
    format(atom(F), '    size_t ~w_len, ~w_cap;~n    own ~w *~w;', [E, E, T, E]).

ambm_part_frees(table(R), [F]) :- format(atom(F), 'free(s->~w);', [R]).
ambm_part_frees(pool(P), [F]) :- format(atom(F), 'free(s->~w);', [P]).
ambm_part_frees(atoms(P), [F1, F2, F3]) :-
    format(atom(F1), 'free(s->~w);', [P]), format(atom(F2), 'free(s->~w_at);', [P]), format(atom(F3), 'free(s->~w_slots);', [P]).
ambm_part_frees(log(E), [F]) :- format(atom(F), 'free(s->~w);', [E]).

%% ---- the store's functions, one at its first call ---------------------------------
%% Name is <part>_<op>, or _<part>_<op> for an inner helper; a name with an arity
%% suffix (node_new2), which the reader asks first, has no text and fails

ambm_fn_text(M, N, Name, Text) :-
    (   atom_concat('_', Rest, Name) -> Helper = true ; Rest = Name, Helper = false ),
    ambm_store(M, _, Parts),
    ambm_fn_part(M, Parts, Rest, Kind, Part, Op),
    ( Helper == true -> atom_concat('_', Op, TplOp) ; TplOp = Op ),
    ambm_fn_body(M, N, Kind, Part, TplOp, Text).

ambm_fn_part(M, Parts, Name, Kind, Part, Op) :-
    (   member(P, Parts), P =.. [Kind, Part]
    ;   amb_c(M, list(Part, _, _, _)), Kind = list
    ),
    atom_concat(Part, '_', Pre), atom_concat(Pre, Op, Name), Op \== '', !.

ambm_fn_body(M, N, table, R, Op, Text) :- !,
    ambm_tpl(table, Op, T0), ambm_rec_t(M, R, RT), ambm_handle_t(M, R, HT),
    ambm_fill(T0, ['$N'=N, '$P'=R, '$R'=RT, '$H'=HT], Text).
ambm_fn_body(_, N, pool, P, Op, Text) :- !,
    ambm_tpl(pool, Op, T0), ambm_fill(T0, ['$N'=N, '$P'=P], Text).
ambm_fn_body(_, N, atoms, P, Op, Text) :- !,
    ambm_tpl(atoms, Op, T0), ambm_fill(T0, ['$N'=N, '$P'=P], Text).
ambm_fn_body(M, N, log, E, Op, Text) :- !,
    ambm_tpl(log, Op, T0), ambm_t(M, E, ET), ambm_fill(T0, ['$N'=N, '$P'=E, '$R'=ET], Text).
ambm_fn_body(M, N, list, L, Op, Text) :-
    amb_c(M, list(L, O, I, Links)), !,
    ambm_list_fn(Op, L, O, I, Links, T0),
    ambm_fill(T0, ['$N'=N, '$L'=L, '$O'=O, '$I'=I], Text).

%% a table of records: $P the part, $R the record's type, $H its handle's
ambm_tpl(table, '_grow', "static void $N__$P_grow($N *s) {
    size_t c = s->$P_cap ? 2 * s->$P_cap : 16;
    if (c > 0x7FFFFFFF) abort();
    s->$P = ($R *)realloc(s->$P, c * sizeof($R));
    if (!s->$P) abort();
    s->$P_cap = c;
}").
ambm_tpl(table, new, "$H $N_$P_new($N *s) {
    uint32_t i;
    if (s->$P_free) {
        i = s->$P_free;
        s->$P_free = s->$P[i].free_next;
    } else {
        if (s->$P_len == 0) {
            if (s->$P_cap == 0) $N__$P_grow(s);
            memset(&s->$P[0], 0, sizeof($R));
            s->$P_len = 1;
        }
        if (s->$P_len == s->$P_cap) $N__$P_grow(s);
        i = (uint32_t)s->$P_len;
        s->$P_len++;
        s->$P[i].gen = 0;
    }
    uint32_t g = s->$P[i].gen;
    memset(&s->$P[i], 0, sizeof($R));
    s->$P[i].gen = g;
    s->$P[i].live = 1;
    $H h = { s->id, i, g };
    return h;
}").
ambm_tpl(table, live, "int $N_$P_live(const $N *s, $H h) {
    return h.doc == s->id && h.index != 0 && h.index < s->$P_len && s->$P[h.index].live && s->$P[h.index].gen == h.gen;
}").
ambm_tpl(table, get, "$R $N_$P_get(const $N *s, $H h) {
    if (!$N_$P_live(s, h)) abort();
    return s->$P[h.index];
}").
ambm_tpl(table, ref, "$R *$N_$P_ref(const $N *s, $H h) tie s {
    if (!$N_$P_live(s, h)) abort();
    return &s->$P[h.index];
}").
ambm_tpl(table, slot, "$R *$N_$P_slot(const $N *s, uint32_t i) tie s {
    if (i == 0 || i >= s->$P_len) abort();
    return &s->$P[i];
}").
ambm_tpl(table, handle, "$H $N_$P_handle(const $N *s, uint32_t i) {
    $H h = { 0, 0, 0 };
    if (i != 0 && i < s->$P_len && s->$P[i].live) {
        h.doc = s->id;
        h.index = i;
        h.gen = s->$P[i].gen;
    }
    return h;
}").
ambm_tpl(table, free, "void $N_$P_free($N *s, $H h) {
    if (!$N_$P_live(s, h)) abort();
    s->$P[h.index].live = 0;
    s->$P[h.index].gen++;
    s->$P[h.index].free_next = s->$P_free;
    s->$P_free = h.index;
}").
ambm_tpl(table, len, "size_t $N_$P_len(const $N *s) { return s->$P_len; }").
ambm_tpl(table, count, "size_t $N_$P_count(const $N *s) {
    size_t n = 0;
    for (size_t i = 1; i < s->$P_len; i++) if (s->$P[i].live) n++;
    return n;
}").

%% a pool of bytes: $P the part. Its first byte is a 0, so an empty span reads.
%% An add from a span of the same pool is safe: the source is found again after
%% the buffer moves
ambm_tpl(pool, '_room', "static void $N__$P_room($N *s, size_t n) {
    if (s->$P_len + n <= s->$P_cap) return;
    size_t c = s->$P_cap ? s->$P_cap : 256;
    while (c < s->$P_len + n) c *= 2;
    if (c > 0x7FFFFFFF) abort();
    s->$P = (char *)realloc(s->$P, c);
    if (!s->$P) abort();
    s->$P_cap = c;
}").
ambm_tpl(pool, add, "amb_span $N_$P_add($N *s, const char *p, size_t n) {
    amb_span sp = { 0, 0 };
    if (s->$P_len == 0) {
        $N__$P_room(s, 1);
        s->$P[0] = 0;
        s->$P_len = 1;
    }
    if (n == 0) return sp;
    int inside = p >= s->$P && p < s->$P + s->$P_len;
    size_t from = inside ? (size_t)(p - s->$P) : 0;
    $N__$P_room(s, n);
    if (inside) memmove(s->$P + s->$P_len, s->$P + from, n);
    else memcpy(s->$P + s->$P_len, p, n);
    sp.off = (uint32_t)s->$P_len;
    sp.len = (uint32_t)n;
    s->$P_len += n;
    return sp;
}").
ambm_tpl(pool, ptr, "const char *$N_$P_ptr(const $N *s, amb_span sp) tie s {
    if (s->$P_len == 0 || (size_t)sp.off + sp.len > s->$P_len) abort();
    return s->$P + sp.off;
}").
ambm_tpl(pool, eq, "int $N_$P_eq(const $N *s, amb_span sp, const char *p, size_t n) {
    if (sp.len != n) return 0;
    if (n == 0) return 1;
    if ((size_t)sp.off + sp.len > s->$P_len) abort();
    return memcmp(s->$P + sp.off, p, n) == 0;
}").
ambm_tpl(pool, put, "void $N_$P_put(const $N *s, FILE *out, amb_span sp) {
    if (sp.len == 0) { amb_put_qatom(out, \"\", 0); return; }
    if ((size_t)sp.off + sp.len > s->$P_len) abort();
    amb_put_qatom(out, s->$P + sp.off, sp.len);
}").
ambm_tpl(pool, size, "size_t $N_$P_size(const $N *s) { return s->$P_len; }").

%% interned names: $P the part. Id 0 is none; each name is kept with a 0 after it
ambm_tpl(atoms, '_hash', "static uint32_t $N__$P_hash(const char *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)p[i];
        h *= 16777619u;
    }
    return h;
}").
ambm_tpl(atoms, '_rehash', "static void $N__$P_rehash($N *s, size_t cap) {
    own uint32_t *slots = (uint32_t *)calloc(cap, sizeof(uint32_t));
    if (!slots) abort();
    uint32_t m = (uint32_t)(cap - 1);
    for (uint32_t id = 1; id < s->$P_n; id++) {
        amb_span sp = s->$P_at[id];
        uint32_t h = $N__$P_hash(s->$P + sp.off, sp.len) & m;
        while (slots[h]) h = (h + 1) & m;
        slots[h] = id;
    }
    free(s->$P_slots);
    s->$P_slots = move(slots);
    s->$P_scap = cap;
}").
ambm_tpl(atoms, find, "uint32_t $N_$P_find(const $N *s, const char *p, size_t n) {
    if (s->$P_scap == 0) return 0;
    uint32_t m = (uint32_t)(s->$P_scap - 1);
    uint32_t h = $N__$P_hash(p, n) & m;
    while (s->$P_slots[h]) {
        uint32_t id = s->$P_slots[h];
        amb_span sp = s->$P_at[id];
        if (sp.len == n && (n == 0 || memcmp(s->$P + sp.off, p, n) == 0)) return id;
        h = (h + 1) & m;
    }
    return 0;
}").
ambm_tpl(atoms, intern, "uint32_t $N_$P_intern($N *s, const char *p, size_t n) {
    uint32_t id = $N_$P_find(s, p, n);
    if (id) return id;
    if (s->$P_n == 0) {
        s->$P_at = (amb_span *)realloc(s->$P_at, 16 * sizeof(amb_span));
        if (!s->$P_at) abort();
        s->$P_ncap = 16;
        s->$P_at[0].off = 0;
        s->$P_at[0].len = 0;
        s->$P_n = 1;
    }
    if (s->$P_n == s->$P_ncap) {
        size_t c = 2 * s->$P_ncap;
        if (c > 0x7FFFFFFF) abort();
        s->$P_at = (amb_span *)realloc(s->$P_at, c * sizeof(amb_span));
        if (!s->$P_at) abort();
        s->$P_ncap = c;
    }
    if (s->$P_len + n + 1 > s->$P_cap) {
        size_t c = s->$P_cap ? s->$P_cap : 256;
        while (c < s->$P_len + n + 1) c *= 2;
        if (c > 0x7FFFFFFF) abort();
        s->$P = (char *)realloc(s->$P, c);
        if (!s->$P) abort();
        s->$P_cap = c;
    }
    if (n) memcpy(s->$P + s->$P_len, p, n);
    s->$P[s->$P_len + n] = 0;
    id = (uint32_t)s->$P_n;
    s->$P_at[id].off = (uint32_t)s->$P_len;
    s->$P_at[id].len = (uint32_t)n;
    s->$P_len += n + 1;
    s->$P_n++;
    if (2 * s->$P_n > s->$P_scap) {
        $N__$P_rehash(s, s->$P_scap ? 2 * s->$P_scap : 64);
    } else {
        uint32_t m = (uint32_t)(s->$P_scap - 1);
        uint32_t h = $N__$P_hash(s->$P + s->$P_at[id].off, n) & m;
        while (s->$P_slots[h]) h = (h + 1) & m;
        s->$P_slots[h] = id;
    }
    return id;
}").
ambm_tpl(atoms, ptr, "const char *$N_$P_ptr(const $N *s, uint32_t id) tie s {
    if (id == 0 || id >= s->$P_n) abort();
    return s->$P + s->$P_at[id].off;
}").
ambm_tpl(atoms, len, "size_t $N_$P_len(const $N *s, uint32_t id) {
    if (id == 0 || id >= s->$P_n) return 0;
    return s->$P_at[id].len;
}").
ambm_tpl(atoms, is, "int $N_$P_is(const $N *s, uint32_t id, const char *p, size_t n) {
    if (id == 0 || id >= s->$P_n) return n == 0;
    amb_span sp = s->$P_at[id];
    return sp.len == n && (n == 0 || memcmp(s->$P + sp.off, p, n) == 0);
}").
ambm_tpl(atoms, put, "void $N_$P_put(const $N *s, FILE *out, uint32_t id) {
    if (id == 0 || id >= s->$P_n) { amb_put_qatom(out, \"\", 0); return; }
    amb_put_qatom(out, s->$P + s->$P_at[id].off, s->$P_at[id].len);
}").
ambm_tpl(atoms, count, "size_t $N_$P_count(const $N *s) { return s->$P_n ? s->$P_n - 1 : 0; }").

%% an event log: $P the event's name, $R its type
ambm_tpl(log, push, "void $N_$P_push($N *s, $R e) {
    if (s->$P_len == s->$P_cap) {
        size_t c = s->$P_cap ? 2 * s->$P_cap : 64;
        s->$P = ($R *)realloc(s->$P, c * sizeof($R));
        if (!s->$P) abort();
        s->$P_cap = c;
    }
    s->$P[s->$P_len] = e;
    s->$P_len++;
}").
ambm_tpl(log, len, "size_t $N_$P_len(const $N *s) { return s->$P_len; }").
ambm_tpl(log, at, "$R $N_$P_at(const $N *s, size_t i) {
    if (i >= s->$P_len) abort();
    return s->$P[i];
}").

%% an intrusive list, by slot: $L the list, $O the owner's table, $I the items'.
%% Its texts are made from its links, the optional last and prev included or not
ambm_list_fn(Op, _L, O, I, Links, Text) :-
    memberchk(first(F), Links), memberchk(next(X), Links), memberchk(up(U), Links),
    ( memberchk(last(LA), Links) -> HasLast = true ; HasLast = false, LA = none ),
    ( memberchk(prev(PV), Links) -> HasPrev = true ; HasPrev = false, PV = none ),
    ambm_list_lines(Op, O, I, F, X, U, LA, PV, HasLast, HasPrev, Ls),
    ambm_lines(Ls, Text).

ambm_list_lines(append, _, _, F, X, U, LA, PV, HL, HP, Ls) :-
    format(atom(A0), 'void $N_$L_append($N *s, uint32_t o, uint32_t i) {', []),
    A1 = '    if (o == 0 || i == 0 || o >= s->$O_len || i >= s->$I_len) abort();',
    (   HL == true -> format(atom(A2), '    uint32_t t = s->$O[o].~w;', [LA])
    ;   format(atom(A2), '    uint32_t t = s->$O[o].~w;~n    if (t) while (s->$I[t].~w) t = s->$I[t].~w;', [F, X, X]) ),
    format(atom(A3), '    s->$I[i].~w = o;~n    s->$I[i].~w = 0;', [U, X]),
    ( HP == true -> format(atom(A4), '    s->$I[i].~w = t;', [PV]) ; A4 = '' ),
    format(atom(A5), '    if (t) s->$I[t].~w = i; else s->$O[o].~w = i;', [X, F]),
    ( HL == true -> format(atom(A6), '    s->$O[o].~w = i;', [LA]) ; A6 = '' ),
    exclude(==(''), [A0, A1, A2, A3, A4, A5, A6, '}'], Ls).
ambm_list_lines(insert_before, _, _, F, X, U, _, PV, _, HP, Ls) :-
    A0 = 'void $N_$L_insert_before($N *s, uint32_t o, uint32_t i, uint32_t b) {',
    A1 = '    if (b == 0) { $N_$L_append(s, o, i); return; }',
    format(atom(A2), '    if (o == 0 || i == 0 || o >= s->$O_len || i >= s->$I_len || b >= s->$I_len || s->$I[b].~w != o) abort();', [U]),
    (   HP == true -> format(atom(A3), '    uint32_t p = s->$I[b].~w;', [PV])
    ;   format(atom(A3), '    uint32_t p = 0;~n    for (uint32_t x = s->$O[o].~w; x && x != b; x = s->$I[x].~w) p = x;', [F, X]) ),
    format(atom(A4), '    s->$I[i].~w = o;~n    s->$I[i].~w = b;', [U, X]),
    ( HP == true -> format(atom(A5), '    s->$I[i].~w = p;~n    s->$I[b].~w = i;', [PV, PV]) ; A5 = '' ),
    format(atom(A6), '    if (p) s->$I[p].~w = i; else s->$O[o].~w = i;', [X, F]),
    exclude(==(''), [A0, A1, A2, A3, A4, A5, A6, '}'], Ls).
ambm_list_lines(unlink, _, _, F, X, U, LA, PV, HL, HP, Ls) :-
    A0 = 'void $N_$L_unlink($N *s, uint32_t i) {',
    A1 = '    if (i == 0 || i >= s->$I_len) abort();',
    format(atom(A2), '    uint32_t o = s->$I[i].~w;~n    if (o == 0) return;~n    if (o >= s->$O_len) abort();~n    uint32_t n = s->$I[i].~w;', [U, X]),
    (   HP == true -> format(atom(A3), '    uint32_t p = s->$I[i].~w;', [PV])
    ;   format(atom(A3), '    uint32_t p = 0;~n    for (uint32_t x = s->$O[o].~w; x && x != i; x = s->$I[x].~w) p = x;', [F, X]) ),
    format(atom(A4), '    if (p) s->$I[p].~w = n; else s->$O[o].~w = n;', [X, F]),
    ( HP == true -> format(atom(A5), '    if (n) s->$I[n].~w = p;', [PV]) ; A5 = '' ),
    ( HL == true -> format(atom(A6), '    if (s->$O[o].~w == i) s->$O[o].~w = p;', [LA, LA]) ; A6 = '' ),
    format(atom(A7), '    s->$I[i].~w = 0;~n    s->$I[i].~w = 0;', [U, X]),
    ( HP == true -> format(atom(A8), '    s->$I[i].~w = 0;', [PV]) ; A8 = '' ),
    exclude(==(''), [A0, A1, A2, A3, A4, A5, A6, A7, A8, '}'], Ls).
ambm_list_lines(first, _, _, F, _, _, _, _, _, _, [L]) :-
    format(atom(L), 'uint32_t $N_$L_first(const $N *s, uint32_t o) { if (o >= s->$O_len) abort(); return s->$O[o].~w; }', [F]).
ambm_list_lines(next, _, _, _, X, _, _, _, _, _, [L]) :-
    format(atom(L), 'uint32_t $N_$L_next(const $N *s, uint32_t i) { if (i >= s->$I_len) abort(); return s->$I[i].~w; }', [X]).
ambm_list_lines(up, _, _, _, _, U, _, _, _, _, [L]) :-
    format(atom(L), 'uint32_t $N_$L_up(const $N *s, uint32_t i) { if (i >= s->$I_len) abort(); return s->$I[i].~w; }', [U]).
ambm_list_lines(count, _, _, F, X, _, _, _, _, _, Ls) :-
    format(atom(L0), 'size_t $N_$L_count(const $N *s, uint32_t o) {~n    size_t n = 0;~n    if (o >= s->$O_len) abort();', []),
    format(atom(L1), '    for (uint32_t i = s->$O[o].~w; i; i = s->$I[i].~w) {~n        n++;~n        if (n > s->$I_len) abort();~n    }~n    return n;~n}', [F, X]),
    Ls = [L0, L1].
ambm_list_lines(inclusive_ancestor, O, O, _, _, U, _, _, _, _, Ls) :-
    L0 = 'int $N_$L_inclusive_ancestor(const $N *s, uint32_t a, uint32_t b) {',
    format(atom(L1), '    size_t steps = 0;~n    for (uint32_t x = b; x; x = s->$I[x].~w) {', [U]),
    L2 = '        if (x >= s->$I_len) abort();\n        if (x == a) return 1;\n        if (++steps > s->$I_len) return 1;\n    }\n    return 0;\n}',
    Ls = [L0, L1, L2].

%% ---- the implementation ---------------------------------------------------------

ambm_implementation_text(M, Text) :-
    ambm_store_type(M, ST),
    amb_contract_fingerprint(M, FP),
    format(atom(Head), '/* Ambitious: the implementation of module ~w, generated from contracts/~w.pl. Do not edit. */', [M, M]),
    ambm_names_text(M, T1),
    format(atom(T2), 'const char ~w_fingerprint[] = "~w";', [M, FP]),
    ambm_init_text(M, ST, T3),
    format(atom(T4), 'uint64_t ~w_revision(const ~w *d) { return d->revision; }', [M, ST]),
    findall(T, ( amb_c(M, root(R, _, _)), ambm_handle_t(M, R, HT),
                 format(atom(T), '~w ~w_root(const ~w *d) { return ~w_handle(d, 1); }', [HT, M, ST, R]) ), Ts5),
    findall(T, ( amb_c(M, operation(Op, Ps, _, Pres, _, _)), ambm_check_text(M, ST, Op, Ps, Pres, T) ), Ts6),
    findall(T, ( amb_c(M, list(L, O, I, Links)), ambm_trav_text(M, ST, L, O, I, Links, T) ), Ts7),
    findall(T, ( ambm_store(M, _, Parts), member(log(E), Parts), amb_c(M, event(E, Fs)), ambm_log_text(M, ST, E, Fs, T) ), Ts8),
    ambm_invariants_text(M, ST, T9),
    ambm_export_text(M, ST, T10),
    ambm_dispatch_text(M, ST, T11),
    append([[Head, T1, T2, T3, T4], Ts5, Ts6, Ts7, Ts8, [T9, T10, T11]], All),
    ambm_lines(All, Text).

ambm_names_text(M, Text) :-
    ambm_status_codes(M, Codes),
    ambm_name_fn(M, status, Codes, [ambm_status_c], T1),
    ambm_ops(M, Ops),
    ( Ops == [] -> T2 = '' ; ambm_name_fn(M, op, Ops, [ambm_op_c], T2) ),
    findall(T, ( amb_c(M, enum(E, Vs)), ambm_name_fn(M, E, Vs, [], T) ), Ts),
    exclude(==(''), [T1, T2|Ts], All),
    ambm_lines(All, Text).

ambm_name_fn(M, E, Vs, How, Text) :-
    ambm_t(M, E, T),
    findall(L, ( member(V, Vs), ambm_enum_const(M, E, V, How, C),
                 format(atom(L), '    case ~w: return "~w";', [C, V]) ), Ls),
    atomic_list_concat(Ls, '\n', Cases),
    format(atom(Text), 'const char *~w_name(~w x) {~n    switch (x) {~n~w~n    }~n    return "?";~n}', [T, T, Cases]).

ambm_enum_const(M, _, V, [ambm_status_c], C) :- !, ambm_status_c(M, V, C).
ambm_enum_const(M, _, V, [ambm_op_c], C) :- !, ambm_op_c(M, V, C).
ambm_enum_const(M, E, V, _, C) :- ambm_k([M, E, V], C).

%% init: an empty store made ready -- each pool's first byte, the root record
ambm_init_text(M, ST, Text) :-
    ambm_store(M, _, Parts),
    ( member(table(R0), Parts) -> true ; R0 = none ),
    findall(L, ( member(pool(P), Parts), format(atom(L), '    ~w_add(d, "", 0);', [P]) ), PLs),
    findall(L, ( amb_c(M, root(R, F, V)), ambm_handle_t(M, R, HT), amb_c(M, record(R, Fs)), memberchk(field(F, enum(E)), Fs),
                 ambm_k([M, E, V], C),
                 format(atom(L), '    ~w r = ~w_new(d);~n    ~w_slot(d, r.index)->~w = ~w;', [HT, R, R, F, C]) ), RLs),
    ( R0 == none -> Guard = '' ; format(atom(Guard), '    if (d->~w_len != 0) abort();~n', [R0]) ),
    append(PLs, RLs, Body), atomic_list_concat(Body, '\n', BA),
    format(atom(Text), 'void ~w_init(~w *d, uint32_t id) {~n~w    d->id = id;~n    d->revision = 0;~n~w~n}', [M, ST, Guard, BA]).

%% M_check_Op: every handle live first, then the preconditions in their order
ambm_check_text(M, ST, Op, Ps, Pres, Text) :-
    ambm_check_proto(M, ST, Op, Ps, Proto),
    findall(L, ( member(param(P, handle(R)), Ps),
                 ( member(pre(live(P), S), Pres) -> true ; S = stale_handle ),
                 ambm_status_c(M, S, SC),
                 format(atom(L), '    if (!~w_live(d, ~w)) return ~w;', [R, P, SC]) ), Lives),
    findall(L, ( member(pre(C, S), Pres), C \= live(_), ambm_pre_line(M, Ps, C, S, L) ), Conds),
    ambm_status_c(M, ok, OK),
    format(atom(Ret), '    return ~w;', [OK]),
    append([[Proto, '{'], Lives, Conds, [Ret, '}']], All),
    ambm_lines(All, Text0),
    atom_concat(Text0, '', Text).

ambm_pre_line(M, Ps, kind_in(P, Vs), S, L) :-
    memberchk(param(P, handle(R)), Ps), amb_c(M, record(R, Fs)), memberchk(field(kind, enum(E)), Fs),
    ambm_t(M, E, ET), ambm_status_c(M, S, SC),
    findall(X, ( member(V, Vs), ambm_k([M, E, V], K), format(atom(X), 'k == ~w', [K]) ), Xs),
    atomic_list_concat(Xs, ' || ', XA),
    format(atom(L), '    { ~w k = d->~w[~w.index].kind; if (!(~w)) return ~w; }', [ET, R, P, XA, SC]).
ambm_pre_line(M, Ps, unset(P, F), S, L) :-
    memberchk(param(P, handle(R)), Ps), ambm_status_c(M, S, SC),
    format(atom(L), '    if (d->~w[~w.index].~w != 0) return ~w;', [R, P, F, SC]).
ambm_pre_line(M, Ps, set(P, F), S, L) :-
    memberchk(param(P, handle(R)), Ps), ambm_status_c(M, S, SC),
    format(atom(L), '    if (d->~w[~w.index].~w == 0) return ~w;', [R, P, F, SC]).
ambm_pre_line(M, _, child_of(C, P, Lst), S, L) :-
    amb_c(M, list(Lst, _, I, Links)), memberchk(up(U), Links), ambm_status_c(M, S, SC),
    format(atom(L), '    if (d->~w[~w.index].~w != ~w.index) return ~w;', [I, C, U, P, SC]).
ambm_pre_line(M, _, not_inclusive_ancestor(A, B, Lst), S, L) :-
    ambm_status_c(M, S, SC),
    format(atom(L), '    if (~w_inclusive_ancestor(d, ~w.index, ~w.index)) return ~w;', [Lst, A, B, SC]).
ambm_pre_line(M, _, valid_name(T), S, L) :-
    ambm_status_c(M, S, SC),
    format(atom(L), '    if (!amb_valid_name(~w, ~w_len)) return ~w;', [T, T, SC]).

%% traversal by handle: a stale handle, or none, answers the none handle
ambm_trav_text(M, ST, L, O, I, Links, Text) :-
    ambm_handle_t(M, O, OH), ambm_handle_t(M, I, IH),
    memberchk(first(F), Links), memberchk(next(X), Links), memberchk(up(U), Links),
    findall(T, ( member(What-In-Out-Tab-Fld-ResTab,
                        [first-OH-IH-O-F-I, next-IH-IH-I-X-I, up-IH-OH-I-U-O]
                        ),
                 format(atom(T), '~w ~w_~w_~w(const ~w *d, ~w h) {~n    ~w none = { 0, 0, 0 };~n    if (!~w_live(d, h)) return none;~n    return ~w_handle(d, d->~w[h.index].~w);~n}',
                        [Out, M, L, What, ST, In, Out, Tab, ResTab, Tab, Fld]) ), Ts0),
    findall(T, ( memberchk(last(LA), Links),
                 format(atom(T), '~w ~w_~w_last(const ~w *d, ~w h) {~n    ~w none = { 0, 0, 0 };~n    if (!~w_live(d, h)) return none;~n    return ~w_handle(d, d->~w[h.index].~w);~n}',
                        [IH, M, L, ST, OH, IH, O, I, O, LA]) ), Ts1),
    findall(T, ( memberchk(prev(PV), Links),
                 format(atom(T), '~w ~w_~w_prev(const ~w *d, ~w h) {~n    ~w none = { 0, 0, 0 };~n    if (!~w_live(d, h)) return none;~n    return ~w_handle(d, d->~w[h.index].~w);~n}',
                        [IH, M, L, ST, IH, IH, I, I, I, PV]) ), Ts2),
    format(atom(TC), 'size_t ~w_~w_count(const ~w *d, ~w h) {~n    if (!~w_live(d, h)) return 0;~n    return ~w_count(d, h.index);~n}', [M, L, ST, OH, O, L]),
    append([Ts0, Ts1, Ts2, [TC]], All),
    ambm_lines(All, Text).

ambm_log_text(M, ST, E, Fs, Text) :-
    ambm_log_proto(M, ST, E, Fs, Proto),
    ambm_t(M, E, ET),
    findall(V, ( member(field(F, _), Fs), ( F == revision -> V = 'd->revision' ; V = F ) ), Vs),
    atomic_list_concat(Vs, ', ', VA),
    format(atom(Text), '~w {~n    d->revision++;~n    ~w e = { ~w };~n    ~w_push(d, e);~n}', [Proto, ET, VA, E]).

%% ---- the invariants -------------------------------------------------------------

ambm_invariants_text(M, ST, Text) :-
    findall(T, ( amb_c(M, invariant(Name, _, Check)), ambm_inv(M, Name, Check, T) ), Ts),
    atomic_list_concat(Ts, '\n', Body),
    format(atom(Text), 'int ~w_check_invariants(const ~w *d, FILE *report) {~n    int bad = 0;~n~w~n    return bad;~n}', [M, ST, Body]).

ambm_inv(M, Name, root(R, F, V), T) :-
    amb_c(M, record(R, Fs)), memberchk(field(F, enum(E)), Fs), ambm_k([M, E, V], C),
    findall(X, ( amb_c(M, list(_, _, R, Links)), memberchk(up(U), Links), format(atom(X), ' || d->~w[1].~w != 0', [R, U]) ), Xs),
    atomic_list_concat(Xs, '', XA),
    format(atom(T), '    /* ~w */~n    if (d->~w_len < 2 || !d->~w[1].live || d->~w[1].~w != ~w~w) { bad++; amb_report(report, "~w", "~w", 1); }',
           [Name, R, R, R, F, C, XA, M, Name]).
ambm_inv(M, Name, list_consistent(L), T) :-
    amb_c(M, list(L, O, I, Links)),
    memberchk(first(F), Links), memberchk(next(X), Links), memberchk(up(U), Links),
    ( memberchk(prev(PV), Links) -> format(atom(PrevChk), '~n            if (d->~w[i].~w != prev) { bad++; amb_report(report, "~w", "~w", i); }', [I, PV, M, Name]) ; PrevChk = '' ),
    ( memberchk(last(LA), Links) -> format(atom(LastChk), '~n        if (d->~w[o].~w != prev) { bad++; amb_report(report, "~w", "~w", o); }', [O, LA, M, Name]) ; LastChk = '' ),
    format(atom(T), '    /* ~w */~n    {~n        own uint32_t *seen = (uint32_t *)calloc(d->~w_len ? d->~w_len : 1, sizeof(uint32_t));~n        if (!seen) abort();~n        for (uint32_t o = 1; o < d->~w_len; o++) {~n            if (!d->~w[o].live) continue;~n            uint32_t prev = 0;~n            size_t steps = 0;~n            for (uint32_t i = d->~w[o].~w; i; i = d->~w[i].~w) {~n                if (i >= d->~w_len || !d->~w[i].live || d->~w[i].~w != o || ++steps > d->~w_len || seen[i]) { bad++; amb_report(report, "~w", "~w", o); break; }~n                seen[i] = o;~w~n                prev = i;~n            }~w~n        }~n        for (uint32_t i = 1; i < d->~w_len; i++)~n            if (d->~w[i].live && d->~w[i].~w && seen[i] != d->~w[i].~w) { bad++; amb_report(report, "~w", "~w", i); }~n        free(seen);~n    }',
           [Name, I, I, O, O, O, F, I, X, I, I, I, U, I, M, Name, PrevChk, LastChk, I, I, I, U, I, U, M, Name]).
ambm_inv(M, Name, acyclic(L), T) :-
    amb_c(M, list(L, R, R, Links)), memberchk(up(U), Links),
    format(atom(T), '    /* ~w */~n    for (uint32_t i = 1; i < d->~w_len; i++) {~n        if (!d->~w[i].live) continue;~n        size_t steps = 0;~n        for (uint32_t x = d->~w[i].~w; x; x = x < d->~w_len ? d->~w[x].~w : 0) {~n            if (x == i || ++steps > d->~w_len) { bad++; amb_report(report, "~w", "~w", i); break; }~n        }~n    }',
           [Name, R, R, R, U, R, R, U, R, M, Name]).
ambm_inv(M, Name, owner_kinds(L, F, Vs), T) :-
    amb_c(M, list(L, O, I, Links)), memberchk(first(Fi), Links), memberchk(up(U), Links),
    amb_c(M, record(O, Fs)), memberchk(field(F, enum(E)), Fs),
    findall(X, ( member(V, Vs), ambm_k([M, E, V], K), format(atom(X), 'k == ~w', [K]) ), Xs),
    atomic_list_concat(Xs, ' || ', XA),
    ambm_t(M, E, ET),
    format(atom(T), '    /* ~w */~n    for (uint32_t o = 1; o < d->~w_len; o++) {~n        if (!d->~w[o].live || !d->~w[o].~w) continue;~n        ~w k = d->~w[o].~w;~n        if (!(~w)) { bad++; amb_report(report, "~w", "~w", o); }~n    }~n    for (uint32_t i = 1; i < d->~w_len; i++) {~n        if (!d->~w[i].live || !d->~w[i].~w || d->~w[i].~w >= d->~w_len) continue;~n        ~w k = d->~w[d->~w[i].~w].~w;~n        if (!(~w)) { bad++; amb_report(report, "~w", "~w", i); }~n    }',
           [Name, O, O, O, Fi, ET, O, F, XA, M, Name, I, I, I, U, I, U, O, ET, O, I, U, F, XA, M, Name]).
ambm_inv(M, Name, refs_live, T) :-
    ambm_store(M, _, Parts),
    findall(X, ( member(table(R), Parts), amb_c(M, record(R, Fs)), member(field(F, ref(R2)), Fs),
                 format(atom(X), '        for (uint32_t i = 1; i < d->~w_len; i++) {~n            uint32_t r = d->~w[i].~w;~n            if (d->~w[i].live && r && (r >= d->~w_len || !d->~w[r].live)) { bad++; amb_report(report, "~w", "~w", i); }~n        }',
                        [R, R, F, R, R2, R2, M, Name]) ), Xs),
    atomic_list_concat(Xs, '\n', XA),
    format(atom(T), '    /* ~w */~n    {~n~w~n    }', [Name, XA]).

%% ---- the exporter ---------------------------------------------------------------

ambm_export_text(M, ST, Text) :-
    ambm_store(M, _, Parts),
    findall(T, ( member(table(R), Parts), ambm_export_table(M, R, T) ), Ts),
    atomic_list_concat(Ts, '\n', Body),
    format(atom(Text),
           'void ~w_export(const ~w *d, FILE *out) {~n    fprintf(out, "amb_snapshot(~w, %u, %llu, \'%s\').\\n", (unsigned)d->id, (unsigned long long)d->revision, ~w_fingerprint);~n~w~n}',
           [M, ST, M, M, Body]).

ambm_export_table(M, R, Text) :-
    findall(F, amb_c(M, fact(F, table(R), _)), Heads), Heads \== [], !,
    findall(L, ( amb_c(M, fact(H, table(R), W)), ambm_fact_lines(M, R, H, W, L) ), Ls),
    atomic_list_concat(Ls, '\n', Body),
    format(atom(Text), '    for (uint32_t i = 1; i < d->~w_len; i++) {~n        if (!d->~w[i].live) continue;~n~w~n    }', [R, R, Body]).
ambm_export_table(_, _, '').

ambm_fact_lines(M, R, Head, Where, Text) :-
    Head =.. [F|Args],
    ambm_where_c(M, R, Where, WC),
    findall(A, ( nth1(K, Args, Arg), ambm_arg_c(M, R, Arg, A0), ( K == 1 -> A = A0 ; format(atom(A), 'fputs(",", out); ~w', [A0]) ) ), As),
    atomic_list_concat(As, ' ', AA),
    format(atom(Text), '        if (~w) { fputs("~w(", out); ~w fputs(").\\n", out); }', [WC, F, AA]).

ambm_where_c(_, _, true, '1') :- !.
ambm_where_c(M, R, (A, B), C) :- !, ambm_where_c(M, R, A, CA), ambm_where_c(M, R, B, CB), format(atom(C), '(~w) && (~w)', [CA, CB]).
ambm_where_c(M, R, field(F) == V, C) :- !, ambm_where_side(M, R, F, V, VC), format(atom(C), 'd->~w[i].~w == ~w', [R, F, VC]).
ambm_where_c(M, R, field(F) \== V, C) :- !, ambm_where_side(M, R, F, V, VC), format(atom(C), 'd->~w[i].~w != ~w', [R, F, VC]).

ambm_where_side(M, R, F, V, VC) :-
    amb_c(M, record(R, Fs)), memberchk(field(F, T), Fs),
    (   T = enum(E) -> ambm_k([M, E, V], VC)
    ;   V == none -> VC = '0'
    ;   V == true -> VC = '1'
    ;   V == false -> VC = '0'
    ;   VC = V
    ).

ambm_arg_c(M, R, handle, A) :- !, ambm_handle_f(M, R, Fn),
    format(atom(A), 'amb_put_handle(out, "~w", d->id, i, d->~w[i].gen);', [Fn, R]).
ambm_arg_c(_, _, doc, 'fprintf(out, "%u", (unsigned)d->id);') :- !.
ambm_arg_c(_, _, rev, 'fprintf(out, "%llu", (unsigned long long)d->revision);') :- !.
ambm_arg_c(M, R, field(F), A) :-
    amb_c(M, record(R, Fs)), memberchk(field(F, T), Fs),
    ambm_field_put(M, R, F, T, A).

ambm_field_put(M, R, F, enum(E), A) :- !, ambm_t(M, E, ET), format(atom(A), 'fputs(~w_name(d->~w[i].~w), out);', [ET, R, F]).
ambm_field_put(_, R, F, atom(P), A) :- !, format(atom(A), '~w_put(d, out, d->~w[i].~w);', [P, R, F]).
ambm_field_put(_, R, F, span(P), A) :- !, format(atom(A), '~w_put(d, out, d->~w[i].~w);', [P, R, F]).
ambm_field_put(M, R, F, ref(R2), A) :- !, ambm_handle_f(M, R2, Fn),
    format(atom(A), '{ uint32_t r = d->~w[i].~w; amb_put_ref(out, "~w", d->id, r, r && r < d->~w_len ? d->~w[r].gen : 0); }', [R, F, Fn, R2, R2]).
ambm_field_put(_, R, F, bool, A) :- !, format(atom(A), 'fputs(d->~w[i].~w ? "true" : "false", out);', [R, F]).
ambm_field_put(_, R, F, f64, A) :- !, format(atom(A), 'fprintf(out, "%.17g", d->~w[i].~w);', [R, F]).
ambm_field_put(_, R, F, _, A) :- format(atom(A), 'fprintf(out, "%lld", (long long)d->~w[i].~w);', [R, F]).

%% ---- the request dispatcher -----------------------------------------------------
%% amb_request(Doc, Rev, Op(Args...)). -- refused unless Doc is this store's, Rev
%% is the batch's and Op is requestable; then the operation itself, which checks
%% its own preconditions

ambm_dispatch_text(M, ST, Text) :-
    ambm_status_c(M, bad_request, BAD), ambm_status_c(M, stale_revision, STALE),
    ambm_status_c(M, not_requestable, NOTREQ),
    findall(T, ( amb_c(M, operation(Op, Ps, _, _, _, Opts)), memberchk(requestable, Opts), ambm_dispatch_op(M, Op, Ps, BAD, T) ), Ts),
    findall(C, ( amb_c(M, operation(Op, _, _, _, _, Opts)), \+ memberchk(requestable, Opts),
                 format(atom(C), 'amb_name_is(name, name_len, "~w")', [Op]) ), Cs),
    (   Cs == [] -> NotReq = ''
    ;   atomic_list_concat(Cs, ' || ', CA),
        format(atom(NotReq), '    if (~w) { amb_result(report, name, "not_requestable", d->revision); return ~w; }~n', [CA, NOTREQ])
    ),
    atomic_list_concat(Ts, '\n', Body),
    format(atom(Text),
'~w_status ~w_dispatch(~w *d, const char *t, size_t n, uint64_t batch_rev, FILE *report) {
    size_t p = 0;
    char name[64];
    size_t name_len = 0;
    int64_t doc = 0;
    int64_t rev = 0;
    ~w_status st = ~w;
    name[0] = 0;
    if (!amb_lex_word(t, n, &p, "amb_request") || !amb_lex_char(t, n, &p, \'(\')) return ~w;
    if (!amb_lex_int(t, n, &p, &doc) || !amb_lex_char(t, n, &p, \',\')) return ~w;
    if (!amb_lex_int(t, n, &p, &rev) || !amb_lex_char(t, n, &p, \',\')) return ~w;
    if (!amb_lex_name(t, n, &p, name, sizeof name, &name_len)) return ~w;
    if (doc != (int64_t)d->id) { amb_result(report, name, "bad_request", d->revision); return ~w; }
    if (rev != (int64_t)batch_rev) { amb_result(report, name, "stale_revision", d->revision); return ~w; }
~w
~w    amb_result(report, name, "bad_request", d->revision);
    return st;
}',
           [M, M, ST, M, BAD, BAD, BAD, BAD, BAD, BAD, STALE, Body, NotReq]).

ambm_dispatch_op(M, Op, Ps, BAD, Text) :-
    findall(D, ( member(param(P, T), Ps), ambm_dispatch_decl(M, P, T, D) ), Decls),
    findall(R, ( nth1(K, Ps, param(P, T)), ambm_dispatch_read(M, P, T, BAD, R0),
                 ( K == 1 -> R = R0 ; format(atom(R), '        if (!amb_lex_char(t, n, &p, \',\')) return ~w;~n~w', [BAD, R0]) ) ), Reads),
    findall(A, ( member(param(P, T), Ps), ( T == text -> format(atom(A), ', a_~w, a_~w_len', [P, P]) ; format(atom(A), ', a_~w', [P]) ) ), As),
    atomic_list_concat(As, '', AA),
    atomic_list_concat(Decls, '\n', DA), atomic_list_concat(Reads, '\n', RA),
    ( Ps == [] -> Open = '' ; format(atom(Open), '        if (!amb_lex_char(t, n, &p, \'(\')) return ~w;~n', [BAD]) ),
    ( Ps == [] -> Close = '' ; format(atom(Close), '        if (!amb_lex_char(t, n, &p, \')\')) return ~w;~n', [BAD]) ),
    format(atom(Text),
'    if (amb_name_is(name, name_len, "~w")) {
~w
~w~w
~w        if (!amb_lex_char(t, n, &p, \')\') || !amb_lex_char(t, n, &p, \'.\') || !amb_lex_end(t, n, &p)) return ~w;
        st = ~w_~w(d~w);
        amb_result(report, name, ~w_status_name(st), d->revision);
        return st;
    }',
           [Op, DA, Open, RA, Close, BAD, M, Op, AA, M]).

ambm_dispatch_decl(M, P, handle(R), D) :- !, ambm_handle_t(M, R, HT), format(atom(D), '        ~w a_~w = { 0, 0, 0 };', [HT, P]).
ambm_dispatch_decl(_, P, text, D) :- !, format(atom(D), '        char a_~w[4096];~n        size_t a_~w_len = 0;', [P, P]).
ambm_dispatch_decl(M, P, enum(E), D) :- !, ambm_ctype(M, enum(E), T), format(atom(D), '        ~w a_~w;~n        char w_~w[64];~n        size_t w_~w_len = 0;', [T, P, P, P]).
ambm_dispatch_decl(M, P, T, D) :- ambm_ctype(M, T, CT), format(atom(D), '        ~w a_~w = 0;~n        int64_t v_~w = 0;', [CT, P, P]).

ambm_dispatch_read(M, P, handle(R), BAD, Rd) :- !, ambm_handle_f(M, R, Fn),
    format(atom(Rd), '        if (!amb_lex_handle(t, n, &p, "~w", &a_~w.doc, &a_~w.index, &a_~w.gen)) return ~w;', [Fn, P, P, P, BAD]).
ambm_dispatch_read(_, P, text, BAD, Rd) :- !,
    format(atom(Rd), '        if (!amb_lex_atom(t, n, &p, a_~w, sizeof a_~w, &a_~w_len)) return ~w;', [P, P, P, BAD]).
ambm_dispatch_read(M, P, enum(E), BAD, Rd) :- !,
    amb_c(M, enum(E, Vs)),
    findall(X, ( member(V, Vs), ambm_k([M, E, V], K), format(atom(X), 'if (amb_name_is(w_~w, w_~w_len, "~w")) a_~w = ~w;', [P, P, V, P, K]) ), Xs),
    atomic_list_concat(Xs, ' else ', XA),
    format(atom(Rd), '        if (!amb_lex_name(t, n, &p, w_~w, sizeof w_~w, &w_~w_len)) return ~w;~n        ~w else return ~w;', [P, P, P, BAD, XA, BAD]).
ambm_dispatch_read(M, P, T, BAD, Rd) :- ambm_ctype(M, T, CT),
    format(atom(Rd), '        if (!amb_lex_int(t, n, &p, &v_~w)) return ~w;~n        a_~w = (~w)v_~w;', [P, BAD, P, CT, P]).
