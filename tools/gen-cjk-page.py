#!/usr/bin/env python3
"""tools/gen-cjk-page.py SECTIONS > page.html -- a page of CJK text to measure line breaking with: SECTIONS
sections, each a heading and three paragraphs of Japanese, of Chinese, and of Japanese with Latin, numbers, a URL,
currency and percent in it (one paragraph 300px wide), every one broken between its characters as Chromium breaks
it (docs/performance.md, Phase 12ai). Deterministic."""
import sys

n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
ja = "日本語の文章は、単語の間に空白を置かずに書かれる。行は文字と文字の間で折り返され、句読点「。」や括弧は行頭に来ない。"
zh = "中文的句子同样没有空格，换行可以发生在任意两个汉字之间，但标点符号不能出现在行首。"
mix = "Ambitious は 2026 年に ICU 78 の規則で行を折り返す：URL https://example.com/path や 50% と ¥1,980 も。"
out = ["<!DOCTYPE html><html lang=ja><head><meta charset=utf-8><title>A page of CJK</title><style>",
       "body { font: 15px/1.6 serif; margin: 0 auto; max-width: 760px; padding: 0 12px }",
       "section p:nth-child(odd) { background: #f8f8f8 } .n { width: 300px }",
       "</style></head><body><h1>A page of CJK</h1>"]
for s in range(n):
    out.append('<section id="s%d"><h2>第%d節</h2>' % (s, s))
    out.append("<p>%s%s</p>" % (ja * 2, mix))
    out.append('<p lang=zh>%s%s</p>' % (zh * 2, zh))
    out.append('<p class=n>%s<em>%s</em></p>' % (ja, mix))
    out.append("</section>")
out.append("</body></html>")
sys.stdout.write("\n".join(out) + "\n")
