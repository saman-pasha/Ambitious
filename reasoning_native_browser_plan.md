# Reasoning-Native Browser — Architecture and Implementation Plan

## 1. Vision

Build a browser whose modules are implemented in **Cicilang**, whose module contracts and generated interfaces are driven by **Cocolog macros**, and whose live state and module knowledge are available to **Cocolog** for reasoning.

The goal is not to attach a reasoning assistant to a conventional browser. The goal is to make the browser itself a reasoning environment.

The architecture has four cooperating parts:

- **Cicilang — execution:** implements browser modules and performs deterministic, performance-sensitive work.
- **Cocolog — declarative definition and reasoning:** expresses module contracts, invariants, relationships, rules, queries, and explanations.
- **Cocolog macros — integration and generation:** connect declarative module descriptions to generated Cicilang interfaces, implementation scaffolding, metadata, and reasoning predicates.
- **ZiguratIP — persistence:** stores selected durable browser knowledge and state.

```text
                 COCOLOG
      Declarative definitions and reasoning
       rules · facts · constraints · queries
                    ▲   │
                    │   ▼
             COCOLOG MACROS
       contracts · generation · bindings
                    ▲   │
                    │   ▼
                 CICILANG
       All browser execution modules
                    ▲   │
                    │   ▼
                ZIGURATIP
          Durable knowledge and state
```

The arrows represent explicit interfaces and controlled data exchange, not unrestricted shared memory.

## 2. Core architectural principles

### 2.1 One implementation language for browser modules

Implement the browser modules in Cicilang. Avoid splitting the browser's core implementation across unrelated languages without a demonstrated need.

Cicilang's ownership and lifetime facilities should express resource ownership and references. For example, a document can own its DOM nodes, while references to those nodes are tied to the document's lifetime where appropriate.

This does not imply that every low-level platform facility must be reimplemented. Native operating-system, graphics, font, image-decoding, and other system libraries can be integrated through explicit Cicilang interfaces.

### 2.2 One source of truth for execution and reasoning

A module's declarative contract should describe the concepts needed by both its implementation and its reasoning interface.

For example, the DOM contract can describe:

- Types and fields.
- Ownership and lifetime relationships.
- Operations and their preconditions.
- Invariants that must remain true.
- Observable state and events.
- Facts and relations exposed to Cocolog.
- Rules or derived properties that Cocolog can reason about.

Cocolog should reason about the actual state produced by the Cicilang implementation, not an independently maintained approximation.

### 2.3 Separate execution from reasoning

Cicilang performs hot-path operations such as HTML parsing and DOM mutation, CSS selector matching and cascade computation, layout and geometry calculations, painting and compositing orchestration, event dispatch, and network/resource processing.

Cocolog reasons about meaningful state, rules, constraints, dependencies, and explanations. It should not be invoked as a general proof engine for every selector match, layout calculation, paint operation, or event.

### 2.4 Explicit module boundaries

Each module should expose two related interfaces:

1. **Execution interface:** typed operations used by other Cicilang modules.
2. **Reasoning interface:** facts, relations, rules, and explanations exposed to Cocolog.

Generate or validate these interfaces from a common module contract wherever practical.

### 2.5 Stable identity and versioned state

Use stable node/resource identifiers and document revisions for cross-module references, asynchronous work, diagnostics, and reasoning snapshots.

Cicilang ownership and lifetime ties remain the primary memory-safety mechanism. Stable handles complement them; they do not replace ownership.

Illustrative handle representation:

```c
struct NodeHandle {
    uint32_t document_id;
    uint32_t node_id;
    uint32_t generation;
};
```

The generation helps detect stale references after deletion or reuse. This is illustrative C-like notation, not a prescribed Cicilang declaration.

### 2.6 Reason over controlled snapshots

Define how live module state becomes Cocolog facts. Use explicit snapshots or versioned fact updates, and associate them with document/module revisions.

Reasoning results that request changes must pass through validated execution operations. Cocolog should not mutate internal browser memory directly.

## 3. The module contract

Before implementing the complete browser, define a reusable contract format for every module.

