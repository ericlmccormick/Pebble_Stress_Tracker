Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS is ready!');
});

Pebble.addEventListener('appmessage', function(e) {
  var dict = e.payload;
  console.log('AppMessage received: ' + JSON.stringify(dict));
  
  try {
      // Safely pull the data using the exact package.json string or integer ID
      var rawPinType = dict['PIN_TYPE'] !== undefined ? dict['PIN_TYPE'] : dict[104];
      var rawScore = dict['SCORE'] !== undefined ? dict['SCORE'] : dict[105];
      var rawRhr = dict['RHR'] !== undefined ? dict['RHR'] : dict[106];
      
      if (rawPinType !== undefined) {
        
        var score = Number(rawScore);
        var rhr = Number(rawRhr);
        
        console.log('Processing Event -> Score: ' + score + ' RHR: ' + rhr);
        
        var titleText = "Heart Rate Event";
        var bodyText = "Average HR: " + rhr + " BPM\nStress Score: " + score + "/100";
    
        // 1. Force native popup locally via Bluetooth (Immediate feedback)
        Pebble.showSimpleNotificationOnPebble(titleText, bodyText);
    
        // 2. Build Timeline Pin according to strict API Schema
        var pinDate = new Date();
        var pin = {
          "id": "hr-alert-" + Math.round(pinDate.getTime() / 1000),
          "time": pinDate.toISOString(),
          "layout": {
            "type": "genericPin",
            "title": titleText,
            "tinyIcon": "system://images/NOTIFICATION_GENERIC", // Strict Whitelist Icon
            "body": bodyText
          }
        };
    
        // 3. Fetch Token and push to the stable Rebble endpoint
        Pebble.getTimelineToken(
            function(token) {
                console.log('Token acquired: ' + token);
                var req = new XMLHttpRequest();
                req.open('PUT', 'https://timeline-api.rebble.io/v1/user/pins/' + pin.id, true);
                req.setRequestHeader('Content-Type', 'application/json');
                req.setRequestHeader('X-User-Token', token);
                
                req.onload = function() {
                  console.log('Timeline API responded with status: ' + req.status);
                };
                req.onerror = function() {
                  console.log('Error: Timeline API connection failed.');
                };
                
                req.send(JSON.stringify(pin));
            }, 
            function(error) { 
                console.log('Error getting timeline token. Do you have "timeline" in capabilities? Error: ' + error); 
            }
        );
      }
  } catch (err) {
      console.log('CRITICAL JS ERROR in appmessage listener: ' + err.message);
  }
});

Pebble.addEventListener('showConfiguration', function() {
  var url = 'https://cdn.rawgit.com/pebble-examples/slate-config-example/master/config/index.html';
  var bgScanEnabled = localStorage.getItem('bgScanEnabled') !== 'false'; 
  Pebble.openURL(url + '?bg_scan=' + bgScanEnabled);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e.response) {
    var configData = JSON.parse(decodeURIComponent(e.response));
    localStorage.setItem('bgScanEnabled', configData.bg_scan);
    
    var enabledInt = configData.bg_scan ? 1 : 0;
    var dict = { 
        'BG_SCAN_ENABLED': enabledInt,
        103: enabledInt
    };
    
    Pebble.sendAppMessage(dict, function() {
        console.log("Settings sent successfully");
    }, function() {
        console.log("Settings failed to send");
    });
  }
});