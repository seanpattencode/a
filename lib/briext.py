#!/usr/bin/env python3
"""a briext  —  ONE script that generates BOTH browser extensions (Firefox MV2 + Chrome MV3)
into adata/local/ext/ on demand. Mirrors `a apk`: single source file, makes the folder tree.
Shared payload (instant-preload, pageflip, icons) is defined ONCE here so the two cannot drift.
  a briext           generate -> adata/local/ext/{bri-ext,bri-chrome}, print load paths
  a briext verify    generate, then diff -r vs lib/bri-ext + lib/bri-chrome (proof it matches)
Edit this file to change either extension; rerun to redeploy. Firefox xpi: a bri deploy."""
import os,sys,base64,subprocess
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT=os.path.join(ROOT,"adata/local/ext")

SHARED={
"instant-preload.js": r'''/*! instant.page v5.2.0 (modified for 0ms hover) - (C) 2019-2025 Alexandre Dieulot - https://instant.page/license */

let _chromiumMajorVersionInUserAgent = null
  , _speculationRulesType
  , _allowQueryString
  , _allowExternalLinks
  , _useWhitelist
  , _delayOnHover = 0  // Changed from 65ms to 0ms
  , _lastTouchstartEvent
  , _mouseoverTimer
  , _preloadedList = new Set()
  , _preloadedTimestamps = new Map()
  , _preloadedElements = new Map()  // Store DOM elements for cleanup
  , _speculationRulesScript = null  // Single speculation rules script
  , _debugNotifications = false
  , _enablePrerender = true
  , _verboseDebugMode = false  // New comprehensive debug mode
  , _debugOverlay = null  // Visual debug overlay
  , _debugMonitoringStarted = false  // Flag to prevent duplicate monitoring
  , _processedLinks = new WeakSet()  // Track processed links globally
  , _mutationObserver = null  // Store MutationObserver reference
  , _lastMouseX = 0, _lastMouseY = 0  // Track mouse position for webcomic-style navigation

if (_verboseDebugMode) console.log('[Instant Preload Extension] Initializing...');

// For sites like Google that may need delayed initialization
const hostname = window.location.hostname;
const needsDelayedInit = ['google.com', 'www.google.com', 'youtube.com', 'www.youtube.com', 'amazon.com', 'www.amazon.com'].some(domain => 
  hostname.includes(domain)
);

// Load debug mode setting from storage
chrome.storage.sync.get({ debugMode: false }, (items) => {
  if (items.debugMode) {
    _verboseDebugMode = true;
    console.log('[Instant Preload Extension] DEBUG MODE ENABLED from settings');
    setupDebugOverlay();
    startContinuousDebugMonitoring();
  }
});

// Listen for storage changes to toggle debug mode at runtime
chrome.storage.onChanged.addListener((changes, namespace) => {
  if (namespace === 'sync' && changes.debugMode) {
    if (changes.debugMode.newValue && !_verboseDebugMode) {
      _verboseDebugMode = true;
      console.log('[Instant Preload Extension] DEBUG MODE ENABLED via settings change');
      setupDebugOverlay();
      startContinuousDebugMonitoring();
    } else if (!changes.debugMode.newValue && _verboseDebugMode) {
      console.log('[Instant Preload Extension] DEBUG MODE DISABLED via settings change');
      _verboseDebugMode = false;
      // Remove debug overlay
      if (_debugOverlay) {
        _debugOverlay.remove();
        _debugOverlay = null;
      }
    }
  }
});

if (needsDelayedInit) {
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Detected complex site, using delayed initialization');
  // Try multiple initialization attempts
  init();
  setTimeout(() => {
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Re-initializing after 1 second');
    reinitializeEventListeners();
  }, 1000);
  setTimeout(() => {
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Re-initializing after 3 seconds');
    reinitializeEventListeners();
  }, 3000);
} else {
  init();
}

function init() {
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Init function called');
  const supportChecksRelList = document.createElement('link').relList

  const supportsPrefetch = supportChecksRelList.supports('prefetch')
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Prefetch support:', supportsPrefetch);
  if (!supportsPrefetch) {
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Browser does not support prefetch, exiting');
    return
  }

  const chromium100Check = 'throwIfAborted' in AbortSignal.prototype // Chromium 100+, Safari 15.4+, Firefox 97+
  const firefox115AndSafari17_0Check = supportChecksRelList.supports('modulepreload') // Firefox 115+, Safari 17.0+, Chromium 66+
  const safari15_4AndFirefox116Check = Intl.PluralRules && 'selectRange' in Intl.PluralRules.prototype // Safari 15.4+, Firefox 116+, Chromium 106+
  const firefox115AndSafari15_4Check = firefox115AndSafari17_0Check || safari15_4AndFirefox116Check
  const isBrowserSupported = chromium100Check && firefox115AndSafari15_4Check
  if (!isBrowserSupported) {
    return
  }
  // In order to lessen maintenance and unnoticed bugs we only support:
  // - Chromium ⩾ 100 — UC Browser 14
  // - Gecko as in Firefox ⩾ 115 — last version supported on Windows 7
  // - WebKit as in Safari ⩾ 15.4 — last major WebKit version supported on iPhone 6s & 7
  //
  // WebKit doesn't support prefetch anyway, but instant.page might
  // eventually drop this requirement by providing an option for
  // fetch()-based preloading.
  //
  // Additionally, instant.page should not cause JavaScript errors in:
  // - Chromium ⩾ 61
  // - Gecko as in Firefox ⩾ 60
  // - WebKit as in Safari ⩾ 10.1 (iOS ⩾ 10.3 and macOS ⩾ 10.10)
  // Browser engines older than that don't support <script type=module>
  // and thus don't load instant.page at all.

  const handleVaryAcceptHeader = 'instantVaryAccept' in document.body.dataset || 'Shopify' in window
  // The `Vary: Accept` header when received in Chromium 79–109 makes prefetches
  // unusable, as Chromium used to send a different `Accept` header.
  // It's applied on all Shopify sites by default, as Shopify is very popular
  // and is the main source of this problem.
  // `window.Shopify` only exists on "classic" Shopify sites. Those using
  // Hydrogen (Remix SPA) aren't concerned.

  const chromiumUserAgentIndex = navigator.userAgent.indexOf('Chrome/')
  if (chromiumUserAgentIndex > -1) {
    _chromiumMajorVersionInUserAgent = parseInt(navigator.userAgent.substring(chromiumUserAgentIndex + 'Chrome/'.length))
  }
  // The user agent client hints API is a theoretically more reliable way to
  // get Chromium's version… but it's not available in Samsung Internet 20.
  // It also requires a secure context, which would make debugging harder,
  // and is only available in recent Chromium versions.
  // In practice, Chromium browsers never shy from announcing "Chrome" in
  // their regular user agent string, as that maximizes their compatibility.

  if (handleVaryAcceptHeader && _chromiumMajorVersionInUserAgent && _chromiumMajorVersionInUserAgent < 110) {
    return
  }

  // Initialize speculation rules type
  // For browsers that don't support speculation rules, 'none' will trigger fallback to link prefetch
  _speculationRulesType = 'none'
  
  // Check if speculation rules are supported
  const supportsSpeculationRules = typeof HTMLScriptElement !== 'undefined' && 
      HTMLScriptElement.supports && 
      HTMLScriptElement.supports('speculationrules');
  
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Speculation rules support:', supportsSpeculationRules);
  
  if (supportsSpeculationRules) {
    // Browser supports speculation rules
    // Check for data attribute configuration first
    const speculationRulesConfig = document.body.dataset.instantSpecrules
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Data attribute config:', speculationRulesConfig);
    if (speculationRulesConfig == 'prerender') {
      _speculationRulesType = 'prerender'
    } else if (speculationRulesConfig == 'no') {
      _speculationRulesType = 'none'
    } else {
      // Default to prerender if no explicit config (will be updated by storage settings)
      _speculationRulesType = 'prerender'
    }
  }
  // If browser doesn't support speculation rules, _speculationRulesType stays 'none'
  // which will trigger the fallback to link prefetch in the preload function
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Initial speculation rules type:', _speculationRulesType);

  // Load settings from storage
  if (typeof chrome !== 'undefined' && chrome.storage) {
    chrome.storage.sync.get({ debugNotifications: false, enablePrerender: true }, (items) => {
      _debugNotifications = items.debugNotifications;
      _enablePrerender = items.enablePrerender;
      // Update speculation rules type based on prerender setting if not explicitly configured
      if (typeof HTMLScriptElement !== 'undefined' && 
          HTMLScriptElement.supports && 
          HTMLScriptElement.supports('speculationrules')) {
        const speculationRulesConfig = document.body.dataset.instantSpecrules
        // Only update if not explicitly configured via data attribute
        if (!speculationRulesConfig || speculationRulesConfig === '') {
          _speculationRulesType = _enablePrerender ? 'prerender' : 'prefetch';
        }
      }
      // If speculation rules aren't supported, _speculationRulesType remains 'none' to use link prefetch
    });
    
    // Listen for settings changes
    chrome.storage.onChanged.addListener((changes, namespace) => {
      if (namespace === 'sync') {
        if (changes.debugNotifications) {
          _debugNotifications = changes.debugNotifications.newValue;
        }
        if (changes.enablePrerender) {
          _enablePrerender = changes.enablePrerender.newValue;
          // Update speculation rules type when setting changes if not explicitly configured
          if (typeof HTMLScriptElement !== 'undefined' && 
              HTMLScriptElement.supports && 
              HTMLScriptElement.supports('speculationrules')) {
            const speculationRulesConfig = document.body.dataset.instantSpecrules
            // Only update if not explicitly configured via data attribute
            if (!speculationRulesConfig || speculationRulesConfig === '') {
              _speculationRulesType = _enablePrerender ? 'prerender' : 'prefetch';
            }
          }
          // If speculation rules aren't supported, keep using 'none' for link prefetch fallback
        }
      }
    });
  }

  const useMousedownShortcut = 'instantMousedownShortcut' in document.body.dataset
  // CHANGED: Allow query strings by default for sites like Google
  _allowQueryString = true // Was: 'instantAllowQueryString' in document.body.dataset
  // CHANGED: Allow external links on Google/YouTube to handle their subdomains
  if (hostname.includes('google.com') || hostname.includes('youtube.com')) {
    _allowExternalLinks = true
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Enabling external links for Google/YouTube domains');
  } else {
    _allowExternalLinks = 'instantAllowExternalLinks' in document.body.dataset
  }
  _useWhitelist = 'instantWhitelist' in document.body.dataset

  let preloadOnMousedown = false
  let preloadOnlyOnMousedown = false
  let preloadWhenVisible = false
  if ('instantIntensity' in document.body.dataset) {
    const intensityParameter = document.body.dataset.instantIntensity

    if (intensityParameter == 'mousedown' && !useMousedownShortcut) {
      preloadOnMousedown = true
    }

    if (intensityParameter == 'mousedown-only' && !useMousedownShortcut) {
      preloadOnMousedown = true
      preloadOnlyOnMousedown = true
    }

    if (intensityParameter == 'viewport') {
      const isOnSmallScreen = document.documentElement.clientWidth * document.documentElement.clientHeight < 450000
      // Smartphones are the most likely to have a slow connection, and
      // their small screen size limits the number of links (and thus
      // server load).
      //
      // Foldable phones (being expensive as of 2023), tablets and PCs
      // generally have a decent connection, and a big screen displaying
      // more links that would put more load on the server.
      //
      // iPhone 14 Pro Max (want): 430×932 = 400 760
      // Samsung Galaxy S22 Ultra with display size set to 80% (want):
      // 450×965 = 434 250
      // Small tablet (don't want): 600×960 = 576 000
      // Those number are virtual screen size, the viewport (used for
      // the check above) will be smaller with the browser's interface.

      const isNavigatorConnectionSaveDataEnabled = navigator.connection && navigator.connection.saveData
      const isNavigatorConnectionLike2g = navigator.connection && navigator.connection.effectiveType && navigator.connection.effectiveType.includes('2g')
      const isNavigatorConnectionAdequate = !isNavigatorConnectionSaveDataEnabled && !isNavigatorConnectionLike2g

      if (isOnSmallScreen && isNavigatorConnectionAdequate) {
        preloadWhenVisible = true
      }
    }

    if (intensityParameter == 'viewport-all') {
      preloadWhenVisible = true
    }

    const intensityAsInteger = parseInt(intensityParameter)
    if (!isNaN(intensityAsInteger)) {
      _delayOnHover = intensityAsInteger
    }
  }

  const eventListenersOptions = {
    capture: true,
    passive: true,
  }

  if (_verboseDebugMode) console.log('[Instant Preload Extension] Setting up event listeners...');
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Hover delay:', _delayOnHover + 'ms');
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Preload on mousedown:', preloadOnMousedown);
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Preload only on mousedown:', preloadOnlyOnMousedown);

  if (preloadOnlyOnMousedown) {
    document.addEventListener('touchstart', touchstartEmptyListener, eventListenersOptions)
  }
  else {
    document.addEventListener('touchstart', touchstartListener, eventListenersOptions)
  }

  if (!preloadOnMousedown) {
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Adding mouseover listener');
    document.addEventListener('mouseover', mouseoverListener, eventListenersOptions)
  }

  if (preloadOnMousedown) {
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Adding mousedown listener');
    document.addEventListener('mousedown', mousedownListener, eventListenersOptions)
  }
  if (useMousedownShortcut) {
    document.addEventListener('mousedown', mousedownShortcutListener, eventListenersOptions)
  }

  // Capture click position for webcomic-style navigation check
  document.addEventListener('click', (e) => { _lastMouseX = e.clientX; _lastMouseY = e.clientY }, eventListenersOptions)

  if (preloadWhenVisible) {
    let requestIdleCallbackOrFallback = window.requestIdleCallback
    // Safari has no support as of 16.3: https://webkit.org/b/164193
    if (!requestIdleCallbackOrFallback) {
      requestIdleCallbackOrFallback = (callback) => {
        callback()
        // A smarter fallback like setTimeout is not used because devices that
        // may eventually be eligible to a Safari version supporting prefetch
        // will be very powerful.
        // The weakest devices that could be eligible are the 2017 iPad and
        // the 2016 MacBook.
      }
    }

    requestIdleCallbackOrFallback(function observeIntersection() {
      const intersectionObserver = new IntersectionObserver((entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            const anchorElement = entry.target
            intersectionObserver.unobserve(anchorElement)
            preload(anchorElement.href, 'auto', 'viewport')
          }
        })
      })

      document.querySelectorAll('a').forEach((anchorElement) => {
        if (isPreloadable(anchorElement)) {
          intersectionObserver.observe(anchorElement)
        }
      })
    }, {
      timeout: 1500,
    })
  }
  
  // Set up MutationObserver to handle dynamically added links
  setupMutationObserver()
  
  // Listen for SPA navigation changes
  setupSPANavigationDetection()
  
  // Attach direct listeners to all existing links after a short delay
  setTimeout(() => {
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Attaching direct link listeners...');
    attachDirectLinkListeners();
  }, 100);
}

function setupMutationObserver() {
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Setting up MutationObserver for dynamic content');
  
  // Disconnect existing observer if any
  if (_mutationObserver) {
    _mutationObserver.disconnect();
  }
  
  _mutationObserver = new MutationObserver((mutations) => {
    // Batch process all mutations
    const newLinks = []
    
    mutations.forEach((mutation) => {
      // Check added nodes for links
      mutation.addedNodes.forEach((node) => {
        if (node.nodeType === Node.ELEMENT_NODE) {
          // Check if the node itself is a link
          if (node.tagName === 'A' && !_processedLinks.has(node)) {
            newLinks.push(node)
            _processedLinks.add(node)
          }
          
          // Check for links within the added node
          if (node.querySelectorAll) {
            node.querySelectorAll('a').forEach((link) => {
              if (!_processedLinks.has(link)) {
                newLinks.push(link)
                _processedLinks.add(link)
              }
            })
          }
        }
      })
    })
    
    // Process new links
    if (newLinks.length > 0) {
      if (_verboseDebugMode) console.log('[Instant Preload Extension] Found', newLinks.length, 'new links via MutationObserver');
      
      // Attach direct listeners to new links to ensure they work
      attachDirectLinkListeners();
    }
  })
  
  // Start observing the document body for changes
  _mutationObserver.observe(document.body, {
    childList: true,
    subtree: true
  })
  
  if (_verboseDebugMode) console.log('[Instant Preload Extension] MutationObserver started');
}

function setupSPANavigationDetection() {
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Setting up SPA navigation detection');
  
  let lastUrl = location.href;
  
  // Check for URL changes periodically
  // This catches navigation that doesn't trigger popstate
  setInterval(() => {
    const currentUrl = location.href;
    if (currentUrl !== lastUrl) {
      if (_verboseDebugMode) console.log('[Instant Preload Extension] URL changed from', lastUrl, 'to', currentUrl);
      lastUrl = currentUrl;
      
      // Clear preloaded list when navigating to allow re-preloading
      _preloadedList.clear();
      _preloadedTimestamps.clear();
      
      // Clear speculation rules
      if (_speculationRulesScript && _speculationRulesScript.parentNode) {
        _speculationRulesScript.parentNode.removeChild(_speculationRulesScript);
        _speculationRulesScript = null;
      }
      
      if (_verboseDebugMode) console.log('[Instant Preload Extension] Cleared preload cache after navigation');

      // Check for link at current mouse position (webcomic-style navigation)
      const linkAtMouse = document.elementFromPoint(_lastMouseX, _lastMouseY)?.closest('a');
      if (linkAtMouse && isPreloadable(linkAtMouse)) preload(linkAtMouse.href, 'high', 'pageload');
    }
  }, 500);
  
  // Also listen for popstate events (back/forward navigation)
  window.addEventListener('popstate', () => {
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Popstate event detected');
    // Clear preloaded list
    _preloadedList.clear();
    _preloadedTimestamps.clear();
    
    // Clear speculation rules
    if (_speculationRulesScript && _speculationRulesScript.parentNode) {
      _speculationRulesScript.parentNode.removeChild(_speculationRulesScript);
      _speculationRulesScript = null;
    }
  });
}

function reinitializeEventListeners() {
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Reinitializing event listeners');
  
  // Clean up existing link listeners
  const existingLinks = document.querySelectorAll('a[data-instant-preload-attached="true"]');
  existingLinks.forEach(link => {
    if (link._mouseoverHandler) {
      link.removeEventListener('mouseover', link._mouseoverHandler);
      delete link._mouseoverHandler;
    }
    if (link._mouseoutHandler) {
      link.removeEventListener('mouseout', link._mouseoutHandler);
      delete link._mouseoutHandler;
    }
    if (link._mouseoverTimer) {
      clearTimeout(link._mouseoverTimer);
      delete link._mouseoverTimer;
    }
    delete link.dataset.instantPreloadAttached;
  });
  
  // Count and log all links on the page
  const allLinks = document.querySelectorAll('a');
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Found', allLinks.length, 'total links on page');
  
  // Check how many are preloadable
  let preloadableCount = 0;
  allLinks.forEach(link => {
    if (isPreloadable(link)) {
      preloadableCount++;
      if (_verboseDebugMode) console.log('[Instant Preload Extension] Preloadable link:', link.href);
    }
  });
  if (_verboseDebugMode) console.log('[Instant Preload Extension] Found', preloadableCount, 'preloadable links');
  
  // Attach direct listeners to links
  attachDirectLinkListeners();
  
  // Force test: Try to preload the first preloadable link
  if (preloadableCount > 0) {
    const firstPreloadable = Array.from(allLinks).find(link => isPreloadable(link));
    if (firstPreloadable) {
      if (_verboseDebugMode) console.log('[Instant Preload Extension] Force preloading first link as test:', firstPreloadable.href);
      preload(firstPreloadable.href, 'high', 'test');
    }
  }
}

function attachDirectLinkListeners() {
  // Attach listeners directly to each link to bypass event stopping
  const links = document.querySelectorAll('a');
  let attachedCount = 0;
  
  links.forEach(link => {
    // Skip if already processed
    if (link.dataset.instantPreloadAttached) return;
    
    if (!isPreloadable(link)) return;
    
    link.dataset.instantPreloadAttached = 'true';
    attachedCount++;
    
    // Create named functions for event listeners so they can be removed later
    link._mouseoverHandler = function(e) {
      // Clear any existing timer for this link
      if (link._mouseoverTimer) {
        clearTimeout(link._mouseoverTimer);
      }
      
      // Prevent duplicate preloads
      if (_preloadedList.has(this.href)) return;
      
      if (_verboseDebugMode) console.log('[Instant Preload] Direct mouseover on:', this.href);
      
      // Set timer for this specific link
      link._mouseoverTimer = setTimeout(() => {
        if (!_preloadedList.has(this.href)) {
          if (_verboseDebugMode) console.log('[Instant Preload] Direct preload triggered for:', this.href);
          preload(this.href, 'high', 'hover');
        }
        link._mouseoverTimer = null;
      }, _delayOnHover);
    };
    
    link._mouseoutHandler = function(e) {
      if (link._mouseoverTimer) {
        clearTimeout(link._mouseoverTimer);
        link._mouseoverTimer = null;
      }
    };
    
    // Add the event listeners
    link.addEventListener('mouseover', link._mouseoverHandler, { passive: true, capture: false });
    link.addEventListener('mouseout', link._mouseoutHandler, { passive: true, capture: false });
  });
  
  if (attachedCount > 0) {
    if (_verboseDebugMode) console.log('[Instant Preload Extension] Attached direct listeners to', attachedCount, 'links');
  }
}

function touchstartListener(event) {
  _lastTouchstartEvent = event

  const anchorElement = event.target.closest('a')

  if (!isPreloadable(anchorElement)) {
    return
  }

  preload(anchorElement.href, 'high', 'hover')
}

function touchstartEmptyListener(event) {
  _lastTouchstartEvent = event
}

function mouseoverListener(event) {
  if (_verboseDebugMode) {
    if (_verboseDebugMode) console.log('[DEBUG] Mouseover event triggered:', event.target);
  }
  
  if (isEventLikelyTriggeredByTouch(event)) {
    // This avoids uselessly adding a mouseout event listener and setting a timer.
    if (_verboseDebugMode) console.log('[DEBUG] Event likely triggered by touch, ignoring');
    return
  }

  if (!('closest' in event.target)) {
    if (_verboseDebugMode) console.log('[DEBUG] No closest method on target');
    return
    // Without this check sometimes an error "event.target.closest is not a function" is thrown, for unknown reasons
    // That error denotes that `event.target` isn't undefined. My best guess is that it's the Document.
    //
    // Details could be gleaned from throwing such an error:
    //throw new TypeError(`instant.page non-element event target: timeStamp=${~~event.timeStamp}, type=${event.type}, typeof=${typeof event.target}, nodeType=${event.target.nodeType}, nodeName=${event.target.nodeName}, viewport=${innerWidth}x${innerHeight}, coords=${event.clientX}x${event.clientY}, scroll=${scrollX}x${scrollY}`)
  }
  const anchorElement = event.target.closest('a')

  if (!anchorElement) {
    if (_verboseDebugMode) console.log('[DEBUG] No anchor element found');
    return
  }

  if (!isPreloadable(anchorElement)) {
    if (_verboseDebugMode) console.log('[Instant Preload] Link not preloadable:', anchorElement?.href);
    return
  }

  if (_verboseDebugMode) console.log('[Instant Preload] Mouseover detected on:', anchorElement.href, 'Delay:', _delayOnHover + 'ms');
  anchorElement.addEventListener('mouseout', mouseoutListener, {passive: true})

  _mouseoverTimer = setTimeout(() => {
    if (_verboseDebugMode) console.log('[Instant Preload] Timer fired, preloading:', anchorElement.href);
    preload(anchorElement.href, 'high', 'hover')
    _mouseoverTimer = null
  }, _delayOnHover)
}

function mousedownListener(event) {
  if (isEventLikelyTriggeredByTouch(event)) {
    // When preloading only on mousedown, not touch, we need to stop there
    // because touches send compatibility mouse events including mousedown.
    //
    // (When preloading on touchstart, instructions below this block would
    // have no effect.)
    return
  }

  const anchorElement = event.target.closest('a')

  if (!isPreloadable(anchorElement)) {
    return
  }

  preload(anchorElement.href, 'high', 'hover')
}

function mouseoutListener(event) {
  if (event.relatedTarget && event.target.closest('a') == event.relatedTarget.closest('a')) {
    return
  }

  if (_mouseoverTimer) {
    clearTimeout(_mouseoverTimer)
    _mouseoverTimer = null
  }
}

function mousedownShortcutListener(event) {
  if (isEventLikelyTriggeredByTouch(event)) {
    // Due to a high potential for complications with this mousedown shortcut
    // combined with other parties' JavaScript code, we don't want it to run
    // at all on touch devices, even though mousedown and click are triggered
    // at almost the same time on touch.
    return
  }

  const anchorElement = event.target.closest('a')

  if (event.which > 1 || event.metaKey || event.ctrlKey) {
    return
  }

  if (!anchorElement) {
    return
  }

  anchorElement.addEventListener('click', function (event) {
    if (event.detail == 1337) {
      return
    }

    event.preventDefault()
  }, {capture: true, passive: false, once: true})

  const customEvent = new MouseEvent('click', {view: window, bubbles: true, cancelable: false, detail: 1337})
  anchorElement.dispatchEvent(customEvent)
}

function isEventLikelyTriggeredByTouch(event) {
  // Touch devices fire "mouseover" and "mousedown" (and other) events after
  // a touch for compatibility reasons.
  // This function checks if it's likely that we're dealing with such an event.

  if (!_lastTouchstartEvent || !event) {
    return false
  }

  if (event.target != _lastTouchstartEvent.target) {
    return false
  }

  const now = event.timeStamp
  // Chromium (tested Chrome 95 and 122 on Android) sometimes uses the same
  // event.timeStamp value in touchstart, mouseover, and mousedown.
  // Testable in test/extras/delay-not-considered-touch.html
  // This is okay for our purpose: two equivalent timestamps will be less
  // than the max duration, which means they're related events.
  // TODO: fill/find Chromium bug
  const durationBetweenLastTouchstartAndNow = now - _lastTouchstartEvent.timeStamp

  const MAX_DURATION_TO_BE_CONSIDERED_TRIGGERED_BY_TOUCHSTART = 2500
  // How long after a touchstart event can a simulated mouseover/mousedown event fire?
  // /test/extras/delay-not-considered-touch.html tries to answer that question.
  // I saw up to 1450 ms on an overwhelmed Samsung Galaxy S2.
  // On the other hand, how soon can an unrelated mouseover event happen after an unrelated touchstart?
  // Meaning the user taps a link, then grabs their pointing device and clicks another/the same link.
  // That scenario could occur if a user taps a link, thinks it hasn't worked, and thus fall back to their pointing device.
  // I do that in about 1200 ms on a Chromebook. In which case this function returns a false positive.
  // False positives are okay, as this function is only used to decide to abort handling mouseover/mousedown/mousedownShortcut.
  // False negatives could lead to unforeseen state, particularly in mousedownShortcutListener.

  return durationBetweenLastTouchstartAndNow < MAX_DURATION_TO_BE_CONSIDERED_TRIGGERED_BY_TOUCHSTART

  // TODO: Investigate if pointer events could be used.
  // https://developer.mozilla.org/en-US/docs/Web/API/PointerEvent/pointerType

  // TODO: Investigate if InputDeviceCapabilities could be used to make it
  // less hacky on Chromium browsers.
  // https://developer.mozilla.org/en-US/docs/Web/API/InputDeviceCapabilities_API
  // https://wicg.github.io/input-device-capabilities/
  // Needs careful reading of the spec and tests (notably, what happens with a
  // mouse connected to an Android or iOS smartphone?) to make sure it's solid.
  // Also need to judge if WebKit could implement it differently, as they
  // don't mind doing when a spec gives room to interpretation.
  // It seems to work well on Chrome on ChromeOS.

  // TODO: Consider using event screen position as another heuristic.
}

function isPreloadable(anchorElement) {
  const debugMode = window.location.hostname.includes('google.com') || window.location.hostname.includes('youtube.com');
  
  if (!anchorElement || !anchorElement.href) {
    if (debugMode) console.log('[Instant Preload] Rejected: No element or href');
    return
  }

  if (_useWhitelist && !('instant' in anchorElement.dataset)) {
    if (debugMode) console.log('[Instant Preload] Rejected: Whitelist mode, no instant dataset');
    return
  }

  if (anchorElement.origin != location.origin) {
    let allowed = _allowExternalLinks || 'instant' in anchorElement.dataset
    if (!allowed || !_chromiumMajorVersionInUserAgent) {
      if (debugMode) console.log('[Instant Preload] Rejected: External link not allowed', anchorElement.href);
      // Chromium-only: see comment on "restrictive prefetch" and "cross-site speculation rules prefetch"
      return
    }
  }

  if (!['http:', 'https:'].includes(anchorElement.protocol)) {
    if (debugMode) console.log('[Instant Preload] Rejected: Invalid protocol', anchorElement.protocol);
    return
  }

  if (anchorElement.protocol == 'http:' && location.protocol == 'https:') {
    if (debugMode) console.log('[Instant Preload] Rejected: HTTP link on HTTPS page');
    return
  }

  if (!_allowQueryString && anchorElement.search && !('instant' in anchorElement.dataset)) {
    if (debugMode) console.log('[Instant Preload] Rejected: Query string not allowed', anchorElement.href);
    return
  }

  if (anchorElement.hash && anchorElement.pathname + anchorElement.search == location.pathname + location.search) {
    if (debugMode) console.log('[Instant Preload] Rejected: Same page hash link');
    return
  }

  if ('noInstant' in anchorElement.dataset) {
    if (debugMode) console.log('[Instant Preload] Rejected: noInstant dataset');
    return
  }

  if (debugMode) console.log('[Instant Preload] ACCEPTED:', anchorElement.href);
  return true
}

function preload(url, fetchPriority = 'auto', triggerType = 'hover') {
  const startTime = performance.now();
  
  if (_preloadedList.has(url)) {
    if (_verboseDebugMode) console.log('[Instant Preload] URL already preloaded:', url);
    return
  }
  
  // Check if we're at the limit for prerenders
  const MAX_PRERENDERS = 5  // Conservative limit for speculation rules
  if (_speculationRulesType !== 'none' && _preloadedList.size >= MAX_PRERENDERS) {
    // Remove the oldest prerender to make room
    const oldestUrl = _preloadedList.values().next().value
    if (_verboseDebugMode) console.log('[Instant Preload] At prerender limit, removing oldest:', oldestUrl);
    
    _preloadedList.delete(oldestUrl)
    _preloadedTimestamps.delete(oldestUrl)
  }

  if (_verboseDebugMode) console.log('[Instant Preload] Preloading:', url, 'Method:', _speculationRulesType);
  
  if (_speculationRulesType != 'none') {
    if (_verboseDebugMode) console.log('[Instant Preload] Using speculation rules:', _speculationRulesType);
    preloadUsingSpeculationRules(url)
  } else {
    if (_verboseDebugMode) console.log('[Instant Preload] Using link prefetch fallback');
    preloadUsingLinkElement(url, fetchPriority)
  }

  _preloadedList.add(url)
  _preloadedTimestamps.set(url, Date.now())
  
  // Remove URL from the list after 4 seconds to allow re-prerendering
  setTimeout(() => {
    if (_verboseDebugMode) console.log('[Instant Preload] Removing URL from preloaded list after 4 seconds:', url);
    _preloadedList.delete(url)
    _preloadedTimestamps.delete(url)
    
    // For link prefetch, clean up the DOM element
    if (_speculationRulesType === 'none') {
      const element = _preloadedElements.get(url)
      if (element && element.parentNode) {
        if (_verboseDebugMode) console.log('[Instant Preload] Removing link element for:', url);
        element.parentNode.removeChild(element)
      }
      _preloadedElements.delete(url)
    } else {
      // For speculation rules, update the script with the new URL list
      updateSpeculationRules()
    }
  }, 4000)
  
  // Send debug notification if enabled
  if (_debugNotifications && typeof chrome !== 'undefined' && chrome.runtime) {
    const duration = performance.now() - startTime;
    try {
      chrome.runtime.sendMessage({
        type: 'preload-debug',
        data: {
          url: url,
          duration: duration.toFixed(2),
          triggerType: triggerType,
          currentPage: window.location.href,
          timestamp: new Date().toISOString(),
          method: _speculationRulesType !== 'none' ? _speculationRulesType : 'prefetch',
          attemptedPrerender: _enablePrerender && _speculationRulesType === 'prerender'
        }
      });
    } catch (e) {
      if (_verboseDebugMode) console.log('Debug notification:', {
        url,
        duration: `${duration.toFixed(2)}ms`,
        triggerType,
        currentPage: window.location.href,
        method: _speculationRulesType !== 'none' ? _speculationRulesType : 'prefetch'
      });
    }
  }
}

function preloadUsingSpeculationRules(url) {
  // Remove any existing speculation rules script
  if (_speculationRulesScript && _speculationRulesScript.parentNode) {
    _speculationRulesScript.parentNode.removeChild(_speculationRulesScript)
  }
  
  // Create a new script with all current URLs
  const urls = Array.from(_preloadedList)
  urls.push(url)  // Add the new URL
  
  // Limit the number of URLs in speculation rules
  const MAX_SPECULATION_URLS = 5  // Chrome can handle more, but let's be conservative
  if (urls.length > MAX_SPECULATION_URLS) {
    urls.splice(0, urls.length - MAX_SPECULATION_URLS)  // Keep only the most recent ones
  }
  
  const scriptElement = document.createElement('script')
  scriptElement.type = 'speculationrules'

  scriptElement.textContent = JSON.stringify({
    [_speculationRulesType]: [{
      source: 'list',
      urls: urls
    }]
  })

  // When using speculation rules, cross-site prefetch is supported, but will
  // only work if the user has no cookies for the destination site. The
  // prefetch will not be sent, if the user does have such cookies.

  document.head.appendChild(scriptElement)
  _speculationRulesScript = scriptElement
}

function preloadUsingLinkElement(url, fetchPriority = 'auto') {
  const linkElement = document.createElement('link')
  linkElement.rel = 'prefetch'
  linkElement.href = url

  linkElement.fetchPriority = fetchPriority
  // By default, a prefetch is loaded with a low priority.
  // When there's a fair chance that this prefetch is going to be used in the
  // near term (= after a touch/mouse event), giving it a high priority helps
  // make the page load faster in case there are other resources loading.
  // Prioritizing it implicitly means deprioritizing every other resource
  // that's loading on the page. Due to HTML documents usually being much
  // smaller than other resources (notably images and JavaScript), and
  // prefetches happening once the initial page is sufficiently loaded,
  // this theft of bandwidth should rarely be detrimental.

  linkElement.as = 'document'
  // as=document is Chromium-only and allows cross-origin prefetches to be
  // usable for navigation. They call it "restrictive prefetch" and intend
  // to remove it: https://crbug.com/1352371
  //
  // This document from the Chrome team dated 2022-08-10
  // https://docs.google.com/document/d/1x232KJUIwIf-k08vpNfV85sVCRHkAxldfuIA5KOqi6M
  // claims (I haven't tested) that data- and battery-saver modes as well as
  // the setting to disable preloading do not disable restrictive prefetch,
  // unlike regular prefetch. That's good for prefetching on a touch/mouse
  // event, but might be bad when prefetching every link in the viewport.

  document.head.appendChild(linkElement)
  
  // Store the element for later cleanup
  _preloadedElements.set(url, linkElement)
}

function updateSpeculationRules() {
  // Update the speculation rules script with the current URL list
  if (!_speculationRulesScript || _preloadedList.size === 0) {
    // Remove the script if no URLs left
    if (_speculationRulesScript && _speculationRulesScript.parentNode) {
      _speculationRulesScript.parentNode.removeChild(_speculationRulesScript)
      _speculationRulesScript = null
    }
    return
  }
  
  // Remove the old script
  if (_speculationRulesScript.parentNode) {
    _speculationRulesScript.parentNode.removeChild(_speculationRulesScript)
  }
  
  // Create a new script with current URLs
  const urls = Array.from(_preloadedList)
  
  const scriptElement = document.createElement('script')
  scriptElement.type = 'speculationrules'
  scriptElement.textContent = JSON.stringify({
    [_speculationRulesType]: [{
      source: 'list',
      urls: urls,
      referrer_policy: 'strict-origin-when-cross-origin'
    }]
  })
  
  document.head.appendChild(scriptElement)
  _speculationRulesScript = scriptElement
}

function setupDebugOverlay() {
  if (!_verboseDebugMode) return;
  
  // Check if overlay already exists
  if (_debugOverlay || document.getElementById('instant-preload-debug-overlay')) {
    return;
  }
  
  // Create debug overlay
  const overlay = document.createElement('div');
  overlay.id = 'instant-preload-debug-overlay';
  overlay.style.cssText = `
    position: fixed;
    top: 10px;
    right: 10px;
    width: 400px;
    max-height: 300px;
    background: rgba(0, 0, 0, 0.9);
    color: #0f0;
    font-family: monospace;
    font-size: 11px;
    padding: 10px;
    z-index: 999999;
    overflow-y: auto;
    border: 1px solid #0f0;
    pointer-events: none;
  `;
  overlay.innerHTML = '<div>INSTANT PRELOAD DEBUG</div><div id="debug-content"></div>';
  document.body.appendChild(overlay);
  _debugOverlay = overlay;
}

function updateDebugOverlay(message) {
  if (!_debugOverlay) return;
  const content = document.getElementById('debug-content');
  if (!content) return;
  
  const timestamp = new Date().toISOString().substr(11, 12);
  const entry = document.createElement('div');
  entry.textContent = `${timestamp} ${message}`;
  content.appendChild(entry);
  
  // Keep only last 20 entries
  while (content.children.length > 20) {
    content.removeChild(content.firstChild);
  }
}

function startContinuousDebugMonitoring() {
  if (!_verboseDebugMode) return;
  
  // Prevent duplicate monitoring setup
  if (_debugMonitoringStarted) return;
  _debugMonitoringStarted = true;
  
  if (_verboseDebugMode) console.log('[DEBUG] Starting continuous monitoring');
  
  // Monitor mouse position and what element is under cursor
  let lastLoggedElement = null;
  document.addEventListener('mousemove', (e) => {
    const element = document.elementFromPoint(e.clientX, e.clientY);
    if (element !== lastLoggedElement) {
      lastLoggedElement = element;
      
      if (element && element.tagName === 'A') {
        const href = element.href;
        const preloadable = isPreloadable(element);
        if (_verboseDebugMode) console.log('[DEBUG] Mouse over link:', {
          href: href,
          preloadable: preloadable,
          origin: element.origin,
          protocol: element.protocol,
          search: element.search,
          pathname: element.pathname,
          classList: element.className,
          dataset: element.dataset
        });
        updateDebugOverlay(`Link: ${href?.substring(0, 50)}... [${preloadable ? 'OK' : 'BLOCKED'}]`);
      } else if (element) {
        const nearestLink = element.closest('a');
        if (nearestLink) {
          if (_verboseDebugMode) console.log('[DEBUG] Inside link container:', nearestLink.href);
          updateDebugOverlay(`Near link: ${nearestLink.href?.substring(0, 40)}...`);
        }
      }
    }
  });
  
  // Log all mouseover events
  document.addEventListener('mouseover', (e) => {
    if (_verboseDebugMode) {
      if (_verboseDebugMode) console.log('[DEBUG] Mouseover event fired:', {
        target: e.target.tagName,
        targetClass: e.target.className,
        targetId: e.target.id,
        isLink: e.target.tagName === 'A',
        href: e.target.href,
        closest: e.target.closest('a')?.href
      });
    }
  }, true);
  
  // Monitor DOM changes in detail
  const observer = new MutationObserver((mutations) => {
    let linkCount = 0;
    mutations.forEach((mutation) => {
      mutation.addedNodes.forEach((node) => {
        if (node.nodeType === Node.ELEMENT_NODE) {
          const links = node.tagName === 'A' ? [node] : (node.querySelectorAll ? Array.from(node.querySelectorAll('a')) : []);
          linkCount += links.length;
          if (links.length > 0) {
            if (_verboseDebugMode) console.log('[DEBUG] DOM Mutation added', links.length, 'links:', links.map(l => l.href));
          }
        }
      });
    });
    if (linkCount > 0) {
      updateDebugOverlay(`DOM: Added ${linkCount} new links`);
    }
  });
  
  observer.observe(document.body, {
    childList: true,
    subtree: true
  });
  
  // Monitor what's preventing our events
  const originalAddEventListener = EventTarget.prototype.addEventListener;
  EventTarget.prototype.addEventListener = function(type, listener, options) {
    if ((type === 'mouseover' || type === 'mouseout' || type === 'click') && this.tagName === 'A') {
      if (_verboseDebugMode) console.log('[DEBUG] Other script adding', type, 'listener to link:', this.href);
    }
    return originalAddEventListener.call(this, type, listener, options);
  };
  
  // Log every 2 seconds what we see
  setInterval(() => {
    const links = document.querySelectorAll('a');
    const preloadableLinks = Array.from(links).filter(l => isPreloadable(l));
    if (_verboseDebugMode) console.log('[DEBUG] Status Check:', {
      totalLinks: links.length,
      preloadableLinks: preloadableLinks.length,
      preloadedCount: _preloadedList.size,
      preloadedUrls: Array.from(_preloadedList),
      speculationRulesActive: _speculationRulesScript !== null,
      currentUrl: window.location.href
    });
    updateDebugOverlay(`Links: ${preloadableLinks.length}/${links.length} | Loaded: ${_preloadedList.size}`);
  }, 2000);
}
''',
"pageflip.js": r'''// pageflip — discrete viewport paging, SHARED by both extensions (FF bri-ext + Chrome bri-chrome). Down=next, Up=prev (one tap).
// Universal step: detects the scroll container (window, or a large inner scrollable div) and the obscured
// top/bottom band (fixed/sticky bars) via pixel probes re-run each flip — so sticky / hide-on-scroll headers
// never skip content, on any site. ▲▼ buttons; skips text fields; toggle chrome.storage.sync.pageflip (default on).
// Single source: lib/briext.py generates both — edit there, then `a briext`.
(()=>{
  if(window.__pf)return; window.__pf=1;
  document.documentElement.style.scrollBehavior='auto';
  let zones=[],active=false;
  const cx=()=>Math.round(innerWidth/2);
  // px obscured by pinned fixed/sticky bars from the top edge (fromTop=1) or bottom (0): first pixel showing flowing content.
  const obsc=fromTop=>{const lim=Math.floor(innerHeight*0.5);for(let i=0;i<lim;i+=4){const y=fromTop?i:innerHeight-1-i;
    const st=document.elementsFromPoint(cx(),y);if(!st.length)return i;
    const t=st.find(e=>!(e.classList&&e.classList.contains('__pf_btn')));if(!t)return i;
    const p=getComputedStyle(t).position;if(p!=='fixed'&&p!=='sticky')return i; // flowing content → edge is clear
    // fixed/sticky obscures only if scrollable content sits behind it (else it's an in-flow sticky block, not a bar)
    if(!st.slice(st.indexOf(t)+1).some(e=>{const q=getComputedStyle(e).position;return q!=='fixed'&&q!=='sticky';}))return i;}return 0;};
  // scroll container: window (null) unless the document itself doesn't scroll and a large inner element does.
  const scr=()=>{const se=document.scrollingElement||document.documentElement;if(se&&se.scrollHeight>se.clientHeight+2)return null;
    let best=null,ba=0;for(const el of document.querySelectorAll('div,main,section,article,ul')){if(el.scrollHeight<=el.clientHeight+8)continue;
      if(!/(auto|scroll)/.test(getComputedStyle(el).overflowY))continue;const r=el.getBoundingClientRect(),a=r.width*r.height;if(a>ba){ba=a;best=el;}}return best;};
  const go=d=>{const s=scr();if(s){s.scrollBy(0,d*Math.max(1,s.clientHeight-8));return;}scrollBy(0,d*Math.max(1,innerHeight-obsc(1)-obsc(0)));};
  const onkey=e=>{const t=e.target;if(t&&(/^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName)||t.isContentEditable))return;
    if(e.key==='ArrowDown'||(e.key===' '&&!e.shiftKey)){go(1);e.preventDefault();}else if(e.key==='ArrowUp'||(e.key===' '&&e.shiftKey)){go(-1);e.preventDefault();}};
  const mk=(l,d,b)=>{const e=document.createElement('div');e.textContent=l;e.className='__pf_btn';
    e.style.cssText=`position:fixed;right:12px;bottom:${b}px;width:52px;height:52px;z-index:2147483647;display:flex;align-items:center;justify-content:center;font:26px sans-serif;background:#000a;color:#fff;border-radius:11px;user-select:none;cursor:pointer`;
    e.addEventListener('pointerdown',ev=>{go(d);ev.preventDefault();});document.body.appendChild(e);return e;};
  const apply=on=>{
    if(on&&!active){active=true;addEventListener('keydown',onkey,true);zones=[mk('▲',-1,74),mk('▼',1,14)];}
    else if(!on&&active){active=false;removeEventListener('keydown',onkey,true);zones.forEach(z=>z.remove());zones=[];}};
  chrome.storage.sync.get({pageflip:true},d=>apply(d.pageflip));
  chrome.storage.onChanged.addListener(c=>c.pageflip&&apply(c.pageflip.newValue));
})();
''',
"clickdown.js": r'''// clickdown (SHARED) — open links on PRESS not release, for speed. Plain primary click, no mods, same-tab http(s) <a href> only.
// Tradeoff by design (#32 act-on-press): a drag/text-select STARTING on a link navigates. Toggle chrome.storage.sync.clickdown (default ON).
(()=>{let on=true;chrome.storage.sync.get({clickdown:true},d=>on=d.clickdown);
chrome.storage.onChanged.addListener(c=>c.clickdown&&(on=c.clickdown.newValue));
addEventListener('mousedown',e=>{if(on&&!e.button&&!e.ctrlKey&&!e.metaKey&&!e.shiftKey&&!e.altKey){
const a=e.target.closest&&e.target.closest('a[href]');
if(a&&a.target!=='_blank'&&!a.hasAttribute('download')&&/^https?:/.test(a.href)){e.preventDefault();location.href=a.href;}}},true);})();
''',
}
ICONS={
"icon16.png": "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAIAAACQkWg2AAAAiklEQVR4nGP0WR/AQApgIkn1SNXAgsYX5BBI0Un++ffH9Xc3dj/cS9gGdwW3TXc3Tzo/1VzSjJGRkbAGGR7pJ1+eMjAwfPr5SYCdn7AGBgYGBob/DAwMDAyM//9jkUPX8OjzY1leWQYGBgF2/o+/PmJqQPf0zge70nRT3BVcjzw99h+bFYyDL/EBAI+3KYtvPAM2AAAAAElFTkSuQmCC",
"icon48.png": "iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAIAAADYYG7QAAACN0lEQVR4nO3Yv2sTYRgH8O97uUviXWiS0vYuDaJBbQcHDWmsSBUHUQSHglLcxE2cBUG6SsFJnKSI+AcUY13EyR9LnJpqhdKkifgjTY2hsWlyIbnrncN1qInwwhXfZni/08vz3PvwgRfuPY5cSU2ilyLsN6AzHEQLB9HCQbRwEC0cRAsH0cJBtHAQLRxEi7iXzQMHBqZGriXUeNgfbprNXDU/n3+ZKS/uDygWPDwzcV+R5K321spGNujrS6jxhBp/+vlZanWeNUggwt3kHUWSX315Pbv0xLRMAPGhk9Pj924ev/Hx11Jhs+BysrttpyPj0UC0WC8+/jTraABkyoup1ReEkKvHJt2NdQ86pSUBvP3x3rKt3fU3398BGNMSHuJhCjoaOgIgV8111Iv1om7qsigPByJMQRFFA1DWK92tSrMCQFM0diBR8Hg9XgC6oXd3G4YOICAp7ECS4HUWpm10dw3L2P0MC5BhtZ2FSKTuriRIANpWix3ItLZb2y0Ayr/OxTmservBDgRgrV4CoCpDHXVCyKA8CKBYX2MKylZzAEbDIx31WF/M7/Fvtmrr+jpTULr0AcD5g+dE4a8X4MVDFwCkS2nbtpmCFn5m8r8LqqzePnFLFHYuxInomcuxS6ZlzmVT7sYCIK5/6Q0HIg/OzgR9wVq79rX2rd8fjgailm09XHjkXCCsQQBCvtD10amkNtbvDzcMfXljeS77fKWadT1wr6D/kZ77hOUgWjiIFg6ihYNo6TnQH7GDr3b+0FoyAAAAAElFTkSuQmCC",
"icon128.png": "iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAIAAABMXPacAAAGxUlEQVR4nO2cbWxTVRjHz73t1hdW2rVbV9gYONq9dLxubICDL0ZJVGIwJsYgEOMbISQmhoQPJibETDEaFYIxfjVK0BAFzSAaxA2MTAgDBuLkrevWsbKOjXV9XXvb64d9uT01W3t7L88ZPr9v55+eJ0/6y05P7zkdt/nYFoLAwUM38H8HBQCDAoBBAcCgAGBQADAoABgUAAwKAAYFAIMCgEEBwKAAYFAAMCgAGBQADAoABgUAgwKAQQHAoABgUAAwKAAYFAAMCgAGBQCDAoBBAcCgAGBQADAoABgUAAwKAAYFAIMCgEEBwKAAYFAAMCgAGBQADAoABgUAgwKAQQHAoABgUAAwKAAYFAAMCgAGBQCDAoBBAcCgAGC00A3kR7Wpeo2jqcFaX1lSadWX6jR6kaSjyVggFhic9PWOXr04cjGUCEO3mQfcXPm3lc0VTS/VvVhvrZv5ZYlUsmuo63DfkfH4g4fTWIHMAQFGrfGtpt1tCx/PfUpMiH3R+2WX76x6XSkF60uQWWfev6F9kakqr1kGrWFP89sVxorvbhxVqTGlYFqATqNrb9v3n+/+3fBwf9A7mZjkOd6mt9aWusw6M/WabQ1bQ4nQyf6fH0qzMmFawJsrXlsyfwkVdvq6jt783hcakoYcxzXZV+1wb68xPybNX1/+6j/jNzzBfrVblQ2729AGa/2mxU9Jk2Q6uf/CR5/2HKTefUKIKIo9I5f3nNn7m69TmhfxRbtW7lS91wJgV8ArjTuo5MClQ+eGu2eYIqSFA5cOXQn0SsN6a926BWuV708hGBXgsjjdtgZp8sfwubNDv886URTFg5c/n0pNScMtzucU7k85GBWwacmT0qFIxMN93+Y4937s/qmB09Kk0eauLKlUrDlFYVEAz/HrF6yXJtfH/vaFfLlX+MV7iko2VrYp0JkKsCjAZXGadfOlybm7My392Xgnvf6IX5qscTQr0JkKsChgedkyKrkyejXfIr2j16RDl8Vp0BoKaksdWBRQa3VJh1EhOpS175yVmw9uSYc8x7sszkI7UwEWBVBfvgaCgyIR8y3iDXrpsubFBTSlFswJ0PJah7FCmgxHhmXUGc78DCCELDItkt+WajAnoNxQznGcNBmJBmTUiSQjkWRUmtiN5QV1pg7MCSgz2KhkYmpCXqngVFA6LDeUyaujKswJoDaghJDg1KS8UsFEhoD5xfTjUhZgTkBJUQmVxISYvFLUxJLieTJ7UhPmBGTv1uNCXF4pSoCG0xRrimW2pRrMCdDwGioRREFeqVQ6RSVFPHPnH8wJ0HL0e5RKp+WVSom0AC0KmB1u9pfIriXm/X1OdZgTIKTpBUfDy2xSw9GrWUruaqYec0CA7HUje2IynZRXSj2YExBN0ptOg0bmU0xj5oZKSAuJFAqYjXAyRCXGIpkCDJkT2byyyJyAicznB4SQ7As/OVKqK82sPCGvjqowJyAQHaUSq84qow5HuFK9JbOynId6asOcgLH4GPU57JjnkFHHZrAV8UXS5F50pKDO1IE5AaIoDoUzzr+qTAtl1Kky0dcgvMEB+W2pBnMCCCF3JjJuElabqrVZzydmxWlZSiWeoKegttSBRQF9433SoZbXOvM/zl1ma5QOI8modxL/AnKjN+sORJN9dV4VijXFjWVuaXLt/rW0KPOZkqqwKOBeZIS6hrWxckNeFdY6WvUavTTpHj6vQGcqwKIAQkjXUMaPW6pMlSvLV+Q+fXPNM9JhIpXo9v+pTGdKw6iAXwdOU5vR7e6XudyelLY4mqmLvacHO2Ufq6kNowLG4w86fV3SpK609oXa52edaNaZd6/aJU1SYurY7R8V7U5JGBVACPmm70g8lXEYucO9jVpbKKx6a3vbPps+415Fh+eEP+uOEDuwK2A8Pv7V9a+lCUe4nSveeHfdO0stNdSL9Vr9szVPH3riM+pWnT/iz/1eOwjMHdFJ6fCcbLS5N2TeLG91tLQ6WgLRwJ2gJ5wIa3mt3Wh3WZzZB+4xIfbhhY+ZXf2nYVoAIeSTngM6jb4l63K53Wi3G+0zTIwJsfe632f553nTsLsETSOkhfbzH/xw67iYz3nuYGhwz5m9f41dV68xpZgDv5SfxmVxbnNvXW1fNfNmdCw+dvz2Tx2eE0LWnRQ2mTMCpllYsmCto7XR5q4yVZXqLXqNXkgL4WTEH/F7Jvp7Aj1XAlezb6OwzBwT8OjB+mfAIw8KAAYFAIMCgEEBwKAAYFAAMCgAGBQADAoABgUAgwKAQQHAoABgUAAwKAAYFAAMCgAGBQCDAoBBAcCgAGBQADAoABgUAAwKAAYFAPMvG5/baVXmjx8AAAAASUVORK5CYII=",
}
FF={
"manifest.json": r'''{
  "manifest_version": 2,
  "name": "a-bridge",
  "version": "0.9",
  "description": "HTTP long-poll bridge for `a` automation. The SINGLE poll connection lives in background.js (one connection, not tab-throttled); content.js runs dispatched commands per-frame. Deps: Firefox Nightly + xpinstall.signatures.required=false in user.js.",
  "permissions": [
    "<all_urls>",
    "storage",
    "notifications",
    "tabs",
    "activeTab",
    "webNavigation"
  ],
  "options_ui": {"page": "options.html"},
  "user_scripts": {"api_script": "api.js"},
  "browser_action": {"default_title": "bri-ext", "default_popup": "options.html", "default_icon": {"16":"icon16.png","48":"icon48.png"}},
  "icons": {"16":"icon16.png","48":"icon48.png","128":"icon128.png"},
  "browser_specific_settings": {
    "gecko": {
      "id": "a-bridge@seanpatten",
      "strict_min_version": "115.0"
    }
  },
  "background": {"scripts": ["background.js"]},
  "chrome_url_overrides": {"newtab": "newtab.html"},
  "content_scripts": [
    {
      "matches": [
        "<all_urls>"
      ],
      "js": [
        "content.js"
      ],
      "run_at": "document_end",
      "all_frames": true
    },
    {
      "matches": ["<all_urls>"],
      "js": ["instant-preload.js"],
      "run_at": "document_idle"
    },
    {
      "matches": ["<all_urls>"],
      "js": ["pageflip.js", "clickdown.js"],
      "run_at": "document_idle"
    }
  ]
}''',
"background.js": r'''// a-bridge background — owns the SINGLE poll connection to the bridge. Previously every
// content-script frame polled independently; two failures forced this redesign:
//   1) Firefox caps persistent connections per server at 6. On Google sites every frame's
//      poll is CSP-routed through here as a held connection, so >6 frames (Gmail main +
//      its many subframes + other Google tabs) saturate the 6 slots — the target frame's
//      poll never registers, so it can POST a hello but never RECEIVE a command. ONE
//      background-owned poll = one connection, no saturation.
//   2) Firefox throttles timers in background/unfocused tabs, starving a content-script
//      poll loop. The persistent background page is NOT tab-throttled.
// Flow: background long-polls; each command is fanned out to every frame of every tab via
// tabs.sendMessage (message handlers fire even in throttled tabs); each frame's reply is
// POSTed to /resp with the command id. open/screenshot are handled HERE (no fan-out).
const POLL = 'http://127.0.0.1:1234/poll', RESP = 'http://127.0.0.1:1234/resp';
const post = (d) => fetch(RESP, {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(d)}).catch(()=>{});

// open+focus a tab — deduped so a broadcast opens ONE tab (or focuses an existing one).
// Match by NORMALIZED url (origin+path, no trailing slash / query / hash): real pages mutate their
// URL (Yahoo /quote/ACN → /quote/ACN/, ?p=…), and exact-match would miss the prefetched tab → dupes.
const _opening = new Map();
const _norm = u => { try { const x = new URL(u); return x.origin + x.pathname.replace(/\/+$/,''); }
                     catch (e) { return u.split(/[?#]/)[0].replace(/\/+$/,''); } };
function openTab(url, bg) {            // bg:true → load in background; dedup only the find-or-create,
  const key = _norm(url);             // activate PER-CALL so a foreground flip always focuses even if a bg prefetch is in flight
  if (!_opening.has(key)) _opening.set(key, (async () => {
    const hit = (await browser.tabs.query({})).find(t => t.url && _norm(t.url) === key);
    const tab = hit || await browser.tabs.create({url, active:false});
    setTimeout(() => _opening.delete(key), 3000);
    return tab.id;
  })());
  const p = _opening.get(key);
  return bg ? p.then(id => ({id, focused:false}))
            : p.then(async id => { await browser.tabs.update(id, {active:true}); return {id, focused:true}; });
}

// execute one command: open/screenshot run here; everything else fans out to all frames.
async function run(cmd) {
  const id = cmd.id;
  if (cmd.action === 'open') {
    try { return post({id, src:'background', ok:true, value: await openTab(cmd.url, cmd.bg)}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'screenshot') {
    try { return post({id, src:'background', ok:true, value: await browser.tabs.captureVisibleTab(null, {format: cmd.format||'png'})}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'close') {   // close the tab matching cmd.url (deck flip) or the active tab; privileged → must live here
    try { const ts = await browser.tabs.query(cmd.url ? {} : {active:true, currentWindow:true});
      const t = cmd.url ? ts.find(x => x.url && _norm(x.url) === _norm(cmd.url)) : ts[0];
      if (t) await browser.tabs.remove(t.id);
      return post({id, src:'background', ok:true, value:{closed: t ? t.id : null}}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  const tabs = await browser.tabs.query({});
  await Promise.all(tabs.map(async (tab) => {
    let frames = null;
    try { frames = await browser.webNavigation.getAllFrames({tabId: tab.id}); } catch (e) {}
    const fids = (frames && frames.length) ? frames.map(f => f.frameId) : [0];
    await Promise.all(fids.map(async (fid) => {
      try {
        const out = await browser.tabs.sendMessage(tab.id, {__bri_cmd: cmd}, {frameId: fid});
        if (out) await post({id, ...out});
      } catch (e) { /* frame has no content script (about:/pdf/discarded) — skip silently */ }
    }));
  }));
}

// the one poll loop — re-registers immediately, runs the command without blocking the next poll
async function loop() {
  let r;
  try { r = await fetch(POLL); } catch (e) { setTimeout(loop, 1500); return; }
  if (r.status === 200) {
    let cmd = null; try { cmd = await r.json(); } catch (e) {}
    loop();                       // re-register the poll before dispatching
    if (cmd) run(cmd).catch(()=>{});
    return;
  }
  loop();                          // 204 (idle) → poll again
}
loop();
post({src:'background', hello:'bri-ext background poll', v:'0.9'});

// internal messages from the other content scripts (instant-preload.js etc.). open/screenshot
// kept here too so any in-page caller still works, sharing the same dedup as the poll path.
browser.runtime.onMessage.addListener(async (msg) => {
  if (!msg) return;
  if (msg.a === 'fetch') {
    const r = await fetch(msg.url, msg.opts || {});
    return {status: r.status, body: r.status === 200 ? await r.json() : null};
  }
  if (msg.action === 'open') return openTab(msg.url);
  if (msg.action === 'screenshot')
    return browser.tabs.captureVisibleTab(null, {format: msg.format || 'png'});
  if (msg.type === 'preload-debug') {
    const { debugNotifications } = await browser.storage.sync.get({debugNotifications: false});
    if (!debugNotifications) return;
    const d = msg.data, u = d.url.length > 60 ? d.url.slice(0,57)+'...' : d.url;
    const nid = `preload-${Date.now()}`;
    browser.notifications.create(nid, {type:'basic', iconUrl:'icon48.png', title:'Page Preloaded',
      message:u, contextMessage:`${d.triggerType} | ${d.duration}ms${d.method?' | '+d.method:''}${d.attemptedPrerender?' (prerender)':''}`});
    setTimeout(() => browser.notifications.clear(nid), 3000);
  }
});
''',
"content.js": r'''// a-bridge content script — runs in EVERY frame (all_frames). It no longer polls; the
// SINGLE poll connection lives in background.js (see why there). This script just executes
// one dispatched command in ITS OWN frame and returns the result to the background, which
// POSTs it. Receiving a runtime message fires even in throttled/background tabs, so this
// path works where a content-script poll loop would be starved.
(() => {
  const $ = s => document.querySelector(s);
  const dispatch = async (m) => {
    try {
      switch (m.action) {
        case 'navigate': if (self === top) top.location = m.url; return {ok:true};
        case 'click':    $(m.sel).click(); return {ok:true};
        case 'type':     { let e=$(m.sel);
                           if (!e.isContentEditable && e.tagName==='DIV')
                             e = e.querySelector('[contenteditable]') || e;
                           e.focus();
                           if (e.isContentEditable) document.execCommand('insertText',false,m.text);
                           else e.value = m.text;
                           e.dispatchEvent(new InputEvent('input',{bubbles:true,data:m.text,inputType:'insertText'}));
                           return {ok:true}; }
        case 'keys':     { const e=$(m.sel)||document.activeElement; e.focus();
                           (Array.isArray(m.keys)?m.keys:[m.keys]).forEach(k=>{
                             ['keydown','keyup'].forEach(t=>e.dispatchEvent(new KeyboardEvent(t,{key:k,bubbles:true,cancelable:true}))); });
                           return {ok:true}; }
        case 'text':     return {ok:true, value:$(m.sel).innerText};
        case 'html':     return {ok:true, value:document.documentElement.outerHTML.slice(0,200000)};
        case 'find':     { const need=(m.text||'').toLowerCase().trim();
                           const sel=m.sel||'button, [role="button"], a, [tabindex]:not([tabindex="-1"])';
                           const hits=[];
                           const walk=root=>{
                             for(const el of root.querySelectorAll(sel)){
                               const t=((el.innerText||el.textContent||'')+' '+(el.getAttribute('aria-label')||'')).toLowerCase();
                               if(!need||t.includes(need)) hits.push(el);
                             }
                             for(const el of root.querySelectorAll('*')) if(el.shadowRoot) walk(el.shadowRoot);
                           };
                           walk(document);
                           if(m.click&&hits.length) hits[0].click();
                           return {ok:true, value:{n:hits.length, first:(hits[0]?(hits[0].innerText||hits[0].getAttribute('aria-label')||'').trim().slice(0,80):null)}}; }
        case 'eval':     { let c=m.code; try{if(window.trustedTypes&&trustedTypes.createPolicy){const tt=window._abp||(window._abp=trustedTypes.createPolicy('abridge',{createScript:s=>s}));c=tt.createScript(m.code);}}catch(e){}
                           return {ok:true, value:await (async()=>eval(c))()}; }
        case 'wait':     await new Promise(r=>setTimeout(r,m.ms||500)); return {ok:true};
        case 'url':      return {ok:true, value:location.href};
        default: return {error:'unknown action: '+m.action};
      }
    } catch (e) { return {error:String(e)}; }
  };
  // Background fans each command here as {__bri_cmd}. Return the tagged result; background POSTs it.
  browser.runtime.onMessage.addListener((msg) => {
    if (msg && msg.__bri_cmd) return dispatch(msg.__bri_cmd).then(out => ({src: location.href, ...out}));
    // not ours → return undefined so other listeners (preload-debug etc.) still see it
  });
})();
''',
"api.js": r'''// bri-ext apiScript — exposes bridge_fetch to each registered userscript.
// Routes HTTP through background (CSP-exempt) so the userscript can talk to
// 127.0.0.1:1234 even when page CSP's connect-src would block window.fetch.
// This is the GM_xmlhttpRequest equivalent, simplified to one call.
browser.userScripts.onBeforeScript.addListener((script) => {
  script.defineGlobals({
    bridge_fetch: async (url, opts) =>
      browser.runtime.sendMessage({a: 'fetch', url, opts: opts || {}}),
  });
});
''',
"options.html": r'''<!doctype html><meta charset=utf-8><title>bri-ext</title>
<style>
body{font:15px system-ui;width:380px;margin:0;padding:14px;background:#000;color:#fff}
#cap{padding:.6em;border-radius:4px;margin-bottom:.6em;font-weight:600;color:#000}
.ok{background:#2c2}.warn{background:#fa3}
#live{white-space:pre-wrap;font:13px/1.5 ui-monospace,monospace;background:#000;color:#5f5;border:1px solid #555;padding:.6em;border-radius:4px;margin:.5em 0}
label{display:block;margin:.7em 0;cursor:pointer;font-size:15px}
input[type=checkbox]{width:1.3em;height:1.3em;accent-color:#2c2;vertical-align:-3px;margin-right:.5em}
button{font:14px system-ui;padding:.3em .8em;background:#333;color:#fff;border:1px solid #666;border-radius:4px;cursor:pointer;float:right}
button:hover{background:#444}
#stat{color:#5f5;font-size:13px;min-height:1.2em;margin-top:.4em}
</style>
<div id=cap class=warn></div>
<button id=refresh>↻ refresh</button>
<div id=live>checking…</div>
<label><input type=checkbox id=enablePrerender> Enable prerender (full page load)</label>
<label><input type=checkbox id=debugNotifications> Debug notifications</label>
<label><input type=checkbox id=debugMode> Debug mode (console + overlay)</label>
<label><input type=checkbox id=pageflip> Pageflip — ↓ next page / ↑ prev (one tap), ▲▼ buttons</label>
<label><input type=checkbox id=clickdown> Clickdown — open links on press, before release (faster; most sites)</label>
<div id=stat></div>
<script src=options.js></script>
''',
"options.js": r'''const $ = id => document.getElementById(id);
// Firefox has no Speculation Rules API (libxul.so: 0 'speculation_rules' strings
// — not in Gecko at all, not just behind a pref). Always falls back to
// <link rel=prefetch>; for true prerender use a Chromium browser.
const sup = HTMLScriptElement.supports?.('speculationrules');
const isFF = navigator.userAgent.includes('Firefox');
$('cap').textContent = sup
  ? '✓ Speculation Rules supported — full prerender available'
  : isFF
    ? '⚠ Firefox: no Speculation Rules API (not implemented in Gecko yet — no about:config flag). Using <link rel=prefetch> fallback, which is the best Firefox offers.'
    : '⚠ No Speculation Rules — prefetch fallback only';
$('cap').className = sup ? 'ok' : 'warn';

const defs = {enablePrerender:true, debugNotifications:false, debugMode:false, pageflip:true, clickdown:true};
chrome.storage.sync.get(defs, items => {
  for (const k in defs) {
    $(k).checked = items[k];
    $(k).onchange = e => chrome.storage.sync.set({[k]: e.target.checked}, () => {
      $('stat').textContent = `${k} = ${e.target.checked}`;
    });
  }
});

const probe = `({host:location.host, ready:document.readyState,
  inst: typeof _delayOnHover === 'number',
  preloaded: typeof _preloadedList !== 'undefined' ? _preloadedList.size : null,
  prefetch: document.querySelectorAll('link[rel=prefetch]').length,
  specrules: document.querySelectorAll('script[type=speculationrules]').length})`;
const refresh = () => chrome.tabs.query({active:true, currentWindow:true}, ([t]) => {
  if (!t) return;
  chrome.tabs.executeScript(t.id, {code: probe}, ([r]) => {
    if (chrome.runtime.lastError || !r) {
      $('live').textContent = `${t.url||'<no url>'}\n(extension can't access this URL — chrome://, about:, file://, or AMO)`;
      return;
    }
    $('live').textContent = `${r.host} [${r.ready}]
ext loaded:  ${r.inst ? '✓' : '✗ (script not active here)'}
preloaded:   ${r.preloaded ?? '—'} urls this session
link[rel=prefetch]:     ${r.prefetch}
script[type=specrules]: ${r.specrules}`;
  });
});
refresh();
$('refresh').onclick = refresh;
''',
"newtab.html": r'''<!doctype html><style>html,body{margin:0;height:100vh;background:#000}</style><script src="newtab.js"></script>''',
"newtab.js": r'''// Ctrl+T pins keyboard focus to the urlbar no matter what the page does (bugzilla 1411465);
// a tabs.CREATEd tab focuses content. NTO's trick: spawn the real tab, remove this shell.
browser.tabs.getCurrent().then(t => {
  browser.tabs.create({url: 'http://localhost:1111/', index: t.index + 1});
  browser.tabs.remove(t.id);
});
''',
}
CH={
"manifest.json": r'''{
  "manifest_version": 3,
  "name": "bri-chrome",
  "version": "0.8",
  "description": "Chrome extension: a-bridge automation (HTTP long-poll :1234, eval via userScripts world) + instant-preload (hover prerender) + pageflip (Space/Down=next, Shift+Space/Up=prev). Toggle preload/pageflip in options. Dev hot-reload: a extload reload.",
  "permissions": ["storage", "notifications", "userScripts"],
  "host_permissions": ["<all_urls>"],
  "background": { "service_worker": "sw.js" },
  "action": { "default_title": "bri-chrome — click for options" },
  "options_ui": { "page": "options.html", "open_in_tab": true },
  "chrome_url_overrides": { "newtab": "newtab.html" },
  "content_scripts": [
    { "matches": ["<all_urls>"], "exclude_matches": ["http://localhost:1111/*", "http://127.0.0.1:1111/*"], "js": ["instant-preload.js", "pageflip.js", "clickdown.js"], "run_at": "document_idle", "all_frames": false }
  ]
}
''',
"sw.js": r'''// bri-chrome service worker: open options on toolbar click + show debug-preload notifications (gated by the debugNotifications toggle).
// dev hot-reload: hold a WS to the a-server; "reload" -> reload focused tab + the extension (picks up edited files on disk). Trigger: a extload reload.
(function r(){let ws;try{ws=new WebSocket('ws://localhost:1111/extreload')}catch(e){return setTimeout(r,3000)}
  ws.onmessage=e=>{if(e.data!=='reload')return;chrome.tabs.query({active:true,currentWindow:true},t=>{if(t[0])chrome.tabs.reload(t[0].id);chrome.runtime.reload()})};
  ws.onclose=ws.onerror=()=>setTimeout(r,3000);})();
chrome.action.onClicked.addListener(()=>chrome.runtime.openOptionsPage());
chrome.runtime.onMessage.addListener(msg=>{
  if(msg.type!=='preload-debug')return;
  chrome.storage.sync.get({debugNotifications:false},({debugNotifications})=>{
    if(!debugNotifications)return;
    const d=msg.data, u=d.url.length>60?d.url.slice(0,57)+'...':d.url, id='preload-'+Date.now();
    chrome.notifications.create(id,{type:'basic',iconUrl:'icon48.png',title:'Page Preloaded',
      message:u,contextMessage:`${d.triggerType} | ${d.duration}ms${d.method?' | '+d.method:''}${d.attemptedPrerender?' (prerender)':''}`});
    setTimeout(()=>chrome.notifications.clear(id),3000);
  });
});
// a-bridge: register a USER_SCRIPT-world poller (bridge.js) with an eval-capable CSP — MV3 analog of bri-ext's
// browser.userScripts path. Isolated world bypasses page CSP so eval works on chatgpt/claude. Needs Developer Mode (on for unpacked).
async function bridgeSetup(){
  if(!chrome.userScripts)return; // user-scripts toggle off
  // Frozen-shell: --load-extension is dead in branded Chrome (2026, Canary incl.) and repack+re-drag
  // to change logic is the scream. So this package is a stable SHELL — at each SW start it pulls the
  // LIVE bridge logic from the bri server (:1234 /bridge.js) and registers it as a userScripts CODE
  // string (userScripts is MV3's one sanctioned dynamic-code path). Edit lib/bri-chrome/bridge.js →
  // `a extload reload` (restarts SW → re-fetch) = new logic live, no repack, no re-drag. Same path
  // works once the shell is frozen (packed / policy force-install), since logic isn't in the crx.
  // Falls back to the packaged bridge.js when the server is down (last-known-good).
  let js=[{file:'bridge.js'}];
  try{const r=await fetch('http://127.0.0.1:1234/bridge.js');if(r.ok){const code=await r.text();if(code.trim())js=[{code}];}}catch(e){}
  try{
    await chrome.userScripts.configureWorld({csp:"script-src 'self' 'unsafe-eval'",messaging:true});
    await chrome.userScripts.unregister().catch(()=>{});
    await chrome.userScripts.register([{id:'bri-bridge',matches:['<all_urls>'],js,runAt:'document_end',world:'USER_SCRIPT',allFrames:true}]);
  }catch(e){console.error('bri bridge register failed',e);}
}
bridgeSetup();chrome.runtime.onStartup.addListener(bridgeSetup);
// newtab → :1111 is handled entirely by newtab.html (iframe-embeds the a-server). Deliberately NOT in
// the SW: a webNavigation/tabs redirect needs those permissions, and adding permissions to an unpacked
// extension needs a HARD reload (chrome.runtime.reload / `a extload reload` won't grant them) — which
// breaks the zero-touch reload workflow. Iframe in newtab.html needs no perms and no worker.
// bridge.js networking + screenshot proxy (CSP-exempt SW context). USER_SCRIPT-world messages arrive on onUserScriptMessage.
chrome.runtime.onUserScriptMessage?.addListener((msg,_s,reply)=>{
  if(msg&&msg.a==='fetch'){fetch(msg.url,msg.opts||{}).then(async r=>reply({status:r.status,body:r.status===200?await r.json():null})).catch(e=>reply({status:0,error:String(e)}));return true;}
  if(msg&&msg.action==='screenshot'){chrome.tabs.captureVisibleTab({format:msg.format||'png'}).then(reply).catch(()=>reply(null));return true;}
});
''',
"bridge.js": r'''// a-bridge poller for Chrome — runs in the userScripts USER_SCRIPT world: DOM access + eval allowed
// (sw.js configures that world's CSP with 'unsafe-eval', so eval works on chatgpt/claude despite page CSP).
// Networking is routed through the service worker (chrome.runtime.sendMessage) to bypass page connect-src.
// MV3 analog of lib/bri-ext/content.js; same /poll + /resp protocol, same dispatch actions.
(()=>{
  const POLL='http://127.0.0.1:1234/poll',RESP='http://127.0.0.1:1234/resp',$=s=>document.querySelector(s);
  const xfer=(url,opts)=>chrome.runtime.sendMessage({a:'fetch',url,opts:opts||{}});
  const dispatch=async m=>{try{switch(m.action){
    case 'navigate':top.location=m.url;return{ok:true};
    case 'click':$(m.sel).click();return{ok:true};
    case 'type':{let e=$(m.sel);if(!e.isContentEditable&&e.tagName==='DIV')e=e.querySelector('[contenteditable]')||e;e.focus();
      if(e.isContentEditable)document.execCommand('insertText',false,m.text);else e.value=m.text;
      e.dispatchEvent(new InputEvent('input',{bubbles:true,data:m.text,inputType:'insertText'}));return{ok:true};}
    case 'keys':{const e=$(m.sel)||document.activeElement;e.focus();
      (Array.isArray(m.keys)?m.keys:[m.keys]).forEach(k=>['keydown','keyup'].forEach(t=>e.dispatchEvent(new KeyboardEvent(t,{key:k,bubbles:true,cancelable:true}))));return{ok:true};}
    case 'text':return{ok:true,value:$(m.sel).innerText};
    case 'html':return{ok:true,value:document.documentElement.outerHTML.slice(0,200000)};
    case 'find':{const need=(m.text||'').toLowerCase().trim(),sel=m.sel||'button,[role="button"],a,[tabindex]:not([tabindex="-1"])',hits=[];
      const walk=root=>{for(const el of root.querySelectorAll(sel)){const t=((el.innerText||el.textContent||'')+' '+(el.getAttribute('aria-label')||'')).toLowerCase();if(!need||t.includes(need))hits.push(el);}
        for(const el of root.querySelectorAll('*'))if(el.shadowRoot)walk(el.shadowRoot);};
      walk(document);if(m.click&&hits.length)hits[0].click();
      return{ok:true,value:{n:hits.length,first:hits[0]?(hits[0].innerText||hits[0].getAttribute('aria-label')||'').trim().slice(0,80):null}};}
    case 'eval':return{ok:true,value:await(async()=>eval(m.code))()};
    case 'wait':await new Promise(r=>setTimeout(r,m.ms||500));return{ok:true};
    case 'url':return{ok:true,value:location.href};
    case 'screenshot':return{ok:true,value:await chrome.runtime.sendMessage({action:'screenshot',format:m.format})};
    default:return{error:'unknown action: '+m.action};
  }}catch(e){return{error:String(e)};}};
  const post=d=>xfer(RESP,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({src:location.href,...d})});
  post({hello:location.href,title:document.title,source:'chrome-userscript',v:'0.8'});
  const loop=async()=>{try{const r=await xfer(POLL);const c=r&&r.status===200?r.body:null;
    if(c){const out=await dispatch(c);await post({id:c.id,...out});}loop();}catch(e){setTimeout(loop,2000);}};
  loop();
})();
''',
"options.html": r'''<!doctype html><meta charset=utf-8><title>bri-chrome options</title>
<!-- bri-chrome (Chrome): instant-preload + pageflip, no automation bridge. Standalone extension — the Firefox a-bridge is separate at lib/bri-ext (no shared source). -->
<style>body{font:15px system-ui;padding:20px;max-width:34em}label{display:flex;gap:10px;align-items:center;font-size:16px;margin:.6em 0}small{color:#666}h3{margin-bottom:.2em}</style>
<h3>bri-chrome</h3>
<label><input type=checkbox id=enablePrerender> Instant preload — prerender links on hover (speculation rules)</label>
<label><input type=checkbox id=debugNotifications> Debug notifications — popup on each preload</label>
<label><input type=checkbox id=debugMode> Debug mode — console logging + on-page overlay</label>
<label><input type=checkbox id=pageflip> Pageflip — Down=next page, Up=prev (one tap), ▲▼ buttons</label>
<label><input type=checkbox id=clickdown> Clickdown — open links on press, before release (faster; most sites)</label>
<p><small>Changes take effect immediately, no reload.</small></p>
<script src=options.js></script>
''',
"options.js": r'''// bri-chrome options — instant-preload (prerender / debug notifications / debug mode) + pageflip. Persists in chrome.storage.sync.
const defs={enablePrerender:true, debugNotifications:false, debugMode:false, pageflip:true, clickdown:true};
chrome.storage.sync.get(defs, items=>{
  for(const k in defs){const e=document.getElementById(k);e.checked=items[k];
    e.onchange=()=>chrome.storage.sync.set({[k]:e.checked});}
});
''',
"newtab.html": r'''<!doctype html><meta charset=utf-8>
<!-- New tab = the a-server :1111 page, embedded. No redirect, no service worker, no extra permissions
     — those three were what kept breaking (NTP self-redirect blocked; new perms need a hard reload).
     a-server sends no X-Frame-Options/CSP so it frames fine. FROZEN file: change the new tab by editing
     the :1111 page server-side, never here. -->
<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}iframe{display:block;border:0;width:100vw;height:100vh}</style>
<iframe src="http://localhost:1111/" allow="clipboard-read; clipboard-write"></iframe>
<script>addEventListener('load',()=>{var f=document.querySelector('iframe');f.focus();})</script>
''',
}

def build():
    tg={"bri-ext":FF,"bri-chrome":CH}
    for name,files in tg.items():
        d=os.path.join(OUT,name); os.makedirs(d,exist_ok=True)
        for fn,c in {**files,**SHARED}.items(): open(os.path.join(d,fn),"w",encoding="utf-8").write(c)
        for fn,b in ICONS.items(): open(os.path.join(d,fn),"wb").write(base64.b64decode(b))
    return [os.path.join(OUT,n) for n in tg]

def main(argv):
    paths=build()
    if "verify" in argv[1:]:
        for p in paths:
            old=os.path.join(ROOT,"lib",os.path.basename(p))
            print("=== diff -r %s  (-old +new) ==="%os.path.basename(p))
            subprocess.run(["diff","-r",old,p])
        print("\nclean diff above (besides intended dedup) = generated extensions match the working ones")
    for p in paths: print("\u2713 "+p)
    print("load: a extload "+paths[1]+"   |   firefox xpi: a bri deploy")

if __name__=='__main__': main(sys.argv)   # `import briext; briext.build()` regenerates without running main