| Contract section | Purpose |
|---|---|
| `TYPES` | Data types, fields, enumerations, and relationships |
| `OWNERSHIP` | Owners, borrows, lifetime ties, and resource cleanup |
| `OPERATIONS` | Public functions and their input/output contracts |
| `INVARIANTS` | Conditions that must always hold |
| `EVENTS` | Observable state changes and their versioning |
| `REASONING` | Facts, predicates, rules, and queries exposed to Cocolog |
| `GENERATION` | Cicilang declarations, visitors, metadata, and bindings to generate |
| `VALIDATION` | Compile-time checks, runtime checks, and contract tests |

Illustrative contract:

```text
MODULE DOM

  TYPES
    Node
    Document
    Attribute

  OWNERSHIP
    Document owns Node
    Node owns its children
    References tie to Document

  OPERATIONS
    create
    append
    remove
    query
    mutate

  INVARIANTS
    A node has at most one parent
    A document cannot contain ownership cycles

  REASONING
    parent/2
    child/2
    attribute/3
    descendant/2
    mutation_effect/3
```

This is conceptual notation. The exact syntax must be designed to fit Cocolog's actual macro and language capabilities.

## 4. Responsibilities of Cocolog macros

Cocolog macros are the bridge between declarative module knowledge and Cicilang implementation.

The first design task is to establish what the existing Cocolog macro system can express and generate. Do not assume that every capability below already exists.

Potential responsibilities:

1. Read and validate module contracts.
2. Generate Cicilang type and interface declarations.
3. Generate enumerations, visitors, and metadata tables.
4. Generate or scaffold reasoning fact exporters.
5. Generate predicate declarations or adapters for Cocolog.
6. Validate consistency between module contracts and implementation interfaces.
7. Generate contract tests and documentation where feasible.
8. Report source locations and useful diagnostics when generation or validation fails.

Possible pipeline:

```text
Declarative module contract
          |
          v
Cocolog parsing and validation
          |
          v
Normalized module description
          |
          +------> Cicilang declarations / scaffolding
          |
          +------> Reasoning predicates / fact schema
          |
          +------> Metadata / visitors / tests
          |
          v
Cicilang module implementation
          |
          v
Runtime state and versioned events
          |
          v
Cocolog fact snapshots and reasoning
```

If Cocolog cannot directly generate the required Cicilang source, use a deterministic export format between Cocolog validation and Cicilang macro expansion. Keep this boundary explicit and testable.

## 5. Browser module architecture

All core browser modules are implemented in Cicilang. Their contracts define the corresponding reasoning interfaces.

| Module | Cicilang execution responsibilities | Cocolog reasoning responsibilities |
|---|---|---|
| HTML | Tokenization, parsing, tree construction, serialization | Document structure, element relationships, parse diagnostics |
| DOM | Node ownership, traversal, mutation, attributes, text | Parent/child relations, ancestry, mutation consequences |
| CSS | Tokenization, parsing, values, selectors, cascade, computed styles | Selector explanations, conflicts, dependencies, rule provenance |
| Layout | Formatting contexts, box generation, geometry | Layout constraints, dependency explanations, diagnostics |
| Rendering | Paint commands, compositing orchestration, invalidation | Explainable rendering dependencies and state |
| Networking | Navigation, fetching, resource loading, cancellation | Resource relationships, policy reasoning, dependency analysis |
| Storage | Cache and browser data interfaces | Stored knowledge, associations, queryable metadata |
| JavaScript integration | Runtime integration, bindings, host APIs, event loop integration | Explainable host-object relationships and selected execution metadata |
| Accessibility | Accessibility tree and platform bridge | Missing names, invalid relationships, accessibility diagnostics |
| Developer tools | Inspection, tracing, diagnostics UI interfaces | Queries and explanations over browser facts |

The reasoning interface should expose useful semantic state, not every implementation detail or every transient machine-level operation.

## 6. First proof of concept: DOM

The DOM should be the first complete module because it exercises the central architecture: Cicilang ownership and lifetime rules, typed interfaces, Cocolog macro generation, runtime state export, facts and relations, reasoning over actual browser state, and controlled actions flowing back to execution.

