#pragma once

#include "esphome.h"
#include "esphome/components/wifi/wifi_component.h"
#include <vector>
#include <string>
#include <sstream>
#include <ArduinoJson.h>

#include <cctype>
#include <cstdio>
#include <ctime>
#include <cstdlib>

static inline std::string trim_string(const std::string &str) {
  size_t first = str.find_first_not_of(" \t\n\r");
  if (first == std::string::npos) return "";
  size_t last = str.find_last_not_of(" \t\n\r");
  return str.substr(first, (last - first + 1));
}

static inline int64_t to_utc_epoch(int year, int month, int day, int hour, int minute, int second) {
  int y = year - 1970;
  int leap_days = (year - 1969) / 4 - (year - 1901) / 100 + (year - 1601) / 400;
  int64_t days = (int64_t)y * 365 + leap_days;

  static const int days_before_month[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  if (month >= 1 && month <= 12) {
    days += days_before_month[month - 1];
    bool is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
    if (is_leap && month > 2) {
      days += 1;
    }
  }
  days += (day - 1);
  return days * 86400LL + hour * 3600LL + minute * 60LL + second;
}

static inline std::string format_utc_epoch(int64_t epoch_sec) {
  time_t t = (time_t)epoch_sec;
  struct tm tm_buf;
  gmtime_r(&t, &tm_buf);
  char buf[64];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
           tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
  return std::string(buf);
}

static inline std::string normalize_timestamp(const std::string &raw) {
  std::string s = trim_string(raw);
  if (s.empty()) return "";

  // Check if purely numeric (epoch)
  bool all_digits = true;
  for (char c : s) {
    if (!isdigit(static_cast<unsigned char>(c))) {
      all_digits = false;
      break;
    }
  }
  if (all_digits && s.length() >= 9) {
    int64_t epoch = strtoll(s.c_str(), nullptr, 10);
    if (epoch > 100000000000LL) {
      epoch /= 1000LL;
    }
    return format_utc_epoch(epoch);
  }

  // Try ISO-8601 parsing: YYYY-MM-DD
  int year = 0, month = 0, day = 0;
  int hour = 0, minute = 0, second = 0;
  int consumed = 0;
  char sep = 0;

  if (sscanf(s.c_str(), "%4d-%2d-%2d%c%n", &year, &month, &day, &sep, &consumed) >= 3) {
    if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
      if (consumed > 0 && (sep == 'T' || sep == ' ' || sep == 't')) {
        const char *time_part = s.c_str() + consumed;
        int time_consumed = 0;
        if (sscanf(time_part, "%2d:%2d:%2d%n", &hour, &minute, &second, &time_consumed) >= 3 ||
            sscanf(time_part, "%2d:%2d%n", &hour, &minute, &time_consumed) >= 2) {
          const char *rest = time_part + time_consumed;
          // Skip fractional seconds (.123 or ,123)
          if (*rest == '.' || *rest == ',') {
            rest++;
            while (isdigit(static_cast<unsigned char>(*rest))) rest++;
          }
          int tz_offset = 0;
          if (*rest == 'Z' || *rest == 'z') {
            tz_offset = 0;
          } else if (*rest == '+' || *rest == '-') {
            char tz_sign = *rest;
            int tz_h = 0, tz_m = 0;
            if (sscanf(rest + 1, "%2d:%2d", &tz_h, &tz_m) == 2 ||
                sscanf(rest + 1, "%2d%2d", &tz_h, &tz_m) == 2 ||
                sscanf(rest + 1, "%2d", &tz_h) == 1) {
              int total_min = tz_h * 60 + tz_m;
              tz_offset = (tz_sign == '-') ? -total_min * 60 : total_min * 60;
            }
          }
          int64_t epoch = to_utc_epoch(year, month, day, hour, minute, second) - tz_offset;
          return format_utc_epoch(epoch);
        }
      } else {
        // Date only: format as midnight UTC
        int64_t epoch = to_utc_epoch(year, month, day, 0, 0, 0);
        return format_utc_epoch(epoch);
      }
    }
  }

  return s;
}

