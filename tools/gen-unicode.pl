#!/usr/bin/env perl
# tools/gen-unicode.pl > src/js/unicode.h -- the Unicode tables the engine's regular
# expressions and case conversions read, from perl's own Unicode::UCD (its Unicode
# version is the table's). Run it again when a newer Unicode is wanted; the header
# is committed, so a build needs no perl.
#
# What it writes:
#   - every property a \p{...} names (ECMA-262's lists): the general categories,
#     the scripts and their extensions, the binary properties, as ranges;
#   - the simple case mappings (upper, lower, folding) as runs of one delta, and the
#     full mappings that give more than one code point (SpecialCasing's
#     unconditional ones);
#   - the case orbits: the code points whose canonical forms are the same, under
#     simple case folding (a pattern with u) and under the uppercase canonicalization
#     of code units (one without).
use strict;
use warnings;
use feature qw(unicode_strings);
use Unicode::UCD qw(prop_invlist prop_invmap prop_values prop_value_aliases prop_aliases);
no warnings 'utf8';

my @ranges;            # [first, last] pairs of every property, in one array
my @props;             # [names, kind, offset, count]

sub add_prop {
    my ($names, $kind, @inv) = @_;
    my $off = scalar(@ranges) / 2;
    for (my $i = 0; $i < @inv; $i += 2) {
        my $lo = $inv[$i];
        my $hi = ($i + 1 < @inv ? $inv[$i + 1] : 0x110000) - 1;
        push @ranges, $lo, $hi;
    }
    push @props, [$names, $kind, $off, scalar(@ranges) / 2 - $off];
}

sub uniq { my %s; grep { !$s{$_}++ } @_ }

# the general categories (kind 0), each by its short and long names and their aliases
for my $v (prop_values("General_Category")) {
    my @names = uniq(prop_value_aliases("General_Category", $v));
    add_prop(join("|", @names), 0, prop_invlist("General_Category=$v"));
}
# the scripts (kind 1) and their extensions (kind 2), by the same names
for my $v (prop_values("Script")) {
    my @names = uniq(prop_value_aliases("Script", $v));
    add_prop(join("|", @names), 1, prop_invlist("Script=$v"));
    add_prop(join("|", @names), 2, prop_invlist("Script_Extensions=$v"));
}
# the binary properties ECMA-262 names (kind 3)
my @binary = qw(ASCII ASCII_Hex_Digit Alphabetic Any Assigned Bidi_Control Bidi_Mirrored Case_Ignorable Cased
    Changes_When_Casefolded Changes_When_Casemapped Changes_When_Lowercased Changes_When_NFKC_Casefolded
    Changes_When_Titlecased Changes_When_Uppercased Dash Default_Ignorable_Code_Point Deprecated Diacritic
    Emoji Emoji_Component Emoji_Modifier Emoji_Modifier_Base Emoji_Presentation Extended_Pictographic Extender
    Grapheme_Base Grapheme_Extend Hex_Digit IDS_Binary_Operator IDS_Trinary_Operator ID_Continue ID_Start
    Ideographic Join_Control Logical_Order_Exception Lowercase Math Noncharacter_Code_Point Pattern_Syntax
    Pattern_White_Space Quotation_Mark Radical Regional_Indicator Sentence_Terminal Soft_Dotted
    Terminal_Punctuation Unified_Ideograph Uppercase Variation_Selector White_Space XID_Continue XID_Start);
for my $p (@binary) {
    my @names = $p eq 'ASCII' || $p eq 'Any' || $p eq 'Assigned' ? ($p) : uniq(prop_aliases($p));
    add_prop(join("|", @names), 3, prop_invlist($p));
}

# a simple mapping's every changed code point, from its inversion map (format a:
# a range's first maps to the value given, each next one to one more; 0 itself)
sub mapping {
    my ($prop) = @_;
    my ($l, $m, $f) = prop_invmap($prop);
    die "$prop: format $f" unless $f eq 'a';
    my %map;
    for my $i (0 .. $#$l) {
        next if !$m->[$i];
        my $end = $i < $#$l ? $l->[$i + 1] : 0x110000;
        for my $cp ($l->[$i] .. $end - 1) { my $to = $m->[$i] + ($cp - $l->[$i]); $map{$cp} = $to if $to != $cp; }
    }
    return \%map;
}

# the mapping's runs: [lo, hi, delta, stride], stride 2 only over code points between
# that map to themselves, so that no two runs overlap
sub runs {
    my ($map) = @_;
    my @cps = sort { $a <=> $b } keys %$map;
    my @out;
    my $i = 0;
    while ($i < @cps) {
        my $lo = $cps[$i];
        my $d = $map->{$lo} - $lo;
        my $j = $i;
        if ($i + 1 < @cps && $cps[$i + 1] == $lo + 1 && $map->{$lo + 1} - ($lo + 1) == $d) {
            $j++ while $j + 1 < @cps && $cps[$j + 1] == $cps[$j] + 1 && $map->{$cps[$j + 1]} - $cps[$j + 1] == $d;
            push @out, [$lo, $cps[$j], $d, 1];
        } elsif ($i + 1 < @cps && $cps[$i + 1] == $lo + 2 && $map->{$lo + 2} - ($lo + 2) == $d && !exists $map->{$lo + 1}) {
            $j++ while $j + 1 < @cps && $cps[$j + 1] == $cps[$j] + 2 && $map->{$cps[$j + 1]} - $cps[$j + 1] == $d && !exists $map->{$cps[$j] + 1};
            push @out, [$lo, $cps[$j], $d, 2];
        } else {
            push @out, [$lo, $lo, $d, 1];
        }
        $i = $j + 1;
    }
    return \@out;
}

