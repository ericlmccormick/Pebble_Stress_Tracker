// ============================================================================
// --- SECTION 1: GLOBAL CONFIGURATION & LISTENERS ---
// ============================================================================

// Register event listener executing when PebbleKit JS environment initializes
Pebble.addEventListener('ready', function(e) {
  // Output diagnostic log confirming successful initialization
  console.log('PebbleKit JS is ready. AppSync communication enabled.');
});

// ============================================================================
// --- SECTION 2: APPSYNC CONFIGURATION MANAGEMENT ---
// ============================================================================

// Register event listener catching incoming configuration updates from smartphone UI
Pebble.addEventListener('showConfiguration', function(e) {
  // Direct user to external settings portal (Mock URL for example purposes)
  Pebble.openURL('https://vitalgauge.example.com/settings');
});

// Register event listener catching returned data when user closes configuration portal
Pebble.addEventListener('webviewclosed', function(e) {
  // Verify configuration data was returned by webview
  if (e.response && e.response.length > 0) {
    // Parse returned JSON string into Javascript object
    var configData = JSON.parse(decodeURIComponent(e.response));
    
    // Construct dictionary object formatted for AppSync transmission
    var dict = {
      // Map configuration volume integer to predefined key 0
      '0': parseInt(configData.volume_level, 10),
      // Map configuration target HR integer to predefined key 1
      '1': parseInt(configData.target_hr, 10)
    };
    
    // Dispatch constructed dictionary via AppMessage; AppSync on watch intercepts automatically
    Pebble.sendAppMessage(dict, function() {
      // Output diagnostic log confirming successful transmission
      console.log('AppSync configuration successfully dispatched to watch.');
    }, function(error) {
      // Output diagnostic log detailing transmission failure
      console.log('AppSync configuration dispatch failed: ' + JSON.stringify(error));
    });
  }
});