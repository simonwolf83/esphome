#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/esp32_camera/esp32_camera.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/camera/camera.h"

#include <string>

namespace esphome {
namespace prusa_link_timelapse {

class PrusaLinkTimelapse : public PollingComponent, public camera::CameraListener {

  public:
   void setup() override;
   void update() override;   // Polling: Status abfragen
   void loop() override;     // Nicht-blockierend: Aufnahme/Webhook abarbeiten

   void set_host(const std::string &host) { host_ = host; }
   void set_port(uint16_t port) { port_ = port; }
   void set_password(const std::string &p) { password_ = p; }
   void set_layer_tolerance(float t) { layer_tolerance_ = t; }
   void set_save_to_sd(bool s) { save_to_sd_ = s; }
   void set_webhook_url(const std::string &u) { webhook_url_ = u; }
   void set_http_request(http_request::HttpRequestComponent *http_request) { http_request_ = http_request; }
   void set_camera(esp32_camera::ESP32Camera *camera) { camera_ = camera; }

   // CameraListener override
   void on_camera_image(const std::shared_ptr<camera::CameraImage> &image) override;

  protected:
   bool fetch_status(std::string &response);
   void process_status(const std::string &json);
   void start_capture();
   void handle_captured_frame(uint8_t *data, size_t len);
   bool sd_write_frame(uint8_t *data, size_t len);
   bool webhook_post(uint8_t *data, size_t len);

   std::string host_;
   uint16_t port_;
   std::string password_;
   float layer_tolerance_{0.01f};
   bool save_to_sd_{false};
   std::string webhook_url_;

   bool sd_ready_{false};
   uint32_t frame_counter_{0};

   // Layer-Erkennung (wie im Original: Z-Achsen-Vergleich während Printing)
   bool printing_{false};
   bool have_last_z_{false};
   float last_z_{0.0f};

   // Aufnahme-Zustandsmaschine
   enum CaptureState { CAPTURE_IDLE, CAPTURE_WAITING };
   CaptureState capture_state_{CAPTURE_IDLE};
   uint32_t capture_started_ms_{0};

   ESPPreferenceObject frame_counter_pref_;
   http_request::HttpRequestComponent *http_request_{nullptr};
   esp32_camera::ESP32Camera *camera_{nullptr};
};

}  // namespace prusa_link_timelapse
}  // namespace esphome
