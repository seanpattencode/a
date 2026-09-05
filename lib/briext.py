#!/usr/bin/env python3
"""a briext — build both browser extensions, one script (Firefox MV2 + Chrome MV3)
into adata/local/ext/ on demand. Mirrors `a apk`: single source file, makes the folder tree.
Shared payload (instant-preload, pageflip, icons) is defined ONCE here so the two cannot drift.
  a briext           generate -> adata/local/ext/{bri-ext,bri-chrome}, print load paths
Edit this file to change either extension; rerun to redeploy. Firefox xpi: a bri deploy."""
import os,sys,base64
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
  // Try multiple initialization attempts
  init();
  setTimeout(() => {
    reinitializeEventListeners();
  }, 1000);
  setTimeout(() => {
    reinitializeEventListeners();
  }, 3000);
} else {
  init();
}

function init() {
  const supportChecksRelList = document.createElement('link').relList

  const supportsPrefetch = supportChecksRelList.supports('prefetch')
  if (!supportsPrefetch) {
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
  
  
  if (supportsSpeculationRules) {
    // Browser supports speculation rules
    // Check for data attribute configuration first
    const speculationRulesConfig = document.body.dataset.instantSpecrules
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


  if (preloadOnlyOnMousedown) {
    document.addEventListener('touchstart', touchstartEmptyListener, eventListenersOptions)
  }
  else {
    document.addEventListener('touchstart', touchstartListener, eventListenersOptions)
  }

  if (!preloadOnMousedown) {
    document.addEventListener('mouseover', mouseoverListener, eventListenersOptions)
  }

  if (preloadOnMousedown) {
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
    attachDirectLinkListeners();
  }, 100);
}

function setupMutationObserver() {
  
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
      
      // Attach direct listeners to new links to ensure they work
      attachDirectLinkListeners();
    }
  })
  
  // Start observing the document body for changes
  _mutationObserver.observe(document.body, {
    childList: true,
    subtree: true
  })
  
}