struct NotificationItem {
  uint8_t r{0};
  uint8_t g{0};
  uint8_t b{0};
  uint32_t delay_ms{1000};
  std::string text;
  std::string timestamp;
  std::vector<std::string> wrapped_lines;
  bool lines_cached{false};
};

static constexpr size_t MAX_PERSISTED_DISMISSED = 50;

struct DismissedStorage {
  uint32_t count{0};
  uint32_t head{0};
  uint32_t hashes[MAX_PERSISTED_DISMISSED]{0};
};

class NotificationManager : public esphome::Component {
 public:
  esphome::display::Display *display{nullptr};
  std::vector<NotificationItem> items;
  std::vector<std::string> dismissed_signatures;
  size_t current_index{0};
  uint32_t last_switch_time{0};
  uint32_t last_dismiss_time{0};
  uint32_t pause_rotation_until{0};
  std::string last_payload;
  bool needs_redraw{true};
  bool wifi_connected{false};
  bool last_ap_state{false};
  bool is_resetting{false};

  esphome::ESPPreferenceObject pref_;
  DismissedStorage storage_;
  bool pref_initialized_{false};

  void init_preferences() {
    if (this->pref_initialized_) return;
    if (esphome::global_preferences != nullptr) {
      uint32_t pref_key = esphome::fnv1_hash("notify_dismissed_v1");
      this->pref_ = esphome::global_preferences->make_preference<DismissedStorage>(pref_key, true);
      if (this->pref_.load(&this->storage_)) {
        ESP_LOGI("notify", "Loaded %u dismissed notification hashes from flash/NVS", (unsigned)this->storage_.count);
      } else {
        ESP_LOGI("notify", "No previous dismissed notification hashes found in flash/NVS");
        this->storage_ = DismissedStorage();
      }
      this->pref_initialized_ = true;
    }
  }

  void save_dismissed_hash(uint32_t h) {
    this->init_preferences();
    this->storage_.hashes[this->storage_.head] = h;
    this->storage_.head = (this->storage_.head + 1) % MAX_PERSISTED_DISMISSED;
    if (this->storage_.count < MAX_PERSISTED_DISMISSED) {
      this->storage_.count++;
    }
    this->pref_.save(&this->storage_);
    if (esphome::global_preferences != nullptr) {
      esphome::global_preferences->sync();
    }
  }

  void on_touch_interaction() {
    this->pause_rotation_until = millis() + 3000;
  }

  static std::string get_signature(const NotificationItem &item) {
    if (!item.timestamp.empty()) {
      return item.text + "@" + item.timestamp;
    }
    return item.text + "@" + std::to_string(item.r) + "," + std::to_string(item.g) + "," + std::to_string(item.b);
  }

  bool is_dismissed(const std::string &sig) {
    this->init_preferences();
    for (const auto &d : this->dismissed_signatures) {
      if (d == sig) return true;
    }
    uint32_t h = esphome::fnv1_hash(sig);
    for (size_t i = 0; i < this->storage_.count && i < MAX_PERSISTED_DISMISSED; i++) {
      if (this->storage_.hashes[i] == h) return true;
    }
    return false;
  }

  bool is_already_queued(const std::string &sig) const {
    for (const auto &it : this->items) {
      if (get_signature(it) == sig) return true;
    }
    return false;
  }

  void trigger_wifi_reset() {
    this->is_resetting = true;
    if (this->display != nullptr) {
      this->display->update();
    }
    this->init_preferences();
    this->storage_ = DismissedStorage();
    this->pref_.save(&this->storage_);
    if (esphome::global_preferences != nullptr) {
      esphome::global_preferences->sync();
      esphome::global_preferences->reset();
    }
  }

