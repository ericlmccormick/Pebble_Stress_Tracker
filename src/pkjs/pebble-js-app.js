var timelineToken = "";

Pebble.addEventListener('ready', function() {
  Pebble.getTimelineToken(function(token) {
    timelineToken = token;
  }, function(error) {
    console.log('Error getting timeline token: ' + error);
  });
});

Pebble.addEventListener('appmessage', function(e) {
  var dict = e.payload;
  
  // Verify we received a command to push a pin
  if (dict['MESSAGE_KEY_PIN_TYPE']) {
    var pinType = dict['MESSAGE_KEY_PIN_TYPE'];
    var score = dict['MESSAGE_KEY_SCORE'];
    var rhr = dict['MESSAGE_KEY_RHR'];
    
    var pinDate = new Date();
    // Schedule the pin 1 minute in the future so it appears right at the top of the timeline
    pinDate.setMinutes(pinDate.getMinutes() + 1); 

    var pin = {
      "id": "stress-sense-" + Math.round((new Date()).getTime() / 1000),
      "time": pinDate.toISOString(),
      "layout": {
        "type": "genericPin",
        "title": pinType === 1 ? "Morning Readiness" : "High Stress Detected",
        "tinyIcon": "system://images/HEART_RATE_MONITOR",
        "body": pinType === 1 ? 
                "Score: " + score + "/100\nResting HR: " + rhr + " BPM\nGreat time for a workout!" : 
                "Stress Score: " + score + "/100\nConsider a 5-minute breathing session to lower your baseline."
      }
    };

    pushPin(pin);
  }
});

// Pushes the formatted pin directly to the timeline servers
function pushPin(pin) {
  if (!timelineToken) return;
  var req = new XMLHttpRequest();
  req.open('PUT', 'https://timeline-api.rebble.io/v1/user/pins/' + pin.id, true);
  req.setRequestHeader('Content-Type', 'application/json');
  req.setRequestHeader('X-User-Token', timelineToken);
  req.send(JSON.stringify(pin));
}