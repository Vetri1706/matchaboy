"""Verify authored static references and pinned release targets; no dependencies."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlparse
import json

ROOT = Path(__file__).resolve().parents[1] / "dist"
class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids, self.refs, self.images = set(), [], []
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            assert attrs["id"] not in self.ids, attrs["id"]
            self.ids.add(attrs["id"])
        for key in ("href", "src"):
            if key in attrs:
                self.refs.append(attrs[key])
        if tag == "img":
            assert "alt" in attrs and "width" in attrs and "height" in attrs, attrs
            self.images.append(attrs["src"])

results = []
for path in ROOT.glob("*.html"):
    page = Page()
    page.feed(path.read_text())
    for ref in page.refs:
        parsed = urlparse(ref)
        if parsed.scheme or parsed.netloc:
            continue
        if ref.startswith("#"):
            assert parsed.fragment in page.ids, ref
        else:
            target = path.parent / parsed.path
            assert target.exists(), target
    results.append({"page": path.name, "references": len(page.refs), "images": len(page.images), "passed": True})
index = (ROOT / "index.html").read_text()
for platform in ("macos-arm64", "windows-x64", "linux-x64"):
    assert f"https://github.com/Vetri1706/matchaboy/releases/download/v0.1.0/matchaboy-{platform}.zip" in index
print(json.dumps({"static_checks": results, "release_targets": "three exact v0.1.0 assets", "passed": True}, indent=2))
