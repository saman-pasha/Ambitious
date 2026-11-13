// tests/js/uniprops.js -- the Unicode properties a \p{...} names, each line against V8's
// (uniprops.expect): every general category, script, script extension and binary property
// ECMA-262 names (its tables 67 to 69), at Unicode 17.0, over every code point: how many
// code points, in how many ranges, the first two and the last of them, and a hash of all;
// a few with i and with v; and each name and alias V8 takes, with those it refuses.

const p = (...a) => console.log(a.join(' '));
const hex = c => c.toString(16);

// every code point but the surrogates, in order; the surrogates apart, the low ones first
// (no two of them then make a pair)
const parts = [];
for (let b = 0; b < 0x110000; b += 0x400) {
  if (b >= 0xD800 && b < 0xE000) continue;
  const a = [];
  for (let i = 0; i < 0x400; i++) a.push(b + i);
  parts.push(String.fromCodePoint(...a));
}
const S = parts.join('');
const at = i => i < 0xD800 ? i : i < 0xF800 ? i + 0x800 : 0x10000 + ((i - 0xF800) >> 1);
const su = [];
for (let c = 0xDC00; c < 0xE000; c++) su.push(c);
for (let c = 0xD800; c < 0xDC00; c++) su.push(c);
const T = String.fromCharCode(...su);
const tat = i => i < 0x400 ? 0xDC00 + i : 0xD800 + i - 0x400;

// the code points a /...+/g (u or v) matches, as sorted [first, last] ranges
function ranges(re) {
  const r = [];
  for (const m of S.matchAll(re)) r.push([at(m.index), at(m.index + m[0].length - 1)]);
  for (const m of T.matchAll(re)) {
    const a = m.index, b = m.index + m[0].length - 1;
    if (a < 0x400 && b >= 0x400) { r.push([tat(a), 0xDFFF]); r.push([0xD800, tat(b)]); }
    else r.push([tat(a), tat(b)]);
  }
  r.sort((x, y) => x[0] - y[0]);
  const out = [];
  for (const x of r) {
    if (out.length && x[0] <= out[out.length - 1][1] + 1) out[out.length - 1][1] = Math.max(out[out.length - 1][1], x[1]);
    else out.push(x);
  }
  return out;
}
const show = r => r.map(x => x[0] === x[1] ? hex(x[0]) : hex(x[0]) + '-' + hex(x[1]));
function set(label, src, flags) {
  let re;
  try { re = new RegExp(src + '+', 'g' + flags); } catch (e) { p(label, e.name, e.message); return; }
  const r = ranges(re);
  let n = 0, h = 0;
  for (const [lo, hi] of r) { n += hi - lo + 1; h = (h * 31 + lo) >>> 0; h = (h * 31 + hi) >>> 0; }
  p(label, n, r.length, show(r.slice(0, 2)).join(' '), '..', show(r.slice(-1)).join(' '), h);
}

const GC = [
  'C|Other', 'Cc|Control|cntrl', 'Cf|Format', 'Cn|Unassigned', 'Co|Private_Use', 'Cs|Surrogate', 'L|Letter',
  'LC|Cased_Letter', 'Ll|Lowercase_Letter', 'Lm|Modifier_Letter', 'Lo|Other_Letter', 'Lt|Titlecase_Letter',
  'Lu|Uppercase_Letter', 'M|Mark|Combining_Mark', 'Mc|Spacing_Mark', 'Me|Enclosing_Mark', 'Mn|Nonspacing_Mark',
  'N|Number', 'Nd|Decimal_Number|digit', 'Nl|Letter_Number', 'No|Other_Number', 'P|Punctuation|punct',
  'Pc|Connector_Punctuation', 'Pd|Dash_Punctuation', 'Pe|Close_Punctuation', 'Pf|Final_Punctuation',
  'Pi|Initial_Punctuation', 'Po|Other_Punctuation', 'Ps|Open_Punctuation', 'S|Symbol', 'Sc|Currency_Symbol',
  'Sk|Modifier_Symbol', 'Sm|Math_Symbol', 'So|Other_Symbol', 'Z|Separator', 'Zl|Line_Separator',
  'Zp|Paragraph_Separator', 'Zs|Space_Separator'];
