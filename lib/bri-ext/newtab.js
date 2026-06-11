// Ctrl+T pins keyboard focus to the urlbar no matter what the page does (bugzilla 1411465);
// a tabs.CREATEd tab focuses content. NTO's trick: spawn the real tab, remove this shell.
browser.tabs.getCurrent().then(t => {
  browser.tabs.create({url: 'http://localhost:1111/', index: t.index + 1});
  browser.tabs.remove(t.id);
});