  void set_display(esphome::display::Display *disp) {
    this->display = disp;
    this->init_preferences();
    if (this->display != nullptr) {
      this->display->update();
    }
  }

  void on_wifi_status(bool connected) {
    if (this->wifi_connected != connected) {
      this->wifi_connected = connected;
      this->needs_redraw = true;
      if (this->display != nullptr) {
        this->display->update();
      }
    }
  }

  void parse_payload(const std::string &body) {
    if (body.empty()) {
      ESP_LOGW("notify", "Empty HTTP response body");
      return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      ESP_LOGE("notify", "Failed to deserialize JSON: %s", err.c_str());
      return;
    }

    if (!doc.is<JsonArray>()) {
      ESP_LOGW("notify", "JSON root is not an array");
      return;
    }

    bool added_any = false;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
      NotificationItem item;
      item.r = obj["r"] | 0;
      item.g = obj["g"] | 0;
      item.b = obj["b"] | 0;
      item.delay_ms = obj["delay"] | 1000;
      if (item.delay_ms < 100) {
        item.delay_ms = 1000;
      }
      const char *txt = obj["text"] | "";
      item.text = txt ? std::string(txt) : "";
      std::string raw_ts;
      if (obj["timestamp"].is<const char *>()) {
        const char *ts = obj["timestamp"].as<const char *>();
        raw_ts = ts ? std::string(ts) : "";
      } else if (obj["timestamp"].is<std::string>()) {
        raw_ts = obj["timestamp"].as<std::string>();
      } else if (obj["timestamp"].is<long long>() || obj["timestamp"].is<int>()) {
        raw_ts = std::to_string(obj["timestamp"].as<long long>());
      } else if (obj["timestamp"].is<double>()) {
        raw_ts = std::to_string((int64_t)obj["timestamp"].as<double>());
      }
      item.timestamp = normalize_timestamp(raw_ts);

      std::string sig = get_signature(item);
      // Append only if not already in items queue and not previously dismissed
      if (!is_already_queued(sig) && !is_dismissed(sig)) {
        this->items.push_back(item);
        added_any = true;
        ESP_LOGI("notify", "Appended new notification: '%s' (%s)", item.text.c_str(), sig.c_str());
      }
    }

    this->last_payload = body;

    if (added_any) {
      this->needs_redraw = true;
      if (this->display != nullptr) {
        this->display->update();
      }
    }
  }

  void dismiss_current() {
    uint32_t now = millis();
    if (now - this->last_dismiss_time < 500) {
      return;
    }
    this->last_dismiss_time = now;
    this->pause_rotation_until = now + 3000;

    if (this->items.empty()) {
      return;
    }

    std::string sig = get_signature(this->items[this->current_index]);
    ESP_LOGI("notify", "Dismissed notification at index %zu: '%s'",
             this->current_index, this->items[this->current_index].text.c_str());

    this->dismissed_signatures.push_back(sig);
    if (this->dismissed_signatures.size() > 100) {
      this->dismissed_signatures.erase(this->dismissed_signatures.begin());
    }

    uint32_t h = esphome::fnv1_hash(sig);
    this->save_dismissed_hash(h);

    this->items.erase(this->items.begin() + this->current_index);

    if (this->items.empty()) {
      this->current_index = 0;
    } else {
      this->current_index = this->current_index % this->items.size();
      this->last_switch_time = millis();
    }

    this->needs_redraw = true;
    if (this->display != nullptr) {
      this->display->update();
    }
  }

  void loop() override {
    // Single notification: ignore delay, stay on screen indefinitely!
    if (this->items.size() <= 1) {
      return;
    }

    uint32_t now = millis();
    if (now < this->pause_rotation_until) {
      this->last_switch_time = now;
      return;
    }

    uint32_t current_delay = this->items[this->current_index].delay_ms;
    if (now - this->last_switch_time >= current_delay) {
      this->current_index = (this->current_index + 1) % this->items.size();
      this->last_switch_time = now;
      this->needs_redraw = true;
      if (this->display != nullptr) {
        this->display->update();
      }
    }
  }

  NotificationItem *get_current_item_ptr() {
    if (this->items.empty() || this->current_index >= this->items.size()) {
      return nullptr;
    }
    return &this->items[this->current_index];
  }

  bool get_current_item(NotificationItem &item) const {
    if (this->items.empty()) {
      return false;
    }
    item = this->items[this->current_index];
    return true;
  }
};

