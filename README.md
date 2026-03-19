# Train 2 Time

A customizable train departure countdown application for Pebble smartwatches, developed using **CloudPebble** and **C**.

This project, including the source code and this documentation, was generated and assisted by **Gemini (AI)**.

## Overview

<p align="center">
  <img src="path/to/your/image_0.png" alt="Train 2 Time Screenshot" width="300"/>
</p>

Inspired by [TrainTime by otoone_dev](https://apps.rebble.io/en_US/application/5904a0c90dfc329df8000706), this app was created to improve usability and tailor the experience to specific commuting needs. It fetches timetable data from user-defined online sources and provides a simple countdown to the next departure, as seen in the screenshot above.

> **Note:** This app is primarily designed for the Japanese railway system. Some behaviors or time logic (like the 24-hour cycle from 04:00 to 27:59) may differ from local transit rules in other regions.

---

## Features
* **Dynamic Data Fetching**: Fetches timetable data from online Markdown (.md) or Text (.txt) files only at app launch to save battery.
* **Flexible Schedules**: Supports up to 3 stations, each with 3 schedule types (Weekdays, Saturdays, and Sundays/Holidays).
    * *Fallback Logic*: If Saturday or Holiday schedules are omitted, the app defaults to the Weekday schedule.
* **Customizable Capacity**: Adjustable limits for the number of trains and time ranges (within stable memory limits). Optimized for approximately 5-10 stations.
* **Extended 24h Format**: Operates on a 04:00 to 27:59 cycle (considering 4:00 AM as the start of the day).
* **Visual Customization**: For color-enabled Pebbles, users can customize the background color for the current time and highlight colors for post-departure count-ups.
* **Japanese Holiday Support**: Integration with the [Holidays JP API](https://holidays-jp.github.io/api/v1/date.json) to automatically fetch public holidays (Disabled by default).

---

## Controls
| Button | Action |
| :--- | :--- |
| **[UP] / [DOWN]** | Change the current train |
| **[SELECT]** | Change the current station |

---

## Timetable Data Format
To use this app, you need to host your timetable on a service like **GitHub Gist**. The app requires the "Raw" URL of the file.

### Sample Data
You can find a reference for the required data format here:
[View Sample Timetable (GitHub Gist)](https://gist.githubusercontent.com/tc5206/77ab86c9297b9fce32f7f3c891474a9f/raw/9d59b9bf258020a06ae7e06c172ca40195278318/timetable_test.md)

* **Remarks Support**: Supports `\n` for up to 2 lines of text (approx. 9 full-width characters per line).

---

## Technical Details
* **Development Environment**: CloudPebble
* **Language**: C
* **AI Assistance**: Developed with the help of Gemini.

## Acknowledgments
* **Original Inspiration**: "TrainTime" by [otoone_dev](https://apps.rebble.io/en_US/application/5904a0c90dfc329df8000706).
* **Holiday API**: [Holidays JP API](https://holidays-jp.github.io/api/v1/date.json) by Hiroshi Matsuo.

## License
[MIT License](LICENSE)