const SC = [
  'Adlm|Adlam', 'Aghb|Caucasian_Albanian', 'Ahom', 'Arab|Arabic', 'Armi|Imperial_Aramaic', 'Armn|Armenian',
  'Avst|Avestan', 'Bali|Balinese', 'Bamu|Bamum', 'Bass|Bassa_Vah', 'Batk|Batak', 'Beng|Bengali', 'Berf|Beria_Erfe',
  'Bhks|Bhaiksuki', 'Bopo|Bopomofo', 'Brah|Brahmi', 'Brai|Braille', 'Bugi|Buginese', 'Buhd|Buhid', 'Cakm|Chakma',
  'Cans|Canadian_Aboriginal', 'Cari|Carian', 'Cham', 'Cher|Cherokee', 'Chrs|Chorasmian', 'Copt|Coptic|Qaac',
  'Cpmn|Cypro_Minoan', 'Cprt|Cypriot', 'Cyrl|Cyrillic', 'Deva|Devanagari', 'Diak|Dives_Akuru', 'Dogr|Dogra',
  'Dsrt|Deseret', 'Dupl|Duployan', 'Egyp|Egyptian_Hieroglyphs', 'Elba|Elbasan', 'Elym|Elymaic', 'Ethi|Ethiopic',
  'Gara|Garay', 'Geor|Georgian', 'Glag|Glagolitic', 'Gong|Gunjala_Gondi', 'Gonm|Masaram_Gondi', 'Goth|Gothic',
  'Gran|Grantha', 'Grek|Greek', 'Gujr|Gujarati', 'Gukh|Gurung_Khema', 'Guru|Gurmukhi', 'Hang|Hangul', 'Hani|Han',
  'Hano|Hanunoo', 'Hatr|Hatran', 'Hebr|Hebrew', 'Hira|Hiragana', 'Hluw|Anatolian_Hieroglyphs', 'Hmng|Pahawh_Hmong',
  'Hmnp|Nyiakeng_Puachue_Hmong', 'Hung|Old_Hungarian', 'Ital|Old_Italic', 'Java|Javanese', 'Kali|Kayah_Li',
  'Kana|Katakana', 'Kawi', 'Khar|Kharoshthi', 'Khmr|Khmer', 'Khoj|Khojki', 'Kits|Khitan_Small_Script',
  'Knda|Kannada', 'Krai|Kirat_Rai', 'Kthi|Kaithi', 'Lana|Tai_Tham', 'Laoo|Lao', 'Latn|Latin', 'Lepc|Lepcha',
  'Limb|Limbu', 'Lina|Linear_A', 'Linb|Linear_B', 'Lisu', 'Lyci|Lycian', 'Lydi|Lydian', 'Mahj|Mahajani',
  'Maka|Makasar', 'Mand|Mandaic', 'Mani|Manichaean', 'Marc|Marchen', 'Medf|Medefaidrin', 'Mend|Mende_Kikakui',
  'Merc|Meroitic_Cursive', 'Mero|Meroitic_Hieroglyphs', 'Mlym|Malayalam', 'Modi', 'Mong|Mongolian', 'Mroo|Mro',
  'Mtei|Meetei_Mayek', 'Mult|Multani', 'Mymr|Myanmar', 'Nagm|Nag_Mundari', 'Nand|Nandinagari',
  'Narb|Old_North_Arabian', 'Nbat|Nabataean', 'Newa', 'Nkoo|Nko', 'Nshu|Nushu', 'Ogam|Ogham', 'Olck|Ol_Chiki',
  'Onao|Ol_Onal', 'Orkh|Old_Turkic', 'Orya|Oriya', 'Osge|Osage', 'Osma|Osmanya', 'Ougr|Old_Uyghur', 'Palm|Palmyrene',
  'Pauc|Pau_Cin_Hau', 'Perm|Old_Permic', 'Phag|Phags_Pa', 'Phli|Inscriptional_Pahlavi', 'Phlp|Psalter_Pahlavi',
  'Phnx|Phoenician', 'Plrd|Miao', 'Prti|Inscriptional_Parthian', 'Rjng|Rejang', 'Rohg|Hanifi_Rohingya', 'Runr|Runic',
  'Samr|Samaritan', 'Sarb|Old_South_Arabian', 'Saur|Saurashtra', 'Sgnw|SignWriting', 'Shaw|Shavian', 'Shrd|Sharada',
  'Sidd|Siddham', 'Sidt|Sidetic', 'Sind|Khudawadi', 'Sinh|Sinhala', 'Sogd|Sogdian', 'Sogo|Old_Sogdian',
  'Sora|Sora_Sompeng', 'Soyo|Soyombo', 'Sund|Sundanese', 'Sunu|Sunuwar', 'Sylo|Syloti_Nagri', 'Syrc|Syriac',
  'Tagb|Tagbanwa', 'Takr|Takri', 'Tale|Tai_Le', 'Talu|New_Tai_Lue', 'Taml|Tamil', 'Tang|Tangut', 'Tavt|Tai_Viet',
  'Tayo|Tai_Yo', 'Telu|Telugu', 'Tfng|Tifinagh', 'Tglg|Tagalog', 'Thaa|Thaana', 'Thai', 'Tibt|Tibetan',
  'Tirh|Tirhuta', 'Tnsa|Tangsa', 'Todr|Todhri', 'Tols|Tolong_Siki', 'Toto', 'Tutg|Tulu_Tigalari', 'Ugar|Ugaritic',
  'Vaii|Vai', 'Vith|Vithkuqi', 'Wara|Warang_Citi', 'Wcho|Wancho', 'Xpeo|Old_Persian', 'Xsux|Cuneiform',
  'Yezi|Yezidi', 'Yiii|Yi', 'Zanb|Zanabazar_Square', 'Zinh|Inherited|Qaai', 'Zyyy|Common', 'Zzzz|Unknown'];
