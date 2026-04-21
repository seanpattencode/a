const DEFAULTS = [
  { url: "https://claude.ai/", label: "Claude" },
  { url: "https://www.meta.ai/", label: "Meta" },
  { url: "https://gemini.google.com/", label: "Gemini" },
  { url: "https://aistudio.google.com/", label: "AI Studio" },
  { url: "https://mail.google.com/", label: "Gmail" },
  { url: "https://web.whatsapp.com/", label: "WhatsApp" }
];
const ensure = async () => {
  const { user = [] } = await chrome.storage.sync.get({ user: [] });
  for (const { url } of [...DEFAULTS, ...user]) {
    const tabs = await chrome.tabs.query({ url: url + "*" });
    if (!tabs.length) chrome.tabs.create({ url, active: false, pinned: true });
  }
};
chrome.runtime.onInstalled.addListener(ensure);
chrome.runtime.onStartup.addListener(ensure);
chrome.storage.onChanged.addListener(ensure);
