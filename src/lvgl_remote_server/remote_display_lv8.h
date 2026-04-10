/**
 * @file remote_display_lv8.h
 * @brief Adaptação do CubeCoders/LVGLRemoteServer para LVGL 8.x.
 *
 * Upstream (LVGL 9 + lv_indev próprio): https://github.com/CubeCoders/LVGLRemoteServer
 * Cliente Windows: https://downloads.cubecoders.com/LVGLRemote/Viewer_Signed.zip
 *
 * Esta versão não regista um lv_indev separado: o toque UDP é integrado em `touchpad_read`
 * via `lvgl_remote_server_poll_input()` / `lvgl_remote_server_get_pointer()`.
 */
#ifndef REMOTE_DISPLAY_LV8_H
#define REMOTE_DISPLAY_LV8_H

#include <lvgl.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#define MAX_PACKET_SIZE 1430

class RemoteDisplay {
public:
    RemoteDisplay(int udpPort = 24680, const char *ssid = nullptr, const char *password = nullptr);
    RemoteDisplay(const char *ssid, const char *password);

    /** Só abre UDP; Wi-Fi deve já estar ligado pela aplicação (sem bloquear em WiFi.begin). */
    void init(void);

    void send(const lv_area_t *area, uint8_t *pixelmap);
    void sendRLE(const lv_area_t *area, uint8_t *pixelmap);
    void sendPalettedRLE(const lv_area_t *area, uint8_t *pixelmap);

    void connectRemote(char *ipStr);
    void connectRemote(IPAddress ip);

    /** Chamado a partir do read do touch: processa pacotes UDP de 5 bytes do viewer. */
    void pollTouchFromUdp(void);

    /** Devolve o último toque remoto se existir sessão activa (udpAddress != 0). */
    bool getRemotePointer(lv_coord_t *x, lv_coord_t *y, bool *pressed);

private:
    WiFiUDP udp;
    uint32_t udpAddress;
    int udpPort;
    const char *ssid;
    const char *password;
    bool wifiConnected;
    uint8_t packetBuffer[MAX_PACKET_SIZE];
    uint16_t rleBuffer[MAX_PACKET_SIZE / 2];
    uint8_t infoBuffer[10];
    uint16_t paletteBuffer[16];

    lv_indev_state_t m_lastState;
    uint16_t m_lastX;
    uint16_t m_lastY;

    void sendRLEPacket(uint16_t transmittedX, uint16_t transmittedY, uint16_t tileWidth, uint16_t progressStart,
                       uint32_t rleLength, uint8_t paletteSize = 0);
    void transmitInfoPacket(void);
    void refreshDisplay(void);
};

#endif