### 6.1 DOM implementation

Implement a small but real DOM model in Cicilang:

- Document.
- Element.
- Text node.
- Attribute.
- Parent/child relationships.
- Node creation and deletion.
- Append, remove, and mutation.
- Traversal.
- Serialization.

Use Cicilang ownership and lifetime facilities. Establish the ownership model before adding asynchronous or cross-module references.

### 6.2 DOM reasoning facts

A DOM snapshot might export facts such as:

```prolog
element(node_12, button).
attribute(node_12, type, submit).
inside(node_12, node_4).
accessible_name(node_12, "Submit").
```

These are illustrative facts. The actual fact schema should be generated from or validated against the DOM contract.

Example query:

```prolog
?- element(Node, button),
   accessible_name(Node, Name).
```

Possible reasoning tasks:

- Find all buttons and their accessible names.
- Find elements with missing required attributes.
- Determine ancestor/descendant relationships.
- Explain why a node is inside a particular subtree.
- Identify which facts changed after a mutation.
- Explain the consequences of removing a node.

### 6.3 DOM milestone acceptance criteria

The proof of concept is complete when:

1. A DOM module contract can be parsed and validated.
2. Cocolog macros generate or validate its Cicilang-facing declarations and reasoning schema.
3. Cicilang compiles and runs the DOM implementation.
4. DOM ownership and lifetime errors are rejected as expected.
5. DOM mutations preserve the declared invariants.
6. A DOM snapshot can be exported to Cocolog.
7. Cocolog queries return results corresponding to the actual DOM state.
8. A reasoning result can request a controlled DOM operation through the execution interface.
9. Tests demonstrate that stale identities and old snapshots are handled correctly.

## 7. HTML and DOM parsing strategy

Cocolog's current HTML facilities can be evaluated as a starting point, but they should not be assumed to implement the complete HTML5 parsing algorithm.

The browser needs standards-compatible parsing behavior, including error recovery and tree-construction rules. Explicitly cover:

- Tokenization.
- Tree construction.
- Insertion modes.
- Implied elements.
- Foster parenting.
- Adoption agency behavior.
- Error recovery.
- Fragment parsing.
- Serialization.

Decide whether to extend the existing Cocolog HTML implementation, implement the parser in Cicilang, or integrate a compatible parser. Keep the parser's output contract independent from the DOM storage implementation.

## 8. CSS and style system

After the DOM proof of concept, build CSS in layers:

1. CSS tokenizer.
2. Parser and syntax tree.
3. CSS values and units.
4. Selector representation and matching.
5. Stylesheet and rule model.
6. Cascade and specificity.
7. Computed style.
8. Invalidation and dependency tracking.
9. Media and environment conditions.

Cicilang should perform selector matching and style computation. Cocolog should explain which selector matched, which declarations participated in the cascade, why a declaration won or lost, which changes invalidate a computed result, and which dependencies connect a style result to its sources.

## 9. Layout and rendering

Keep the DOM, computed style, layout tree, and paint representation distinct. A DOM node does not necessarily correspond to exactly one layout box or paint fragment.

Build progressively:

1. Basic block and inline layout.
2. Box model and sizing.
3. Text shaping and line layout.
4. Positioning.
5. Flexbox.
6. Grid.
7. Overflow and scrolling.
8. Paint command generation.
9. Compositing and invalidation.

Use Cicilang for orchestration and browser-specific logic, integrating native platform libraries where appropriate for text shaping, image decoding, graphics, windows, and GPU operations.

Cocolog should reason about constraints, dependencies, and explanations, not calculate every pixel.

## 10. Networking and navigation

Implement networking as a browser subsystem, not merely as an HTTP client.

Define explicit behavior for URL parsing and normalization, navigation lifecycle, redirects, resource fetching, cancellation, MIME and content handling, cache policy, cookies and credentials, referrer policy, origin and site relationships, TLS validation, Content Security Policy, and cross-origin resource rules.

Cocolog can reason about resource dependencies and policy decisions, but security-sensitive decisions must be enforced by deterministic execution-layer checks.

