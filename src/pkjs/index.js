var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var stations = [];
var currentStationIdx = 0;
var currentTrainIdx = 0;
var isHoliday = false;
var specialDates = [];
var specialSaturdays = [];

Pebble.addEventListener('ready', function(e) { fetchTrainData(); });

// 設定画面が閉じられたときに再読み込みを実行
Pebble.addEventListener('webviewclosed', function(e) {
  console.log('Webview closed. Reloading settings and data...');
  fetchTrainData();
});

Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload;
  if (stations.length === 0) return;
  if (payload.KEY_REQUEST_NEXT || payload[1] === 1) {
    if (currentTrainIdx < stations[currentStationIdx].trains.length - 1) currentTrainIdx++;
  } else if (payload.KEY_REQUEST_PREV || payload[0] === 1) {
    if (currentTrainIdx > 0) currentTrainIdx--;
  } else if (payload.KEY_REQUEST_SWITCH || payload[2] === 1) {
    currentStationIdx = (currentStationIdx + 1) % stations.length;
    findNearestTrain(); 
  }
  sendToPebble();
});

function fetchTrainData() {
  var settings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
  // KEY_HOLIDAY_CONFIG が "1" (able) の場合のみ API を取得
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
    // APIを使用しない場合はフラグを false のままにして次へ
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
  xhr.send();
}

function parseMarkdown(text, range, settings) {
  var lines = text.split('\n');
  stations = [];
  specialDates = []; specialSaturdays = [];
  var currentStation = null;
  var currentMode = "";
  var currentHighlightColor = null; // #### 用

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
    var modeKey = "weekday";
    if (isHoliday || isSpecialHoliday || day === 0) modeKey = "holiday";
    else if (isSpecialSaturday || day === 6) modeKey = "saturday";

    var selectedData = (st.modes[modeKey] && st.modes[modeKey].trains.length) ? st.modes[modeKey] : st.modes["weekday"];
    
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
    if (platform === 'emery') {
      dict['KEY_TYPE_TEXT'] = train.type || "";
      dict['KEY_DEST'] = train.dest || "";
      // --- 追加：色情報を数値に変換して送信 ---
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
    if (platform === 'emery') {
        dict['KEY_TYPE_TEXT'] = "";
        dict['KEY_TYPE_COLOR'] = 0xFFFFFF;
        dict['KEY_TYPE_BG_COLOR'] = 0x000000;
    }
  }

  Pebble.sendAppMessage(dict, function(e) { console.log('Sent'); }, function(e) { console.log('Failed'); });
}