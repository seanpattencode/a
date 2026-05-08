const NAME = "a-pins";
const tiles = document.getElementById("tiles");
const folder = async () => (await chrome.bookmarks.search({ title: NAME })).find(n => !n.url && n.title === NAME)?.id;
const get = async () => {
  const fid = await folder();
  return fid ? (await chrome.bookmarks.getChildren(fid)).filter(b => b.url) : [];
};

function activate(url) {
  chrome.tabs.query({ url: url + "*" }, ts => {
    const t = ts.find(x => !x.active) || ts[0];
    if (t) chrome.windows.update(t.windowId, { focused: true }, () =>
      chrome.tabs.update(t.id, { active: true }, () =>
        chrome.tabs.getCurrent(c => c && chrome.tabs.remove(c.id))));
    else chrome.tabs.update({ url });
  });
}

function tile({ id, url, title }) {
  const d = document.createElement("div"); d.className = "tile";
  const a = document.createElement("a"); a.href = url; a.textContent = title;
  a.onclick = e => { e.preventDefault(); activate(url); };
  const x = document.createElement("button"); x.textContent = "×";
  x.onclick = () => chrome.bookmarks.remove(id);
  d.append(a, x); tiles.appendChild(d);
}

async function render() { tiles.innerHTML = ""; (await get()).forEach(tile); }

document.getElementById("add").onclick = async () => {
  const raw = prompt("URL:"); if (!raw) return;
  let url;
  try { url = new URL(/^https?:/.test(raw) ? raw : "https://" + raw).href; }
  catch { alert("Bad URL"); return; }
  const title = prompt("Label:", new URL(url).hostname.replace(/^www\./, ""));
  if (!title) return;
  let fid = await folder();
  if (!fid) fid = (await chrome.bookmarks.create({ parentId: "1", title: NAME })).id;
  chrome.bookmarks.create({ parentId: fid, url, title });
};

for (const e of ["onCreated", "onChanged", "onMoved", "onRemoved"]) chrome.bookmarks[e].addListener(render);
render();
