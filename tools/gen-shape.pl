#!/usr/bin/env perl
# tools/gen-shape.pl > src/paint/shape_data.h -- the character properties the shaper
# (src/paint/font.c's amb_shape) reads as HarfBuzz reads them, from perl's own Unicode::UCD
# and Unicode::Normalize (their Unicode version is the tables'): each code point's
# combining class as HarfBuzz modifies it (hb-unicode.hh: Hebrew's points in the SBL order,
# Arabic's shadda first, ...), the marks (General_Category Mn, Mc, Me), the canonical
# decompositions of one level and the primary composites (Hangul's are algorithmic), the
# joining types (ArabicShaping.txt's, as DerivedJoiningType has them), the scripts (each
# range's OpenType script tag), the mirrored glyphs (Bidi_Mirroring_Glyph) and the vertical
# orientations (UAX #50's Vertical_Orientation, vo). Run it again
# when a newer Unicode is wanted; the header is committed, so a build needs no perl.
use strict;
use warnings;
use Unicode::UCD qw(prop_invmap);
use Unicode::Normalize ();

# each property's ranges: [first, last, value] for each run of one value
sub ranges {
    my ($prop) = @_;
    my ($s, $v) = prop_invmap($prop);
    my @out;
    for my $i (0 .. $#$s) {
        my $last = $i < $#$s ? $s->[$i + 1] - 1 : 0x10FFFF;
        push @out, [$s->[$i], $last, $v->[$i]];
    }
    return @out;
}

# hb-unicode.hh's modified combining classes: Hebrew's fixed-position classes 10 to 26 in the
# SBL Hebrew manual's order, Arabic's shadda (33) before the other marks, Telugu's length marks
# (84, 91) as 4 and 5, Thai's sara u and uu (103) as 3, Tibetan's 130 and 132 swapped
my %modified = (10 => 22, 11 => 15, 12 => 16, 13 => 17, 14 => 23, 15 => 18, 16 => 19, 17 => 20, 18 => 21, 19 => 14,
                20 => 24, 21 => 12, 22 => 25, 23 => 13, 24 => 10, 25 => 11, 26 => 26, 27 => 28, 28 => 29, 29 => 30,
                30 => 31, 31 => 32, 32 => 33, 33 => 27, 84 => 4, 91 => 5, 103 => 3, 130 => 132, 132 => 131);
my @ccc;
for my $r (ranges('Canonical_Combining_Class')) {
    my ($a, $b, $c) = @$r;
    next unless $c;
    for my $cp ($a .. $b) {
        my $m = exists $modified{$c} ? $modified{$c} : $c;
        # hb_unicode_funcs_t::modified_combining_class's own cases (Tai Tham's, Tibetan's)
        $m = 0 if $cp == 0x1A60 || ($cp >= 0x1A75 && $cp <= 0x1A7C);
        $m = 254 if $cp == 0x0FC6;
        $m = 127 if $cp == 0x0F39;
        push @ccc, $cp << 8 | $m if $m;
    }
}

my %markkind = (Mn => 1, Mc => 2, Me => 3);
my @marks;
for my $r (ranges('General_Category')) {
    my ($a, $b, $g) = @$r;
    next unless $markkind{$g};
    push @marks, $a, $b | $markkind{$g} << 24;
}