## 11. JavaScript integration

Treat JavaScript as a major independent engineering effort.

Recommended initial strategy:

- Integrate an existing embeddable JavaScript engine.
- Implement browser host objects and bindings in Cicilang.
- Define explicit DOM, event, timer, and networking interfaces.
- Keep engine internals behind a module boundary.
- Expose only selected, safe execution metadata to Cocolog.

A custom JavaScript engine should be considered only after the rest of the architecture is established and there is a compelling reason to build one.

## 12. Accessibility

Build accessibility as a first-class browser model, not as a late rendering feature.

The Cicilang implementation should construct the accessibility tree and expose the platform interface.

Cocolog can reason about missing accessible names, invalid or broken relationships, inconsistent roles and attributes, unreachable or misleading controls, and differences between DOM structure and accessibility structure.

Diagnostics should identify the relevant nodes and explain the rules behind each finding.

## 13. ZiguratIP persistence

Use ZiguratIP for durable, queryable browser knowledge and state.

Potential persisted data:

- Browsing history.
- Sessions and tabs.
- Bookmarks.
- Permissions and preferences.
- Search indexes.
- Annotations.
- Resource/cache metadata.
- Selected semantic summaries and relationships.

Do not persist live pointers, raw ownership references, or the complete JavaScript heap as ordinary durable records. Persist stable identifiers and versioned data that can be reconstructed or validated.

Define privacy and isolation boundaries from the beginning: separate browser profiles, private browsing behavior, retention and deletion rules, origin/site partitioning where required, and permission boundaries for page content and extensions.

## 14. Security architecture

Security is a foundational subsystem, not a final checklist.

The design must explicitly address same-origin policy, CORS, site isolation and process boundaries where applicable, sandboxing, Content Security Policy, cookie and credential rules, TLS certificate validation, mixed content, permission grants, download handling, extension isolation, and resource and execution limits.

Cocolog reasoning must not give untrusted page content unrestricted access to browser history, other tabs, local files, credentials, or privileged browser state. Reasoning capabilities should be mediated by explicit capabilities and access-control rules.

## 15. Development phases

### Phase 0 — Freeze and verify the toolchain

- Record exact Cicilang and Cocolog commits.
- Build and test each project independently.
- Verify Cicilang against the intended current Cocolog version.
- Reconcile dependency pins and recorded test versions.
- Record compiler, runtime, and platform requirements.
- Do not treat a version number or README statement as proof of compatibility; run the actual gates.

**Deliverable:** reproducible baseline and compatibility report.

### Phase 1 — Define the module contract

- Specify contract syntax and semantics.
- Define ownership and lifetime declarations.
- Define operations, invariants, and events.
- Define reasoning fact schemas.
- Define generated outputs and diagnostics.
- Build a minimal contract validator.

**Deliverable:** module contract specification and a small working example.

### Phase 2 — Validate Cocolog macro integration

- Inspect current Cocolog macro facilities.
- Identify what can be generated directly.
- Implement missing generation or deterministic export steps.
- Generate Cicilang declarations and reasoning adapters for a sample module.
- Add consistency and generation tests.

**Deliverable:** an end-to-end contract-to-Cicilang-and-Cocolog generation pipeline.

### Phase 3 — Implement the DOM proof of concept

- Implement DOM types and ownership in Cicilang.
- Implement core operations and invariants.
- Export versioned DOM facts.
- Query those facts in Cocolog.
- Route validated reasoning requests back through DOM operations.

**Deliverable:** a working reasoning-aware DOM vertical slice.

### Phase 4 — Add HTML parsing

- Define parser/DOM integration.
- Implement or integrate the required HTML parsing behavior.
- Add parsing and serialization tests.
- Verify that the resulting tree is visible to Cocolog.

**Deliverable:** HTML input to queryable DOM.

### Phase 5 — Add CSS

- Implement parsing, selectors, cascade, and computed style.
- Add invalidation and dependency tracking.
- Add Cocolog explanations for selector matches and cascade decisions.

**Deliverable:** a page with explainable computed styles.