my $upper = mapping("Simple_Uppercase_Mapping");
my $lower = mapping("Simple_Lowercase_Mapping");
my $fold = mapping("Simple_Case_Folding");

# the full mappings to more than one code point
my (@upsp, @losp);
for my $cp (0 .. 0x10FFFF) {
    next if $cp >= 0xD800 && $cp <= 0xDFFF;
    my $c = chr($cp);
    my $u = uc($c);
    push @upsp, [$cp, map { ord } split //, $u] if length($u) > 1;
    my $l = lc($c);
    push @losp, [$cp, map { ord } split //, $l] if length($l) > 1;
}

# the orbits: code points grouped by their canonical form
sub orbits {
    my ($canon) = @_;
    my %group;
    for my $cp (keys %$canon) { push @{$group{$canon->{$cp}}}, $cp; }
    my @pairs;
    for my $g (values %group) {
        my @m = sort { $a <=> $b } uniq(@$g);
        next if @m < 2;
        for my $k (0 .. $#m) { push @pairs, [$m[$k], $m[($k + 1) % @m]]; }
    }
    return [sort { $a->[0] <=> $b->[0] } @pairs];
}
# with u: simple case folding, every code point that folds, and what it folds to
my %cu;
for my $cp (keys %$fold) { $cu{$cp} = $fold->{$cp}; $cu{$fold->{$cp}} = $fold->{$cp}; }
# without u: a code unit's uppercase, when that is one code unit and not ASCII from beyond it
my %cb;
for my $cp (0 .. 0xFFFF) {
    next if $cp >= 0xD800 && $cp <= 0xDFFF;
    my $u = uc(chr($cp));
    my $c = $cp;
    if (length($u) == 1 && ord($u) <= 0xFFFF && !($cp >= 128 && ord($u) < 128)) { $c = ord($u); }
    $cb{$cp} = $c;
}
my $ou = orbits(\%cu);
my $ob = orbits(\%cb);

# ---- the header ----
my $ver = Unicode::UCD::UnicodeVersion();
print "/* src/js/unicode.h -- generated by tools/gen-unicode.pl from perl's Unicode::UCD, Unicode $ver:\n";
print "   the properties a regular expression's \\p names, the case mappings, and the case orbits\n";
print "   its i flag reads. Included by src/js/regexp.c alone. Do not edit. */\n";
print "#define UNI_VERSION \"$ver\"\n\n";
sub list {
    my ($name, $type, $per, @v) = @_;
    print "static const $type ${name}[] = {\n";
    for (my $i = 0; $i < @v; $i += $per) {
        my $e = $i + $per - 1;
        $e = $#v if $e > $#v;
        print "    ", join(", ", @v[$i .. $e]), ",\n";
    }
    print "};\n";
}
print "/* every property's ranges, [first, last] pairs, one after another */\n";
list("uni_ranges", "uint32_t", 12, @ranges);
print "/* a property: its names (| between aliases), its kind (0 a general category, 1 a script,\n";
print "   2 a script's extensions, 3 a binary property), its ranges' first pair and their count */\n";
print "static const struct uni_prop { const char *names; uint8_t kind; uint32_t off, n; } uni_props[] = {\n";
for my $p (@props) {
    my $n = $p->[0];
    print "    { \"$n\", $p->[1], $p->[2], $p->[3] },\n";
}
print "};\n#define UNI_NPROPS ", scalar(@props), "\n\n";
print "/* a simple case mapping: runs [lo, hi] whose every stride-th code point from lo maps by delta */\n";
print "struct uni_case { uint32_t lo, hi; int32_t delta; uint32_t stride; };\n";
for my $t (["uni_upper", $upper], ["uni_lower", $lower], ["uni_fold", $fold]) {
    my $r = runs($t->[1]);
    print "static const struct uni_case $t->[0]\[] = {\n";
    print "    { $_->[0], $_->[1], $_->[2], $_->[3] },\n" for @$r;
    print "};\n#define \U$t->[0]\E_N ", scalar(@$r), "\n";
}
print "\n/* the full mappings to more than one code point: the code point, then up to three */\n";
print "struct uni_special { uint32_t cp, to[3]; };\n";
for my $t (["uni_upper_special", \@upsp], ["uni_lower_special", \@losp]) {
    print "static const struct uni_special $t->[0]\[] = {\n";
    for my $s (@{$t->[1]}) { my @to = @$s[1 .. $#$s]; push @to, 0 while @to < 3; print "    { $s->[0], { ", join(", ", @to[0 .. 2]), " } },\n"; }
    print "};\n#define \U$t->[0]\E_N ", scalar(@{$t->[1]}), "\n";
}
print "\n/* the case orbits: (code point, the next in its orbit) pairs, sorted; an orbit is a cycle\n";
print "   that rises and comes back to its least. U: simple case folding's, with u; B: the\n";
print "   uppercase canonicalization of code units, without */\n";
list("uni_orbit_u", "uint32_t", 12, map { @$_ } @$ou);
print "#define UNI_ORBIT_U_N ", scalar(@$ou), "\n";
list("uni_orbit_b", "uint32_t", 12, map { @$_ } @$ob);
print "#define UNI_ORBIT_B_N ", scalar(@$ob), "\n";
