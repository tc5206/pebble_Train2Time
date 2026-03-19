module.exports = [
  { "type": "heading", "defaultValue": "Timetable Settings" },
  {
    "type": "section",
    "items": [
      {
        "type": "radiogroup",
        "messageKey": "KEY_URL_INDEX",
        "label": "Choose Timetable",
        "options": [
          { "label": "Timetable A", "value": "0" },
          { "label": "Timetable B", "value": "1" },
          { "label": "Timetable C", "value": "2" }
        ],
        "defaultValue": "0"
      },
      { "type": "input", "messageKey": "KEY_URL_0", "label": "URL A" },
      { "type": "input", "messageKey": "KEY_URL_1", "label": "URL B" },
      { "type": "input", "messageKey": "KEY_URL_2", "label": "URL C" },
      {
        "type": "select",
        "messageKey": "KEY_RANGE",
        "label": "Range (minutes)",
        "options": [
          { "label": "30min.", "value": "30" },
          { "label": "90min.", "value": "90" },
          { "label": "180min.", "value": "180" }
        ],
        "defaultValue": "30"
      },
      {
        "type": "select",
        "messageKey": "KEY_MAX_TRAINS",
        "label": "Max Trains",
        "options": [
          { "label": "10", "value": "10" },
          { "label": "15", "value": "15" },
          { "label": "20", "value": "20" }
        ],
        "defaultValue": "15"
      },
      {
        "type": "color",
        "messageKey": "KEY_HIGHLIGHT_COLOR",
        "label": "Highlight Color",
        "defaultValue": "000000",
        "sunlight": true,
        "capabilities": ["COLOR"],
        "palette": [
          "000000", "000055", "0000AA", "0000FF",
          "005500", "005555", "0055AA", "0055FF",
          "00AA00", "00AA55", "00AAAA", "00AAFF",
          "00FF00", "00FF55", "00FFAA", "00FFFF",
          "550000", "550055", "5500AA", "5500FF",
          "555500", "555555", "5555AA", "5555FF",
          "55AA00", "55AA55", "55AAAA", "55AAFF",
          "55FF00", "55FF55", "55FFAA", "55FFFF",
          "AA0000", "AA0055", "AA00AA", "AA00FF",
          "AA5500", "AA5555", "AA55AA", "AA55FF",
          "AAAA00", "AAAA55", "AAAAAA", "AAAAFF",
          "AAFF00", "AAFF55", "AAFFAA", "AAFFFF",
          "FF0000", "FF0055", "FF00AA", "FF00FF",
          "FF5500", "FF5555", "FF55AA", "FF55FF",
          "FFAA00", "FFAA55", "FFAAAA", "FFAAFF",
          "FFFF00", "FFFF55", "FFFFAA", "FFFFFF"
        ]
      },
      {
        "type": "select",
        "messageKey": "KEY_HOLIDAY_CONFIG",
        "label": "JPN Holiday API",
        "options": [
          { "label": "disable", "value": "0" },
          { "label": "enable", "value": "1" }
        ],
        "defaultValue": "0"
      }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];