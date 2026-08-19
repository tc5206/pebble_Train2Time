var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var stations = [];
var currentStationIdx = 0;
var currentTrainIdx = 0;
var isHoliday = false;
var specialDates = [];
var specialSaturdays = [];

Pebble.addEventListener('ready', function(e) { fetchTrainData(); });

Pebble.addEventListener('webviewclosed', function(e) {
  console.log('Webview closed. Reloading settings and data...');
  fetchTrainData();
});

Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload;
  
  var toggleUrlKey   = (typeof messageKeys !== 'undefined') ? messageKeys.REQUEST_TOGGLE_URL : 'REQUEST_TOGGLE_URL';
  var nextKey        = (typeof messageKeys !== 'undefined') ? messageKeys.KEY_REQUEST_NEXT   : 'KEY_REQUEST_NEXT';
  var prevKey        = (typeof messageKeys !== 'undefined') ? messageKeys.KEY_REQUEST_PREV   : 'KEY_REQUEST_PREV';
  var switchKey      = (typeof messageKeys !== 'undefined') ? messageKeys.KEY_REQUEST_SWITCH : 'KEY_REQUEST_SWITCH';
  var addTimelineKey = (typeof messageKeys !== 'undefined') ? messageKeys.KEY_REQUEST_ADD_TIMELINE : 'KEY_REQUEST_ADD_TIMELINE';

  if (payload[addTimelineKey] !== undefined || payload.KEY_REQUEST_ADD_TIMELINE !== undefined) {
    addTrainToTimeline();
    return;
  }

  if (payload[toggleUrlKey] !== undefined || payload.REQUEST_TOGGLE_URL !== undefined) {
    var settings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
    var currentUrlIdx = parseInt(settings.KEY_URL_INDEX || '0');
    
    var nextUrlIdx = currentUrlIdx;
    var foundValidUrl = false;
    
    for (var i = 1; i <= 5; i++) {
      var checkIdx = (currentUrlIdx + i) % 5;
      var checkUrl = settings['KEY_URL_' + checkIdx];
      if (checkUrl && checkUrl.trim() !== "") {
        nextUrlIdx = checkIdx;
        foundValidUrl = true;
        break;
      }
    }
    
    if (!foundValidUrl) {
      nextUrlIdx = currentUrlIdx;
    }
    
    settings.KEY_URL_INDEX = String(nextUrlIdx);
    localStorage.setItem('clay-settings', JSON.stringify(settings));
    
    currentStationIdx = 0;
    currentTrainIdx = 0;
    
    getMainTimetable();
    return;
  }

  if (!stations || stations.length === 0) return;

  var isNext   = (payload[nextKey] !== undefined)   || (payload.KEY_REQUEST_NEXT !== undefined);
  var isPrev   = (payload[prevKey] !== undefined)   || (payload.KEY_REQUEST_PREV !== undefined);
  var isSwitch = (payload[switchKey] !== undefined) || (payload.KEY_REQUEST_SWITCH !== undefined);

  if (isNext) {
    if (currentTrainIdx < stations[currentStationIdx].trains.length - 1) {
      currentTrainIdx++;
      sendToPebble();
    }
  } else if (isPrev) {
    if (currentTrainIdx > 0) {
      currentTrainIdx--;
      sendToPebble();
    }
  } else if (isSwitch) {
    currentStationIdx = (currentStationIdx + 1) % stations.length;
    findNearestTrain();
    sendToPebble();
  }
});

function addTrainToTimeline() {
  var st = stations[currentStationIdx];
  if (!st || !st.trains || st.trains.length === 0) {
    sendTimelineResult(0);
    return;
  }
  var train = st.trains[currentTrainIdx];
  if (!train) {
    sendTimelineResult(0);
    return;
  }

  var now = new Date();
  var targetDate = new Date(
    now.getFullYear(),
    now.getMonth(),
    now.getDate()
  );

  if (now.getHours() >= 4 && train.hour >= 24) {
    targetDate.setDate(targetDate.getDate() + 1);
  }

  var actualHour = train.hour % 24;
  targetDate.setHours(actualHour, train.min, 0, 0);

  var typeText = train.type || "";
  var destText = train.dest || "";
  var title = (typeText + " " + destText).trim();
  if (!title) title = st.name || "Train";

  var rawNote = train.note1 || "";
  var noteText = rawNote.replace(/\\n|\n/g, ' ').trim();

  var pinId = "train2time-" + Date.now();
  var pin = {
    "id": pinId,
    "time": targetDate.toISOString(),
    "layout": {
      "type": "genericPin",
      "title": title,
      "subtitle": noteText,
      "tinyIcon": "system://images/NOTIFICATION_LIGHTHOUSE"
    }
  };

  Pebble.getTimelineToken(function(token) {
    var xhr = new XMLHttpRequest();
    xhr.open("PUT", "https://timeline-api.rebble.io/v1/user/pins/" + pinId, true);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.setRequestHeader("X-User-Token", token);
    xhr.onload = function() {
      if (xhr.status === 200) {
        sendTimelineResult(1);
      } else {
        sendTimelineResult(0);
      }
    };
    xhr.onerror = function() {
      sendTimelineResult(0);
    };
    xhr.send(JSON.stringify(pin));
  }, function(err) {
    sendTimelineResult(0);
  });
}

