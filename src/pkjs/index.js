var SunCalc = require('suncalc');

function two(n) { return n < 10 ? '0' + n : '' + n; }
function fmtHM(d) { return two(d.getHours()) + ':' + two(d.getMinutes()); }
function fmtDate(d) {
  var days = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
  var months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
  return days[d.getDay()] + ' ' + months[d.getMonth()] + ' ' + two(d.getDate());
}

var DEFAULT = { lat: 52.5200, lon: 13.4050, label: 'Berlin' };

function timesFor(date, lat, lon) {
  var t = SunCalc.getTimes(date, lat, lon);
  return {
    DATE: fmtDate(date),
    DAWN: fmtHM(t.dawn),
    SUNRISE: fmtHM(t.sunrise),
    SUNSET: fmtHM(t.sunset),
    DUSK: fmtHM(t.dusk)
  };
}

function sendBundle(lat, lon, centerOffset) {
  try {
    var d0 = new Date();
    var dm1 = new Date(d0.getTime());
    var dC  = new Date(d0.getTime());
    var dp1 = new Date(d0.getTime());

    dm1.setDate(dm1.getDate() + (centerOffset - 1));
    dC.setDate(dC.getDate() + centerOffset);
    dp1.setDate(dp1.getDate() + (centerOffset + 1));

    var m1 = timesFor(dm1, lat, lon);
    var c0 = timesFor(dC,  lat, lon);
    var p1 = timesFor(dp1, lat, lon);

    Pebble.sendAppMessage({
      CENTER: centerOffset,

      DATE_M1: m1.DATE, DAWN_M1: m1.DAWN, SUNRISE_M1: m1.SUNRISE, SUNSET_M1: m1.SUNSET, DUSK_M1: m1.DUSK,
      DATE_0:  c0.DATE, DAWN_0:  c0.DAWN, SUNRISE_0:  c0.SUNRISE, SUNSET_0:  c0.SUNSET, DUSK_0:  c0.DUSK,
      DATE_P1: p1.DATE, DAWN_P1: p1.DAWN, SUNRISE_P1: p1.SUNRISE, SUNSET_P1: p1.SUNSET, DUSK_P1: p1.DUSK
    }, function(){}, function(e){
      console.log('send failed: ' + JSON.stringify(e));
    });
  } catch (ex) {
    console.log('SunCalc error: ' + ex);
    Pebble.sendAppMessage({ ERROR: 'Calc error' });
  }
}

function updateFromLocation(centerOffset) {
  var usedFallback = false;

  var fallbackTimer = setTimeout(function() {
    usedFallback = true;
    console.log('Geolocation timeout; using default ' + DEFAULT.label);
    sendBundle(DEFAULT.lat, DEFAULT.lon, centerOffset);
  }, 7000);

  if (!navigator.geolocation || !navigator.geolocation.getCurrentPosition) {
    clearTimeout(fallbackTimer);
    usedFallback = true;
    console.log('Geolocation API missing; using default ' + DEFAULT.label);
    sendBundle(DEFAULT.lat, DEFAULT.lon, centerOffset);
    return;
  }

  navigator.geolocation.getCurrentPosition(function(pos) {
    if (usedFallback) return;
    clearTimeout(fallbackTimer);
    sendBundle(pos.coords.latitude, pos.coords.longitude, centerOffset);
  }, function(err) {
    if (usedFallback) return;
    clearTimeout(fallbackTimer);
    console.log('Geolocation error: ' + JSON.stringify(err));
    sendBundle(DEFAULT.lat, DEFAULT.lon, centerOffset);
  }, {
    enableHighAccuracy: true,
    maximumAge: 15 * 60 * 1000,
    timeout: 6000
  });
}

Pebble.addEventListener('ready', function() {
  console.log('PKJS ready');
  Pebble.sendAppMessage({ HELLO: 1 });
});

Pebble.addEventListener('appmessage', function(e) {
  var center = 0;
  if (e && e.payload && typeof e.payload.OFFSET === 'number') {
    center = e.payload.OFFSET|0;
  }
  if (e && e.payload && e.payload.REQ) {
    updateFromLocation(center);
  }
});