### Phase 6 — Add layout and rendering

- Implement a basic layout subset.
- Generate paint commands.
- Integrate platform graphics services.
- Expose versioned layout and rendering diagnostics.

**Deliverable:** render a basic page and explain selected layout decisions.

### Phase 7 — Add networking and navigation

- Implement navigation and resource lifecycle.
- Add cache, redirect, cancellation, and origin handling.
- Integrate security policies.
- Expose resource dependencies to Cocolog.

**Deliverable:** load a page with multiple resources under explicit browser policies.

### Phase 8 — Add JavaScript integration

- Integrate an embeddable JavaScript engine.
- Implement host bindings and event-loop integration.
- Add DOM and event interoperability tests.
- Define the boundary between script execution and reasoning.

**Deliverable:** execute a basic interactive page.

### Phase 9 — Add accessibility and developer tools

- Build the accessibility tree.
- Add platform accessibility integration.
- Implement fact queries, diagnostics, and explanations.
- Provide inspection of DOM, style, layout, resources, and reasoning traces.

**Deliverable:** a browser that can inspect and explain its own state.

### Phase 10 — Add ZiguratIP persistence

- Persist sessions, history, bookmarks, permissions, indexes, and selected knowledge.
- Define profile isolation, retention, and private browsing.
- Add consistency and recovery tests.

**Deliverable:** persistent browser knowledge with privacy controls.

### Phase 11 — Compatibility, security, and performance

- Expand standards and interoperability tests.
- Audit security boundaries.
- Profile execution hot paths.
- Optimize data export and reasoning snapshots.
- Verify that reasoning overhead does not compromise normal browsing.

**Deliverable:** a hardened browser prototype with measured performance and documented limitations.

## 16. Testing strategy

Every module should have four categories of tests:

1. **Execution tests:** verify that Cicilang operations produce correct results.
2. **Contract tests:** verify ownership, lifetime, invariants, and interface conformance.
3. **Reasoning tests:** verify that Cocolog facts and queries accurately describe implementation state.
4. **Integration tests:** verify that generated interfaces, runtime state, and reasoning actions work together.

Add cross-module tests for version changes, mutation propagation, stale handles, failed operations, and recovery from malformed input.

For generated code, test both the generator and the generated output. Keep representative generated artifacts in tests where that helps detect unintended changes.

## 17. First concrete milestone

Do not begin by implementing every browser subsystem in parallel.

The first milestone should be:

> Define one DOM module contract, use Cocolog macros to generate or validate its Cicilang-facing interface and reasoning schema, implement the DOM in Cicilang, and prove that Cocolog can query and explain the actual live DOM state.

A successful DOM vertical slice validates the central design before the project takes on HTML5 parsing, CSS, layout, networking, JavaScript, and rendering complexity.

## 18. Open design questions

Resolve these questions during the first two phases:

1. What exact syntax and expansion model should Cocolog macros use for module contracts?
2. Which outputs should Cocolog generate directly, and which should be emitted in an intermediate format for Cicilang?
3. How are module versions and generated artifacts tied to source contracts?
4. How are runtime facts exported: full snapshots, incremental updates, or both?
5. How are document revisions and reasoning snapshots identified?
6. How are reasoning-requested operations authorized and validated?
7. Which parts of ZiguratIP are the durable source of truth, and which are rebuildable indexes or caches?
8. What is the minimum supported platform and standards target for the first browser prototype?

## 19. Success criteria

The architecture is validated when:

- Browser modules are implemented in Cicilang.
- Module contracts are expressed declaratively and handled through Cocolog macro integration.
- Generated interfaces and reasoning schemas remain consistent.
- Cocolog reasons about actual, versioned browser state.
- Reasoning can explain browser behavior and request safe, validated operations.
- Execution remains efficient because hot paths stay in Cicilang.
- ZiguratIP persists selected durable knowledge without retaining unsafe live runtime references.
- Security and privacy boundaries apply equally to execution and reasoning.

The defining achievement is not simply a browser that contains a Prolog engine. It is a browser whose implementation, contracts, state, and behavior are all available to a coherent reasoning system.