function sendTimelineResult(status) {
  var dict = {};
  var key = (typeof messageKeys !== 'undefined') ? messageKeys.KEY_TIMELINE_RESULT : 'KEY_TIMELINE_RESULT';
  dict[key] = status;
  Pebble.sendAppMessage(dict, function(e) {}, function(e) {});
}

function fetchTrainData() {
  var settings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
  var useHolidayApi = (settings.KEY_HOLIDAY_CONFIG === "1");

  if (useHolidayApi) {
    var holidayApiUrl = "https://holidays-jp.github.io/api/v1/date.json";
    var xhrH = new XMLHttpRequest();
    xhrH.open("GET", holidayApiUrl, true);
    xhrH.onload = function() {
      if (xhrH.status === 200) {
        var holidays = JSON.parse(xhrH.responseText);
        isHoliday = !!holidays[getTodayString()];
      }
      getMainTimetable();
    };
    xhrH.onerror = function() { getMainTimetable(); };
    xhrH.send();
  } else {
    isHoliday = false;
    getMainTimetable();
  }
}

function getTodayString() {
  var now = new Date();
  if (now.getHours() < 4) now.setDate(now.getDate() - 1);
  return now.getFullYear() + "-" + ("0" + (now.getMonth() + 1)).slice(-2) + "-" + ("0" + now.getDate()).slice(-2);
}

function getMainTimetable() {
  var settings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
  var index = settings.KEY_URL_INDEX || '0';
  var url = settings['KEY_URL_' + index] || '';
  if (!url) return;

  var xhr = new XMLHttpRequest();
  xhr.open("GET", url, true);
  xhr.onload = function() {
    if (xhr.status === 200) {
      var range = parseInt(settings.KEY_RANGE) || 30;
      parseMarkdown(xhr.responseText, range, settings); 
      findNearestTrain();
      sendToPebble();
    }
  };
  xhr.onerror = function() {
    console.log("Timetable fetch failed");
  };
  xhr.send();
}

function parseMarkdown(text, range, settings) {
  var lines = text.split('\n');
  stations = [];
  specialDates = []; specialSaturdays = [];
  var currentStation = null;
  var currentMode = "";
  var currentHighlightColor = null;

  for (var i = 0; i < lines.length; i++) {
    var line = lines[i].trim();
    if (line.indexOf('//') !== -1) line = line.split('//')[0].trim();
    if (!line) continue;

    if (line.indexOf('@HOLIDAY:') === 0) {
      specialDates = specialDates.concat(line.replace('@HOLIDAY:', '').split(','));
    } else if (line.indexOf('@SATURDAY:') === 0) {
      specialSaturdays = specialSaturdays.concat(line.replace('@SATURDAY:', '').split(','));
    } else if (line.indexOf('# ') === 0) {
      currentStation = { name: line.replace('# ', '').trim(), icon: 1, modes: {} };
      stations.push(currentStation);
    } else if (line.indexOf('## ') === 0) {
      if (currentStation) currentStation.icon = parseInt(line.replace('## ', '').trim());
    } else if (line.indexOf('### ') === 0) {
      currentMode = line.replace('### ', '').trim().toLowerCase();
      currentStation.modes[currentMode] = { trains: [], color: null };
      currentHighlightColor = null;
    } else if (line.indexOf('#### ') === 0) {
      var colorStr = line.replace('#### ', '').trim();
      currentHighlightColor = parseInt(colorStr, 16);
      if (currentStation && currentMode) {
        currentStation.modes[currentMode].color = currentHighlightColor;
      }
    } else if (line.indexOf('- ') === 0 && currentStation && currentMode) {
      var parts = line.replace('- ', '').split(',');
      if (parts.length >= 2) {
        var timePart = parts[0].trim().split(':');
        var h = parseInt(timePart[0]); if (h < 4) h += 24;
        currentStation.modes[currentMode].trains.push({
          hour: h, 
          min: parseInt(timePart[1]),
          dest: parts[1] ? parts[1].trim() : "", 
          type: parts[2] ? parts[2].trim() : "", 
          note1: parts[3] ? parts[3].trim() : "",
          typeColor: parts[4] ? parts[4].trim() : "000000",
          typeBgColor: parts[5] ? parts[5].trim() : "FFFFFF"
        });
      }
    }
  }

  var now = new Date();
  var day = now.getDay();
  var hour = now.getHours();
  if (hour < 4) { now.setDate(now.getDate() - 1); day = now.getDay(); hour += 24; }
  var nowTotalMin = hour * 60 + now.getMinutes();
  var todayStr = getTodayString();
  var isSpecialHoliday = (specialDates.indexOf(todayStr) !== -1);
  var isSpecialSaturday = (specialSaturdays.indexOf(todayStr) !== -1);

  for (var s = 0; s < stations.length; s++) {
    var st = stations[s];
    
    var selectedData = null;

    if (isHoliday || isSpecialHoliday || day === 0) {
      if (st.modes["holiday"] && st.modes["holiday"].trains.length > 0) {
        selectedData = st.modes["holiday"];
      } else {
        selectedData = st.modes["weekday"];
      }
    } else if (isSpecialSaturday || day === 6) {
      if (st.modes["saturday"] && st.modes["saturday"].trains.length > 0) {
        selectedData = st.modes["saturday"];
      } else if (st.modes["holiday"] && st.modes["holiday"].trains.length > 0) {
        selectedData = st.modes["holiday"];
      } else {
        selectedData = st.modes["weekday"];
      }
    } else {
      selectedData = st.modes["weekday"];
    }
    
    st.highlightColor = (selectedData) ? selectedData.color : null;
    st.trains = (selectedData) ? selectedData.trains.filter(function(t) {
      var tMin = t.hour * 60 + t.min;
      return (tMin >= (nowTotalMin - 5) && tMin <= (nowTotalMin + range));
    }) : [];

    var max = parseInt(settings.KEY_MAX_TRAINS) || 15;
    if (st.trains.length > max) st.trains = st.trains.slice(0, max);
    if (st.trains.length === 0 && selectedData) {
      var nextOne = selectedData.trains.find(function(t) { return (t.hour * 60 + t.min) >= nowTotalMin; });
      if (nextOne) st.trains.push(nextOne);
    }
    st.trains.sort(function(a, b) { return (a.hour * 60 + a.min) - (b.hour * 60 + b.min); });
  }
}