const BIN = ['ASCII', 'Any', 'Assigned',
  'AHex|ASCII_Hex_Digit', 'Alpha|Alphabetic', 'Bidi_C|Bidi_Control', 'Bidi_M|Bidi_Mirrored', 'CI|Case_Ignorable',
  'Cased', 'CWCF|Changes_When_Casefolded', 'CWCM|Changes_When_Casemapped', 'CWL|Changes_When_Lowercased',
  'CWKCF|Changes_When_NFKC_Casefolded', 'CWT|Changes_When_Titlecased', 'CWU|Changes_When_Uppercased', 'Dash',
  'DI|Default_Ignorable_Code_Point', 'Dep|Deprecated', 'Dia|Diacritic', 'Emoji', 'EComp|Emoji_Component',
  'EMod|Emoji_Modifier', 'EBase|Emoji_Modifier_Base', 'EPres|Emoji_Presentation', 'ExtPict|Extended_Pictographic',
  'Ext|Extender', 'Gr_Base|Grapheme_Base', 'Gr_Ext|Grapheme_Extend', 'Hex|Hex_Digit', 'IDSB|IDS_Binary_Operator',
  'IDST|IDS_Trinary_Operator', 'IDC|ID_Continue', 'IDS|ID_Start', 'Ideo|Ideographic', 'Join_C|Join_Control',
  'LOE|Logical_Order_Exception', 'Lower|Lowercase', 'Math', 'NChar|Noncharacter_Code_Point',
  'Pat_Syn|Pattern_Syntax', 'Pat_WS|Pattern_White_Space', 'QMark|Quotation_Mark', 'Radical', 'RI|Regional_Indicator',
  'STerm|Sentence_Terminal', 'SD|Soft_Dotted', 'Term|Terminal_Punctuation', 'UIdeo|Unified_Ideograph',
  'Upper|Uppercase', 'VS|Variation_Selector', 'WSpace|White_Space|space', 'XIDC|XID_Continue', 'XIDS|XID_Start'];

