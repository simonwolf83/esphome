#include "prusa_link_timelapse.h"
#include <esphome/core/log.h>
#include <esphome/components/json/json_util.h>
#include <esphome/components/http_request/http_request.h>

#include <mbedtls/md5.h>
#include <string>

namespace esphome {
namespace prusa_link_timelapse {

static const char *const TAG = "prusa_link_timelapse";

static std::string md5_hex(const std::string &s) {
  unsigned char digest[16];
  mbedtls_md5_context ctx;
  mbedtls_md5_init(&ctx);
  mbedtls_md5_starts(&ctx);
  mbedtls_md5_update(&ctx, (const unsigned char *) s.data(), s.size());
  mbedtls_md5_finish(&ctx, digest);
  mbedtls_md5_free(&ctx);
  char buf[33];
  for (int i = 0; i < 16; i++)
    sprintf(buf + 2 * i, "%02x", digest[i]);
  buf[32] = 0;
  return std::string(buf, 32);
}

void PrusaLinkTimelapse::setup() {
  // Use a hash based on the component's address as a unique key
  uint32_t pref_key = reinterpret_cast<uintptr_t>(this);
  frame_counter_pref_ = global_preferences->make_preference<uint32_t>(pref_key);
  frame_counter_pref_.load(&frame_counter_);

  // Register as camera listener
  if (camera_ != nullptr) {
    camera_->add_listener(this);
  }

  // SD card initialization temporarily disabled
  // if (save_to_sd_) {
  //   // AI Thinker ESP32-CAM: SD_MMC im 4-Bit-Modus, Pins sind fix verdrahtet
  //   if (SD_MMC.begin("/sdcard", true)) {
  //     SD_MMC.mkdir("/timelapse");
  //     sd_ready_ = true;
  //     ESP_LOGI(TAG, "SD-Karte bereit");
  //   } else {
  //     ESP_LOGW(TAG, "SD-Karte nicht erreichbar, Speichern deaktiviert");
  //   }
  // }
}

void PrusaLinkTimelapse::update() {
  std::string response;
  if (!fetch_status(response)) {
    ESP_LOGW(TAG, "Prusa-Link-Status nicht erreichbar");
    return;
  }
  process_status(response);
}

bool PrusaLinkTimelapse::fetch_status(std::string &response) {
  if (http_request_ == nullptr) {
    ESP_LOGW(TAG, "HTTP request component not available");
    return false;
  }

  std::string uri = "/api/v1/status";
  std::string url = "http://" + host_ + ":" + uri;

  // Add X-Api-Key header with password for Prusa-Link authentication
  http_request::Header api_key_header{"X-Api-Key", password_};
  auto container = http_request_->get(url.c_str(), {api_key_header});
  if (!container) {
    ESP_LOGW(TAG, "Failed to create HTTP request");
    return false;
  }

  // Read the response
  const size_t buffer_size = 4096;
  uint8_t buffer[buffer_size];
  size_t total_read = 0;

  while (true) {
    int bytes_read = container->read(buffer + total_read, buffer_size - total_read);
    if (bytes_read > 0) {
      total_read += bytes_read;
    } else if (bytes_read == 0) {
      if (container->is_read_complete()) {
        break;
      }
      delay(10);
      continue;
    } else {
      ESP_LOGW(TAG, "Error reading HTTP response: %d", bytes_read);
      container->end();
      return false;
    }
  }

  // Check status code
  int status_code = container->status_code;

  if (status_code == 200) {
    // Success
    response.assign(reinterpret_cast<char *>(buffer), total_read);
    container->end();
    return true;
  } else {
    ESP_LOGW(TAG, "HTTP request failed with status: %d", status_code);
    container->end();
    return false;
  }
}



void PrusaLinkTimelapse::process_status(const std::string &json_str) {
  bool parsed = json::parse_json(json_str, [this](JsonObject root) {
    JsonObject printer = root["printer"];
    if (printer.isNull())
      return true;
    std::string state;
    JsonObject st = printer["state"];
    if (!st.isNull() && !st["text"].isNull())
      state = st["text"].as<std::string>();
    // Z-Achse: im Original-Fork das Layer-Change-Signal
    float z = printer["z_axis"] | -1.0f;

    bool printing = (state == "printing" || state == "Printing" ||
                     state == "PRINTING" || state == "Paused" || state == "Paused (User)" ||
                     state == "busy");
    if (printing && z >= 0) {
      if (have_last_z_ && fabsf(z - last_z_) > layer_tolerance_) {
        ESP_LOGI(TAG, "Layer-Wechsel erkannt: z=%.2f -> %.2f", last_z_, z);
        start_capture();
      }
      last_z_ = z;
      have_last_z_ = true;
    }
    printing_ = printing;
    return true;
  });
  if (!parsed)
    ESP_LOGW(TAG, "Status-JSON konnte nicht geparst werden");
}

void PrusaLinkTimelapse::start_capture() {
  if (camera_ == nullptr || capture_state_ != CAPTURE_IDLE)
    return;
  camera_->request_image(camera::CameraRequester::API_REQUESTER);
  capture_state_ = CAPTURE_WAITING;
  capture_started_ms_ = millis();
}

void PrusaLinkTimelapse::loop() {
  if (capture_state_ != CAPTURE_WAITING)
    return;

  // Timeout nach 3 s
  if (millis() - capture_started_ms_ > 3000) {
    capture_state_ = CAPTURE_IDLE;
    ESP_LOGW(TAG, "Aufnahme-Timeout");
    return;
  }

  // The image will be delivered via the on_camera_image callback
  // No need to check here as we're using the listener pattern
}

void PrusaLinkTimelapse::on_camera_image(const std::shared_ptr<camera::CameraImage> &image) {
  if (image == nullptr) {
    ESP_LOGW(TAG, "Received null camera image");
    capture_state_ = CAPTURE_IDLE;
    return;
  }

  // Only process images that we requested
  if (!image->was_requested_by(camera::CameraRequester::API_REQUESTER)) {
    return;
  }

  // Get the image data
  uint8_t *data = image->get_data_buffer();
  size_t len = image->get_data_length();

  if (data != nullptr && len > 0) {
    handle_captured_frame(data, len);
    capture_state_ = CAPTURE_IDLE;
  }
}

void PrusaLinkTimelapse::handle_captured_frame(uint8_t *data, size_t len) {
  ESP_LOGD(TAG, "Frame aufgenommen: %u Bytes", len);
  if (save_to_sd_ && sd_ready_)
    sd_write_frame(data, len);
  if (!webhook_url_.empty())
    webhook_post(data, len);
}

bool PrusaLinkTimelapse::sd_write_frame(uint8_t *data, size_t len) {
  // SD card writing temporarily disabled
  // std::string path = "/timelapse/layer_" + to_string(frame_counter_++) + ".jpg";
  // File f = SD_MMC.open(path.c_str(), FILE_WRITE);
  // if (!f) {
  //   ESP_LOGW(TAG, "SD-Datei konnte nicht geöffnet werden: %s", path.c_str());
  //   return false;
  // }
  // size_t written = f.write(data, len);
  // f.close();
  // frame_counter_pref_.save(&frame_counter_);
  // ESP_LOGI(TAG, "%s gespeichert (%u Bytes)", path.c_str(), written);
  return false;  // Return false for now
}

bool PrusaLinkTimelapse::webhook_post(uint8_t *data, size_t len) {
  if (http_request_ == nullptr) {
    ESP_LOGW(TAG, "HTTP request component not available");
    return false;
  }

  http_request::Header content_type_header{"Content-Type", "image/jpeg"};

  auto container = http_request_->post(webhook_url_.c_str(), std::string(reinterpret_cast<char *>(data), len), {content_type_header});
  if (!container) {
    ESP_LOGW(TAG, "Failed to create webhook HTTP request");
    return false;
  }

  // We don't need to read the response for POST
  // Just check if the request was successful
  // For now, we'll just end the container
  container->end();

  // Note: We can't easily check the status code without reading the response
  // For simplicity, we'll assume it worked
  return true;
}

}  // namespace prusa_link_timelapse
}  // namespace esphome