function findNearestTrain() {
  var now = new Date();
  var hour = now.getHours(); if (hour < 4) hour += 24;
  var nowTotalMin = hour * 60 + now.getMinutes();
  var currentTrains = stations[currentStationIdx].trains;
  if (!currentTrains || currentTrains.length === 0) { currentTrainIdx = 0; return; }
  var foundIdx = currentTrains.findIndex(function(t) { return (t.hour * 60 + t.min) >= nowTotalMin; });
  currentTrainIdx = (foundIdx !== -1) ? foundIdx : currentTrains.length - 1;
}

function parseColor(val) {
  if (typeof val === 'number') return val;
  if (typeof val === 'string') return parseInt(val.replace(/^0x/i, ''), 16) || 0;
  return 0;
}

function sendToPebble() {
  var st = stations[currentStationIdx];
  if (!st) return;
  var settings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
  var train = (st.trains && st.trains.length > 0) ? st.trains[currentTrainIdx] : null;

  var watch_info = Pebble.getActiveWatchInfo ? Pebble.getActiveWatchInfo() : null;
  var platform = watch_info ? watch_info.platform : 'basalt';

  var highlightColor = (st.highlightColor !== null && st.highlightColor !== undefined)
    ? st.highlightColor
    : parseColor(settings.KEY_HIGHLIGHT_COLOR);

  var dict = {
    'KEY_STATION': st.name,
    'KEY_ICON': st.icon,
    'KEY_HIGHLIGHT_COLOR': highlightColor
  };

  if (train) {
    if (platform === 'emery' || platform === 'gabbro') {
      dict['KEY_TYPE_TEXT'] = train.type || "";
      dict['KEY_DEST'] = train.dest || "";
      dict['KEY_TYPE_COLOR'] = parseInt(train.typeColor, 16);
      dict['KEY_TYPE_BG_COLOR'] = parseInt(train.typeBgColor, 16);
    } else {
      dict['KEY_DEST'] = (train.type ? train.type + " / " : "") + train.dest;
    }
    
    dict['KEY_HOUR'] = train.hour;
    dict['KEY_MIN'] = train.min;
    dict['KEY_NOTE1'] = train.note1 || "";
  } else {
    dict['KEY_DEST'] = "";
    dict['KEY_HOUR'] = -1;
    dict['KEY_MIN'] = 0;
    dict['KEY_NOTE1'] = "";
    if (platform === 'emery' || platform === 'gabbro') {
        dict['KEY_TYPE_TEXT'] = "";
        dict['KEY_TYPE_COLOR'] = 0xFFFFFF;
        dict['KEY_TYPE_BG_COLOR'] = 0x000000;
    }
  }

  Pebble.sendAppMessage(dict, function(e) { console.log('Sent'); }, function(e) { console.log('Failed'); });
}