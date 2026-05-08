const NAME = "a-pins";
const folder = async () => {
  const f = (await chrome.bookmarks.search({ title: NAME })).find(n => !n.url && n.title === NAME);
  if (f) return f.id;
  const fid = (await chrome.bookmarks.create({ parentId: "1", title: NAME })).id;
  const { user = [] } = await chrome.storage.sync.get({ user: [] });
  for (const b of user) await chrome.bookmarks.create({ parentId: fid, url: b.url, title: b.label });
  return fid;
};
const ensure = async () => {
  const list = (await chrome.bookmarks.getChildren(await folder())).filter(b => b.url);
  const { ids = {} } = await chrome.storage.session.get({ ids: {} });
  for (const { url } of list) {
    if (Number.isInteger(ids[url]) && await chrome.tabs.get(ids[url]).catch(() => 0)) continue;
    ids[url] = ((await chrome.tabs.query({ url: url + "*", pinned: true }))[0]
      || await chrome.tabs.create({ url, active: false, pinned: true })).id;
  }
  chrome.storage.session.set({ ids });
};
chrome.runtime.onInstalled.addListener(ensure);
chrome.runtime.onStartup.addListener(ensure);
for (const e of ["onCreated", "onChanged", "onMoved", "onRemoved"]) chrome.bookmarks[e].addListener(ensure);
