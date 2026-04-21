const DEFAULTS = [
  { url: "https://claude.ai/", label: "Claude" },
  { url: "https://www.meta.ai/", label: "Meta" },
  { url: "https://gemini.google.com/", label: "Gemini" },
  { url: "https://aistudio.google.com/", label: "AI Studio" },
  { url: "https://mail.google.com/", label: "Gmail" },
  { url: "https://web.whatsapp.com/", label: "WhatsApp" }
];
const tiles = document.getElementById("tiles");
const getUser = () => new Promise(r => chrome.storage.sync.get({ user: [] }, d => r(d.user)));

function activate(url) {
  chrome.tabs.query({ url: url + "*" }, ts => {
    const t = ts.find(x => !x.active) || ts[0];
    if (t) chrome.windows.update(t.windowId, { focused: true }, () =>
      chrome.tabs.update(t.id, { active: true }, () =>
        chrome.tabs.getCurrent(c => c && chrome.tabs.remove(c.id))));
    else chrome.tabs.update({ url });
  });
}

function tile({ url, label, isUser }) {
  const d = document.createElement("div"); d.className = "tile";
  const a = document.createElement("a"); a.href = url; a.textContent = label;
  a.onclick = e => { e.preventDefault(); activate(url); };
  d.appendChild(a);
  if (isUser) {
    const x = document.createElement("button"); x.textContent = "×";
    x.onclick = async () => chrome.storage.sync.set({
      user: (await getUser()).filter(s => s.url !== url)
    });
    d.appendChild(x);
  }
  tiles.appendChild(d);
  chrome.tabs.query({ url: url + "*" }, t => {
    if (!t.length) chrome.tabs.create({ url, active: false, pinned: true });
  });
}

async function render() {
  tiles.innerHTML = "";
  DEFAULTS.forEach(s => tile({ ...s, isUser: false }));
  (await getUser().catch(() => [])).forEach(s => tile({ ...s, isUser: true }));
}

document.getElementById("add").onclick = async () => {
  const raw = prompt("URL:"); if (!raw) return;
  let url;
  try { url = new URL(/^https?:/.test(raw) ? raw : "https://" + raw).href; }
  catch { alert("Bad URL"); return; }
  const label = prompt("Label:", new URL(url).hostname.replace(/^www\./, ""));
  if (!label) return;
  chrome.storage.sync.set({ user: [...await getUser(), { url, label }] });
};

chrome.storage.onChanged.addListener(render);
render();
