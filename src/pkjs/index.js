Pebble.addEventListener('appmessage', function(e) {
  // Opens the Mayo Clinic stress symptoms page
  Pebble.openURL("https://www.mayoclinic.org/healthy-lifestyle/stress-management/in-depth/stress-symptoms/art-20046037");
});

Pebble.addEventListener('ready', function() {
  console.log("StressSense 2026: Mobile Link Ready");
});