inline NotificationManager notify_mgr;

inline void draw_notification_screen(
    esphome::display::Display &it,
    esphome::font::Font *text_font,
    esphome::font::Font *btn_font,
    NotificationManager &mgr) {
  
  const int screen_w = it.get_width();
  const int screen_h = it.get_height();

  if (mgr.is_resetting) {
    it.fill(esphome::Color(180, 0, 0));
    it.print(screen_w / 2, screen_h / 2 - 16, text_font, esphome::Color(255, 255, 255),
             esphome::display::TextAlign::CENTER, "Resetting Wi-Fi...");
    it.print(screen_w / 2, screen_h / 2 + 16, btn_font, esphome::Color(255, 220, 220),
             esphome::display::TextAlign::CENTER, "Rebooting into setup mode");
    return;
  }

  NotificationItem *item_ptr = mgr.get_current_item_ptr();
  if (item_ptr != nullptr) {
    NotificationItem &item = *item_ptr;

    // 1. Solid background color matching RGB
    it.fill(esphome::Color(item.r, item.g, item.b));

    // 2. Dynamic contrast text color based on relative luminance
    float luminance = 0.299f * item.r + 0.587f * item.g + 0.114f * item.b;
    esphome::Color text_color = (luminance < 130.0f) 
        ? esphome::Color(255, 255, 255) 
        : esphome::Color(0, 0, 0);

    const int margin_x = 16;
    const int max_text_w = screen_w - (margin_x * 2);

    int dummy_x = 0, dummy_y = 0, dummy_w = 0, font_line_h = 22;
    it.get_text_bounds(0, 0, "Ajy109", text_font, esphome::display::TextAlign::TOP_LEFT,
                       &dummy_x, &dummy_y, &dummy_w, &font_line_h);
    if (font_line_h <= 0) font_line_h = 22;
    const int line_spacing = font_line_h + 6;

    // Word wrapping (cached per item so we only compute once)
    if (!item.lines_cached) {
      item.wrapped_lines.clear();
      std::stringstream paragraph_stream(item.text);
      std::string paragraph;

      while (std::getline(paragraph_stream, paragraph)) {
        if (!paragraph.empty() && paragraph.back() == '\r') {
          paragraph.pop_back();
        }
        if (paragraph.empty()) {
          item.wrapped_lines.emplace_back("");
          continue;
        }

        std::stringstream word_stream(paragraph);
        std::string word;
        std::string current_line;

        while (word_stream >> word) {
          std::string test_line = current_line.empty() ? word : current_line + " " + word;
          int test_w = 0, test_h = 0;
          it.get_text_bounds(0, 0, test_line.c_str(), text_font, esphome::display::TextAlign::TOP_LEFT,
                             &dummy_x, &dummy_y, &test_w, &test_h);

          if (test_w <= max_text_w) {
            current_line = test_line;
          } else {
            if (!current_line.empty()) {
              item.wrapped_lines.push_back(current_line);
              current_line.clear();
            }

            int word_w = 0, word_h = 0;
            it.get_text_bounds(0, 0, word.c_str(), text_font, esphome::display::TextAlign::TOP_LEFT,
                               &dummy_x, &dummy_y, &word_w, &word_h);
            if (word_w > max_text_w) {
              std::string chunk;
              for (char c : word) {
                std::string test_chunk = chunk + c;
                int cw = 0, ch = 0;
                it.get_text_bounds(0, 0, test_chunk.c_str(), text_font, esphome::display::TextAlign::TOP_LEFT,
                                   &dummy_x, &dummy_y, &cw, &ch);
                if (cw > max_text_w && !chunk.empty()) {
                  item.wrapped_lines.push_back(chunk);
                  chunk = c;
                } else {
                  chunk = test_chunk;
                }
              }
              if (!chunk.empty()) {
                current_line = chunk;
              }
            } else {
              current_line = word;
            }
          }
        }
        if (!current_line.empty()) {
          item.wrapped_lines.push_back(current_line);
        }
      }

      if (item.wrapped_lines.empty() && !item.text.empty()) {
        item.wrapped_lines.push_back(item.text);
      }

      item.lines_cached = true;
    }

    const auto &lines = item.wrapped_lines;

    // Centering calculation in the area above the bottom DISMISS bar and timestamp
    const int bar_h = 36;
    const int bar_y = screen_h - bar_h - 4;
    const bool has_ts = !item.timestamp.empty();
    const int ts_space = has_ts ? 24 : 0;
    const int content_area_h = bar_y - ts_space;

    int total_text_h = lines.empty() ? 0 : (int)((lines.size() - 1) * line_spacing + font_line_h);
    int start_y = (content_area_h - total_text_h) / 2;
    if (start_y < 8) start_y = 8;
    int center_x = screen_w / 2;

    for (size_t i = 0; i < lines.size(); i++) {
      int y = start_y + (int)(i * line_spacing);
      it.print(center_x, y, text_font, text_color, esphome::display::TextAlign::TOP_CENTER, lines[i].c_str());
    }

    // Draw timestamp directly above the DISMISS button
    if (has_ts) {
      int ts_y = bar_y - 12;
      it.print(center_x, ts_y, btn_font, text_color, esphome::display::TextAlign::CENTER, item.timestamp.c_str());
    }

    // 3. Draw Bottom "DISMISS" Bar
    esphome::Color bar_bg = (luminance < 130.0f)
        ? esphome::Color(255, 255, 255)
        : esphome::Color(30, 30, 30);
    esphome::Color bar_fg = (luminance < 130.0f)
        ? esphome::Color(0, 0, 0)
        : esphome::Color(255, 255, 255);

    it.filled_rectangle(16, bar_y, screen_w - 32, bar_h, bar_bg);
    it.rectangle(16, bar_y, screen_w - 32, bar_h, bar_fg);
    it.print(screen_w / 2, bar_y + (bar_h / 2), btn_font, bar_fg,
             esphome::display::TextAlign::CENTER, "DISMISS");

  } else {
    // Standby / Dismissed screen
    int center_x = screen_w / 2;
    int center_y = screen_h / 2;

    bool is_ap = false;
    if (esphome::wifi::global_wifi_component != nullptr) {
      is_ap = esphome::wifi::global_wifi_component->is_ap_active();
    }

    if (is_ap) {
      it.fill(esphome::Color(0, 50, 150));
      it.print(center_x, center_y - 36, text_font, esphome::Color(255, 220, 0),
               esphome::display::TextAlign::CENTER, "Wi-Fi Setup Mode");
      it.print(center_x, center_y, text_font, esphome::Color(255, 255, 255),
               esphome::display::TextAlign::CENTER, "Join: NotificationScreen-AP");
      it.print(center_x, center_y + 36, btn_font, esphome::Color(200, 230, 255),
               esphome::display::TextAlign::CENTER, "Browse: 192.168.4.1");
    } else {
      // Dark grey background with black text
      it.fill(esphome::Color(80, 80, 85));
      const char *status_msg = mgr.wifi_connected 
          ? "No active notifications" 
          : "Connecting to Wi-Fi...";
      it.print(center_x, center_y, text_font, esphome::Color(0, 0, 0),
               esphome::display::TextAlign::CENTER, status_msg);
    }
  }
}