function setupSPANavigationDetection() {
  
  let lastUrl = location.href;
  
  // Check for URL changes periodically
  // This catches navigation that doesn't trigger popstate
  setInterval(() => {
    const currentUrl = location.href;
    if (currentUrl !== lastUrl) {
      lastUrl = currentUrl;
      
      // Clear preloaded list when navigating to allow re-preloading
      _preloadedList.clear();
      _preloadedTimestamps.clear();
      
      // Clear speculation rules
      if (_speculationRulesScript && _speculationRulesScript.parentNode) {
        _speculationRulesScript.parentNode.removeChild(_speculationRulesScript);
        _speculationRulesScript = null;
      }
      

      // Check for link at current mouse position (webcomic-style navigation)
      const linkAtMouse = document.elementFromPoint(_lastMouseX, _lastMouseY)?.closest('a');
      if (linkAtMouse && isPreloadable(linkAtMouse)) preload(linkAtMouse.href, 'high', 'pageload');
    }
  }, 500);
  
  // Also listen for popstate events (back/forward navigation)
  window.addEventListener('popstate', () => {
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
  
  // Check how many are preloadable
  let preloadableCount = 0;
  allLinks.forEach(link => {
    if (isPreloadable(link)) {
      preloadableCount++;
    }
  });
  
  // Attach direct listeners to links
  attachDirectLinkListeners();
  
  // Force test: Try to preload the first preloadable link
  if (preloadableCount > 0) {
    const firstPreloadable = Array.from(allLinks).find(link => isPreloadable(link));
    if (firstPreloadable) {
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
      
      
      // Set timer for this specific link
      link._mouseoverTimer = setTimeout(() => {
        if (!_preloadedList.has(this.href)) {
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
  }
  
  if (isEventLikelyTriggeredByTouch(event)) {
    // This avoids uselessly adding a mouseout event listener and setting a timer.
    return
  }

  if (!('closest' in event.target)) {
    return
    // Without this check sometimes an error "event.target.closest is not a function" is thrown, for unknown reasons
    // That error denotes that `event.target` isn't undefined. My best guess is that it's the Document.
    //
    // Details could be gleaned from throwing such an error:
    //throw new TypeError(`instant.page non-element event target: timeStamp=${~~event.timeStamp}, type=${event.type}, typeof=${typeof event.target}, nodeType=${event.target.nodeType}, nodeName=${event.target.nodeName}, viewport=${innerWidth}x${innerHeight}, coords=${event.clientX}x${event.clientY}, scroll=${scrollX}x${scrollY}`)
  }
  const anchorElement = event.target.closest('a')

  if (!anchorElement) {
    return
  }

  if (!isPreloadable(anchorElement)) {
    return
  }

  anchorElement.addEventListener('mouseout', mouseoutListener, {passive: true})

  _mouseoverTimer = setTimeout(() => {
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
    return
  }

  if (_useWhitelist && !('instant' in anchorElement.dataset)) {
    return
  }

  if (anchorElement.origin != location.origin) {
    let allowed = _allowExternalLinks || 'instant' in anchorElement.dataset
    if (!allowed || !_chromiumMajorVersionInUserAgent) {
      // Chromium-only: see comment on "restrictive prefetch" and "cross-site speculation rules prefetch"
      return
    }
  }

  if (!['http:', 'https:'].includes(anchorElement.protocol)) {
    return
  }

  if (anchorElement.protocol == 'http:' && location.protocol == 'https:') {
    return
  }

  if (!_allowQueryString && anchorElement.search && !('instant' in anchorElement.dataset)) {
    return
  }

  if (anchorElement.hash && anchorElement.pathname + anchorElement.search == location.pathname + location.search) {
    return
  }

  if ('noInstant' in anchorElement.dataset) {
    return
  }

  return true
}

function preload(url, fetchPriority = 'auto', triggerType = 'hover') {
  const startTime = performance.now();
  
  if (_preloadedList.has(url)) {
    return
  }
  
  // Check if we're at the limit for prerenders
  const MAX_PRERENDERS = 5  // Conservative limit for speculation rules
  if (_speculationRulesType !== 'none' && _preloadedList.size >= MAX_PRERENDERS) {
    // Remove the oldest prerender to make room
    const oldestUrl = _preloadedList.values().next().value
    
    _preloadedList.delete(oldestUrl)
    _preloadedTimestamps.delete(oldestUrl)
  }

  
  if (_speculationRulesType != 'none') {
    preloadUsingSpeculationRules(url)
  } else {
    preloadUsingLinkElement(url, fetchPriority)
  }

  _preloadedList.add(url)
  _preloadedTimestamps.set(url, Date.now())
  
  // Remove URL from the list after 4 seconds to allow re-prerendering
  setTimeout(() => {
    _preloadedList.delete(url)
    _preloadedTimestamps.delete(url)
    
    // For link prefetch, clean up the DOM element
    if (_speculationRulesType === 'none') {
      const element = _preloadedElements.get(url)
      if (element && element.parentNode) {
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
"pageflip.js": r'''// pageflip — discrete viewport paging, SHARED by both extensions (FF bri-ext + Chrome bri-chrome). Wheel/Down=next, Up=prev (one notch/tap).
// Universal step: detects the scroll container (window, or a large inner scrollable div) and the obscured
// top/bottom band (fixed/sticky bars) via pixel probes re-run each flip — so sticky / hide-on-scroll headers
// never skip content, on any site. Skips text fields; toggle chrome.storage.sync.pageflip (default off).
// Single source: lib/briext.py generates both — edit there, then `a briext`.
(()=>{
  if(window.__pf)return; window.__pf=1;
  document.documentElement.style.scrollBehavior='auto';
  let active=false;
  const cx=()=>Math.round(innerWidth/2);
  // px obscured by pinned fixed/sticky bars from the top edge (fromTop=1) or bottom (0): first pixel showing flowing content.
  const obsc=fromTop=>{const lim=Math.floor(innerHeight*0.5);for(let i=0;i<lim;i+=4){const y=fromTop?i:innerHeight-1-i;
    const st=document.elementsFromPoint(cx(),y);const t=st[0];if(!t)return i;
    const p=getComputedStyle(t).position;if(p!=='fixed'&&p!=='sticky')return i; // flowing content → edge is clear
    // fixed/sticky obscures only if scrollable content sits behind it (else it's an in-flow sticky block, not a bar)
    if(!st.slice(1).some(e=>{const q=getComputedStyle(e).position;return q!=='fixed'&&q!=='sticky';}))return i;}return 0;};
  // scroll container: window (null) unless the document itself doesn't scroll and a large inner element does.
  const scr=()=>{const se=document.scrollingElement||document.documentElement;if(se&&se.scrollHeight>se.clientHeight+2)return null;
    let best=null,ba=0;for(const el of document.querySelectorAll('div,main,section,article,ul')){if(el.scrollHeight<=el.clientHeight+8)continue;
      if(!/(auto|scroll)/.test(getComputedStyle(el).overflowY))continue;const r=el.getBoundingClientRect(),a=r.width*r.height;if(a>ba){ba=a;best=el;}}return best;};
  const go=d=>{const s=scr();if(s){s.scrollBy(0,d*Math.max(1,s.clientHeight-8));return;}scrollBy(0,d*Math.max(1,innerHeight-obsc(1)-obsc(0)));};
  const infld=t=>t&&(/^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName)||t.isContentEditable);
  const onkey=e=>{if(infld(e.target))return;
    if(e.key==='ArrowDown'||(e.key===' '&&!e.shiftKey)){go(1);e.preventDefault();}else if(e.key==='ArrowUp'||(e.key===' '&&e.shiftKey)){go(-1);e.preventDefault();}};
  let lw=0;  // wheel = one flip per notch; 200ms cooldown tames momentum wheels/trackpads. ctrl+wheel(zoom), horizontal, fields stay native.
  const onwheel=e=>{if(e.ctrlKey||!e.deltaY||infld(e.target))return;e.preventDefault();
    const n=performance.now();if(n-lw>200){lw=n;go(e.deltaY>0?1:-1);}};
  const apply=on=>{
    if(on&&!active){active=true;addEventListener('keydown',onkey,true);addEventListener('wheel',onwheel,{passive:false,capture:true});}
    else if(!on&&active){active=false;removeEventListener('keydown',onkey,true);removeEventListener('wheel',onwheel,true);}};
  chrome.storage.sync.get('pageflip',d=>apply(d.pageflip));
  chrome.storage.onChanged.addListener(c=>c.pageflip&&apply(c.pageflip.newValue));
})();
''',
"clickdown.js": r'''// clickdown (SHARED) — act on PRESS not release. clickdown(ON): <a href> nav on mousedown (reversible, Back undoes).
// clickdown2(opt-in): also replay click on press for [role=link]/row/listitem/gridcell/article/option (Gmail rows, Keep notes);
//   EXCLUDES button/input/select/textarea/editable/menuitem/checkbox/switch — commits (Delete/Archive/Send) stay on release. #32
(()=>{let on=true,ag=false;
chrome.storage.sync.get({clickdown:true,clickdown2:false},d=>{on=d.clickdown;ag=d.clickdown2});
chrome.storage.onChanged.addListener(c=>{c.clickdown&&(on=c.clickdown.newValue);c.clickdown2&&(ag=c.clickdown2.newValue)});
addEventListener('mousedown',e=>{const t=e.target;if(!on||e.button||e.ctrlKey||e.metaKey||e.shiftKey||e.altKey||!t.closest)return;
const a=t.closest('a[href]');
if(a&&a.target!=='_blank'&&!a.hasAttribute('download')&&/^https?:/.test(a.href)){e.preventDefault();location.href=a.href;return}
if(!ag||t.closest('button,[role=button],input,select,textarea,[contenteditable],[role=menuitem],[role=checkbox],[role=switch],summary')||!t.closest('a[href],[role=link],[role=row],[role=listitem],[role=gridcell],[role=article],[role=option]'))return;
e.preventDefault();const s=ev=>{ev.detail!==1337&&(ev.preventDefault(),ev.stopImmediatePropagation())};
addEventListener('click',s,true);setTimeout(()=>removeEventListener('click',s,true),400);
t.dispatchEvent(new MouseEvent('click',{view:window,bubbles:true,cancelable:true,detail:1337,clientX:e.clientX,clientY:e.clientY}))},true)})()
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
  "version": "1.2",
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
let BRI_CHAN = 'firefox';   // exact channel — UA is frozen ('Firefox/152.0') and hides Nightly; getBrowserInfo isn't
try { browser.runtime.getBrowserInfo().then(i => { let c = /a\d/.test(i.version)?'nightly':/b\d/.test(i.version)?'beta':(i.buildID||'').startsWith('2010')?'release':'build'; BRI_CHAN = 'firefox-'+c+'/'+i.version; }).catch(()=>{}); } catch(e) {}
const post = (d) => fetch(RESP, {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({chan:BRI_CHAN, ...d})}).catch(()=>{});

// open+focus a tab — deduped so a broadcast opens ONE tab (or focuses an existing one).
// Match by NORMALIZED url (origin+path, no trailing slash / query / hash): real pages mutate their
// URL (Yahoo /quote/ACN → /quote/ACN/, ?p=…), and exact-match would miss the prefetched tab → dupes.
const _opening = new Map();
const _norm = u => { try { const x = new URL(u); return x.origin + x.pathname.replace(/\/+$/,''); }
                     catch (e) { return u.split(/[?#]/)[0].replace(/\/+$/,''); } };
function openTab(url, bg, fresh) {     // dedup by origin+path; hit → navigate to exact url
  if (fresh) return browser.tabs.create({url, active:!bg}).then(t => ({id:t.id, focused:!bg}));  // fresh: new tab
  const key = _norm(url);
  if (!_opening.has(key)) _opening.set(key, (async () => {
    const hit = (await browser.tabs.query({})).find(t => t.url && _norm(t.url) === key);
    const tab = hit || await browser.tabs.create({url, active:false});
    setTimeout(() => _opening.delete(key), 3000);
    return tab.id;
  })());
  const p = _opening.get(key);
  return bg ? p.then(id => ({id, focused:false}))
            : p.then(async id => { const t = await browser.tabs.get(id); await browser.tabs.update(id, t.url === url ? {active:true} : {url, active:true}); await browser.windows.update(t.windowId, {focused:true}); return {id, focused:true}; });  // same url = FOCUS only, no reload (a streaming answer survives; Sean 2026-09-03); else land on the EXACT url (SERP re-search), per-call not cached
}

// user.js loadDivertedInBackground (wiki-feed appends) backgrounds even hand-clicked target=_blank links; a click on a
// localhost dashboard must focus like Chrome. Human click = opener tab active+localhost; automation opens have no/bg opener.
browser.tabs.onCreated.addListener(async t => {
  if (t.active || !t.openerTabId) return;
  try { const o = await browser.tabs.get(t.openerTabId);
    if (o.active && /^https?:\/\/(localhost|127\.0\.0\.1):/.test(o.url)) browser.tabs.update(t.id, {active:true}); } catch (e) {}
});

// execute one command: open/screenshot run here; everything else fans out to all frames.
async function run(cmd) {
  const id = cmd.id;
  if (cmd.action === 'open') {
    try { return post({id, src:'background', ok:true, value: await openTab(cmd.url, cmd.bg, cmd.fresh)}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'screenshot') {
    try { return post({id, src:'background', ok:true, value: await browser.tabs.captureVisibleTab(null, {format: cmd.format||'png'})}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'navigate') {   // ACTIVE tab only (or the cmd.match tab). Must live here: the content-script path runs in EVERY frame of EVERY tab, so a bare `a bri <url>` BROADCAST-navigated every open tab — one command converted 4 live tabs, and for a query-by-URL site each hijacked tab started its own search (2026-08-08 perplexity storm; it also ate sibling providers' tabs mid-answer). Chrome's SW already scopes to active tabs; Firefox did not.
    try { const ts = await browser.tabs.query(cmd.match ? {} : {active:true, currentWindow:true});
      const t = cmd.match ? ts.find(x => (x.url||'').includes(cmd.match)) : ts[0];
      if (t) await browser.tabs.update(t.id, {url: cmd.url});
      return post({id, src:'background', ok:true, value:{navigated: t ? t.id : null}}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'close') {   // close the tab matching cmd.url (deck flip) or the active tab; privileged → must live here
    try { const ts = await browser.tabs.query(cmd.url ? {} : {active:true, currentWindow:true});
      const t = cmd.url ? ts.find(x => x.url && _norm(x.url) === _norm(cmd.url)) : ts[0];
      if (t) await browser.tabs.remove(t.id);
      return post({id, src:'background', ok:true, value:{closed: t ? t.id : null}}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'tabs') {   // list ALL tabs incl. error/discarded ones content scripts can't see
    try { return post({id, src:'background', ok:true, value:(await browser.tabs.query({})).filter(t=>!cmd.match||(t.url||'').includes(cmd.match)).map(t=>[t.id, t.windowId, t.discarded?'discarded':t.status, (t.url||'').slice(0,200), (t.title||'').slice(0,60)])}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'closeall') {   // close EVERY tab whose url contains cmd.match (batch job cleanup; match required)
    try { const hs = cmd.match ? (await browser.tabs.query({})).filter(t=>(t.url||'').includes(cmd.match)) : [];
      await browser.tabs.remove(hs.map(t=>t.id));
      return post({id, src:'background', ok:true, value:hs.length}); }
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
  try { r = await fetch(POLL, {headers:{'X-Bri-Chan':BRI_CHAN}}); } catch (e) { setTimeout(loop, 1500); return; }
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
      if (m.match && !(self === top && location.href.includes(m.match))) return {skip:1};   // m.match targets any action at tabs whose URL contains it (top frame only)
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
        case 'links':    return {ok:true, value:[...document.querySelectorAll('a[href^="http"]')].filter(a=>a.offsetParent&&(a.innerText||'').trim().length>2).map(a=>[a.href,(a.innerText||'').trim().replace(/\s+/g,' ').slice(0,80)]).slice(0,300)};
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
<label><input type=checkbox id=pageflip> Pageflip — wheel/↓ next page, ↑ prev (one notch/tap)</label>
<label><input type=checkbox id=clickdown> Clickdown — open links on press, before release (faster; most sites)</label>
<label><input type=checkbox id=clickdown2> Clickdown+ — also press-open JS/role=link nav (Amazon/Google); buttons excluded</label>
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

const defs = {enablePrerender:true, debugNotifications:false, debugMode:false, pageflip:false, clickdown:true, clickdown2:false};
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
  "version": "1.8",
  "description": "Chrome extension: a-bridge automation (offscreen-doc long-poll :1234, focus-immune; commands run via chrome.scripting, no toggle) + instant-preload (hover prerender) + pageflip.",
  "permissions": ["storage", "scripting", "alarms", "offscreen"],
  "host_permissions": ["<all_urls>"],
  "background": { "service_worker": "sw.js" },
  "action": { "default_title": "bri-chrome — click for options" },
  "options_ui": { "page": "options.html", "open_in_tab": true },
  "chrome_url_overrides": { "newtab": "newtab.html" },
  "content_scripts": [
    { "matches": ["<all_urls>"], "js": ["wake.js"], "run_at": "document_start", "all_frames": false },
    { "matches": ["<all_urls>"], "exclude_matches": ["http://localhost:1111/*", "http://127.0.0.1:1111/*"], "js": ["instant-preload.js", "pageflip.js", "clickdown.js"], "run_at": "document_idle", "all_frames": false }
  ]
}
''',
"sw.js": r'''// bri-chrome service worker — privileged half of the bridge, works while Chrome is UNFOCUSED.
// MV3 SWs are killed (~30s) and CANNOT hold a long-poll, so the persistent poll lives in an OFFSCREEN
// DOCUMENT (offscreen.js — a real page, not tab-throttled, not SW-lifetime-capped). The offscreen relays
// each command here; the SW runs it in the target tab via chrome.scripting.executeScript (ISOLATED world,
// no "Allow user scripts" toggle) and POSTs the result to /resp. wake.js (content script) + onStartup/
// onInstalled/alarms re-create the offscreen doc if Chrome ever closes it — self-healing.
const RESP='http://127.0.0.1:1234/resp';
// keepalive:true lets the POST finish even if the SW is torn down the instant after (fire-and-forget from a
// dying worker otherwise aborts — this is why every earlier diagnostic vanished). Learned from claude-in-chrome.
const post=d=>fetch(RESP,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({chan:'chrome',...d}),keepalive:true}).catch(()=>{});
post({sw:'top',off:typeof chrome.offscreen});  // DIAG: SW ran + can reach :1234; reports if chrome.offscreen exists

// DISPATCH runs IN the target tab (ISOLATED world) via executeScript(func,args): pure fn of the command,
// returns the /resp payload. Must be self-contained (serialized standalone) — no outer refs. Edit = repack.
function DISPATCH(m){
  const $=s=>document.querySelector(s);
  if(m.vis&&document.hidden)return {skip:'hidden'};   // only the visible tab acts (Flutter ignores input when hidden)
  const PE=(t,x,y,b)=>new PointerEvent(t,{bubbles:true,cancelable:true,composed:true,clientX:x,clientY:y,view:window,pointerId:1,pointerType:'mouse',isPrimary:true,button:0,buttons:b});
  const run=async()=>{ switch(m.action){
    case 'navigate': top.location=m.url; return {ok:true};
    case 'click': $(m.sel).click(); return {ok:true};
    case 'tap': { const el=document.elementFromPoint(m.x,m.y)||document.body;   // synthetic tap at client coords (canvas/Flutter)
      ['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const up=t.endsWith('up')||t==='click';
        el.dispatchEvent(t[0]==='p'?PE(t,m.x,m.y,up?0:1):new MouseEvent(t,{bubbles:true,cancelable:true,clientX:m.x,clientY:m.y,view:window,buttons:up?0:1}));});
      return {ok:true,value:el.tagName}; }
    case 'drag': { const el=$('flt-glass-pane')||document.elementFromPoint(m.x1,m.y1)||document.body,N=16;   // spin/pan a canvas
      el.dispatchEvent(PE('pointerdown',m.x1,m.y1,1));
      for(let i=1;i<=N;i++){const x=m.x1+(m.x2-m.x1)*i/N,y=m.y1+(m.y2-m.y1)*i/N;el.dispatchEvent(PE('pointermove',x,y,1));await new Promise(r=>setTimeout(r,16));}
      el.dispatchEvent(PE('pointerup',m.x2,m.y2,0)); return {ok:true}; }
    case 'type': { let e=$(m.sel); if(!e.isContentEditable&&e.tagName==='DIV')e=e.querySelector('[contenteditable]')||e; e.focus();
      if(e.isContentEditable)document.execCommand('insertText',false,m.text); else e.value=m.text;
      e.dispatchEvent(new InputEvent('input',{bubbles:true,data:m.text,inputType:'insertText'})); return {ok:true}; }
    case 'keys': { const e=$(m.sel)||document.activeElement; e.focus();
      (Array.isArray(m.keys)?m.keys:[m.keys]).forEach(k=>['keydown','keypress','keyup'].forEach(t=>e.dispatchEvent(new KeyboardEvent(t,{key:k,code:k,keyCode:k==='Enter'?13:0,bubbles:true,cancelable:true})))); return {ok:true}; }
    case 'text': return {ok:true,value:$(m.sel).innerText};
    case 'html': return {ok:true,value:document.documentElement.outerHTML.slice(0,200000)};
    case 'url': return {ok:true,value:location.href};
    case 'size': return {ok:true,value:[innerWidth,innerHeight]};
    case 'sem': { const p=document.querySelector('flt-semantics-placeholder');   // enable Flutter a11y tree, then enumerate labelled nodes
      if(p&&!document.querySelector('flt-semantics-host [role]')){p.click(); await new Promise(r=>setTimeout(r,900));}
      const seen=new Set(),out=[];
      for(const e of document.querySelectorAll('flt-semantics-host [aria-label],flt-semantics-host [role],flt-semantics-host input')){
        const r=e.getBoundingClientRect(); if(r.width<1&&r.height<1)continue;
        const lab=(e.getAttribute('aria-label')||e.getAttribute('role')||e.tagName), k=lab+'@'+(r.x+r.width/2|0)+','+(r.y+r.height/2|0);
        if(seen.has(k))continue; seen.add(k); out.push([e.getAttribute('role')||'',lab.slice(0,36),[r.x+r.width/2|0,r.y+r.height/2|0]]); }
      return {ok:true,value:out.slice(0,60)}; }
    case 'wait': await new Promise(r=>setTimeout(r,m.ms||500)); return {ok:true};
    case 'set': await chrome.storage.sync.set(m.kv||{}); return {ok:true,value:await chrome.storage.sync.get(null)};  // MV3 bans eval; flag flips
    default: return {error:'unknown action: '+m.action};
  } };
  return run().catch(e=>({error:String(e)}));
}

// open+focus; dedup by origin+path; hit → navigate to exact url (SW)
async function openTab(url,bg){const norm=u=>{try{const x=new URL(u);return x.origin+x.pathname.replace(/\/+$/,'')}catch(e){return (u||'').split(/[?#]/)[0]}};
  const key=norm(url);const hit=(await chrome.tabs.query({})).find(t=>t.url&&norm(t.url)===key);
  if(hit&&hit.url!==url)await chrome.tabs.update(hit.id,{url});
  const tab=hit||await chrome.tabs.create({url,active:!bg});if(hit&&!bg)await chrome.tabs.update(hit.id,{active:true});return{id:tab.id,focused:!bg};}

async function execCmd(cmd){
  const id=cmd.id;
  try{
    if(cmd.action==='screenshot')return post({id,src:'sw',ok:true,value:await chrome.tabs.captureVisibleTab({format:cmd.format||'png'})});
    if(cmd.action==='open')return post({id,src:'sw',ok:true,value:await openTab(cmd.url,cmd.bg)});
    if(cmd.action==='tabs')return post({id,src:'sw',ok:true,value:(await chrome.tabs.query({})).filter(t=>!cmd.match||(t.url||'').includes(cmd.match)).map(t=>[t.id,t.windowId,t.discarded?'discarded':t.status,(t.url||'').slice(0,200),(t.title||'').slice(0,60)])});
  }catch(e){return post({id,src:'sw',error:String(e)});}
  // target: tabs whose url contains cmd.host, else the active tab of each window (never a hidden background tab)
  let tabs=(await chrome.tabs.query({})).filter(t=>t.url&&/^https?:/.test(t.url));
  if(cmd.host)tabs=tabs.filter(t=>t.url.includes(cmd.host));
  else{const a=tabs.filter(t=>t.active);if(a.length)tabs=a;}
  await Promise.all(tabs.map(async t=>{try{
    const res=await chrome.scripting.executeScript({target:{tabId:t.id},world:'ISOLATED',func:DISPATCH,args:[cmd]});
    for(const r of (res||[])){const v=r&&r.result;if(v!=null)await post({id,src:t.url,...v});}
  }catch(e){}}));
}

// create the offscreen poller if absent. Called from every SW wake path so a closed doc self-heals.
let offP=null;   // single-flight: createDocument throws if called twice concurrently or if a doc already exists (claude-in-chrome pattern)
function ensureOffscreen(){
  if(offP)return offP;
  offP=(async()=>{
    try{
      const c=await chrome.runtime.getContexts({contextTypes:['OFFSCREEN_DOCUMENT']});   // getContexts is the race-free existence check
      if(c&&c.length){post({sw:'offscreen-exists'});return;}
      await chrome.offscreen.createDocument({url:'offscreen.html',reasons:['BLOBS'],justification:'hold the a-bridge localhost long-poll'});
      post({sw:'offscreen-created'});
    }catch(e){post({sw:'offscreen-err',e:String(e)});}
  })().finally(()=>{offP=null});
  return offP;
}
chrome.runtime.onMessage.addListener((msg,_s,reply)=>{
  if(msg&&msg.bri==='cmd'){execCmd(msg.cmd).then(()=>{try{reply({ok:1})}catch(e){}});return true;}  // await keeps the SW alive through exec
  // wake.js page-load ping → (re)create the offscreen poller. MUST return true + reply after awaiting, else
  // the SW dies before createDocument finishes (async work started from a listener needs the channel held open).
  (async()=>{try{await ensureOffscreen();post({sw:'offscreen-ok'});}catch(e){post({sw:'offscreen-err',e:String(e)});}try{reply({ok:1})}catch(e){}})();
  return true;
});
chrome.runtime.onStartup.addListener(ensureOffscreen);
chrome.runtime.onInstalled.addListener(ensureOffscreen);
chrome.alarms.create('bri',{periodInMinutes:0.4});   // ~24s heartbeat: re-create the offscreen doc if it was closed
chrome.alarms.onAlarm.addListener(ensureOffscreen);
ensureOffscreen();
chrome.action.onClicked.addListener(()=>chrome.runtime.openOptionsPage());
''',
"offscreen.html": r'''<!doctype html><meta charset=utf-8><title>bri poller</title><script src="offscreen.js"></script>''',
"offscreen.js": r'''// bri-chrome persistent poller — runs in an offscreen document (NOT killed like the SW, NOT tab-throttled),
// so it holds the :1234 long-poll while Chrome is unfocused. Each command is relayed to the SW, which has the
// privileged chrome.scripting/tabs APIs to run it in the target tab and POST the result. This is the piece
// that made background driving work: the SW alone can't stay alive to poll.
const POLL='http://127.0.0.1:1234/poll', RESP='http://127.0.0.1:1234/resp';
fetch(RESP,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({hello:'offscreen-boot'}),keepalive:true}).catch(()=>{});
// keepalive: a message from the offscreen doc every <30s resets the SW idle timer, so the SW stays warm to
// relay commands into tabs (Chrome 109+ documented pattern). The offscreen doc itself never dies.
setInterval(()=>chrome.runtime.sendMessage({bri:'ka'}).catch(()=>{}),20000);
async function loop(){
  for(;;){
    let r; try{r=await fetch(POLL,{headers:{'X-Bri-Chan':'chrome'}});}catch(e){await new Promise(s=>setTimeout(s,1500));continue;}
    if(r.status===200){let c=null;try{c=await r.json();}catch(e){} if(c)chrome.runtime.sendMessage({bri:'cmd',cmd:c}).catch(()=>{});}
  }
}
loop();
''',
"wake.js": r'''chrome.runtime.sendMessage({bri:'wake'}).catch(()=>{});  // page-load ping wakes the SW → it (re)creates the offscreen poller''',
"options.html": r'''<!doctype html><meta charset=utf-8><title>bri-chrome options</title>
<!-- bri-chrome (Chrome): instant-preload + pageflip, no automation bridge. Standalone extension — the Firefox a-bridge is separate at lib/bri-ext (no shared source). -->
<style>body{font:15px system-ui;padding:20px;max-width:34em}label{display:flex;gap:10px;align-items:center;font-size:16px;margin:.6em 0}small{color:#666}h3{margin-bottom:.2em}</style>
<h3>bri-chrome</h3>
<label><input type=checkbox id=enablePrerender> Instant preload — prerender links on hover (speculation rules)</label>
<label><input type=checkbox id=debugNotifications> Debug notifications — popup on each preload</label>
<label><input type=checkbox id=debugMode> Debug mode — console logging + on-page overlay</label>
<label><input type=checkbox id=pageflip> Pageflip — wheel/Down=next page, Up=prev (one notch/tap)</label>
<label><input type=checkbox id=clickdown> Clickdown — open links on press, before release (faster; most sites)</label>
<label><input type=checkbox id=clickdown2> Clickdown+ — also press-open JS/role=link nav (Amazon/Google); buttons excluded</label>
<p><small>Changes take effect immediately, no reload.</small></p>
<script src=options.js></script>
''',
"options.js": r'''// bri-chrome options — instant-preload (prerender / debug notifications / debug mode) + pageflip. Persists in chrome.storage.sync.
const defs={enablePrerender:true, debugNotifications:false, debugMode:false, pageflip:false, clickdown:true, clickdown2:false};
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

def chrome_install(chrome='google-chrome-unstable'):
    # zero-drag Chrome install: pack a signed crx and force-install it via enterprise policy off a local
    # file:// update manifest (Chrome blocks http extension downloads; file:// is trusted; --load-extension
    # is DEAD in branded builds \u2014 silently ignored). Reuses the key \u2192 stable ID.
    import subprocess,hashlib,json,time
    build()
    ext=os.path.join(OUT,'bri-chrome'); pem=os.path.join(OUT,'bri-chrome.pem'); crx=os.path.join(OUT,'bri-chrome.crx')
    upd=os.path.join(OUT,'bri-chrome-update.xml')
    mf=os.path.join(ext,'manifest.json');m=json.load(open(mf));t=int(time.time())  # monotonic auto-bump: same/lower version than last-seen never installs
    m['version']=ver='1.%d.%d'%(t>>16,t&0xffff);json.dump(m,open(mf,'w'))
    cmd=[chrome,'--pack-extension='+ext,'--user-data-dir=/tmp/_abrpack','--no-first-run']
    if os.path.exists(pem): cmd.append('--pack-extension-key='+pem)
    subprocess.run(cmd,timeout=90,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    der=subprocess.run(['openssl','rsa','-in',pem,'-pubout','-outform','DER'],capture_output=True).stdout
    ID=''.join(chr(97+int(c,16)) for c in hashlib.sha256(der).hexdigest()[:32])
    open(upd,'w').write(
      "<?xml version='1.0' encoding='UTF-8'?>\n<gupdate xmlns='http://www.google.com/update2/response' protocol='2.0'>\n"
      "<app appid='%s'><updatecheck codebase='file://%s' version='%s'/></app>\n</gupdate>\n"%(ID,crx,ver))
    pol=json.dumps({"ExtensionInstallForcelist":["%s;file://%s"%(ID,upd)],"ExtensionInstallSources":["file:///*"]})
    subprocess.run(['sudo','mkdir','-p','/etc/opt/chrome/policies/managed'],check=True)
    subprocess.run(['sudo','tee','/etc/opt/chrome/policies/managed/bri-chrome.json'],input=pol.encode(),stdout=subprocess.DEVNULL,check=True)
    print("\u2713 force-install policy set  id=%s v%s crx=%d bytes"%(ID,ver,os.path.getsize(crx)))
    print("  -> a briext restart; lands ~1-5 min AFTER boot (roll-call), not at boot.  remove: a briext uninstall")
    return ID

def chrome_uninstall():
    import subprocess
    subprocess.run(['sudo','rm','-f','/etc/opt/chrome/policies/managed/bri-chrome.json'])
    print("\u2713 removed force-install policy (restart Chrome to drop the extension)")

def chrome_restart(chrome='google-chrome-canary'):
    # Chrome can't be restarted from inside the extension (scripts can't open chrome://restart; chrome.runtime.restart is ChromeOS-only),
    # so do it from the terminal: SIGTERM the MAIN browser process (the one with no --type=) for a clean shutdown, then relaunch w/ session restore.
    # pgrep ^-anchored to argv0: vmtouch pins the binary path in ITS argv — unanchored never sees it exit.
    import subprocess,time
    for pid in subprocess.run(['pgrep','-f','^/opt/google/chrome-canary/chrome'],capture_output=True,text=True).stdout.split():
        try: cl=open('/proc/%s/cmdline'%pid,'rb').read().split(b'\0')
        except OSError: continue
        if b'--type=' not in b' '.join(cl): subprocess.run(['kill','-TERM',pid])
    for _ in range(80):
        if not subprocess.run(['pgrep','-f','^/opt/google/chrome-canary/chrome'],capture_output=True).stdout.strip(): break
        time.sleep(0.1)
    subprocess.Popen([chrome,'--restore-last-session'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
    print("\u2713 Canary restarted (session restore) \u2014 new crx lands in ~1-5 min")

def main(argv):
    if "install" in argv[1:]: return chrome_install()
    if "uninstall" in argv[1:]: return chrome_uninstall()
    if "restart" in argv[1:]: return chrome_restart()
    paths=build()
    for p in paths: print("\u2713 "+p)
    print("chrome: a briext install   |   firefox xpi: a bri deploy")

if __name__=='__main__': main(sys.argv)   # `import briext; briext.build()` regenerates without running main
