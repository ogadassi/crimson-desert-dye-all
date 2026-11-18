"""Builds page/preview.html: the mod page as it will roughly look, from description.bbcode and the two images.

    python page/build_preview.py
"""
import html
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
bb = open(os.path.join(HERE, "description.bbcode"), encoding="utf-8").read()

SIZES = {1: 10, 2: 13, 3: 15, 4: 18, 5: 24, 6: 32}


def render(src):
    s = html.escape(src).replace("BANNER_URL", "header_1300x372.png")
    s = re.sub(r"\[spoiler\]\n?(.*?)\n?\[/spoiler\]",
               r'<details class="spoiler"><summary>Spoiler</summary><div>\1</div></details>', s, flags=re.S)
    s = re.sub(r"\[list\]\n?(.*?)\n?\[/list\]",
               lambda m: "<ul>" + "".join("<li>%s</li>" % x.strip() for x in m.group(1).split("[*]") if x.strip()) + "</ul>",
               s, flags=re.S)
    s = re.sub(r"\[center\](.*?)\[/center\]", r'<div class="c">\1</div>', s, flags=re.S)
    s = re.sub(r"\[size=(\d)\](.*?)\[/size\]", lambda m: '<span style="font-size:%dpx">%s</span>' % (SIZES[int(m.group(1))], m.group(2)), s, flags=re.S)
    s = re.sub(r"\[color=(#[0-9a-fA-F]{6})\](.*?)\[/color\]", r'<span style="color:\1">\2</span>', s, flags=re.S)
    s = re.sub(r"\[img\](.*?)\[/img\]", r'<img src="\1" alt="">', s)
    s = re.sub(r"\[url=(.*?)\](.*?)\[/url\]", r'<a href="\1">\2</a>', s)
    for t, h in (("b", "b"), ("i", "i"), ("u", "u")):
        s = s.replace("[%s]" % t, "<%s>" % h).replace("[/%s]" % t, "</%s>" % h)
    s = s.replace("[line]", "<hr>")
    s = s.replace("\n", "<br>\n")
    s = re.sub(r"(<br>\n)*(<hr>|<ul>|</ul>|<details|</details>|<div class=\"c\">|</div>)(<br>\n)?", r"\2", s)
    return s


page = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Dye All page preview</title>
<style>
  :root { --bg:#141414; --panel:#1e1e1e; --line:#333; --text:#cfcfcf; --dim:#8a8a8a; --accent:#c9a45c; }
  * { box-sizing: border-box; }
  body { margin:0; background:var(--bg); color:var(--text); font:15px/1.6 "Segoe UI", Roboto, Arial, sans-serif; }
  .note { background:#2a2414; color:#e8d7a8; font-size:13px; padding:8px 16px; text-align:center; }
  .hero { height:auto; aspect-ratio:1300/372; background:url(header_1300x372.png) center/cover; border-bottom:1px solid var(--line); }
  .wrap { max-width:1080px; margin:0 auto; padding:0 16px; }
  .head { display:flex; gap:20px; align-items:flex-end; margin-top:20px; }
  .thumb { width:240px; aspect-ratio:16/9; background:url(gallery_1920x1080.png) center/cover; border:2px solid var(--panel); border-radius:6px; flex:none; box-shadow:0 6px 20px rgba(0,0,0,.6); }
  h1 { margin:0 0 4px; font-size:30px; color:#fff; }
  .meta { color:var(--dim); font-size:13px; }
  .tabs { display:flex; gap:22px; border-bottom:1px solid var(--line); margin:22px 0 0; font-size:14px; }
  .tabs span { padding:10px 0; color:var(--dim); }
  .tabs .on { color:#fff; border-bottom:2px solid var(--accent); }
  .summary { color:var(--dim); font-style:italic; margin:18px 0; }
  .desc { background:var(--panel); border-radius:6px; padding:28px 32px; margin-bottom:60px; overflow-wrap:anywhere; }
  .desc img { max-width:100%; height:auto; border-radius:4px; }
  .desc .c { text-align:center; }
  .desc hr { border:0; border-top:1px solid var(--line); margin:26px 0; }
  .desc ul { margin:6px 0; padding-left:22px; }
  .spoiler { background:#171717; border:1px solid var(--line); border-radius:4px; padding:8px 14px; margin:6px 0; }
  .spoiler summary { cursor:pointer; color:var(--dim); }
  @media (max-width:640px) {  .head { flex-direction:column; align-items:flex-start; }
    .thumb { width:160px; } .desc { padding:18px 16px; } }
</style></head><body>
<div class="note">Local preview of the mod page. The real site's fonts and spacing differ a little.</div>
<div class="hero"></div>
<div class="wrap">
  <div class="head"><div class="thumb"></div>
    <div><h1>Dye All</h1><div class="meta">Crimson Desert &middot; Version 1.0.0 &middot; Gameplay / User Interface</div></div></div>
  <div class="tabs"><span class="on">Description</span><span>Files</span><span>Images</span><span>Posts</span><span>Bugs</span></div>
  <p class="summary">%SUMMARY%</p>
  <div class="desc">%BODY%</div>
</div></body></html>"""

summary = ("Ever wanted to go on a dyeing spree, not limited to what you have equipped? The camp dyer now lists every "
           "dyeable item you own, in your bag, wardrobe, private chest and warehouse, and you can dye them one after "
           "another without leaving the dye menu.")
out = page.replace("%SUMMARY%", html.escape(summary)).replace("%BODY%", render(bb))
open(os.path.join(HERE, "preview.html"), "w", encoding="utf-8").write(out)
print("wrote preview.html")