// every general category, script and its extensions, binary property, over every code point
{
for (const v of GC) set('gc=' + v.split('|')[0], '\\p{gc=' + v.split('|')[0] + '}', 'u');
for (const v of SC) {
  const s = v.split('|')[0];
  set('sc=' + s, '\\p{sc=' + s + '}', 'u');
  set('scx=' + s, '\\p{scx=' + s + '}', 'u');
}
for (const v of BIN) set(v.split('|')[0], '\\p{' + v.split('|')[0] + '}', 'u');
}

// the complements, with v, and case-insensitively: u's closes a property under case, v's
// complements after closing
{
for (const n of ['L', 'Lu', 'Cn', 'sc=Grek', 'scx=Hira', 'Alpha']) {
  set('P{' + n + '}', '\\P{' + n + '}', 'u');
  set('p{' + n + '}/v', '\\p{' + n + '}', 'v');
}
for (const n of ['Lu', 'Ll', 'Lt', 'LC', 'Lower', 'Upper', 'Cased', 'CWCF', 'CWU', 'sc=Latn', 'sc=Grek', 'sc=Cyrl',
                 'sc=Geor', 'sc=Gara', 'sc=Vith', 'sc=Adlm', 'scx=Copt', 'sc=Cher', 'sc=Tols', 'sc=Berf']) {
  set('p{' + n + '}/iu', '\\p{' + n + '}', 'iu');
  set('P{' + n + '}/iu', '\\P{' + n + '}', 'iu');
  set('p{' + n + '}/iv', '\\p{' + n + '}', 'iv');
  set('P{' + n + '}/iv', '\\P{' + n + '}', 'iv');
}
}

