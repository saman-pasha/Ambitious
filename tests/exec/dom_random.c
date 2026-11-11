/* tests/exec/dom_random.c -- the DOM's invariants under a random walk of
   operations (the architecture's §6.3, criterion 5). A fixed seed drives 20000
   operations over a pool of handles that includes stale ones and nodes of every
   kind; after each, every invariant of the contract is checked, and the
   revision accounted for: an operation that succeeds moves it by one, a refused
   one leaves it, and the log holds one event a revision. Prints the counts, which
   the seed makes the same on every run. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);

static uint64_t rng_state = 0x9E3779B97F4A7C15u;

static uint32_t rnd(uint32_t n) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state % n);
}

int main(void) {
    static const char *names[] = { "div", "p", "span", "a", "b", "i", "ul", "li" };
    dom_document d;
    dom_init(&d, 1);
    vector<dom_node_h> hs;
    push_back(hs, dom_root(&d));
    int counts[16];
    memset(counts, 0, sizeof counts);
    int creates = 0, bad_inv = 0, bad_rev = 0, steps = 20000;
    for (int step = 0; step < steps; step++) {
        uint64_t before = dom_revision(&d);
        dom_node_h a = at(hs, rnd((uint32_t)len(hs)));
        dom_node_h b = at(hs, rnd((uint32_t)len(hs)));
        dom_node_h c = at(hs, rnd((uint32_t)len(hs)));
        dom_status st = DOM_OK;
        int made = 0;
        /* every hundred steps the pool keeps its live handles and one stale in eight */
        if (step % 100 == 99) {
            size_t k = 0;
            for (size_t i = 0; i < len(hs); i++) {
                dom_node_h h = at(hs, i);
                if (node_live(d, h) || rnd(8) == 0) hs[k++] = h;
            }
            resize(hs, k);
        }
        uint32_t op = node_count(d) > 400 ? 4 + rnd(2) : rnd(12);
        if (op == 11) {
            /* a cycle tried on purpose: b's topmost ancestor below the document,
               detached, then appended under b -- which is inside it */
            dom_node_h top = b;
            while (node_live(d, top) && dom_children_up(&d, top).index > 1) top = dom_children_up(&d, top);
            if (node_live(d, top) && top.index != 1) {
                if (dom_children_up(&d, top).index == 1) {
                    dom_remove_child(&d, dom_root(&d), top);
                    before = dom_revision(&d);
                }
                st = dom_append_child(&d, b, top);
                counts[st]++;
                if (dom_revision(&d) != before + (st == DOM_OK ? 1 : 0)) bad_rev++;
                if (dom_check_invariants(&d, NULL) != 0) bad_inv++;
            }
            continue;
        }
        if (op == 0) {
            uint32_t k = rnd(8);
            push_back(hs, dom_create_element(&d, names[k], strlen(names[k])));
            made = 1;
        } else if (op == 1) {
            push_back(hs, dom_create_text(&d, "t", 1));
            made = 1;
        } else if (op == 2) {
            push_back(hs, dom_create_comment(&d, "c", 1));
            made = 1;
        } else if (op == 3) st = dom_append_child(&d, a, b);
        else if (op == 4) st = dom_remove_child(&d, dom_children_up(&d, b), b);
        else if (op == 5) st = dom_destroy(&d, a);
        else if (op == 6) st = dom_insert_before(&d, a, b, c);
        else if (op == 7) st = dom_set_attribute(&d, a, "k", 1, rnd(2) ? "v" : "w", 1);
        else if (op == 8) st = dom_remove_attribute(&d, a, "k", 1);
        else if (op == 9) st = dom_set_data(&d, a, "x", 1);
        else st = dom_append_child(&d, dom_children_up(&d, a), b);
        if (made) creates++;
        else counts[st]++;
        uint64_t want = before + ((made || st == DOM_OK) ? 1 : 0);
        if (dom_revision(&d) != want) bad_rev++;
        if (dom_check_invariants(&d, NULL) != 0) bad_inv++;
    }
    printf("steps: %d, creates: %d\n", steps, creates);
    for (int s = 0; s < 16; s++)
        if (counts[s]) printf("  %-16s %d\n", dom_status_name((dom_status)s), counts[s]);
    printf("live nodes at the end: %zu of %zu slots; revision %llu; events logged %zu\n",
           node_count(d), node_len(d) - 1, (unsigned long long)dom_revision(&d), mutation_len(d));
    int ok = bad_inv == 0 && bad_rev == 0 && mutation_len(d) == dom_revision(&d);
    printf("invariants broken after %d steps; revisions mis-counted after %d\n", bad_inv, bad_rev);
    printf("%s: the random walk\n", ok ? "GREEN" : "RED");
    return ok ? 0 : 1;
}
