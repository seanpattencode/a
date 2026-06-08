/*! instant.page v5.2.0 (modified for 0ms hover) - (C) 2019-2025 Alexandre Dieulot - https://instant.page/license */

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