// the names: each alias by every spelling of its property, 1 where V8 takes it
{
const ok = (s, f) => { try { new RegExp(s, f); return 1; } catch (e) { return 0; } };
for (const v of GC) {
  const r = [];
  for (const a of v.split('|')) for (const f of ['', 'gc=', 'General_Category=']) r.push(ok('\\p{' + f + a + '}', 'u'), ok('\\P{' + f + a + '}', 'v'));
  p(v, r.join(''));
}
for (const v of SC) {
  const r = [];
  for (const a of v.split('|')) for (const f of ['', 'sc=', 'Script=', 'scx=', 'Script_Extensions=']) r.push(ok('\\p{' + f + a + '}', 'u'), ok('\\P{' + f + a + '}', 'v'));
  p(v, r.join(''));
}
for (const v of BIN) {
  const r = [];
  for (const a of v.split('|')) for (const f of ['', '=Y', '=Yes', '=N']) r.push(ok('\\p{' + a + f + '}', 'u'), ok('\\P{' + a + f + '}', 'v'));
  p(v, r.join(''));
}
// those V8 refuses: other spellings, the properties ECMA-262 does not name, values no
// code point has (Katakana_Or_Hiragana), scripts not yet encoded (Chisoi), script codes
// that are not Script values
const no = ['Cntrl', 'Digit', 'Punct', 'Space', 'CNTRL', 'L&', 'lu', 'LU', 'uppercase_letter', 'Uppercase Letter',
  'Uppercase-Letter', 'UppercaseLetter', ' Lu', 'Lu ', 'any', 'ascii', 'assigned', 'ANY', 'Ascii', 'alphabetic', 'White Space',
  'WhiteSpace', 'wspace', 'Is_Lu', 'IsLu', 'InBasic_Latin', 'L=Y', 'gc=L=Y', 'gc', 'sc', 'scx', 'gc=', 'sc=', '=Lu', '',
  'Hrkt', 'sc=Hrkt', 'scx=Hrkt', 'Script=Katakana_Or_Hiragana', 'scx=Katakana_Or_Hiragana', 'sc=Chis', 'sc=Chisoi',
  'scx=Chisoi', 'sc=Zxxx', 'sc=Zmth', 'sc=Zsym', 'sc=Zsye', 'sc=Jpan', 'sc=Hans', 'sc=Hant', 'sc=Kore', 'sc=Hanb',
  'sc=Jamo', 'sc=Latf', 'sc=Latg', 'sc=Syre', 'sc=Syrj', 'sc=Syrn', 'sc=Aran', 'sc=Cyrs', 'sc=Geok', 'sc=Qaaa',
  'sc=latn', 'sc=LATN', 'sc=latin', 'sc=Old Italic', 'sc=OldItalic', 'Latin', 'Latn', 'Greek', 'Script=Greek=Y',
  'script=Latn', 'SC=Latn', 'Sc=Latn', 'gc=Latn', 'sc=Lu', 'gc=Alphabetic', 'General_Category=ASCII', 'gc=Any',
  'Script_Extensions=Any', 'Block=Basic_Latin', 'blk=ASCII', 'Age=17.0', 'age=V17_0', 'Bidi_Class=L', 'bc=AL',
  'Line_Break=AL', 'lb=AL', 'Word_Break=ALetter', 'East_Asian_Width=W', 'ea=W', 'Hangul_Syllable_Type=L', 'hst=LV',
  'Joining_Type=D', 'Indic_Conjunct_Break=Linker', 'InCB=Linker', 'Indic_Syllabic_Category=Vowel',
  'Canonical_Combining_Class=230', 'ccc=230', 'Numeric_Type=De', 'nt=Nu', 'Decomposition_Type=Can',
  'Grapheme_Cluster_Break=L', 'GCB=L', 'Sentence_Break=ATerm', 'Vertical_Orientation=U', 'NFC_QC=N', 'NFKC_QC=M',
  'ID_Compat_Math_Start', 'ID_Compat_Math_Continue', 'IDS_Unary_Operator', 'IDSU', 'Modifier_Combining_Mark', 'MCM',
  'Other_Alphabetic', 'OAlpha', 'Other_Lowercase', 'OLower', 'Other_Uppercase', 'OUpper', 'Other_Math', 'OMath',
  'Other_ID_Start', 'OIDS', 'Other_ID_Continue', 'OIDC', 'Other_Grapheme_Extend', 'OGr_Ext',
  'Other_Default_Ignorable_Code_Point', 'ODI', 'Hyphen', 'Prepended_Concatenation_Mark', 'PCM', 'Grapheme_Link',
  'Gr_Link', 'Full_Composition_Exclusion', 'Comp_Ex', 'Composition_Exclusion', 'CE', 'Expands_On_NFC', 'XO_NFC',
  'FC_NFKC_Closure', 'NFKC_Casefold', 'NFKC_Simple_Casefold', 'Case_Folding', 'Lowercase_Mapping', 'Unified_Ideograph=Y',
  'RGI_Emoji', 'Basic_Emoji', 'Emoji_Keycap_Sequence', 'RGI_Emoji_Modifier_Sequence', 'RGI_Emoji_Flag_Sequence',
  'RGI_Emoji_Tag_Sequence', 'RGI_Emoji_ZWJ_Sequence', 'Emoji_Test', 'RGI_Emoji_Test', 'Emoji_Keycap', 'rgi_emoji',
  'Emoji_Modifier_Sequence', 'Emoji_Flag_Sequence', 'Emoji_Tag_Sequence', 'Emoji_ZWJ_Sequence'];
for (const n of no) p(JSON.stringify(n), ok('\\p{' + n + '}', 'u'), ok('\\P{' + n + '}', 'u'), ok('\\p{' + n + '}', 'v'), ok('[\\p{' + n + '}]', 'v'), ok('\\p{' + n + '}', ''));
}