# canonical decompositions of one level (a code point into one or two), Hangul's syllables apart
my ($ds, $dv) = prop_invmap('Decomposition_Mapping');
my ($ts, $tv) = prop_invmap('Decomposition_Type');
my %dtype;
for my $i (0 .. $#$ts) {
    next unless $tv->[$i] eq 'Canonical';
    my $last = $i < $#$ts ? $ts->[$i + 1] - 1 : 0x10FFFF;
    $dtype{$_} = 1 for $ts->[$i] .. $last;
}
my (@decomp, @comp);
for my $i (0 .. $#$ds) {
    my $v = $dv->[$i];
    next unless ref $v;
    my $cp = $ds->[$i];
    next unless $dtype{$cp};
    next if $cp >= 0xAC00 && $cp <= 0xD7A3;
    my @parts = @$v;
    die sprintf("gen-shape: U+%04X decomposes into %d\n", $cp, scalar @parts) if @parts > 2;
    push @decomp, [$cp, $parts[0], $parts[1] // 0];
    # a primary composite: what NFC makes of its two parts (getComposite gives the excluded ones too,
    # Hebrew's presentation forms among them)
    if (@parts == 2 && Unicode::Normalize::NFC(chr($parts[0]) . chr($parts[1])) eq chr($cp)) {
        push @comp, [$parts[0], $parts[1], $cp];
    }
}
@decomp = sort { $a->[0] <=> $b->[0] } @decomp;
@comp = sort { $a->[0] <=> $b->[0] || $a->[1] <=> $b->[1] } @comp;

# joining types: R 1, D 2, C 3, L 4, T 5 (U, the rest, left out)
my %jt = (R => 1, D => 2, C => 3, L => 4, T => 5);
my @joining;
for my $r (ranges('Joining_Type')) {
    my ($a, $b, $j) = @$r;
    next if $j eq 'U';
    die "gen-shape: joining type $j\n" unless $jt{$j};
    push @joining, $a, $b | $jt{$j} << 24;
}

# scripts: each range's first code point << 8 | its script's index in amb_sh_tags, the OpenType
# script tag HarfBuzz looks for first (hb_ot_old_tag_from_script: the ISO 15924 code in lower
# case, Hiragana as kana, Lao, Yi, N'Ko and Vai space-padded); Common, Inherited and Unknown
# index 0 (no script), each run to the next range's first
my %special = (Hiragana => 'kana', Katakana => 'kana', Lao => 'lao ', Yi => 'yi  ', Nko => 'nko ', Vai => 'vai ');
my (@tags, %tagidx, @scripts);
push @tags, '    ';
for my $r (ranges('Script')) {
    my ($a, $b, $s) = @$r;
    my $idx = 0;
    if ($s ne 'Common' && $s ne 'Inherited' && $s ne 'Unknown') {
        my $tag = $special{$s};
        unless ($tag) {
            my @aliases = Unicode::UCD::prop_value_aliases('Script', $s);
            $tag = lc $aliases[0];
        }
        die "gen-shape: the script $s has no four-letter code\n" unless length $tag == 4;
        unless (exists $tagidx{$tag}) { $tagidx{$tag} = scalar @tags; push @tags, $tag; }
        $idx = $tagidx{$tag};
    }
    # Inherited marks the code point as taking the script before it (2, kept apart from Common, 1)
    $idx = 255 if $s eq 'Inherited';
    $idx = 254 if $s eq 'Common';
    next if @scripts && ($scripts[-1] & 255) == $idx;
    push @scripts, $a << 8 | $idx;
}
die "gen-shape: too many scripts\n" if @tags > 250;

# the vertical orientation (UAX #50's Vertical_Orientation, which perl's Unicode::UCD carries as vo): U 1, Tu 2, Tr 3
# (R, the rest, left out): what a vertical line of text-orientation mixed sets upright
my %vo = (U => 1, Upright => 1, Tu => 2, Transformed_Upright => 2, Tr => 3, Transformed_Rotated => 3);
my @vo;
for my $r (ranges('Vertical_Orientation')) {
    my ($a, $b, $v) = @$r;
    next if $v eq 'R' || $v eq 'Rotated';
    die "gen-shape: vertical orientation $v\n" unless $vo{$v};
    push @vo, $a, $b | $vo{$v} << 24;
}

my ($ms, $mv) = prop_invmap('Bidi_Mirroring_Glyph');
my @mirror;
for my $i (0 .. $#$ms) {
    next if $mv->[$i] eq '';
    push @mirror, $ms->[$i], $mv->[$i] + 0;
}

sub table {
    my ($name, $count, $what, @v) = @_;
    my $out = "#define $count " . scalar(@v) . "\nstatic const uint32_t $name\[$count] = {\n";
    for (my $i = 0; $i < @v; $i += 8) {
        my $end = $i + 7 < $#v ? $i + 7 : $#v;
        $out .= "    " . join(", ", map { sprintf "0x%x", $_ } @v[$i .. $end]) . ",\n";
    }
    return "\n/* $what */\n$out};\n";
}

my $u = Unicode::UCD::UnicodeVersion();
print <<"END";
/* src/paint/shape_data.h -- generated by tools/gen-shape.pl from perl's Unicode::UCD (Unicode
   $u): the character properties src/paint/font.c's shaper reads as HarfBuzz reads them. Do
   not edit; run the script. */
END
print table('amb_sh_ccc', 'AMB_SH_CCC', "each code point of a combining class but 0: cp << 8 | the class as HarfBuzz modifies it, in order", @ccc);
print table('amb_sh_mark', 'AMB_SH_MARK', "the marks: first, last | 1 Mn, 2 Mc, 3 Me << 24, each range", @marks);
print table('amb_sh_decomp', 'AMB_SH_DECOMP', "the canonical decompositions of one level: the code point, its first part, its second (0 for a singleton), in order",
            map { @$_ } @decomp);
print table('amb_sh_comp', 'AMB_SH_COMP', "the primary composites: first part, second part, the composite, in the parts' order", map { @$_ } @comp);
print table('amb_sh_joining', 'AMB_SH_JOINING', "the joining types: first, last | 1 R, 2 D, 3 C, 4 L, 5 T << 24, each range (U the rest)", @joining);
print table('amb_sh_script', 'AMB_SH_SCRIPT', "the scripts: each range's first << 8 | its index in amb_sh_tags (254 Common, 255 Inherited, 0 Unknown), to the next's first", @scripts);
print "\n/* the scripts' OpenType tags, by index (0: none) */\n#define AMB_SH_TAGS " . scalar(@tags) . "\nstatic const char amb_sh_tags[] = \"" . join('', @tags) . "\";\n";
print table('amb_sh_mirror', 'AMB_SH_MIRROR', "the mirrored glyphs: a code point, its mirror, in order", @mirror);
print table('amb_sh_vo', 'AMB_SH_VO', "the vertical orientations: first, last | 1 U, 2 Tu, 3 Tr << 24, each range (R the rest)", @vo);
